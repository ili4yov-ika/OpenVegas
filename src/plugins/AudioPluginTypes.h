#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>
#include <QRegularExpression>

namespace openvegas {

enum class PluginFormat {
    Builtin = 0,
    Vst1,
    Vst2,
    Vst3,
    Ofx,
    /**
     * VEGAS / Sound Forge Shared Plug-In. Each effect is a COM in-process class
     * registered DirectShow-style (Merit + Pins), hosted through the documented
     * DirectShow transform interfaces — see SoundForgeDsHost.
     *
     * Appended last on purpose: ProjectInterchange writes this enum as a plain int,
     * so existing .ovproj files keep their meaning only while earlier values stay put.
     */
    DirectShow
};

/** One plug-in instance in an event or track FX chain. */
struct FxSlot {
    QString pluginId;
    QString displayName;
    PluginFormat format = PluginFormat::Builtin;
    bool bypass = false;
    /** Opaque plug-in state chunk (empty until a real host saves it). */
    QByteArray state;
    /**
     * Stable id for host instance maps. Survives AudioGraph copy of fxChain so
     * VST/OFX process still finds the loaded module (pointer identity must not be used).
     */
    QString hostKey;

    bool operator==(const FxSlot &o) const
    {
        return pluginId == o.pluginId && displayName == o.displayName && format == o.format
               && bypass == o.bypass && state == o.state && hostKey == o.hostKey;
    }
    bool operator!=(const FxSlot &o) const { return !(*this == o); }
};

inline void ensureFxHostKey(FxSlot *slot)
{
    if (slot && slot->hostKey.isEmpty()) {
        slot->hostKey = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
}

inline FxSlot makeFxSlot(const QString &name, PluginFormat format = PluginFormat::Builtin,
                         const QString &pluginId = {})
{
    FxSlot s;
    s.displayName = name;
    s.pluginId = pluginId.isEmpty() ? name : pluginId;
    s.format = format;
    ensureFxHostKey(&s);
    return s;
}

inline QStringList fxNames(const QVector<FxSlot> &chain)
{
    QStringList out;
    out.reserve(chain.size());
    for (const FxSlot &s : chain) {
        out.push_back(s.displayName);
    }
    return out;
}

inline int indexOfFxName(const QVector<FxSlot> &chain, const QString &name)
{
    for (int i = 0; i < chain.size(); ++i) {
        if (chain[i].displayName.compare(name, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

inline void removeFxByName(QVector<FxSlot> &chain, const QString &name)
{
    for (int i = chain.size() - 1; i >= 0; --i) {
        if (chain[i].displayName.compare(name, Qt::CaseInsensitive) == 0) {
            chain.removeAt(i);
        }
    }
}

// Forward decls: a Media Generator / Titles & Text event's own content lives in
// chain[0] (see isTitlesTextName/isMediaGeneratorPluginId below) — ensureFxFirst needs
// to recognize and preserve it, so it can't just always prepend at index 0.
inline bool isTitlesTextName(const QString &displayName);
inline bool isMediaGeneratorPluginId(const QString &pluginId);

inline void ensureFxFirst(QVector<FxSlot> &chain, const QString &name,
                          PluginFormat format = PluginFormat::Builtin)
{
    removeFxByName(chain, name);
    // chain[0] on a generator event IS the event's own picture, not a stacked video
    // effect — every fxChain.first()/fxChain[0]-based lookup (TitlesTextEditorDialog,
    // TimelineView's generator-thumbnail detection, MainWindow's Fx-button routing,
    // VideoCompositor's render) depends on it staying at index 0. Insert right after it
    // instead of displacing it, so it also doesn't show up as a movable/removable node
    // in VideoEventFxDialogExact's plugin chain UI (see firstPluginChainIndex()).
    const bool hasGenerator = !chain.isEmpty()
        && (isTitlesTextName(chain.first().displayName)
            || isMediaGeneratorPluginId(chain.first().pluginId));
    chain.insert(hasGenerator ? 1 : 0, makeFxSlot(name, format));
}

inline PluginFormat pluginFormatFromVegName(const QString &raw)
{
    if (raw.startsWith(QLatin1String("OFX:"), Qt::CaseInsensitive)) {
        return PluginFormat::Ofx;
    }
    if (raw.contains(QLatin1String("VST3"), Qt::CaseInsensitive)
        || raw.endsWith(QLatin1String(".vst3"), Qt::CaseInsensitive)) {
        return PluginFormat::Vst3;
    }
    if (raw.contains(QLatin1String("VST2"), Qt::CaseInsensitive)) {
        return PluginFormat::Vst2;
    }
    // Vegas often tags VST 1.x as "(VST, 64 Bit)" or "(VST1, …)".
    if (raw.contains(QLatin1String("VST1"), Qt::CaseInsensitive)
        || raw.contains(QLatin1String("(VST,"), Qt::CaseInsensitive)
        || raw.contains(QLatin1String("(VST "), Qt::CaseInsensitive)) {
        return PluginFormat::Vst1;
    }
    return PluginFormat::Builtin;
}

inline FxSlot fxSlotFromVegName(const QString &raw)
{
    QString name = raw.trimmed();
    if (name.startsWith(QLatin1String("OFX:"), Qt::CaseInsensitive)) {
        name = name.mid(4).trimmed();
    }
    if (name.startsWith(QLatin1String("{Svfx:"), Qt::CaseInsensitive)) {
        // "{Svfx:Name…}" → Name
        const int colon = name.indexOf(QLatin1Char(':'));
        const int end = name.indexOf(QLatin1Char('}'));
        if (colon >= 0) {
            name = name.mid(colon + 1, end > colon ? end - colon - 1 : -1).trimmed();
        }
    }
    // Vegas OFX Color Grading / Color Corrector → OpenVegas builtins
    if (name.contains(QLatin1String("colorgrading"), Qt::CaseInsensitive)
        || name.compare(QLatin1String("com.vegascreativesoftware:colorgrading"),
                        Qt::CaseInsensitive)
               == 0) {
        return makeFxSlot(QStringLiteral("Color Grading"), PluginFormat::Builtin,
                          QStringLiteral("builtin:Color Grading"));
    }
    if (name.contains(QLatin1String("colorcorrector"), Qt::CaseInsensitive)
        || name.compare(QLatin1String("Color Corrector"), Qt::CaseInsensitive) == 0) {
        return makeFxSlot(QStringLiteral("Color Corrector"), PluginFormat::Builtin,
                          QStringLiteral("builtin:Color Corrector"));
    }
    if (name.compare(QLatin1String("Brightness and Contrast"), Qt::CaseInsensitive) == 0) {
        return makeFxSlot(QStringLiteral("Brightness and Contrast"), PluginFormat::Builtin,
                          QStringLiteral("builtin:Brightness and Contrast"));
    }
    if (name.contains(QLatin1String("chromablur"), Qt::CaseInsensitive)) {
        return makeFxSlot(QStringLiteral("Chroma Blur"), PluginFormat::Ofx, name);
    }
    if (name.contains(QLatin1String("glint"), Qt::CaseInsensitive)) {
        return makeFxSlot(QStringLiteral("Glint"), PluginFormat::Ofx, name);
    }
    if (name.contains(QLatin1String("softcontrast"), Qt::CaseInsensitive)) {
        return makeFxSlot(QStringLiteral("Soft Contrast"), PluginFormat::Ofx, name);
    }
    if (name.contains(QLatin1String(":sepia"), Qt::CaseInsensitive)
        || name.endsWith(QLatin1String("sepia"), Qt::CaseInsensitive)) {
        return makeFxSlot(QStringLiteral("Sepia"), PluginFormat::Ofx, name);
    }
    if (name.contains(QLatin1String("autoframe"), Qt::CaseInsensitive)) {
        return makeFxSlot(QStringLiteral("Auto Frame"), PluginFormat::Ofx, name);
    }
    // "Fresh Air (VST2, 64 Bit)" / "Plug (VST, 64 Bit)" / "Auto-Key\t(VST3, 64 Bit)"
    {
        static const QRegularExpression vstSuffix(
            QStringLiteral(R"([\t ]*\((VST[123]?|OFX)[^)]*\)\s*$)"),
            QRegularExpression::CaseInsensitiveOption);
        name.replace(vstSuffix, QString());
        name = name.trimmed();
    }
    const PluginFormat fmt = pluginFormatFromVegName(raw);
    // OpenVegas builtins: drop "VEGAS " brand from Track Noise Gate / EQ / Compressor labels.
    if (fmt == PluginFormat::Builtin && name.startsWith(QLatin1String("VEGAS "), Qt::CaseInsensitive)) {
        name = name.mid(6).trimmed();
    }
    // Shared Plug-Ins / XFX / ExpressFX / TrackFX → builtin substitute when mapped.
    // (Full resolve lives in VegasSharedAudioCatalog; keep a light inline alias table here
    // so AudioPluginTypes.h stays header-only without pulling the catalog.)
    if (fmt == PluginFormat::Builtin) {
        static const QList<QPair<QString, QString>> sharedAliases = {
            {QStringLiteral("TrackEQ"), QStringLiteral("Track EQ")},
            {QStringLiteral("TrackFX"), QStringLiteral("Track EQ")},
            {QStringLiteral("Graphic EQ"), QStringLiteral("Track EQ")},
            {QStringLiteral("Parametric EQ"), QStringLiteral("Track EQ")},
            {QStringLiteral("Paragraphic EQ"), QStringLiteral("Track EQ")},
            {QStringLiteral("ExpressFX Reverb"), QStringLiteral("Reverb")},
            {QStringLiteral("ExpressFX Flange"), QStringLiteral("Flange")},
        };
        for (const auto &a : sharedAliases) {
            if (name.compare(a.first, Qt::CaseInsensitive) == 0) {
                name = a.second;
                break;
            }
        }
    }
    return makeFxSlot(name, fmt, name);
}

/** Strip "VEGAS " brand from OpenVegas builtin Track FX display names. */
inline QString builtinFxDisplayName(const QString &raw)
{
    QString n = raw.trimmed();
    if (n.startsWith(QLatin1String("VEGAS "), Qt::CaseInsensitive)) {
        n = n.mid(6).trimmed();
    }
    return n;
}

/**
 * Case/spacing/brand-insensitive key for matching VEGAS plug-in names
 * (VEG XML labels, UI display names, alias tables) against each other.
 */
inline QString normalizeVegasPluginKey(const QString &raw)
{
    QString n = builtinFxDisplayName(raw);
    n.replace(QLatin1Char('_'), QLatin1Char(' '));
    n.replace(QLatin1Char('-'), QLatin1Char(' '));
    while (n.contains(QLatin1String("  "))) {
        n.replace(QLatin1String("  "), QStringLiteral(" "));
    }
    return n.toLower();
}

inline void normalizeBuiltinFxSlot(FxSlot *slot)
{
    if (!slot || slot->format != PluginFormat::Builtin) {
        return;
    }
    const QString clean = builtinFxDisplayName(slot->displayName);
    if (clean == slot->displayName) {
        return;
    }
    slot->displayName = clean;
    if (slot->pluginId.startsWith(QLatin1String("VEGAS "), Qt::CaseInsensitive)
        || slot->pluginId.startsWith(QLatin1String("builtin:VEGAS "), Qt::CaseInsensitive)) {
        slot->pluginId = QStringLiteral("builtin:") + clean;
    } else if (slot->pluginId.startsWith(QLatin1String("builtin:"), Qt::CaseInsensitive)) {
        slot->pluginId = QStringLiteral("builtin:") + clean;
    }
}

/** Map Video FX pane / chooser name to FxSlot (see VegasVideoPluginCatalog.cpp). */
FxSlot videoFxSlotFromName(const QString &rawName);

// Builtin video FX name predicates — single source of truth for classifying a
// display name as one of OpenVegas's built-in video effects (vs. an OFX plug-in).
// Shared by ColorCorrectorApply (compositing) and VegasVideoPluginCatalog (chooser/import).

inline bool isPanCropName(const QString &displayName)
{
    return displayName.trimmed().compare(QLatin1String("Pan/Crop"), Qt::CaseInsensitive) == 0;
}

inline bool isBrightnessContrastName(const QString &displayName)
{
    return displayName.trimmed().compare(QLatin1String("Brightness and Contrast"),
                                         Qt::CaseInsensitive)
           == 0;
}

inline bool isColorCorrectorName(const QString &displayName)
{
    const QString n = displayName.trimmed();
    if (isBrightnessContrastName(n)) {
        return false;
    }
    return n.compare(QLatin1String("Color Corrector"), Qt::CaseInsensitive) == 0
           || n.contains(QLatin1String("colorcorrector"), Qt::CaseInsensitive);
}

inline bool isColorGradingName(const QString &displayName)
{
    const QString n = displayName.trimmed();
    return n.compare(QLatin1String("Color Grading"), Qt::CaseInsensitive) == 0
           || n.contains(QLatin1String("colorgrading"), Qt::CaseInsensitive);
}

inline bool isTitlesTextName(const QString &displayName)
{
    const QString n = displayName.trimmed();
    return n.compare(QLatin1String("Titles & Text"), Qt::CaseInsensitive) == 0
           || n.compare(QLatin1String("VEGAS Titles & Text"), Qt::CaseInsensitive) == 0
           || n.contains(QLatin1String("titlesandtext"), Qt::CaseInsensitive);
}

/** True for a non-text Media Generator instance (Checkerboard, Color Gradient, …) —
 *  see MediaGeneratorApply.h's mediaGeneratorSlotFor(). Matched on pluginId (a stable
 *  internal id), not displayName, since displayName holds the human-readable plugin name. */
inline bool isMediaGeneratorPluginId(const QString &pluginId)
{
    return pluginId.startsWith(QLatin1String("builtin:MediaGenerator:"), Qt::CaseInsensitive);
}

/** Descriptor for discovery / Plug-In Chooser (not an instance). */
struct AudioPluginDesc {
    QString id;
    QString name;
    QString vendor;
    PluginFormat format = PluginFormat::Builtin;
    QString path;
    QString category; // VEGAS | Third Party | Track Optimized | 5.1 FX | VST | …
    bool automatable = true;
    bool trackOptimized = false;
    /**
     * Instrument (VSTi) rather than an effect — drives the chooser's icon.
     *
     * The scanner never loads plug-in binaries, so the synth flag itself is out of
     * reach; this is set only when the *installer's own folder layout* says so
     * (a directory literally named "VSTi" / "Instruments" / …). Anything unclear
     * stays false, because in an effects chooser calling an effect an instrument is
     * the more misleading of the two mistakes.
     */
    bool isInstrument = false;
};

inline FxSlot fxSlotFromDesc(const AudioPluginDesc &d)
{
    FxSlot s = makeFxSlot(d.name, d.format, d.id.isEmpty() ? d.name : d.id);
    if (!d.path.isEmpty()
        && (d.format == PluginFormat::Vst1 || d.format == PluginFormat::Vst2
            || d.format == PluginFormat::Vst3)) {
        const QString prefix = d.format == PluginFormat::Vst3   ? QStringLiteral("vst3:")
                               : d.format == PluginFormat::Vst1 ? QStringLiteral("vst1:")
                                                                : QStringLiteral("vst2:");
        s.pluginId = prefix + d.path;
    }
    return s;
}

} // namespace openvegas
