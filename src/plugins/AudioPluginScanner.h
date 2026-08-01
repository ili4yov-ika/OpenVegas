#pragma once

#include "plugins/AudioPluginTypes.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace openvegas {

/** Filesystem discovery for VST1/VST2 (.dll) and VST3 (.vst3) — path/name only, no LoadLibrary. */
class AudioPluginScanner {
public:
    QVector<AudioPluginDesc> scan() const;

    void setVst1Paths(const QStringList &paths) { m_vst1Paths = paths; }
    void setVst2Paths(const QStringList &paths) { m_vst2Paths = paths; }
    void setVst3Paths(const QStringList &paths) { m_vst3Paths = paths; }
    QStringList vst1Paths() const { return m_vst1Paths; }
    QStringList vst2Paths() const { return m_vst2Paths; }
    QStringList vst3Paths() const { return m_vst3Paths; }

    /** Default system folders + Preferences paths from QSettings. */
    static QStringList defaultVst1Roots();
    static QStringList defaultVst2Roots();
    static QStringList defaultVst3Roots();
    static void loadPathsFromSettings(QStringList *vst1, QStringList *vst2, QStringList *vst3);
    static void savePathsToSettings(const QStringList &vst1, const QStringList &vst2,
                                    const QStringList &vst3);

    QString lastSourceSummary() const { return m_lastSource; }

private:
    void scanDllDir(const QString &root, PluginFormat format, QVector<AudioPluginDesc> *out,
                    QSet<QString> *seenPaths) const;
    void scanVst3Dir(const QString &root, QVector<AudioPluginDesc> *out,
                     QSet<QString> *seenPaths) const;

    QStringList m_vst1Paths;
    QStringList m_vst2Paths;
    QStringList m_vst3Paths;
    mutable QString m_lastSource;
};

} // namespace openvegas
