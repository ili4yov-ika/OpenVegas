#include "plugins/VegasSharedAudioCatalog.h"

#include "plugins/SoundForgeHost.h"

#include <QSettings>

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QProcessEnvironment>
#include <QSet>

namespace openvegas {
namespace {

VegasSharedFxEntry entry(const char *dll, const char *pack, const char *vegasName,
                         const char *builtinName, VegasSharedReplacementStatus st)
{
    VegasSharedFxEntry e;
    e.dllFileName = QString::fromUtf8(dll);
    e.packProductName = QString::fromUtf8(pack);
    e.vegasFxName = QString::fromUtf8(vegasName);
    e.openvegasBuiltinName = QString::fromUtf8(builtinName);
    e.status = st;
    return e;
}

} // namespace

QStringList VegasSharedAudioCatalog::defaultSharedRoots()
{
    QStringList roots;
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString pf86 = env.value(QStringLiteral("ProgramFiles(x86)"));
    if (!pf86.isEmpty()) {
        roots << QDir(pf86).filePath(QStringLiteral("VEGAS/Shared Plug-Ins"));
        roots << QDir(pf86).filePath(QStringLiteral("Sony/Shared Plug-Ins"));
    }
    // Stable fallbacks (CI / atypical env)
    roots << QStringLiteral("C:/Program Files (x86)/VEGAS/Shared Plug-Ins");
    roots << QStringLiteral("C:/Program Files (x86)/Sony/Shared Plug-Ins");
    roots.removeDuplicates();
    return roots;
}

QVector<VegasSharedFxEntry> VegasSharedAudioCatalog::catalog()
{
    using S = VegasSharedReplacementStatus;
    QVector<VegasSharedFxEntry> out;

    // TrackFX 1 (sftrkfx1) — RTTI CSfTEQ / CSfTComp
    out.push_back(entry("sftrkfx1_x64.dll", "TrackFX 1", "Track EQ", "Track EQ", S::Implemented));
    out.push_back(entry("sftrkfx1_x64.dll", "TrackFX 1", "Track Compressor", "Track Compressor",
                        S::Implemented));
    out.push_back(entry("sftrkfx1_x64.dll", "TrackFX 1", "Track Noise Gate", "Track Noise Gate",
                        S::Implemented));

    // Wave Hammer
    out.push_back(
        entry("mchammer_x64.dll", "Wave Hammer 5.1", "Wave Hammer Surround", "Wave Hammer", S::CatalogOnly));

    // XFX 1 — Chorus / Reverb / Pitch (RTTI CSfChorus / CSfReverb / CSfPitchs)
    out.push_back(entry("sfppack1_x64.dll", "XFX 1 Plug-In Pack", "Chorus", "Chorus", S::Implemented));
    out.push_back(entry("sfppack1_x64.dll", "XFX 1 Plug-In Pack", "Reverb", "Reverb", S::Implemented));
    out.push_back(entry("sfppack1_x64.dll", "XFX 1 Plug-In Pack", "Pitch Shift", "Pitch Shift",
                        S::CatalogOnly));

    // XFX 2 — Graphic / Parametric EQ family
    out.push_back(entry("sfppack2_x64.dll", "XFX 2 Plug-In Pack", "Graphic EQ", "Track EQ",
                        S::CatalogOnly));
    out.push_back(entry("sfppack2_x64.dll", "XFX 2 Plug-In Pack", "Parametric EQ", "Track EQ",
                        S::CatalogOnly));
    out.push_back(entry("sfppack2_x64.dll", "XFX 2 Plug-In Pack", "Paragraphic EQ", "Track EQ",
                        S::CatalogOnly));

    // XFX 3 — Flange / Distortion
    out.push_back(
        entry("sfppack3_x64.dll", "XFX 3 Plug-In Pack", "Flange/Wah-wah", "Flange", S::CatalogOnly));
    out.push_back(entry("sfppack3_x64.dll", "XFX 3 Plug-In Pack", "Distortion", "Distortion",
                        S::CatalogOnly));

    // ExpressFX packs
    out.push_back(entry("sfxpfx1_x64.dll", "ExpressFX 1", "ExpressFX Chorus", "ExpressFX Chorus",
                        S::Implemented));
    out.push_back(entry("sfxpfx1_x64.dll", "ExpressFX 1", "ExpressFX Distortion",
                        "ExpressFX Distortion", S::CatalogOnly));
    out.push_back(
        entry("sfxpfx1_x64.dll", "ExpressFX 1", "ExpressFX Flange/Wah-Wah", "Flange", S::CatalogOnly));
    out.push_back(
        entry("sfxpfx1_x64.dll", "ExpressFX 1", "ExpressFX Reverb", "Reverb", S::Implemented));

    out.push_back(entry("sfxpfx2_x64.dll", "ExpressFX 2", "ExpressFX Delay", "ExpressFX Delay",
                        S::Implemented));
    out.push_back(
        entry("sfxpfx2_x64.dll", "ExpressFX 2", "Simple Delay", "Simple Delay", S::Implemented));
    out.push_back(entry("sfxpfx2_x64.dll", "ExpressFX 2", "ExpressFX Equalization", "ExpressFX Equalization",
                        S::CatalogOnly));

    out.push_back(entry("sfxpfx3_x64.dll", "ExpressFX 3", "Amplitude Modulation",
                        "Amplitude Modulation", S::CatalogOnly));
    out.push_back(
        entry("sfxpfx3_x64.dll", "ExpressFX 3", "Smooth/Enhance", "Smooth/Enhance", S::CatalogOnly));
    out.push_back(entry("sfxpfx3_x64.dll", "ExpressFX 3", "Vibrato", "Vibrato", S::CatalogOnly));

    // Sound Forge / restoration lineage
    out.push_back(entry("sffrgpnv_x64.dll", "Sound Forge Pro Pan and Volume 1", "Volume", "Volume",
                        S::CatalogOnly));
    out.push_back(entry("sfresfilter_x64.dll", "Resonant Filter", "Resonant Filter", {}, S::Unmapped));
    out.push_back(entry("xpvinyl_x64.dll", "ExpressFX Audio Restoration", "ExpressFX Audio Restoration", {},
                        S::Unmapped));

    // Shared kernel present on VEGAS install only (not an FX by itself)
    out.push_back(entry("apluginsk.dll", "VEGAS Pro", "apluginsk (runtime)", {}, S::Unmapped));

    // Anything the packs actually register but the curated list above does not name yet.
    // The hand-written entries drifted from reality (Wave Hammer is registered as "Wave
    // Hammer Surround", Flange as "Flange/Wah-wah") and were missing a dozen effects
    // outright, so the inventory is completed from the registration itself rather than
    // maintained by hand. These arrive Unmapped: discovering an effect says nothing about
    // being able to run it.
    QSet<QString> known;
    for (const VegasSharedFxEntry &e : out) {
        known.insert(e.vegasFxName.toLower());
    }
    for (const SoundForgeClass &c : SoundForgeHost::discoverEffects()) {
        if (known.contains(c.name.toLower())) {
            continue;
        }
        known.insert(c.name.toLower());
        VegasSharedFxEntry e;
        e.dllFileName = c.dllFileName;
        e.packProductName = QStringLiteral("Shared Plug-Ins");
        e.vegasFxName = c.name;
        e.status = S::Unmapped;
        out.push_back(std::move(e));
    }

    return out;
}

QVector<VegasSharedInstalledPack> VegasSharedAudioCatalog::discoverInstalled(
    const QStringList &rootsIn)
{
    const QStringList roots = rootsIn.isEmpty() ? defaultSharedRoots() : rootsIn;
    QVector<VegasSharedInstalledPack> out;
    QStringList seenDll;

    // Prefer first root that has the DLL (VEGAS listed before Sony).
    const auto cat = catalog();
    QStringList uniqueDlls;
    for (const VegasSharedFxEntry &e : cat) {
        if (!uniqueDlls.contains(e.dllFileName, Qt::CaseInsensitive)) {
            uniqueDlls << e.dllFileName;
        }
    }

    for (const QString &dllName : uniqueDlls) {
        for (const QString &root : roots) {
            const QString path = QDir(root).filePath(QStringLiteral("Audio_x64/") + dllName);
            if (!QFileInfo::exists(path)) {
                continue;
            }
            VegasSharedInstalledPack pack;
            pack.dllFileName = dllName;
            pack.absolutePath = QDir::toNativeSeparators(path);
            pack.rootPath = QDir::toNativeSeparators(root);
            for (const VegasSharedFxEntry &e : cat) {
                if (e.dllFileName.compare(dllName, Qt::CaseInsensitive) == 0) {
                    pack.packProductName = e.packProductName;
                    break;
                }
            }
            out.push_back(pack);
            seenDll << dllName.toLower();
            break;
        }
    }
    Q_UNUSED(seenDll);
    return out;
}

bool VegasSharedAudioCatalog::anyInstalled(const QStringList &roots)
{
    return !discoverInstalled(roots).isEmpty();
}

QString VegasSharedAudioCatalog::resolveBuiltinName(const QString &vegasOrAliasName)
{
    const QString key = normalizeVegasPluginKey(vegasOrAliasName);
    if (key.isEmpty()) {
        return {};
    }

    // Direct aliases seen in VEG / UI
    static const QHash<QString, QString> aliases = {
        {QStringLiteral("trackeq"), QStringLiteral("Track EQ")},
        {QStringLiteral("track eq"), QStringLiteral("Track EQ")},
        {QStringLiteral("trackfx"), QStringLiteral("Track EQ")},
        {QStringLiteral("track compressor"), QStringLiteral("Track Compressor")},
        {QStringLiteral("track noise gate"), QStringLiteral("Track Noise Gate")},
        {QStringLiteral("noise gate"), QStringLiteral("Track Noise Gate")},
        {QStringLiteral("expressfx chorus"), QStringLiteral("ExpressFX Chorus")},
        {QStringLiteral("expressfx delay"), QStringLiteral("ExpressFX Delay")},
        {QStringLiteral("expressfx reverb"), QStringLiteral("Reverb")},
        {QStringLiteral("simple delay"), QStringLiteral("Simple Delay")},
    };
    if (aliases.contains(key)) {
        return aliases.value(key);
    }

    for (const VegasSharedFxEntry &e : catalog()) {
        if (e.status == VegasSharedReplacementStatus::Unmapped) {
            continue;
        }
        if (normalizeVegasPluginKey(e.vegasFxName) == key
            || normalizeVegasPluginKey(e.openvegasBuiltinName) == key) {
            return e.openvegasBuiltinName;
        }
    }
    return {};
}

FxSlot VegasSharedAudioCatalog::replacementSlot(const QString &vegasOrAliasName)
{
    if (substitutionPolicy() == VegasSharedSubstitution::UseOriginal) {
        // No substitution: hand back the registered plug-in itself when it is there.
        // When it is not, return nothing rather than quietly substituting anyway — the
        // whole point of this setting is that the caller asked for the original.
        const QString wanted = normalizeVegasPluginKey(vegasOrAliasName);
        for (const SoundForgeClass &c : SoundForgeHost::discoverEffects()) {
            if (normalizeVegasPluginKey(c.name) == wanted) {
                return makeFxSlot(c.name, PluginFormat::DirectShow,
                                  SoundForgeHost::pluginId(c.clsid));
            }
        }
        return {};
    }
    const QString builtin = resolveBuiltinName(vegasOrAliasName);
    if (builtin.isEmpty()) {
        return {};
    }
    return makeFxSlot(builtin, PluginFormat::Builtin, QStringLiteral("builtin:") + builtin);
}

namespace {
const char *kSubstitutionKey = "plugins/vegasSharedSubstitution";
} // namespace

VegasSharedSubstitution VegasSharedAudioCatalog::substitutionPolicy()
{
    QSettings settings(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    const int raw = settings.value(QString::fromLatin1(kSubstitutionKey),
                                   int(VegasSharedSubstitution::ReplaceWithBuiltin))
                        .toInt();
    return raw == int(VegasSharedSubstitution::UseOriginal)
               ? VegasSharedSubstitution::UseOriginal
               : VegasSharedSubstitution::ReplaceWithBuiltin;
}

void VegasSharedAudioCatalog::setSubstitutionPolicy(VegasSharedSubstitution policy)
{
    QSettings settings(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    settings.setValue(QString::fromLatin1(kSubstitutionKey), int(policy));
}

QVector<AudioPluginDesc> VegasSharedAudioCatalog::chooserDescriptors(bool onlyIfInstalled,
                                                                     const QStringList &roots)
{
    QSet<QString> installedDll;
    if (onlyIfInstalled) {
        for (const VegasSharedInstalledPack &p : discoverInstalled(roots)) {
            installedDll.insert(p.dllFileName.toLower());
        }
        if (installedDll.isEmpty()) {
            return {};
        }
    }

    QVector<AudioPluginDesc> out;
    QSet<QString> seenIds;

    // A name is never offered twice: whichever of the two the policy prefers wins, and
    // the other is left out. Two identical rows would be indistinguishable in the chooser
    // while behaving differently once inserted.
    const bool preferBuiltin =
        substitutionPolicy() == VegasSharedSubstitution::ReplaceWithBuiltin;

    QSet<QString> builtinNames;
    if (preferBuiltin) {
        for (const VegasSharedFxEntry &e : catalog()) {
            if (e.status != VegasSharedReplacementStatus::Unmapped
                && !e.openvegasBuiltinName.isEmpty()) {
                builtinNames.insert(normalizeVegasPluginKey(e.vegasFxName));
            }
        }
    }

    // The registered plug-ins: the real thing, hosted through DirectShow with their own
    // DSP and their own dialog. Under "replace" they step aside for the names OpenVegas
    // implements itself; everything else still comes from here, which is most of them.
    QSet<QString> hostedNames;
    for (const AudioPluginDesc &d : SoundForgeHost::pluginDescriptors()) {
        if (seenIds.contains(d.id) || builtinNames.contains(normalizeVegasPluginKey(d.name))) {
            continue;
        }
        seenIds.insert(d.id);
        hostedNames.insert(normalizeVegasPluginKey(d.name));
        out.push_back(d);
    }

    for (const VegasSharedFxEntry &e : catalog()) {
        if (e.status == VegasSharedReplacementStatus::Unmapped || e.openvegasBuiltinName.isEmpty()) {
            continue;
        }
        if (hostedNames.contains(normalizeVegasPluginKey(e.vegasFxName))) {
            continue;
        }
        if (onlyIfInstalled && !installedDll.contains(e.dllFileName.toLower())) {
            continue;
        }
        const QString id = QStringLiteral("builtin:") + e.openvegasBuiltinName;
        if (seenIds.contains(id)) {
            continue;
        }
        seenIds.insert(id);

        AudioPluginDesc d;
        d.id = id;
        d.name = e.openvegasBuiltinName;
        d.vendor = QStringLiteral("OpenVegas");
        d.format = PluginFormat::Builtin;
        d.category = QStringLiteral("VEGAS Shared");
        d.automatable = true;
        d.trackOptimized = e.dllFileName.startsWith(QLatin1String("sftrkfx"), Qt::CaseInsensitive);
        d.path = e.dllFileName; // reference only — never loaded
        out.push_back(d);
    }
    return out;
}

QVector<VegasSharedFxEntry> VegasSharedAudioCatalog::implementedEntries()
{
    QVector<VegasSharedFxEntry> out;
    for (const VegasSharedFxEntry &e : catalog()) {
        if (e.status == VegasSharedReplacementStatus::Implemented) {
            out.push_back(e);
        }
    }
    return out;
}

QString VegasSharedAudioCatalog::statusLabel(VegasSharedReplacementStatus s)
{
    switch (s) {
    case VegasSharedReplacementStatus::Implemented:
        return QStringLiteral("Implemented");
    case VegasSharedReplacementStatus::CatalogOnly:
        return QStringLiteral("CatalogOnly");
    case VegasSharedReplacementStatus::Unmapped:
        return QStringLiteral("Unmapped");
    }
    return QStringLiteral("?");
}

} // namespace openvegas
