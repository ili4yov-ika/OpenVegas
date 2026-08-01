#include "ui/RenderAsDialog.h"
#include "ui_RenderAsDialog.h"

#include <QFileDialog>

namespace openvegas {

RenderAsDialog::RenderAsDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::RenderAsDialog)
{
    ui->setupUi(this);
    if (ui->formatList->count() > 0) {
        ui->formatList->setCurrentRow(0);
    }
    if (ui->templateList->count() > 0) {
        ui->templateList->setCurrentRow(0);
    }
    connect(ui->browseButton, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Render As"), ui->outputPath->text(),
                                                          tr("Video (*.mp4 *.avi);;All (*.*)"));
        if (!path.isEmpty()) {
            ui->outputPath->setText(path);
        }
    });
}

RenderAsDialog::~RenderAsDialog()
{
    delete ui;
}

} // namespace openvegas
