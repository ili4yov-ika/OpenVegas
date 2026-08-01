#include "ui/EventPropertiesDialog.h"
#include "ui_EventPropertiesDialog.h"

namespace openvegas {

EventPropertiesDialog::EventPropertiesDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::EventPropertiesDialog)
{
    ui->setupUi(this);
}

EventPropertiesDialog::~EventPropertiesDialog()
{
    delete ui;
}

void EventPropertiesDialog::setEvent(const QString &name, double startSec, double lengthSec)
{
    ui->eventName->setText(name);
    ui->spinStart->setValue(startSec);
    ui->spinLength->setValue(lengthSec);
}

QString EventPropertiesDialog::eventName() const
{
    return ui->eventName->text();
}

double EventPropertiesDialog::startSec() const
{
    return ui->spinStart->value();
}

double EventPropertiesDialog::lengthSec() const
{
    return ui->spinLength->value();
}

} // namespace openvegas
