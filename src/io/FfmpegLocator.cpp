#include "io/FfmpegLocator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace openvegas {

QString FfmpegLocator::find()
{
    static QString cached;
    static bool tried = false;
    if (tried) {
        return cached;
    }
    tried = true;

    cached = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (!cached.isEmpty()) {
        return cached;
    }

#ifdef Q_OS_WIN
    const QString exeName = QStringLiteral("ffmpeg.exe");
#else
    const QString exeName = QStringLiteral("ffmpeg");
#endif

    // Bundled next to the application (installer / portable drop-in).
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        const QStringList besideApp = {
            QDir(appDir).filePath(exeName),
            QDir(appDir).filePath(QStringLiteral("ffmpeg/") + exeName),
            QDir(appDir).filePath(QStringLiteral("ffmpeg/bin/") + exeName),
            QDir(appDir).filePath(QStringLiteral("bin/") + exeName),
            QDir(appDir).filePath(QStringLiteral("tools/") + exeName),
            QDir(appDir).filePath(QStringLiteral("tools/ffmpeg/") + exeName),
            QDir(appDir).filePath(QStringLiteral("tools/ffmpeg/bin/") + exeName),
        };
        for (const QString &p : besideApp) {
            if (QFileInfo::exists(p)) {
                cached = QFileInfo(p).absoluteFilePath();
                return cached;
            }
        }
    }

    const QStringList extras = {
#ifdef Q_OS_WIN
        QStringLiteral("C:/ffmpeg/bin/ffmpeg.exe"),
        QStringLiteral("C:/ProgramData/chocolatey/bin/ffmpeg.exe"),
#else
        QStringLiteral("/usr/bin/ffmpeg"),
        QStringLiteral("/usr/local/bin/ffmpeg"),
#endif
    };
    for (const QString &p : extras) {
        if (QFileInfo::exists(p)) {
            cached = p;
            return cached;
        }
    }

#ifdef Q_OS_WIN
    // WinGet Gyan.FFmpeg layout
    const QString wingetRoot =
        QDir::homePath() + QStringLiteral("/AppData/Local/Microsoft/WinGet/Packages");
    const QDir wd(wingetRoot);
    if (wd.exists()) {
        const QFileInfoList packs =
            wd.entryInfoList({QStringLiteral("Gyan.FFmpeg*")}, QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &pack : packs) {
            const QStringList bins =
                QDir(pack.absoluteFilePath())
                    .entryList({QStringLiteral("ffmpeg-*-full_build")}, QDir::Dirs);
            for (const QString &b : bins) {
                const QString cand =
                    pack.absoluteFilePath() + QLatin1Char('/') + b + QStringLiteral("/bin/ffmpeg.exe");
                if (QFileInfo::exists(cand)) {
                    cached = cand;
                    return cached;
                }
            }
        }
    }
#endif
    return cached;
}


} // namespace openvegas
