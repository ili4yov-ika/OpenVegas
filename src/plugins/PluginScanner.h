#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace openvegas {

struct PluginInfo {
    QString name;
    QString path;
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
    QVector<PluginInfo> scanDirectory(const QString &ofxRoot) const;
    QString m_preferredPath;
    QString m_vegasProPath;
    mutable QString m_lastSource;
};

} // namespace openvegas
