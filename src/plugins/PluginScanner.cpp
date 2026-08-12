#include "plugins/PluginScanner.h"
#include "plugins/OfxPluginPaths.h"
#include "plugins/VegasVideoPluginCatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QProcessEnvironment>
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
        const QString pf86 = QProcessEnvironment::systemEnvironment().value(
            QStringLiteral("ProgramFiles(x86)"));
        if (!pf86.isEmpty()) {
            add(QDir(pf86).filePath(QStringLiteral("VEGAS/VEGAS Pro 22")));
        }
    }
#endif

    // The OpenFX standard's own locations, last so an explicitly configured or VEGAS
    // path still wins. These are the only roots that exist on Linux and macOS, where
    // there is no VEGAS install to inherit plug-ins from, so without them those builds
    // would find no OFX video plug-ins at all.
    for (const QString &root : OfxPluginPaths::standardRoots()) {
        add(root);
    }

    roots.removeDuplicates();
    return roots;
}

QVector<PluginInfo> PluginScanner::scanOfx() const
{
    const QVector<VegasVideoPluginEntry> found =
        VegasVideoPluginCatalog::discoverUsingScanner(*this, &m_lastSource);
    if (m_lastSource.isEmpty()) {
        m_lastSource = QStringLiteral("(no Vegas OFX path found)");
    }

    QVector<PluginInfo> out;
    out.reserve(found.size());
    for (const VegasVideoPluginEntry &e : found) {
        PluginInfo info;
        info.name = e.displayName;
        info.path = e.binaryPath;
        info.effectId = e.effectId;
        info.pluginId = VegasVideoPluginCatalog::formatPluginId(e);
        info.grouping = e.grouping;
        info.categories = e.categories;
        info.presets = e.presets;
        info.pluginIndex = e.pluginIndex;
        info.hasBinary = e.hasBinary;
        out.push_back(info);
    }
    return out;
}

} // namespace openvegas
