#pragma once

#include "plugins/PluginScanner.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace openvegas {

/** Metadata for one discovered OFX plug-in (no binary process yet). */
struct OfxPluginDesc {
    QString name;
    QString path;       // preferred binary (.ofx) or bundle root
    QString bundlePath; // .ofx.bundle directory if any
    QString archHint;   // e.g. Win64 / MacOS / Linux-x86-64
    QString apiLabel = QStringLiteral("OFX");
    bool hasBinary = false;
};

/**
 * Phase 2 stub OFX host: discover + describe bundles from PluginScanner roots.
 * Does not LoadLibrary / dlopen proprietary Vegas or third-party OFX binaries yet.
 */
class OfxHost {
public:
    /** Discover plugins using scanner candidate roots (or an explicit root). */
    static QVector<OfxPluginDesc> discover(const PluginScanner &scanner);
    static QVector<OfxPluginDesc> discoverInRoot(const QString &root);

    /** Describe a single bundle/binary path without executing the plug-in. */
    static OfxPluginDesc describe(const QString &path);

    /**
     * Stub load: always fails with a clear reason until a real OFX host lands.
     * Returns false; sets *errorOut when non-null.
     */
    static bool load(const OfxPluginDesc &desc, QString *errorOut = nullptr);

    static QStringList supportedArchFolderNames();
};

} // namespace openvegas
