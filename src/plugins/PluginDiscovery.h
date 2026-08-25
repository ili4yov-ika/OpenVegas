#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace openvegas {

/**
 * Where a fresh install should look for the things OpenVegas can host.
 *
 * The pieces were already known separately — `AudioPluginScanner` knows the VST roots,
 * `OfxPluginPaths` the OFX ones, `SoundForgeHost` finds the Shared Plug-Ins through the
 * registry — but nothing put them together and asked all of them at once. That is what a
 * first run needs: one sweep, one list to show, one confirmation.
 *
 * Only directories that exist are reported, and each carries how many plug-in-looking
 * files are in it, because "found the folder" and "found anything in it" are different
 * answers and a setup screen that conflates them is misleading.
 */
class PluginDiscovery {
public:
    enum class Kind {
        VegasProgram,   ///< The VEGAS Pro program folder itself.
        VegasSharedFx,  ///< Shared Plug-Ins — the Sound Forge audio effects.
        VegasOfx,       ///< VEGAS's own OFX video plug-ins.
        Ofx,            ///< Third-party OFX roots the standard defines.
        Vst1,
        Vst2,
        Vst3,
    };

    struct Found {
        Kind kind = Kind::Ofx;
        QString path;
        /** Plug-in-looking files directly inside, or -1 when not counted. */
        int count = -1;
        /** Set when this is the obvious choice for its kind and nothing else is. */
        bool preferred = false;
    };

    /** One sweep of every location, in the order a setup screen should show them. */
    static QVector<Found> scan();

    /** Human-readable name for a kind, for headings in the setup screen. */
    static QString kindLabel(Kind kind);

    /**
     * Where VEGAS Pro is installed, best guess first.
     *
     * The registry knows when VEGAS installed itself properly; the usual Program Files
     * locations are checked too, because a copied install has no registry entry and is
     * exactly the case a first run should still handle.
     */
    static QStringList vegasProgramRoots();

    /** Shared Plug-Ins folders — VEGAS's and Sound Forge's audio effects. */
    static QStringList sharedPluginRoots();

    /** VEGAS's own OFX video plug-in folders. */
    static QStringList vegasOfxRoots();

    /**
     * Write the chosen paths into the settings the rest of the app already reads:
     * `plugins/vegasProPath`, `plugins/ofxPath`, `plugins/vst1Paths` and the rest. No new
     * keys — a setup screen that stored its answers somewhere of its own would leave
     * Preferences showing something different.
     */
    static void apply(const QVector<Found> &chosen);

    /** True until the first run has been completed once. */
    static bool needsFirstRun();
    /** Remember that setup has been through; `/Preferences` can still change everything. */
    static void markFirstRunDone();
};

} // namespace openvegas
