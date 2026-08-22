#pragma once

#include <QDialog>

namespace Ui {
class PreferencesDialog;
}

namespace openvegas {

class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget *parent = nullptr);
    ~PreferencesDialog() override;

    QString ofxPath() const;
    QString vegasProPath() const;

    /** Open on the tab holding the audio plug-in search paths (VST1 / VST2 / VST3). */
    void showAudioPluginPaths();
    /** Open on the tab holding the OFX / VEGAS video plug-in paths. */
    void showVideoPluginPaths();

protected:
    void accept() override;

private:
    void loadSettings();
    void saveSettings();
    /** Fills the Video tab's status line from the trial-encode verdicts. */
    void refreshHwStatus();

    Ui::PreferencesDialog *ui = nullptr;
};

} // namespace openvegas
