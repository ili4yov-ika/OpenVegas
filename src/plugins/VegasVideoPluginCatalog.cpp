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

QHash<QString, QStringList> parsePresetsXml(const QString &bundlePath)
{
    QHash<QString, QStringList> out;
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
        QStringLiteral(R"re(<OfxPreset\s+plugin="([^"]+)"[^>]*\sname="([^"]+)")re"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = presetRe.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString pluginId = m.captured(1).trimmed();
        const QString presetName = m.captured(2).trimmed();
        if (pluginId.isEmpty() || presetName.isEmpty()) {
            continue;
        }
        QStringList &list = out[pluginId];
        if (!list.contains(presetName, Qt::CaseInsensitive)) {
            list << presetName;
        }
    }
    return out;
}

QVector<VegasVideoPluginEntry> parseResourceXml(const QString &xmlPath, const QString &bundlePath,
                                                const QString &binaryPath,
                                                const QHash<QString, QStringList> &presets)
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
            e.categories = e.grouping.split(QLatin1Char('\\'), Qt::SkipEmptyParts);
            if (!e.categories.isEmpty() && e.categories.first().compare(QLatin1String("VEGAS"),
                                                                          Qt::CaseInsensitive)
                                               == 0) {
                e.categories.removeFirst();
            }
        }

        e.bundlePath = bundlePath;
        e.binaryPath = binaryPath;
        e.hasBinary = !binaryPath.isEmpty() && QFileInfo::exists(binaryPath);
        if (presets.contains(e.effectId)) {
            e.presets = presets.value(e.effectId);
        }
        out.push_back(std::move(e));
    }
    return out;
}

QVector<VegasVideoPluginEntry> parseBundle(const QString &bundlePath)
{
    const QString xmlPath = findPrimaryResourceXml(bundlePath);
    if (xmlPath.isEmpty()) {
        return {};
    }
    const QString binaryPath = findOfxBinaryInBundle(bundlePath);
    const QHash<QString, QStringList> presets = parsePresetsXml(bundlePath);
    return parseResourceXml(xmlPath, bundlePath, binaryPath, presets);
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
        if (!merged.isEmpty()) {
            break;
        }
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
        return slotFromEntry(*e);
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
        add(QObject::tr("Hue sweep"), QStringLiteral("HueSweep"), 30.0, 0.0, 360.0);
        add(QObject::tr("Saturation"), QStringLiteral("Saturation"), 100.0, 0.0, 100.0);
        add(QObject::tr("Orientation"), QStringLiteral("Rotation"), 0.0, 0.0, 360.0);
        add(QObject::tr("Streaks"), QStringLiteral("Streaks"), 4.0, 0.0, 10.0);
        add(QObject::tr("Reduce flicker"), QStringLiteral("ReduceFlicker"), 0.0, 0.0, 1.0);
        add(QObject::tr("Effect only"), QStringLiteral("EffectOnly"), 0.0, 0.0, 1.0);
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
