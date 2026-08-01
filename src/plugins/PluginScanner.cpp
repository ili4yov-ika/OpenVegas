#include "plugins/PluginScanner.h"

#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QSettings>

namespace openvegas {

QStringList PluginScanner::candidateRoots() const
{
    QStringList roots;
    if (!m_preferredPath.isEmpty()) {
        roots << m_preferredPath;
    }

    QSettings settings(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    const QString fromSettings = settings.value(QStringLiteral("plugins/ofxPath")).toString();
    if (!fromSettings.isEmpty()) {
        roots << fromSettings;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    roots << QDir(appDir).absoluteFilePath(QStringLiteral("vegas-runtime"));
    roots << QDir(appDir).absoluteFilePath(QStringLiteral("../vegas-runtime"));

    const QString sourceRoot = QDir(QCoreApplication::applicationDirPath())
                                   .absoluteFilePath(QStringLiteral("../../SAMPLES/VEGAS-PRO-22-PROGRAM-FILES"));
    roots << sourceRoot;

#ifdef Q_OS_WIN
    const QStringList steamGuesses = {
        QStringLiteral("C:/Program Files (x86)/Steam/steamapps/common/VEGAS Pro 22 Steam Edition/VEGAS Pro 22 Steam Edition"),
        QStringLiteral("C:/Program Files/VEGAS/VEGAS Pro 22"),
    };
    roots << steamGuesses;
#endif

    roots.removeDuplicates();
    return roots;
}

QVector<PluginInfo> PluginScanner::scanDirectory(const QString &root) const
{
    QVector<PluginInfo> out;
    QDir ofx(QDir(root).filePath(QStringLiteral("OFX Video Plug-Ins")));
    if (!ofx.exists()) {
        // root itself may already be OFX folder
        ofx = QDir(root);
    }
    if (!ofx.exists()) {
        return out;
    }

    const QFileInfoList entries = ofx.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                                                    QDir::Name);
    for (const QFileInfo &fi : entries) {
        PluginInfo info;
        info.name = fi.completeBaseName().isEmpty() ? fi.fileName() : fi.completeBaseName();
        info.path = fi.absoluteFilePath();
        out.push_back(info);
    }
    return out;
}

QVector<PluginInfo> PluginScanner::scanOfx() const
{
    m_lastSource.clear();
    for (const QString &root : candidateRoots()) {
        if (!QDir(root).exists() && !QDir(QDir(root).filePath(QStringLiteral("OFX Video Plug-Ins"))).exists()) {
            continue;
        }
        QVector<PluginInfo> found = scanDirectory(root);
        if (!found.isEmpty()) {
            m_lastSource = root;
            return found;
        }
        // Also try nested OFX folder existence with empty listing fallback label
        const QString ofxPath = QDir(root).filePath(QStringLiteral("OFX Video Plug-Ins"));
        if (QDir(ofxPath).exists()) {
            m_lastSource = ofxPath;
            PluginInfo stub;
            stub.name = QStringLiteral("(OFX folder found — empty or inaccessible)");
            stub.path = ofxPath;
            return {stub};
        }
    }
    m_lastSource = QStringLiteral("(no Vegas OFX path found)");
    return {
        {QStringLiteral("TitlesAndText (stub)"), QString()},
        {QStringLiteral("Color Corrector (stub)"), QString()},
        {QStringLiteral("Gaussian Blur (stub)"), QString()},
    };
}

} // namespace openvegas
