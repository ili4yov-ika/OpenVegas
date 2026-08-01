#pragma once

#include <QString>
#include <QStringList>

namespace openvegas {

/** Locate SAMPLES / veg_project trees relative to the running app or cwd. */
class SamplePaths {
public:
    /** First existing …/SAMPLES directory (source tree or next to build/). */
    static QString samplesDir();

    /** …/SAMPLES/veg_project — preferred File→Open start folder. */
    static QString vegProjectDir();

    /** Resolve a user/CLI path: absolute, cwd-relative, or under SAMPLES/veg_project. */
    static QString resolveProjectPath(const QString &pathOrName);

    /**
     * Sidecar Vegas EDL CSV.
     * Tries, in order:
     *  1) <vegDir>/edl-text-file/<vegBaseName>.txt
     *  2) SAMPLES/veg_project/edl-text-file/<vegBaseName>.txt
     *  3) same locations using @p altBaseNames (e.g. original name embedded in a renamed .veg)
     */
    static QString sidecarEdlPath(const QString &vegPath, const QStringList &altBaseNames = {});

    static QStringList candidateSamplesRoots();
};

} // namespace openvegas
