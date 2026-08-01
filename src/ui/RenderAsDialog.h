#pragma once

#include <QDialog>

namespace Ui {
class RenderAsDialog;
}

namespace openvegas {

class RenderAsDialog : public QDialog {
    Q_OBJECT
public:
    explicit RenderAsDialog(QWidget *parent = nullptr);
    ~RenderAsDialog() override;

private:
    Ui::RenderAsDialog *ui = nullptr;
};

} // namespace openvegas
