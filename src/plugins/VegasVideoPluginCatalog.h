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
