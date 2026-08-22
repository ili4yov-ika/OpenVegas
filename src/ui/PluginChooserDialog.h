#pragma once

#include "plugins/PluginScanner.h"
#include "plugins/AudioPluginTypes.h"

#include <QDialog>
#include <QVector>

namespace Ui {
class PluginChooserDialog;
}

namespace openvegas {

class PluginChooserDialog : public QDialog {
    Q_OBJECT
public:
    explicit PluginChooserDialog(PluginScanner *scanner, QWidget *parent = nullptr);
    ~PluginChooserDialog() override;

    void setAudioMode(bool audio);
    bool audioMode() const { return m_audioMode; }

    void refresh();

    /** Selected audio plug-ins (audio mode, multi-select). */
    QVector<AudioPluginDesc> selectedAudioPlugins() const;
    /** Selected OFX name (video mode). */
    QString selectedPluginName() const;

private:
    void applyFilter();
    /** Put the scanned-source summary on the settings button's tooltip. */
    void setSourceHint(const QString &summary);
    /** Preferences, opened straight on the audio plug-in paths. */
    void openPluginSettings();
    void rebuildTree();
    void onCategoryChanged();

    Ui::PluginChooserDialog *ui = nullptr;
    PluginScanner *m_scanner = nullptr;
    bool m_audioMode = false;
    QVector<PluginInfo> m_ofxPlugins;
    QVector<AudioPluginDesc> m_audioPlugins;
    QString m_currentCategory;
};

} // namespace openvegas
