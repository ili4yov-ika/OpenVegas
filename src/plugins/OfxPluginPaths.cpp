#include "plugins/OfxPluginPaths.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSysInfo>

namespace openvegas {
namespace {

/** Separator used inside OFX_PLUGIN_PATH — the platform's PATH separator. */
QChar pluginPathSeparator()
{
#ifdef Q_OS_WIN
    return QLatin1Char(';');
#else
    return QLatin1Char(':');
#endif
}

void appendUnique(QStringList *list, const QString &path)
{
    if (path.trimmed().isEmpty()) {
        return;
    }
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
    if (!list->contains(clean, Qt::CaseInsensitive)) {
        list->append(clean);
    }
}

} // namespace

QStringList OfxPluginPaths::standardRootsUnfiltered()
{
    QStringList roots;
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    // The standard's own override. Present on every platform and takes precedence:
    // a user who sets it is telling every OFX host, not just this one, where to look.
    const QString override = env.value(QStringLiteral("OFX_PLUGIN_PATH"));
    if (!override.isEmpty()) {
        const QStringList parts = override.split(pluginPathSeparator(), Qt::SkipEmptyParts);
        for (const QString &p : parts) {
            appendUnique(&roots, p);
        }
    }

    const QString home = QDir::homePath();

#if defined(Q_OS_WIN)
    const QString commonFiles = env.value(QStringLiteral("CommonProgramFiles"));
    if (!commonFiles.isEmpty()) {
        appendUnique(&roots, QDir(commonFiles).filePath(QStringLiteral("OFX/Plugins")));
    }
    const QString commonFilesX86 = env.value(QStringLiteral("CommonProgramFiles(x86)"));
    if (!commonFilesX86.isEmpty()) {
        appendUnique(&roots, QDir(commonFilesX86).filePath(QStringLiteral("OFX/Plugins")));
    }
    appendUnique(&roots, QStringLiteral("C:/Program Files/Common Files/OFX/Plugins"));
#elif defined(Q_OS_MACOS)
    appendUnique(&roots, QStringLiteral("/Library/OFX/Plugins"));
    appendUnique(&roots, QDir(home).filePath(QStringLiteral("Library/OFX/Plugins")));
#else
    appendUnique(&roots, QStringLiteral("/usr/OFX/Plugins"));
    appendUnique(&roots, QStringLiteral("/usr/local/OFX/Plugins"));
    appendUnique(&roots, QStringLiteral("/opt/OFX/Plugins"));
    appendUnique(&roots, QDir(home).filePath(QStringLiteral("OFX/Plugins")));
    appendUnique(&roots, QDir(home).filePath(QStringLiteral(".local/share/OFX/Plugins")));
#endif

    return roots;
}

QStringList OfxPluginPaths::standardRoots()
{
    QStringList out;
    for (const QString &root : standardRootsUnfiltered()) {
        if (QFileInfo(root).isDir()) {
            out << root;
        }
    }
    return out;
}

QStringList OfxPluginPaths::knownArchFolderNames()
{
    return {
        QStringLiteral("Win64"),         QStringLiteral("Win32"),
        QStringLiteral("MacOS"),         QStringLiteral("MacOS-x86-64"),
        QStringLiteral("MacOS-arm-64"),  QStringLiteral("Linux-x86-64"),
        QStringLiteral("Linux-x86"),     QStringLiteral("Linux-arm-64"),
        QStringLiteral("Linux-arm-32"),  QStringLiteral("FreeBSD-x86-64"),
    };
}

QStringList OfxPluginPaths::loadableArchFolderNames()
{
    const QString cpu = QSysInfo::currentCpuArchitecture();
    const bool arm = cpu.startsWith(QStringLiteral("arm"), Qt::CaseInsensitive);
    const bool bits64 = cpu.contains(QStringLiteral("64"));
    Q_UNUSED(arm);
    Q_UNUSED(bits64);

#if defined(Q_OS_WIN)
    // A 32-bit DLL cannot be loaded into a 64-bit process and vice versa, so only the
    // matching word size is ever offered.
    return bits64 ? QStringList{QStringLiteral("Win64")} : QStringList{QStringLiteral("Win32")};
#elif defined(Q_OS_MACOS)
    // Bundles may ship a single fat "MacOS" binary or per-arch directories; on Apple
    // silicon a fat binary and the x86-64 slice are both usable (via Rosetta for the
    // latter), so both are listed, native first.
    QStringList out;
    if (arm) {
        out << QStringLiteral("MacOS-arm-64");
    }
    out << QStringLiteral("MacOS");
    out << QStringLiteral("MacOS-x86-64");
    return out;
#else
    if (arm) {
        return bits64 ? QStringList{QStringLiteral("Linux-arm-64")}
                      : QStringList{QStringLiteral("Linux-arm-32")};
    }
    return bits64 ? QStringList{QStringLiteral("Linux-x86-64")}
                  : QStringList{QStringLiteral("Linux-x86")};
#endif
}

QString OfxPluginPaths::hostArchFolderName()
{
    const QStringList loadable = loadableArchFolderNames();
    return loadable.isEmpty() ? QString() : loadable.first();
}

bool OfxPluginPaths::isArchLoadable(const QString &archFolderName)
{
    if (archFolderName.isEmpty()) {
        // A flat bundle with no Contents/<arch> layout: whoever built it meant it for
        // whatever platform it was dropped on, so let the loader decide.
        return true;
    }
    return loadableArchFolderNames().contains(archFolderName, Qt::CaseInsensitive);
}

QString OfxPluginPaths::archIncompatibilityReason(const QString &archFolderName)
{
    if (isArchLoadable(archFolderName)) {
        return {};
    }
    return QStringLiteral("built for %1; this is a %2 build")
        .arg(archFolderName, hostArchFolderName());
}

} // namespace openvegas
