#pragma once

#include "plugins/AudioPluginTypes.h"
#include "plugins/OfxHost.h"
#include "plugins/PluginScanner.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

namespace openvegas {

/** Parsed OFX effect from a Vegas Pro 22 video plug-in bundle. */
struct VegasVideoPluginEntry {
    QString displayName;   ///< OpenVegas label (VEGAS brand stripped)
    QString vegasLabel;    ///< Original OfxPropLabel from bundle XML
    QString effectId;      ///< e.g. com.vegascreativesoftware:chromablur
    QString bundlePath;    ///< …/Something.ofx.bundle
    QString binaryPath;    ///< …/Win64/Something.ofx (empty if not installed)
    int pluginIndex = -1;  ///< Index inside binary (resolved when loadable)
    QString grouping;      ///< OfxImageEffectPluginPropGrouping, e.g. "VEGAS\\Creative"
    QStringList categories;
    QStringList presets;
    /**
     * Parameter values each named preset sets, straight from the bundle's PresetPackage.
     * Keyed by preset name; the map is the same shape FxSlot state uses, so it can be
     * handed to the real plug-in as-is — which is what lets a preset tile show the effect
     * instead of a placeholder.
     */
    QMap<QString, QVariantMap> presetParams;
    bool hasBinary = false;
    /** OfxPropPluginDescription — the line VEGAS shows under the plug-in list. */
    QString description;
    /**
     * Illustration the bundle ships for this effect, if any
     * (`Contents/Resources/<effectId with ':' as '.'>.png`).
     *
     * VEGAS shows it instead of a rendered preview — which is how AI Colorization gets its
     * diagonal before/after split and Auto Reframe its crop guides. Both are pictures of
     * what the effect does, not output the plug-in could produce on a still frame, and
     * both are identical across the effect's presets.
     */
    QString previewImagePath;
    /**
     * OFX contexts the effect declares (`OfxImageEffectContextFilter`, `…Transition`, …).
     *
     * This is what separates a video effect from a transition or a media generator: VEGAS's
     * grouping does not — Page Roll and Add Noise are both grouped plain "VEGAS" — so
     * without it the Video FX pane listed every transition in the bundle as an effect.
     */
    QStringList contexts;

    /** True when the effect can be applied to a clip (rather than being a transition/generator). */
    bool isVideoFx() const
    {
        if (contexts.isEmpty()) {
            return true; // nothing declared — let it through rather than lose it
        }
        return contexts.contains(QStringLiteral("OfxImageEffectContextFilter"))
               || contexts.contains(QStringLiteral("OfxImageEffectContextGeneral"));
    }

    bool isTransition() const
    {
        return !isVideoFx() && contexts.contains(QStringLiteral("OfxImageEffectContextTransition"));
    }
};

/**
 * Catalog + discovery for VEGAS Pro 22 OFX Video Plug-Ins.
 * Parses bundle resource XML (no LoadLibrary required for listing).
 * When .ofx binaries are present, resolves plugin indices for real processing.
 */
class VegasVideoPluginCatalog {
public:
    static QStringList defaultOfxRoots();

    /**
     * Pin discovery to these roots, ignoring the machine's installs and settings.
     *
     * Without this a lookup by name or effect id quietly re-discovers from every root the
     * scanner knows, so a caller that discovered from one tree can still be handed a
     * plug-in from another. That is not hypothetical: a VEGAS Pro 18 path in Preferences
     * was enough to make the test suite load a different generation of `Vfx1.ofx` than the
     * one it had discovered, and that build faults during render.
     *
     * An empty list restores the normal search. Setting it invalidates the cache.
     */
    static void setDiscoveryRoots(const QStringList &roots);
    static QStringList discoveryRoots();

    /** Full catalog from installed/sample Vegas OFX trees (cached). */
    static QVector<VegasVideoPluginEntry> discover(const QStringList &roots = {});

    /**
     * Try each of the scanner's candidate roots (Preferences path first) and
     * return the catalog from the first one that yields any entries.
     * Optionally reports which candidate root matched.
     */
    static QVector<VegasVideoPluginEntry> discoverUsingScanner(const PluginScanner &scanner,
                                                                QString *resolvedRootOut = nullptr);

    static const VegasVideoPluginEntry *findByDisplayName(const QString &name);
    static const VegasVideoPluginEntry *findByEffectId(const QString &effectId);
    static const VegasVideoPluginEntry *findByVegasLabel(const QString &label);

    /**
     * The other binaries that declare `effectId`, in search order after the one
     * `findByEffectId()` returns.
     *
     * The catalog shows one entry per effect, and the winner is decided by search order —
     * the Preferences path first. Order is not the same as usable: a machine can carry two
     * generations of `Vfx1.ofx`, and an older one can refuse this host outright at
     * `kOfxActionLoad`, at which point every effect it declares is unreachable through it.
     * This is what lets a caller move on to the next install instead of giving up on the
     * effect.
     */
    static QStringList alternateBinaries(const QString &effectId);

    /** Build canonical OFX pluginId: ofx:<path>#<index>#<effectId> or ofx-id:<effectId>. */
    static QString formatPluginId(const VegasVideoPluginEntry &entry);

    /** Fill pluginId/path from catalog; keeps builtins and already-resolved slots. */
    static FxSlot resolveVideoFxSlot(FxSlot slot);

    /** Chooser / Video FX pane entry point. */
    static FxSlot slotFromDisplayName(const QString &rawName);

    /** After fxSlotFromVegName — attach binary path + index when known. */
    static FxSlot resolveVegImportSlot(FxSlot slot);

    /**
     * Real params declared by the installed OFX plug-in (OfxHost::paramsForSlot) when
     * available; otherwise an approximate fallback by display name. Single source of
     * truth for both the Video Event FX and Video Track FX param editors/keyframe lanes.
     */
    static QVector<OfxParamInfo> paramsInfoForSlot(const FxSlot &slot);

    static void invalidateCache();
};

} // namespace openvegas
