#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;

namespace openvegas {

/** Vegas-style Warning dialog: message + "Do not show again" + Yes/No. */
class ConfirmWarningDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConfirmWarningDialog(const QString &message, QWidget *parent = nullptr);

    bool doNotShowAgain() const;

    /**
     * Show confirmation unless settingsKey is set in QSettings (skip).
     * On Yes + checkbox, writes settingsKey = true.
     * @return true if the user confirmed (or skip was active).
     */
    static bool confirm(QWidget *parent, const QString &message, const QString &settingsKey = {});

private:
    QCheckBox *m_doNotShow = nullptr;
};

} // namespace openvegas
