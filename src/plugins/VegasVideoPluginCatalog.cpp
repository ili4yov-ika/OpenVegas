#include "plugins/VegasVideoPluginCatalog.h"

#include "plugins/OfxHost.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace openvegas {
namespace {

QVector<VegasVideoPluginEntry> g_cache;
bool g_cacheValid = false;

bool isLocaleResourceXml(const QString &fileName)
{
    static const QRegularExpression localeSuffix(
        QStringLiteral(R"(\.[a-z]{2}-[A-Z]{2}\.xml$)"),
        QRegularExpression::CaseInsensitiveOption);
    return localeSuffix.match(fileName).hasMatch();
}

QString findPrimaryResourceXml(const QString &bundlePath)
{
    const QDir res(QDir(bundlePath).filePath(QStringLiteral("Contents/Resources")));
    if (!res.exists()) {
        return {};
    }
    const QFileInfoList files = res.entryInfoList({QStringLiteral("*.xml")}, QDir::Files, QDir::Name);

    // The manifest named after the bundle wins. Several bundles ship extra XML next to it
    // (gui.xml, an older ofxStabilizer.xml), and picking whichever sorted first meant
    // 360° Stabilization was read out of the wrong file and ended up mislabelled.
    QString bundleBase = QFileInfo(bundlePath).fileName();
    bundleBase.remove(QStringLiteral(".ofx.bundle"), Qt::CaseInsensitive);
    bundleBase.remove(QStringLiteral(".bundle"), Qt::CaseInsensitive);
    for (const QFileInfo &fi : files) {
        if (!isLocaleResourceXml(fi.fileName())
            && fi.completeBaseName().compare(bundleBase, Qt::CaseInsensitive) == 0) {
            return fi.absoluteFilePath();
        }
    }

    QString fallback;
    for (const QFileInfo &fi : files) {
        if (fi.fileName().compare(QStringLiteral("PresetPackage.xml"), Qt::CaseInsensitive) == 0
            || fi.fileName().startsWith(QStringLiteral("PresetPackage."), Qt::CaseInsensitive)
            || fi.fileName().compare(QStringLiteral("VrPresets.xml"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        if (isLocaleResourceXml(fi.fileName())) {
            continue;
        }
        if (fallback.isEmpty()) {
            fallback = fi.absoluteFilePath();
        }
        const QString base = fi.completeBaseName();
        if (base.compare(QStringLiteral("PresetPackage"), Qt::CaseInsensitive) != 0
            && !base.contains(QLatin1Char('.'))) {
            return fi.absoluteFilePath();
        }
    }
    return fallback;
}

QString findOfxBinaryInBundle(const QString &bundlePath)
{
    const QDir contents(QDir(bundlePath).filePath(QStringLiteral("Contents")));
    if (!contents.exists()) {
        const QFileInfoList files =
            QDir(bundlePath).entryInfoList({QStringLiteral("*.ofx")}, QDir::Files);
        return files.isEmpty() ? QString() : files.first().absoluteFilePath();
    }
    for (const QString &arch : OfxHost::supportedArchFolderNames()) {
        const QDir archDir(contents.filePath(arch));
        if (!archDir.exists()) {
            continue;
        }
        const QFileInfoList files = archDir.entryInfoList({QStringLiteral("*.ofx")}, QDir::Files);
        if (!files.isEmpty()) {
            return files.first().absoluteFilePath();
        }
    }
    return {};
}

/** One bundle's presets: ordered names per effect, plus the values each preset sets. */
struct BundlePresets {
    QHash<QString, QStringList> names;
    QHash<QString, QMap<QString, QVariantMap>> params;
};

/**
 * Parameter values inside a single `<OfxPreset>` block.
 *
 * Booleans arrive as `true`/`false` and are stored as 1/0 doubles so the whole map is
 * uniform — the same shape FxSlot state and OfxHost::processFrame already speak.
 * Multi-value params (`OfxParamTypeDouble2D` and friends) carry several `<OfxParamValue>`
 * entries; only the first is taken, which is all the flat parameter model holds today.
 */
QVariantMap parsePresetParams(const QString &block)
{
    QVariantMap out;
    static const QRegularExpression paramRe(
        QStringLiteral(R"re(<(OfxParamType\w+)\s+name="([^"]+)"\s*>([\s\S]*?)</\1>)re"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression valueRe(
        QStringLiteral(R"(<OfxParamValue>([^<]*)</OfxParamValue>)"),
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatchIterator it = paramRe.globalMatch(block);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString name = m.captured(2).trimmed();
        const QRegularExpressionMatch vm = valueRe.match(m.captured(3));
        if (name.isEmpty() || !vm.hasMatch()) {
            continue;
        }
        const QString raw = vm.captured(1).trimmed();
        if (raw.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0) {
            out.insert(name, 1.0);
        } else if (raw.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0) {
            out.insert(name, 0.0);
        } else {
            bool ok = false;
            const double v = raw.toDouble(&ok);
            if (ok) {
                out.insert(name, v);
            } else if (!raw.isEmpty()) {
                out.insert(name, raw);
            }
        }
    }
    return out;
}

BundlePresets parsePresetsXml(const QString &bundlePath)
{
    BundlePresets out;
    const QDir presetsDir(QDir(bundlePath).filePath(QStringLiteral("Contents/Presets")));
    if (!presetsDir.exists()) {
        return out;
    }
    QString presetXml;
    const QFileInfoList files =
        presetsDir.entryInfoList({QStringLiteral("PresetPackage.xml")}, QDir::Files);
    if (!files.isEmpty()) {
        presetXml = files.first().absoluteFilePath();
    } else {
        const QFileInfoList any =
            presetsDir.entryInfoList({QStringLiteral("*.xml")}, QDir::Files, QDir::Name);
        for (const QFileInfo &fi : any) {
            if (!isLocaleResourceXml(fi.fileName())) {
                presetXml = fi.absoluteFilePath();
                break;
            }
        }
    }
    if (presetXml.isEmpty()) {
        return out;
    }
    QFile f(presetXml);
    if (!f.open(QIODevice::ReadOnly)) {
        return out;
    }
    const QString text = QString::fromUtf8(f.readAll());
    static const QRegularExpression presetRe(
        QStringLiteral(
            R"re(<OfxPreset\s+plugin="([^"]+)"[^>]*\sname="([^"]+)"[^>]*>([\s\S]*?)</OfxPreset>)re"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = presetRe.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString pluginId = m.captured(1).trimmed();
        const QString presetName = m.captured(2).trimmed();
        if (pluginId.isEmpty() || presetName.isEmpty()) {
            continue;
        }
        QStringList &list = out.names[pluginId];
        if (!list.contains(presetName, Qt::CaseInsensitive)) {
            list << presetName;
        }
        out.params[pluginId].insert(presetName, parsePresetParams(m.captured(3)));
    }
    return out;
}

/**
 * Category chips a grouping belongs to, as VEGAS's own Video FX pane labels them.
 *
 * The pane's tabs are a fixed set (AI/ML, Creative, Color, Utility, Blur, 360°,
 * Third Party) that does not match the grouping strings one-to-one: VEGAS writes
 * `VEGAS\AI` but shows "AI/ML", and `VEGAS\360` but shows "360°". A grouping outside
 * the `VEGAS\…` tree is somebody else's plug-in and lands under Third Party.
 *
 * Groupings with no chip of their own (bare `VEGAS`, and `VEGAS\Light`) return empty:
 * those effects are reachable through "All Plug-ins" only. Which chip VEGAS itself files
 * `VEGAS\Light` under is not visible in the reference screenshots, so it is left alone
 * rather than guessed into Creative.
 */
QStringList categoriesForGrouping(const QString &grouping)
{
    const QString g = grouping.trimmed();
    if (g.isEmpty()) {
        return {};
    }
    QStringList parts = g.split(QLatin1Char('\\'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return {};
    }
    if (parts.first().compare(QLatin1String("VEGAS"), Qt::CaseInsensitive) != 0) {
        return {QStringLiteral("Third Party")};
    }
    parts.removeFirst();
    if (parts.isEmpty()) {
        return {};
    }
    const QString leaf = parts.first();
    if (leaf.compare(QLatin1String("AI"), Qt::CaseInsensitive) == 0) {
        return {QStringLiteral("AI/ML")};
    }
    if (leaf.compare(QLatin1String("360"), Qt::CaseInsensitive) == 0) {
        return {QStringLiteral("360°")};
    }
    static const QStringList chips = {QStringLiteral("Creative"), QStringLiteral("Color"),
                                      QStringLiteral("Utility"), QStringLiteral("Blur")};
    for (const QString &chip : chips) {
        if (leaf.compare(chip, Qt::CaseInsensitive) == 0) {
            return {chip};
        }
    }
    return {};
}

QVector<VegasVideoPluginEntry> parseResourceXml(const QString &xmlPath, const QString &bundlePath,
                                                const QString &binaryPath,
                                                const BundlePresets &presets)
{
    QVector<VegasVideoPluginEntry> out;
    QFile f(xmlPath);
    if (!f.open(QIODevice::ReadOnly)) {
        return out;
    }
    const QString text = QString::fromUtf8(f.readAll());

    static const QRegularExpression pluginRe(
        QStringLiteral(R"re(<OfxPlugin\s+name="([^"]+)"[^>]*>([\s\S]*?)</OfxPlugin>)re"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);

    QRegularExpressionMatchIterator it = pluginRe.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        VegasVideoPluginEntry e;
        e.effectId = m.captured(1).trimmed();
        const QString block = m.captured(2);

        static const QRegularExpression labelRe(
            QStringLiteral(R"(<OfxPropLabel>([^<]+)</OfxPropLabel>)"));
        const QRegularExpressionMatch lm = labelRe.match(block);
        if (!lm.hasMatch()) {
            continue;
        }
        e.vegasLabel = lm.captured(1).trimmed();
        e.displayName = builtinFxDisplayName(e.vegasLabel);
        if (e.displayName.isEmpty()) {
            continue;
        }

        static const QRegularExpression groupRe(QStringLiteral(
            R"(<OfxImageEffectPluginPropGrouping>([^<]+)</OfxImageEffectPluginPropGrouping>)"));
        const QRegularExpressionMatch gm = groupRe.match(block);
        if (gm.hasMatch()) {
            e.grouping = gm.captured(1).trimmed();
            e.categories = categoriesForGrouping(e.grouping);
        }

        static const QRegularExpression descRe(QStringLiteral(
            R"(<OfxPropPluginDescription>([^<]*)</OfxPropPluginDescription>)"));
        const QRegularExpressionMatch dm = descRe.match(block);
        if (dm.hasMatch()) {
            e.description = dm.captured(1).trimmed();
        }

        // Every context the manifest declares for this effect. "default" is the
        // catch-all resource block, not a context, so it is skipped.
        static const QRegularExpression ctxRe(
            QStringLiteral(R"re(<OfxImageEffectContext\s+name="([^"]+)")re"));
        QRegularExpressionMatchIterator ctxIt = ctxRe.globalMatch(block);
        while (ctxIt.hasNext()) {
            const QString ctx = ctxIt.next().captured(1).trimmed();
            if (!ctx.isEmpty() && ctx.compare(QLatin1String("default"), Qt::CaseInsensitive) != 0
                && !e.contexts.contains(ctx)) {
                e.contexts << ctx;
            }
        }

        e.bundlePath = bundlePath;
        e.binaryPath = binaryPath;
        e.hasBinary = !binaryPath.isEmpty() && QFileInfo::exists(binaryPath);
        e.presets = presets.names.value(e.effectId);
        e.presetParams = presets.params.value(e.effectId);
        out.push_back(std::move(e));
    }
    return out;
}

/**
 * Catalog entries for a bundle that carries no VEGAS resource manifest, by asking the
 * binary itself what it contains.
 *
 * Every OFX plug-in that did not come from VEGAS lands here — which on Linux and macOS
 * is all of them, since there is no VEGAS installation to inherit a manifest from.
 */
QVector<VegasVideoPluginEntry> enumerateGenericBundle(const QString &bundlePath,
                                                      const QString &binaryPath)
{
    QVector<VegasVideoPluginEntry> out;
    if (binaryPath.isEmpty()) {
        return out;
    }
    const QVector<OfxEffectSummary> effects = OfxHost::enumerateEffects(binaryPath);
    out.reserve(effects.size());
    for (const OfxEffectSummary &e : effects) {
        VegasVideoPluginEntry entry;
        entry.effectId = e.effectId;
        entry.vegasLabel = e.label;
        entry.displayName = e.label;
        entry.grouping = e.grouping;
        // Anything enumerated straight from a binary came without a VEGAS manifest, so by
        // definition it is somebody else's plug-in.
        entry.categories = {QStringLiteral("Third Party")};
        entry.bundlePath = bundlePath;
        entry.binaryPath = binaryPath;
        entry.pluginIndex = e.pluginIndex;
        entry.hasBinary = true;
        out.push_back(entry);
    }
    return out;
}

QVector<VegasVideoPluginEntry> parseBundle(const QString &bundlePath)
{
    const QString binaryPath = findOfxBinaryInBundle(bundlePath);
    const QString xmlPath = findPrimaryResourceXml(bundlePath);
    if (xmlPath.isEmpty()) {
        return enumerateGenericBundle(bundlePath, binaryPath);
    }
    const BundlePresets presets = parsePresetsXml(bundlePath);
    QVector<VegasVideoPluginEntry> fromXml =
        parseResourceXml(xmlPath, bundlePath, binaryPath, presets);
    // A manifest that describes nothing usable (wrong schema, VR-only preset package)
    // should not hide a perfectly good binary.
    return fromXml.isEmpty() ? enumerateGenericBundle(bundlePath, binaryPath) : fromXml;
}

void resolvePluginIndices(QVector<VegasVideoPluginEntry> *entries)
{
    if (!entries) {
        return;
    }
    QHash<QString, QHash<QString, int>> indexCache;
    for (VegasVideoPluginEntry &e : *entries) {
        if (!e.hasBinary) {
            continue;
        }
        QHash<QString, int> &map = indexCache[e.binaryPath];
        if (map.isEmpty()) {
            map = OfxHost::effectIndexMap(e.binaryPath);
        }
        const auto it = map.constFind(e.effectId.toLower());
        if (it != map.cend()) {
            e.pluginIndex = it.value();
        }
    }
}

/** Names resolved as OpenVegas builtins rather than OFX plug-ins (shared with ColorCorrectorApply). */
bool isBuiltinVideoName(const QString &name)
{
    return isPanCropName(name) || isColorCorrectorName(name) || isColorGradingName(name);
}

FxSlot slotFromEntry(const VegasVideoPluginEntry &e)
{
    FxSlot s;
    s.displayName = e.displayName;
    s.format = PluginFormat::Ofx;
    s.pluginId = VegasVideoPluginCatalog::formatPluginId(e);
    ensureFxHostKey(&s);
    return s;
}

} // namespace

QStringList VegasVideoPluginCatalog::defaultOfxRoots()
{
    // Delegates to the same install-path guesses + Preferences-configured path
    // used everywhere else in the app (PluginScanner is the source of truth).
    return PluginScanner().candidateRoots();
}

QVector<VegasVideoPluginEntry> VegasVideoPluginCatalog::discoverUsingScanner(
    const PluginScanner &scanner, QString *resolvedRootOut)
{
    for (const QString &root : scanner.candidateRoots()) {
        const QString ofxSub = QDir(root).filePath(QStringLiteral("OFX Video Plug-Ins"));
        const QString resolvedRoot = QDir(ofxSub).exists() ? ofxSub : root;
        if (!QDir(resolvedRoot).exists()) {
            continue;
        }
        const QVector<VegasVideoPluginEntry> found = discover({QDir(resolvedRoot).absolutePath()});
        if (!found.isEmpty()) {
            if (resolvedRootOut) {
                *resolvedRootOut = root;
            }
            return found;
        }
    }
    if (resolvedRootOut) {
        resolvedRootOut->clear();
    }
    return {};
}

QVector<VegasVideoPluginEntry> VegasVideoPluginCatalog::discover(const QStringList &rootsIn)
{
    if (g_cacheValid && rootsIn.isEmpty()) {
        return g_cache;
    }

    const QStringList roots = rootsIn.isEmpty() ? defaultOfxRoots() : rootsIn;
    QVector<VegasVideoPluginEntry> merged;
    QSet<QString> seenIds;

    for (const QString &root : roots) {
        QDir ofxDir(QDir(root).filePath(QStringLiteral("OFX Video Plug-Ins")));
        if (!ofxDir.exists()) {
            ofxDir = QDir(root);
        }
        if (!ofxDir.exists()) {
            continue;
        }

        const QFileInfoList bundles =
            ofxDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &fi : bundles) {
            if (!fi.fileName().contains(QStringLiteral(".ofx"), Qt::CaseInsensitive)
                && !fi.fileName().endsWith(QStringLiteral(".bundle"), Qt::CaseInsensitive)) {
                continue;
            }
            const QVector<VegasVideoPluginEntry> batch = parseBundle(fi.absoluteFilePath());
            for (const VegasVideoPluginEntry &e : batch) {
                const QString key = e.effectId.toLower();
                if (seenIds.contains(key)) {
                    continue;
                }
                seenIds.insert(key);
                merged.push_back(e);
            }
        }
        // Deliberately no early exit: a machine can have VEGAS's bundles *and*
        // third-party plug-ins in the standard OFX locations, and both belong in the
        // catalog. Duplicate effectIds are already filtered above, first root winning.
    }

    resolvePluginIndices(&merged);
    if (rootsIn.isEmpty()) {
        g_cache = merged;
        g_cacheValid = true;
    }
    return merged;
}

const VegasVideoPluginEntry *VegasVideoPluginCatalog::findByDisplayName(const QString &name)
{
    const QString key = normalizeVegasPluginKey(name);
    if (key.isEmpty()) {
        return nullptr;
    }
    discover();
    for (const VegasVideoPluginEntry &e : g_cache) {
        if (normalizeVegasPluginKey(e.displayName) == key
            || normalizeVegasPluginKey(e.vegasLabel) == key) {
            return &e;
        }
    }
    return nullptr;
}

const VegasVideoPluginEntry *VegasVideoPluginCatalog::findByEffectId(const QString &effectId)
{
    QString id = effectId.trimmed();
    if (id.startsWith(QLatin1String("{Svfx:"), Qt::CaseInsensitive)) {
        const int colon = id.indexOf(QLatin1Char(':'));
        const int end = id.indexOf(QLatin1Char('}'));
        if (colon >= 0) {
            id = id.mid(colon + 1, end > colon ? end - colon - 1 : -1).trimmed();
        }
    }
    const QString key = id.toLower();
    if (key.isEmpty()) {
        return nullptr;
    }
    discover();
    for (const VegasVideoPluginEntry &e : g_cache) {
        if (e.effectId.compare(id, Qt::CaseInsensitive) == 0) {
            return &e;
        }
        if (key.contains(e.effectId.toLower()) || e.effectId.toLower().contains(key)) {
            return &e;
        }
    }
    return nullptr;
}

const VegasVideoPluginEntry *VegasVideoPluginCatalog::findByVegasLabel(const QString &label)
{
    const QString key = normalizeVegasPluginKey(label);
    discover();
    for (const VegasVideoPluginEntry &e : g_cache) {
        if (normalizeVegasPluginKey(e.vegasLabel) == key) {
            return &e;
        }
    }
    return nullptr;
}

QString VegasVideoPluginCatalog::formatPluginId(const VegasVideoPluginEntry &entry)
{
    if (entry.hasBinary && !entry.binaryPath.isEmpty()) {
        const int idx = entry.pluginIndex >= 0 ? entry.pluginIndex : 0;
        return QStringLiteral("ofx:%1#%2#%3").arg(entry.binaryPath).arg(idx).arg(entry.effectId);
    }
    return QStringLiteral("ofx-id:%1").arg(entry.effectId);
}

FxSlot VegasVideoPluginCatalog::resolveVideoFxSlot(FxSlot slot)
{
    if (slot.bypass || slot.format != PluginFormat::Ofx) {
        return slot;
    }
    if (isBuiltinVideoName(slot.displayName)) {
        return slot;
    }
    if (slot.pluginId.startsWith(QStringLiteral("ofx:"), Qt::CaseInsensitive)
        && slot.pluginId.count(QLatin1Char('#')) >= 2) {
        return slot;
    }

    const VegasVideoPluginEntry *e = nullptr;
    if (slot.pluginId.startsWith(QStringLiteral("ofx-id:"), Qt::CaseInsensitive)) {
        e = findByEffectId(slot.pluginId.mid(7));
    } else if (slot.pluginId.contains(QLatin1Char(':'))) {
        e = findByEffectId(slot.pluginId);
    }
    if (!e) {
        e = findByDisplayName(slot.displayName);
    }
    if (e) {
        // Resolution attaches the binary path and effect index — it must not rebuild the
        // slot from the catalog entry, or everything the caller had already recovered
        // (parameter values from the project, bypass, the hostKey instances are keyed by)
        // is silently dropped on the floor.
        slot.displayName = e->displayName;
        slot.pluginId = formatPluginId(*e);
        ensureFxHostKey(&slot);
    }
    return slot;
}

FxSlot VegasVideoPluginCatalog::slotFromDisplayName(const QString &rawName)
{
    const QString name = rawName.trimmed();
    if (name.isEmpty()) {
        return {};
    }
    if (isPanCropName(name)) {
        return makeFxSlot(QStringLiteral("Pan/Crop"), PluginFormat::Builtin,
                          QStringLiteral("builtin:Pan/Crop"));
    }
    if (isColorCorrectorName(name)) {
        return makeFxSlot(QStringLiteral("Color Corrector"), PluginFormat::Builtin,
                          QStringLiteral("builtin:Color Corrector"));
    }
    if (isColorGradingName(name)) {
        return makeFxSlot(QStringLiteral("Color Grading"), PluginFormat::Builtin,
                          QStringLiteral("builtin:Color Grading"));
    }
    if (isBrightnessContrastName(name)) {
        if (const VegasVideoPluginEntry *e = findByDisplayName(name)) {
            return slotFromEntry(*e);
        }
        return makeFxSlot(QStringLiteral("Brightness and Contrast"), PluginFormat::Builtin,
                          QStringLiteral("builtin:Brightness and Contrast"));
    }

    if (const VegasVideoPluginEntry *e = findByDisplayName(name)) {
        return slotFromEntry(*e);
    }
    return makeFxSlot(name, PluginFormat::Ofx, name);
}

FxSlot VegasVideoPluginCatalog::resolveVegImportSlot(FxSlot slot)
{
    return resolveVideoFxSlot(std::move(slot));
}

QVector<OfxParamInfo> VegasVideoPluginCatalog::paramsInfoForSlot(const FxSlot &slot)
{
    if (slot.format == PluginFormat::Ofx) {
        const QVector<OfxParamInfo> real = OfxHost::instance().paramsForSlot(slot);
        if (!real.isEmpty()) {
            return real;
        }
    }

    // Approximate fallback — used when the plug-in isn't installed, or the OFX
    // host couldn't fully load it (see OfxHost::paramsForSlot / ISSUES_AND_PLANS.md).
    QVector<OfxParamInfo> out;
    auto add = [&](const QString &label, const QString &key, double def, double lo, double hi) {
        OfxParamInfo info;
        info.name = key;
        info.label = label;
        info.defaultValue = def;
        info.minValue = lo;
        info.maxValue = hi;
        out.push_back(info);
    };
    auto addToggle = [&](const QString &label, const QString &key, bool def) {
        OfxParamInfo info;
        info.name = key;
        info.label = label;
        info.defaultValue = def ? 1.0 : 0.0;
        info.minValue = 0.0;
        info.maxValue = 1.0;
        info.toggle = true;
        out.push_back(info);
    };
    const QString n = slot.displayName;
    if (isColorCorrectorName(n)) {
        add(QObject::tr("Brightness"), QStringLiteral("brightness"), 0.0, -1.0, 1.0);
        add(QObject::tr("Contrast"), QStringLiteral("contrast"), 1.0, 0.0, 2.0);
        add(QObject::tr("Saturation"), QStringLiteral("saturation"), 1.0, 0.0, 2.0);
        add(QObject::tr("Gamma"), QStringLiteral("gamma"), 1.0, 0.1, 3.0);
    } else if (n.contains(QLatin1String("sepia"), Qt::CaseInsensitive)) {
        // Real params (com.vegascreativesoftware:sepia): Color (RGB, not a slider —
        // out of scope) + these two Double params.
        add(QObject::tr("Blending strength"), QStringLiteral("BlendingStrength"), 0.5, 0.0, 1.0);
        add(QObject::tr("Blending falloff"), QStringLiteral("BlendingFalloff"), 0.5, 0.0, 1.0);
    } else if (n.contains(QLatin1String("soft contrast"), Qt::CaseInsensitive)
               || n.contains(QLatin1String("softcontrast"), Qt::CaseInsensitive)) {
        // Real params (com.vegascreativesoftware:softcontrastvelvetmatter) — subset;
        // Vignette sub-group left out of the flat fallback list for now.
        add(QObject::tr("Stretch range"), QStringLiteral("EffectStretchRange"), 0.0, 0.0, 1.0);
        add(QObject::tr("Contrast"), QStringLiteral("EffectContrast"), 0.5, 0.0, 1.0);
        add(QObject::tr("Diffusion"), QStringLiteral("EffectDiffusion"), 0.5, 0.0, 1.0);
        add(QObject::tr("Low trim"), QStringLiteral("EffectLowTrim"), 0.0, 0.0, 1.0);
        add(QObject::tr("High trim"), QStringLiteral("EffectHighTrim"), 0.0, 0.0, 1.0);
    } else if (n.contains(QLatin1String("soften"), Qt::CaseInsensitive)
               || n.contains(QLatin1String("blur"), Qt::CaseInsensitive)
               || n.contains(QLatin1String("chroma"), Qt::CaseInsensitive)) {
        add(QObject::tr("Horizontal pixels"), QStringLiteral("radius"), 2.0, 1.0, 24.0);
        add(QObject::tr("Vertical pixels"), QStringLiteral("radiusV"), 2.0, 1.0, 24.0);
    } else if (n.contains(QLatin1String("glint"), Qt::CaseInsensitive)
               || n.contains(QLatin1String("мерцание"), Qt::CaseInsensitive)) {
        // Real params (com.vegascreativesoftware:glintvelvetmatter), from the <Glint> VEG XML
        // schema — matches the real Vegas "Glint" dialog's Effect tab (Mask sub-tab out of
        // scope for this flat fallback list).
        add(QObject::tr("Threshold"), QStringLiteral("Threshold"), 67.0, 0.0, 100.0);
        add(QObject::tr("Boost"), QStringLiteral("Boost"), -40.0, -100.0, 100.0);
        add(QObject::tr("Horizontal radius"), QStringLiteral("HorizontalRadius"), 50.0, 0.0, 100.0);
        add(QObject::tr("Vertical radius"), QStringLiteral("VerticalRadius"), 50.0, 0.0, 100.0);
        add(QObject::tr("Hue"), QStringLiteral("Hue"), 0.0, 0.0, 360.0);
        // Hue sweep runs both ways — projects really do store negative sweeps, and a
        // 0…360 slider would silently clamp them on import.
        add(QObject::tr("Hue sweep"), QStringLiteral("HueSweep"), 30.0, -360.0, 360.0);
        add(QObject::tr("Saturation"), QStringLiteral("Saturation"), 100.0, 0.0, 100.0);
        add(QObject::tr("Orientation"), QStringLiteral("Rotation"), 0.0, 0.0, 360.0);
        add(QObject::tr("Streaks"), QStringLiteral("Streaks"), 4.0, 0.0, 10.0);
        addToggle(QObject::tr("Reduce flicker"), QStringLiteral("ReduceFlicker"), false);
        addToggle(QObject::tr("Effect only"), QStringLiteral("EffectOnly"), false);
    } else if (n.contains(QLatin1String("brightness"), Qt::CaseInsensitive)) {
        add(QObject::tr("Brightness"), QStringLiteral("brightness"), 0.0, -1.0, 1.0);
        add(QObject::tr("Contrast"), QStringLiteral("contrast"), 1.0, 0.0, 2.0);
    } else if (!n.contains(QLatin1String("invert"), Qt::CaseInsensitive)) {
        add(QObject::tr("Gain"), QStringLiteral("gain"), 1.0, 0.0, 4.0);
    }
    return out;
}

void VegasVideoPluginCatalog::invalidateCache()
{
    g_cacheValid = false;
    g_cache.clear();
}

FxSlot videoFxSlotFromName(const QString &rawName)
{
    return VegasVideoPluginCatalog::slotFromDisplayName(rawName);
}

} // namespace openvegas
