#include "plugins/PluginScanner.h"
#include "plugins/OfxHost.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace openvegas {

QString PluginScanner::sampleVegasProPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList guesses = {
        QDir(appDir).absoluteFilePath(QStringLiteral("../../SAMPLES/VEGAS-PRO-22-PROGRAM-FILES")),
        QDir(appDir).absoluteFilePath(QStringLiteral("../../../SAMPLES/VEGAS-PRO-22-PROGRAM-FILES")),
        QDir(appDir).absoluteFilePath(QStringLiteral("../SAMPLES/VEGAS-PRO-22-PROGRAM-FILES")),
    };
    for (const QString &g : guesses) {
        if (QDir(g).exists()) {
            return QDir(g).absolutePath();
        }
    }
    return {};
}

QStringList PluginScanner::candidateRoots() const
{
    QStringList roots;

    auto add = [&](const QString &p) {
        const QString t = p.trimmed();
        if (!t.isEmpty()) {
            roots << t;
        }
    };

    add(m_vegasProPath);

    QSettings settings(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    add(settings.value(QStringLiteral("plugins/vegasProPath")).toString());

    add(m_preferredPath);
    add(settings.value(QStringLiteral("plugins/ofxPath")).toString());

    const QString appDir = QCoreApplication::applicationDirPath();
    add(QDir(appDir).absoluteFilePath(QStringLiteral("vegas-runtime")));
    add(QDir(appDir).absoluteFilePath(QStringLiteral("../vegas-runtime")));

    add(sampleVegasProPath());

#ifdef Q_OS_WIN
    if (settings.value(QStringLiteral("plugins/useVegasOfx"), true).toBool()) {
        add(QStringLiteral(
            "C:/Program Files (x86)/Steam/steamapps/common/VEGAS Pro 22 Steam Edition/VEGAS Pro 22 Steam Edition"));
        add(QStringLiteral("C:/Program Files/VEGAS/VEGAS Pro 22"));
    }
#endif

    roots.removeDuplicates();
    return roots;
}

QVector<PluginInfo> PluginScanner::scanDirectory(const QString &root) const
{
    QVector<PluginInfo> out;
    const QVector<OfxPluginDesc> found = OfxHost::discoverInRoot(root);
    out.reserve(found.size());
    for (const OfxPluginDesc &d : found) {
        PluginInfo info;
        info.name = d.name;
        info.path = d.hasBinary ? d.path : (d.bundlePath.isEmpty() ? d.path : d.bundlePath);
        out.push_back(info);
    }
    return out;
}

QVector<PluginInfo> PluginScanner::scanOfx() const
{
    m_lastSource.clear();
    for (const QString &root : candidateRoots()) {
        if (!QDir(root).exists()
            && !QDir(QDir(root).filePath(QStringLiteral("OFX Video Plug-Ins"))).exists()) {
            continue;
        }
        QVector<PluginInfo> found = scanDirectory(root);
        if (!found.isEmpty()) {
            m_lastSource = root;
            return found;
        }
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
