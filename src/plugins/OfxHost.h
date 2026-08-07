#pragma once

#include "plugins/AudioPluginTypes.h"
#include "plugins/PluginScanner.h"

#include <QHash>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

namespace openvegas {

/** Parsed OpenVegas OFX pluginId (ofx:<path>#<index>#<effectId> or ofx-id:<effectId>). */
struct OfxPluginIdParts {
    QString path;
    int index = 0;
    QString effectId;
};

/** Real declared Double param (name/label/range/default) straight from an OFX plug-in's Describe. */
struct OfxParamInfo {
    QString name;   // OFX identifier — matches the key processFrame() applies via loadSlotParams()
    QString label;  // UI label (kOfxPropLabel; falls back to name)
    double defaultValue = 0.0;
    double minValue = 0.0;
    double maxValue = 1.0;
};

/** Metadata for one discovered OFX plug-in. */
struct OfxPluginDesc {
    QString name;
    QString path;       // preferred binary (.ofx) or bundle root
    QString bundlePath; // .ofx.bundle directory if any
    QString archHint;   // e.g. Win64 / MacOS / Linux-x86-64
    QString effectId;   // com.vegascreativesoftware:… when known
    QString apiLabel = QStringLiteral("OFX");
    int pluginIndex = 0;
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

    /** Parse pluginId produced by VegasVideoPluginCatalog / FxSlot. */
    static OfxPluginIdParts parsePluginId(const QString &pluginId);

    /**
     * Map effectId → plugin index inside a loaded .ofx binary (fail-soft, empty on error).
     */
    static QHash<QString, int> effectIndexMap(const QString &binaryPath);

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

    /**
     * Real Double params declared by the resolved plug-in's own Describe (name/label/range/default).
     * Empty when the slot isn't OFX, doesn't resolve to an installed binary, or has no Double params —
     * callers should fall back to an approximation in that case (see VideoEventFxDialogExact).
     */
    QVector<OfxParamInfo> paramsForSlot(FxSlot slot);

private:
    OfxHost();
    ~OfxHost();
    OfxHost(const OfxHost &) = delete;
    OfxHost &operator=(const OfxHost &) = delete;

    struct Impl;
    Impl *m_ = nullptr;
};

} // namespace openvegas
