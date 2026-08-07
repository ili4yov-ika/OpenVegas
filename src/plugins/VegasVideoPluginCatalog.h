#pragma once

#include "plugins/AudioPluginTypes.h"
#include "plugins/PluginScanner.h"

#include <QString>
#include <QStringList>
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
    QString grouping;      ///< OfxImageEffectPluginPropGrouping
    QStringList categories;
    QStringList presets;
    bool hasBinary = false;
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

    static void invalidateCache();
};

} // namespace openvegas
