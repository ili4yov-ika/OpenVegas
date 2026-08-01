#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QRegularExpression>

namespace openvegas {

enum class PluginFormat {
    Builtin = 0,
    Vst1,
    Vst2,
    Vst3,
    Ofx
};

/** One plug-in instance in an event or track FX chain. */
struct FxSlot {
    QString pluginId;
    QString displayName;
    PluginFormat format = PluginFormat::Builtin;
    bool bypass = false;
    /** Opaque plug-in state chunk (empty until a real host saves it). */
    QByteArray state;

    bool operator==(const FxSlot &o) const
    {
        return pluginId == o.pluginId && displayName == o.displayName && format == o.format
               && bypass == o.bypass && state == o.state;
    }
    bool operator!=(const FxSlot &o) const { return !(*this == o); }
};

inline FxSlot makeFxSlot(const QString &name, PluginFormat format = PluginFormat::Builtin,
                         const QString &pluginId = {})
{
    FxSlot s;
    s.displayName = name;
    s.pluginId = pluginId.isEmpty() ? name : pluginId;
    s.format = format;
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

inline void ensureFxFirst(QVector<FxSlot> &chain, const QString &name,
                          PluginFormat format = PluginFormat::Builtin)
{
    removeFxByName(chain, name);
    chain.prepend(makeFxSlot(name, format));
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
    // "Fresh Air (VST2, 64 Bit)" / "Plug (VST, 64 Bit)" / "Auto-Key\t(VST3, 64 Bit)"
    {
        static const QRegularExpression vstSuffix(
            QStringLiteral(R"([\t ]*\((VST[123]?|OFX)[^)]*\)\s*$)"),
            QRegularExpression::CaseInsensitiveOption);
        name.replace(vstSuffix, QString());
        name = name.trimmed();
    }
    // "VEGAS Track Compressor" → keep full display name
    return makeFxSlot(name, pluginFormatFromVegName(raw), name);
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
