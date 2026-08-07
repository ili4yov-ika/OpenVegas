#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace openvegas {

struct PluginInfo {
    QString name;
    QString path;       ///< OFX binary path when available
    QString effectId;
    QString pluginId;   ///< Full FxSlot pluginId (ofx:…)
    QString grouping;
    QStringList categories;
    QStringList presets;
    int pluginIndex = 0;
    bool hasBinary = false;
};

class PluginScanner {
public:
    QVector<PluginInfo> scanOfx() const;
    QStringList candidateRoots() const;
    QString resolvedSource() const { return m_lastSource; }

    void setPreferredPath(const QString &path) { m_preferredPath = path; }
    QString preferredPath() const { return m_preferredPath; }

    void setVegasProPath(const QString &path) { m_vegasProPath = path; }
    QString vegasProPath() const { return m_vegasProPath; }

    /** SAMPLES/VEGAS-PRO-22-PROGRAM-FILES relative to app or source tree, if present. */
    static QString sampleVegasProPath();

private:
    QString m_preferredPath;
    QString m_vegasProPath;
    mutable QString m_lastSource;
};

} // namespace openvegas
