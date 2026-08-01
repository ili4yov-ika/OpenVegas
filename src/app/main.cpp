#include "app/MainWindow.h"
#include "ui/Theme.h"
#include "io/SamplePaths.h"

#include <QApplication>
#include <QIcon>
#include <QSettings>
#include <QTimer>
#include <QFileInfo>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("OpenVegas"));
    QApplication::setApplicationName(QStringLiteral("OpenVegas"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    const QIcon appIcon(QStringLiteral(":/icons/logo.svg"));
    QApplication::setWindowIcon(appIcon);

    openvegas::applyTheme(app);

    openvegas::MainWindow window;
    window.setWindowIcon(appIcon);
    window.show();

    QString openPath;
    if (argc >= 2) {
        openPath = openvegas::SamplePaths::resolveProjectPath(QString::fromLocal8Bit(argv[1]));
    }

    if (!openPath.isEmpty() && QFileInfo::exists(openPath)) {
        QTimer::singleShot(100, &window, [&window, openPath]() { window.openProjectPath(openPath); });
    } else {
        const bool showWelcome = QSettings().value(QStringLiteral("welcome/showOnStartup"), true).toBool();
        if (showWelcome) {
            QTimer::singleShot(200, &window, &openvegas::MainWindow::onWelcome);
        }
    }

    return app.exec();
}
