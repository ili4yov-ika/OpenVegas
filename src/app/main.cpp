#include "app/MainWindow.h"
#include "ui/FirstRunDialog.h"
#include "ui/Theme.h"
#include "video/NestedProjectSource.h"
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

    // Lets the filmstrip and preview caches draw a VEGAS project used as a clip. They
    // reach it through a hook rather than a direct call, so drawing thumbnails does not
    // drag project loading in with it.
    openvegas::NestedProjectSource::installAsFrameProvider();

    openvegas::MainWindow window;
    window.setWindowIcon(appIcon);
    window.show();

    QString openPath;
    if (argc >= 2) {
        openPath = openvegas::SamplePaths::resolveProjectPath(QString::fromLocal8Bit(argv[1]));
    }

    // Setup comes before anything else on a fresh install: which plug-in folders exist
    // decides what the rest of the session can even offer, and asking after a project is
    // already open means rescanning behind the user's back. It runs once and marks itself
    // done; Preferences remains the way to change any of it later.
    QTimer::singleShot(0, &window, [&window]() {
        if (openvegas::FirstRunDialog::runIfNeeded(&window)) {
            window.rescanPlugins();
        }
    });

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
