#pragma once

#include <QString>
#include <QStringList>

namespace openvegas {

/**
 * Where OFX plug-ins live, and which of them this build can actually load.
 *
 * Two separate concerns, deliberately kept apart from the VEGAS-specific discovery in
 * `PluginScanner` / `VegasVideoPluginCatalog`:
 *
 *  * **Search roots** — the directories the OpenFX standard tells every host to scan,
 *    which is what makes third-party OFX plug-ins (Resolve's, Natron's, TuttleOFX,
 *    open-source filters) work on Linux and macOS where no VEGAS installation exists.
 *  * **ABI** — which `Contents/<arch>` subdirectory of a bundle holds a binary this
 *    process can load. A VEGAS bundle ships `Contents/Win64` only, so on Linux and
 *    macOS it is discoverable but not loadable, and the host has to say so rather than
 *    handing a PE file to `dlopen`.
 */
class OfxPluginPaths {
public:
    /**
     * Standard OFX search roots for the running platform, most specific first.
     *
     * `OFX_PLUGIN_PATH` wins when set (the standard's own override, `;`-separated on
     * Windows and `:`-separated elsewhere), followed by the per-OS system and per-user
     * locations. Only existing directories are returned.
     */
    static QStringList standardRoots();

    /** Same list without the existence filter — for diagnostics and Preferences hints. */
    static QStringList standardRootsUnfiltered();

    /**
     * Bundle architecture directory names this build can load, best first.
     *
     * Windows x64 → `Win64`; macOS → `MacOS-arm-64`/`MacOS-x86-64`/`MacOS` as
     * appropriate; Linux → `Linux-x86-64` / `Linux-arm-64`. Never contains a foreign
     * ABI: a name in this list is a promise that loading may be attempted.
     */
    static QStringList loadableArchFolderNames();

    /** Every arch directory name the OFX bundle layout defines, loadable or not. */
    static QStringList knownArchFolderNames();

    /** True when `archFolderName` names an ABI this process can load. */
    static bool isArchLoadable(const QString &archFolderName);

    /**
     * Human-readable reason a bundle cannot be loaded here, e.g.
     * "built for Win64; this is a Linux-x86-64 build". Empty when it is loadable.
     */
    static QString archIncompatibilityReason(const QString &archFolderName);

    /** Arch folder name matching the running build, e.g. `Win64`. */
    static QString hostArchFolderName();
};

} // namespace openvegas
