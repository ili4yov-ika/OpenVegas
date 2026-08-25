// OpenVegas Capture — the recorder on its own.
//
// The same window the editor opens, run as its own program. That is the useful shape for a
// screen recorder: the thing being recorded is usually everything except the editor, and a
// recorder that only exists inside a running NLE cannot be started before it, cannot be left
// running while the editor is closed, and takes the editor down with it if it falls over.
//
// Nothing is duplicated to get here. `src/capture/` never depended on the editor, and the
// window only ever talked to it through one signal — so standalone is a different `main`
// and a different thing done with a finished take, not a second implementation.

#include "ui/CaptureWindow.h"

#include <QApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QMessageBox>
#include <QStringList>
#include <QUrl>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("OpenVegas"));
    QCoreApplication::setApplicationName(QStringLiteral("OpenVegas"));
    QGuiApplication::setApplicationDisplayName(QObject::tr("OpenVegas Capture"));

    openvegas::CaptureWindow window;

    // There is no project to drop a take onto here, so the take is handed to the desktop:
    // the folder opens with the files in it. Offering to import would need the editor.
    QObject::connect(&window, &openvegas::CaptureWindow::takeRecorded,
                     [&window](const QStringList &files) {
                         if (files.isEmpty()) {
                             return;
                         }
                         const QString folder = QFileInfo(files.first()).absolutePath();
                         const auto answer = QMessageBox::question(
                             &window, QObject::tr("OpenVegas Capture"),
                             QObject::tr("Recorded %n file(s). Open the folder?", "",
                                         int(files.size())));
                         if (answer == QMessageBox::Yes) {
                             QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
                         }
                     });

    // Closing the window ends the program: the window is the program.
    QObject::connect(&window, &QDialog::finished, &app, &QCoreApplication::quit);
    QApplication::setQuitOnLastWindowClosed(true);

    window.show();
    return app.exec();
}
