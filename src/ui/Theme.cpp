#include "ui/Theme.h"

#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QColor>
#include <QStyle>
#include <QStyleFactory>

namespace openvegas {

void applyTheme(QApplication &app)
{
    QPalette pal = app.palette();
    pal.setColor(QPalette::Window, QColor(0x1a, 0x1a, 0x1a));
    pal.setColor(QPalette::WindowText, QColor(0xe0, 0xe0, 0xe0));
    pal.setColor(QPalette::Base, QColor(0x25, 0x25, 0x25));
    pal.setColor(QPalette::AlternateBase, QColor(0x1e, 0x1e, 0x1e));
    pal.setColor(QPalette::Text, QColor(0xe0, 0xe0, 0xe0));
    pal.setColor(QPalette::Button, QColor(0x2b, 0x2b, 0x2b));
    pal.setColor(QPalette::ButtonText, QColor(0xe0, 0xe0, 0xe0));
    pal.setColor(QPalette::Highlight, QColor(0x00, 0x78, 0xd7));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::ToolTipBase, QColor(0x2b, 0x2b, 0x2b));
    pal.setColor(QPalette::ToolTipText, QColor(0xe0, 0xe0, 0xe0));
    app.setPalette(pal);
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        app.setStyle(fusion);
    }

    QFile qss(QStringLiteral(":/openvegas.qss"));
    if (qss.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
    }
}

} // namespace openvegas
