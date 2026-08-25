#pragma once

#include "plugins/AudioPluginTypes.h"
#include "plugins/PluginScanner.h"

#include <QHash>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

/**
 * OpenVegas's own stand-in renderers for VEGAS video effects — **off, deliberately.**
 *
 * The host loads and renders real VEGAS `.ofx` binaries correctly, but the OFX parameter
 * blob inside a `.veg` is not decoded yet, so an effect restored from a project runs at
 * the plug-in's declared defaults (0 for Chroma Blur's radii) and does nothing visible.
 * The emulated path used to hide that: a box blur stood in for Chroma Blur, a sepia
 * matrix for Sepia, a gain for any failed render — so the app looked like it was running
 * VEGAS's effects when it was running approximations of them.
 *
 * With this off, an effect that cannot really be rendered renders as nothing. That is the
 * honest state and it is what the docs now say: VEGAS video plug-ins do not work yet.
 * **Do not write more of these approximations** — the way forward is decoding the real
 * parameters, not widening the imitation. See MARKDOWN/PLAN_OFX_VIDEO_PLUGINS.md.
 *
 * The code is kept (compiled out) rather than deleted so the switch can be flipped back
 * for A/B comparison while working on the parameter decoder.
 */
#ifndef OPENVEGAS_EMULATED_VIDEO_FX
#define OPENVEGAS_EMULATED_VIDEO_FX 0
#endif

namespace openvegas {

/** Parsed OpenVegas OFX pluginId (ofx:<path>#<index>#<effectId> or ofx-id:<effectId>). */
struct OfxPluginIdParts {
    QString path;
    int index = 0;
    QString effectId;
};

/** Real declared param (name/label/range/default) straight from an OFX plug-in's Describe. */
struct OfxParamInfo {
    QString name;   // OFX identifier — matches the key processFrame() applies via loadSlotParams()
    QString label;  // UI label (kOfxPropLabel; falls back to name)
    double defaultValue = 0.0;
    double minValue = 0.0;
    double maxValue = 1.0;
    /**
     * On/off parameter — VEGAS draws these as check boxes, and a 0.00…1.00 slider in
     * their place is the sort of thing that makes a faithful panel look like a stand-in.
     * Values are still carried as 0/1 doubles so nothing downstream has to special-case them.
     */
    bool toggle = false;
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
    /**
     * False when the bundle only ships binaries for another platform — the normal case
     * for VEGAS's own `Contents/Win64` bundles seen from Linux or macOS. Such a plug-in
     * is still listed (so the UI can name it and a project referencing it still opens),
     * but is never dlopen'd; `archNote` says why.
     */
    bool archLoadable = true;
    QString archNote;
};

/** What DescribeInContext made of one effect in one context. */
struct OfxContextReport {
    QString context;   ///< the kOfxImageEffectContext… value asked for
    bool accepted = false;
    int status = 0;    ///< the OfxStatus the plug-in returned
    QStringList clips; ///< clip names it defined, sorted
    QStringList params;
};

/** One effect declared inside an .ofx binary, as the binary itself describes it. */
struct OfxEffectSummary {
    QString effectId;  ///< OfxPlugin::pluginIdentifier
    QString label;     ///< kOfxPropLabel from the plug-in's own Describe
    QString grouping;  ///< kOfxImageEffectPluginPropGrouping ("Filter/Blur")
    int pluginIndex = 0;
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
     * Every effect in an .ofx binary with the label and grouping it declares itself.
     *
     * This is how a plug-in with no VEGAS resource manifest gets into the catalog —
     * i.e. every third-party OFX plug-in, and the only kind that exists on Linux and
     * macOS. Runs Load + Describe per effect, so it is cached by the catalog rather
     * than called per frame. Empty when the binary is missing, foreign-ABI or broken.
     */
    static QVector<OfxEffectSummary> enumerateEffects(const QString &binaryPath);

    /**
     * What one effect accepts when asked to describe itself in each context.
     *
     * Answers a question nothing else could: whether a bundle's effects are filters,
     * transitions or generators, and what clips and parameters each context gives them.
     * VEGAS's own bundles leave kOfxImageEffectPropSupportedContexts unset during plain
     * Describe, so asking one context at a time is the only way to find out.
     *
     * Runs Load + Describe + DescribeInContext on a fresh library handle and does not
     * touch the instance cache, so it is a diagnostic, not a render path.
     */
    static QVector<OfxContextReport> describeContexts(const QString &binaryPath,
                                                      int pluginIndex);

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
     * Render one frame of a transition through a plug-in that declares two source clips.
     *
     * `from` is the clip being left, `to` the one being arrived at, and `progress` runs
     * 0 to 1. The progress is written into the plug-in's own "Transition" parameter — the
     * transition context declares it, so the host fills it in rather than inventing one.
     *
     * Fail-soft like processFrame: false and an explanation, never a substitute picture.
     */
    bool processTransition(int instanceId, const QImage &from, const QImage &to, QImage *out,
                           double progress, const QVariantMap &params,
                           QString *errorOut = nullptr);

    /**
     * Stand-in video FX by display name (Soften/Blur, Invert, Sepia, Brightness and
     * Contrast, Gain). **Always returns false** unless OPENVEGAS_EMULATED_VIDEO_FX is
     * turned on — see the note at the top of this header.
     */
    static bool processEmulated(QImage *rgba, const QString &displayName,
                                const QVariantMap &params);

    /**
     * Render through the real OFX instance when the binary is known / already loaded.
     * With the stand-ins off (the default) this is the only path: false means the frame
     * was left untouched.
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
