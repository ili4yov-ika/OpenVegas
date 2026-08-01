#include "io/SamplePaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace openvegas {

QStringList SamplePaths::candidateSamplesRoots()
{
    QStringList out;
    auto add = [&](const QString &p) {
        const QString clean = QDir::cleanPath(p);
        if (!clean.isEmpty() && !out.contains(clean)) {
            out.push_back(clean);
        }
    };

    // Hard-coded workspace (dev)
    add(QStringLiteral("D:/Devs/C++/OpenVegas/SAMPLES"));

    const QString appDir = QCoreApplication::applicationDirPath();
    add(appDir + QStringLiteral("/SAMPLES"));
    add(appDir + QStringLiteral("/../SAMPLES"));
    add(appDir + QStringLiteral("/../../SAMPLES"));
    add(appDir + QStringLiteral("/../../../SAMPLES"));

    const QString cwd = QDir::currentPath();
    add(cwd + QStringLiteral("/SAMPLES"));
    add(cwd + QStringLiteral("/../SAMPLES"));

    return out;
}

QString SamplePaths::samplesDir()
{
    for (const QString &root : candidateSamplesRoots()) {
        if (QDir(root).exists()) {
            return root;
        }
    }
    return {};
}

QString SamplePaths::vegProjectDir()
{
    for (const QString &root : candidateSamplesRoots()) {
        const QString vp = QDir(root).filePath(QStringLiteral("veg_project"));
        if (QDir(vp).exists()) {
            return vp;
        }
    }
    const QString samples = samplesDir();
    if (!samples.isEmpty()) {
        return QDir(samples).filePath(QStringLiteral("veg_project"));
    }
    return {};
}

QString SamplePaths::resolveProjectPath(const QString &pathOrName)
{
    if (pathOrName.isEmpty()) {
        return {};
    }
    const QFileInfo direct(pathOrName);
    if (direct.exists()) {
        return direct.absoluteFilePath();
    }

    const QFileInfo cwdRel(QDir::current().absoluteFilePath(pathOrName));
    if (cwdRel.exists()) {
        return cwdRel.absoluteFilePath();
    }

    const QString name = QFileInfo(pathOrName).fileName();
    const QStringList dirs = {
        vegProjectDir(),
        samplesDir(),
        QDir(samplesDir()).filePath(QStringLiteral("veg_project")),
    };
    for (const QString &d : dirs) {
        if (d.isEmpty()) {
            continue;
        }
        const QString cand = QDir(d).filePath(name);
        if (QFileInfo::exists(cand)) {
            return QFileInfo(cand).absoluteFilePath();
        }
        // Allow "SAMPLES/veg_project/foo.veg" relative fragments
        const QString nested = QDir(d).filePath(pathOrName);
        if (QFileInfo::exists(nested)) {
            return QFileInfo(nested).absoluteFilePath();
        }
    }
    return pathOrName;
}

QString SamplePaths::sidecarEdlPath(const QString &vegPath, const QStringList &altBaseNames)
{
    const QFileInfo fi(vegPath);
    if (!fi.exists()) {
        return {};
    }

    QStringList bases;
    auto addBase = [&](const QString &b) {
        const QString clean = b.trimmed();
        if (!clean.isEmpty() && !bases.contains(clean, Qt::CaseInsensitive)) {
            bases.push_back(clean);
        }
    };
    addBase(fi.completeBaseName());
    for (const QString &alt : altBaseNames) {
        addBase(QFileInfo(alt).completeBaseName());
    }

    QStringList edlDirs;
    auto addDir = [&](const QString &d) {
        const QString clean = QDir::cleanPath(d);
        if (!clean.isEmpty() && QDir(clean).exists() && !edlDirs.contains(clean)) {
            edlDirs.push_back(clean);
        }
    };
    addDir(QDir(fi.absolutePath()).filePath(QStringLiteral("edl-text-file")));
    const QString vp = vegProjectDir();
    if (!vp.isEmpty()) {
        addDir(QDir(vp).filePath(QStringLiteral("edl-text-file")));
    }

    for (const QString &base : bases) {
        const QString fileName = base + QStringLiteral(".txt");
        for (const QString &dir : edlDirs) {
            const QString cand = QDir(dir).filePath(fileName);
            if (QFileInfo::exists(cand)) {
                return cand;
            }
        }
    }
    return {};
}

} // namespace openvegas
