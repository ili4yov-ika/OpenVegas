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

protected:
    void accept() override;

private:
    void loadSettings();
    void saveSettings();

    Ui::PreferencesDialog *ui = nullptr;
};

} // namespace openvegas
