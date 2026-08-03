#pragma once

#include "plugins/AudioPluginTypes.h"
#include "plugins/PluginScanner.h"

#include <QImage>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

namespace openvegas {

/** Metadata for one discovered OFX plug-in. */
struct OfxPluginDesc {
    QString name;
    QString path;       // preferred binary (.ofx) or bundle root
    QString bundlePath; // .ofx.bundle directory if any
    QString archHint;   // e.g. Win64 / MacOS / Linux-x86-64
    QString apiLabel = QStringLiteral("OFX");
    bool hasBinary = false;
};

/**
 * Minimal OFX host: discover/describe, LoadLibrary load, instance cache,
 * CPU frame process for simple filter plugs, plus emulated Soften/Invert/…
 */
class OfxHost {
public:
    static OfxHost &instance();

    /** Discover plugins using scanner candidate roots (or an explicit root). */
    static QVector<OfxPluginDesc> discover(const PluginScanner &scanner);
    static QVector<OfxPluginDesc> discoverInRoot(const QString &root);

    /** Describe a single bundle/binary path without executing the plug-in. */
    static OfxPluginDesc describe(const QString &path);

    /**
     * Load .ofx via QLibrary, resolve OfxGetPlugin, run setHost + Load + Describe.
     * Fail soft: returns false + error string; never aborts.
     */
    static bool load(const OfxPluginDesc &desc, QString *errorOut = nullptr);

    static QStringList supportedArchFolderNames();

    /**
     * Create a process instance (caches by path + plugin index).
     * Returns id > 0 on success, 0 on failure.
     */
    int createInstance(const OfxPluginDesc &desc, QString *errorOut = nullptr);
    void destroyInstance(int id);

    /**
     * Process one RGBA frame through a loaded OFX instance.
     * Updates *rgba in place. Fail soft on errors.
     */
    bool processFrame(int instanceId, QImage *rgba, double timeSec, const QVariantMap &params,
                      QString *errorOut = nullptr);

    /**
     * Emulated video FX by display name: Soften/Blur, Invert, Sepia,
     * Brightness and Contrast, Gain.
     */
    static bool processEmulated(QImage *rgba, const QString &displayName,
                                const QVariantMap &params);

    /**
     * Try real OFX instance if binary path is known / already loaded;
     * otherwise fall back to processEmulated by displayName.
     */
    bool processSlot(FxSlot &slot, QImage *rgba, double timeSec = 0.0);

private:
    OfxHost();
    ~OfxHost();
    OfxHost(const OfxHost &) = delete;
    OfxHost &operator=(const OfxHost &) = delete;

    struct Impl;
    Impl *m_ = nullptr;
};

} // namespace openvegas
