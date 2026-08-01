#pragma once

#include "plugins/AudioPluginTypes.h"
#include "plugins/AudioPluginScanner.h"

#include <QObject>
#include <QStringList>
#include <QVector>

namespace openvegas {

/**
 * Merged catalog: builtin Vegas/MAGIX-like FX + scanned VST1/VST2/VST3.
 * Used by Plug-In Chooser (audio mode) and Preferences Rescan.
 */
class AudioPluginRegistry : public QObject {
    Q_OBJECT
public:
    static AudioPluginRegistry &instance();

    void refresh();
    QVector<AudioPluginDesc> all() const { return m_all; }
    QVector<AudioPluginDesc> filtered(const QString &category, const QString &text) const;

    QStringList categories() const;
    QString sourceSummary() const { return m_sourceSummary; }

    AudioPluginScanner &scanner() { return m_scanner; }
    const AudioPluginScanner &scanner() const { return m_scanner; }

signals:
    void refreshed();

private:
    explicit AudioPluginRegistry(QObject *parent = nullptr);

    AudioPluginScanner m_scanner;
    QVector<AudioPluginDesc> m_all;
    QString m_sourceSummary;
};

} // namespace openvegas
