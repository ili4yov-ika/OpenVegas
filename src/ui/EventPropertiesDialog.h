#pragma once

#include <QDialog>
#include <QString>

namespace Ui {
class EventPropertiesDialog;
}

namespace openvegas {

class EventPropertiesDialog : public QDialog {
    Q_OBJECT
public:
    explicit EventPropertiesDialog(QWidget *parent = nullptr);
    ~EventPropertiesDialog() override;

    void setEvent(const QString &name, double startSec, double lengthSec);
    QString eventName() const;
    double startSec() const;
    double lengthSec() const;

private:
    Ui::EventPropertiesDialog *ui = nullptr;
};

} // namespace openvegas
