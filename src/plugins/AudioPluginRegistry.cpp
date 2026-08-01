#include "plugins/AudioPluginRegistry.h"
#include "plugins/BuiltinAudioCatalog.h"

#include <QSet>

namespace openvegas {

AudioPluginRegistry &AudioPluginRegistry::instance()
{
    static AudioPluginRegistry reg;
    return reg;
}

AudioPluginRegistry::AudioPluginRegistry(QObject *parent)
    : QObject(parent)
{
    refresh();
}

void AudioPluginRegistry::refresh()
{
    QStringList v1;
    QStringList v2;
    QStringList v3;
    AudioPluginScanner::loadPathsFromSettings(&v1, &v2, &v3);
    m_scanner.setVst1Paths(v1);
    m_scanner.setVst2Paths(v2);
    m_scanner.setVst3Paths(v3);

    m_all = BuiltinAudioCatalog::all();
    const QVector<AudioPluginDesc> scanned = m_scanner.scan();
    m_all += scanned;

    m_sourceSummary =
        QStringLiteral("Builtin %1 + VST scan: %2")
            .arg(BuiltinAudioCatalog::all().size())
            .arg(m_scanner.lastSourceSummary());
    emit refreshed();
}

QStringList AudioPluginRegistry::categories() const
{
    QSet<QString> set;
    for (const AudioPluginDesc &d : m_all) {
        if (!d.category.isEmpty()) {
            set.insert(d.category);
        }
    }
    QStringList ordered = {
        QStringLiteral("VEGAS"),
        QStringLiteral("Track Optimized"),
        QStringLiteral("Third Party"),
        QStringLiteral("VST"),
        QStringLiteral("5.1 FX"),
    };
    QStringList out;
    for (const QString &c : ordered) {
        if (set.contains(c)) {
            out << c;
            set.remove(c);
        }
    }
    QStringList rest = set.values();
    rest.sort(Qt::CaseInsensitive);
    out += rest;
    return out;
}

QVector<AudioPluginDesc> AudioPluginRegistry::filtered(const QString &category,
                                                       const QString &text) const
{
    QVector<AudioPluginDesc> out;
    for (const AudioPluginDesc &d : m_all) {
        if (!category.isEmpty() && category != QLatin1String("All")
            && d.category.compare(category, Qt::CaseInsensitive) != 0) {
            // "VST" folder also shows Vst1/Vst2/Vst3 regardless of category string
            if (!(category == QLatin1String("VST")
                  && (d.format == PluginFormat::Vst1 || d.format == PluginFormat::Vst2
                      || d.format == PluginFormat::Vst3))) {
                continue;
            }
        }
        if (!text.isEmpty() && !d.name.contains(text, Qt::CaseInsensitive)
            && !d.vendor.contains(text, Qt::CaseInsensitive)) {
            continue;
        }
        out.push_back(d);
    }
    return out;
}

} // namespace openvegas
