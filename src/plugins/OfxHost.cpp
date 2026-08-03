#include "plugins/OfxHost.h"

#include <QDir>
#include <QFileInfo>

namespace openvegas {
namespace {

QString tidyNameFromBundle(const QString &bundleName)
{
    QString n = bundleName;
    if (n.endsWith(QStringLiteral(".ofx.bundle"), Qt::CaseInsensitive)) {
        n.chop(QStringLiteral(".ofx.bundle").size());
    } else if (n.endsWith(QStringLiteral(".bundle"), Qt::CaseInsensitive)) {
        n.chop(QStringLiteral(".bundle").size());
    }
    return n;
}

QString findOfxBinaryInBundle(const QString &bundlePath, QString *archOut)
{
    const QDir contents(QDir(bundlePath).filePath(QStringLiteral("Contents")));
    if (!contents.exists()) {
        // Flat folder with a lone .ofx
        const QFileInfoList files =
            QDir(bundlePath).entryInfoList({QStringLiteral("*.ofx")}, QDir::Files);
        if (!files.isEmpty()) {
            if (archOut) {
                *archOut = QString();
            }
            return files.first().absoluteFilePath();
        }
        return {};
    }

    for (const QString &arch : OfxHost::supportedArchFolderNames()) {
        const QDir archDir(contents.filePath(arch));
        if (!archDir.exists()) {
            continue;
        }
        const QFileInfoList files = archDir.entryInfoList({QStringLiteral("*.ofx")}, QDir::Files);
        if (!files.isEmpty()) {
            if (archOut) {
                *archOut = arch;
            }
            return files.first().absoluteFilePath();
        }
    }

    // Any nested *.ofx under Contents/*/ 
    const auto entries = contents.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &d : entries) {
        const QFileInfoList files =
            QDir(d.absoluteFilePath()).entryInfoList({QStringLiteral("*.ofx")}, QDir::Files);
        if (!files.isEmpty()) {
            if (archOut) {
                *archOut = d.fileName();
            }
            return files.first().absoluteFilePath();
        }
    }
    return {};
}

OfxPluginDesc describeBundleOrFile(const QFileInfo &fi)
{
    OfxPluginDesc d;
    if (fi.isDir()) {
        d.bundlePath = fi.absoluteFilePath();
        d.name = tidyNameFromBundle(fi.fileName());
        QString arch;
        const QString bin = findOfxBinaryInBundle(fi.absoluteFilePath(), &arch);
        d.archHint = arch;
        if (!bin.isEmpty()) {
            d.path = bin;
            d.hasBinary = true;
        } else {
            d.path = fi.absoluteFilePath();
            d.hasBinary = false;
        }
        return d;
    }
    d.name = fi.completeBaseName();
    d.path = fi.absoluteFilePath();
    d.hasBinary = fi.suffix().compare(QStringLiteral("ofx"), Qt::CaseInsensitive) == 0;
    return d;
}

} // namespace

QStringList OfxHost::supportedArchFolderNames()
{
    return {
        QStringLiteral("Win64"),
        QStringLiteral("Win32"),
        QStringLiteral("MacOS"),
        QStringLiteral("Linux-x86-64"),
        QStringLiteral("Linux-x86"),
    };
}

OfxPluginDesc OfxHost::describe(const QString &path)
{
    const QFileInfo fi(path);
    if (!fi.exists()) {
        OfxPluginDesc d;
        d.name = fi.fileName();
        d.path = path;
        return d;
    }
    return describeBundleOrFile(fi);
}

QVector<OfxPluginDesc> OfxHost::discoverInRoot(const QString &root)
{
    QVector<OfxPluginDesc> out;
    if (root.isEmpty()) {
        return out;
    }

    QDir ofx(QDir(root).filePath(QStringLiteral("OFX Video Plug-Ins")));
    if (!ofx.exists()) {
        ofx = QDir(root);
    }
    if (!ofx.exists()) {
        return out;
    }

    const QFileInfoList entries =
        ofx.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &fi : entries) {
        const QString name = fi.fileName();
        const bool looksBundle = name.contains(QStringLiteral(".ofx"), Qt::CaseInsensitive)
                                 || fi.isDir();
        if (!looksBundle && !name.endsWith(QStringLiteral(".ofx"), Qt::CaseInsensitive)) {
            continue;
        }
        // Skip obvious non-plugin clutter
        if (name.startsWith(QLatin1Char('.'))) {
            continue;
        }
        OfxPluginDesc d = describeBundleOrFile(fi);
        if (d.name.isEmpty()) {
            continue;
        }
        out.push_back(d);
    }
    return out;
}

QVector<OfxPluginDesc> OfxHost::discover(const PluginScanner &scanner)
{
    QVector<OfxPluginDesc> out;
    QStringList seen;
    for (const QString &root : scanner.candidateRoots()) {
        const QVector<OfxPluginDesc> batch = discoverInRoot(root);
        if (batch.isEmpty()) {
            continue;
        }
        for (const OfxPluginDesc &d : batch) {
            const QString key = d.path.isEmpty() ? d.bundlePath : d.path;
            if (seen.contains(key)) {
                continue;
            }
            seen << key;
            out.push_back(d);
        }
        // First root that yields plugins wins (same policy as PluginScanner::scanOfx)
        if (!out.isEmpty()) {
            break;
        }
    }
    return out;
}

bool OfxHost::load(const OfxPluginDesc &desc, QString *errorOut)
{
    const QString msg =
        QStringLiteral("OFX host is a stub (Phase 2): cannot load \"%1\" yet — describe/discover only")
            .arg(desc.name.isEmpty() ? desc.path : desc.name);
    if (errorOut) {
        *errorOut = msg;
    }
    Q_UNUSED(desc.hasBinary);
    return false;
}

} // namespace openvegas
