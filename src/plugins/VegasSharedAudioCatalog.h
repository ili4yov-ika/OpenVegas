#pragma once

#include "plugins/AudioPluginTypes.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace openvegas {

/** How far OpenVegas can stand in for a Vegas Shared Plug-In effect. */
enum class VegasSharedReplacementStatus {
    /** BuiltinDsp processes this name (Gate / EQ / Comp / Chorus / Delay / Reverb). */
    Implemented = 0,
    /** Listed in chooser / catalog; no (or stub) realtime DSP yet. */
    CatalogOnly,
    /** Known in a Shared pack; no OpenVegas stand-in yet. */
    Unmapped
};

/**
 * What to do when a standard VEGAS / Sound Forge audio effect is available both as the
 * real plug-in and as an OpenVegas builtin of the same name.
 *
 * Both are real processors now: the plug-in is hosted through DirectShow, and the
 * builtins have been measured against it (see MARKDOWN/VEGAS_SHARED_PLUGINS_REVERSE_FULL.md).
 * So this is a genuine choice rather than a fallback, and it is the user's to make.
 */
enum class VegasSharedSubstitution {
    /** Offer OpenVegas's own implementation for names it covers. Default. */
    ReplaceWithBuiltin = 0,
    /** Never substitute — offer the VEGAS plug-in itself for every registered effect. */
    UseOriginal
};

/**
 * One Vegas Shared audio FX → OpenVegas builtin substitute.
 * Does **not** LoadLibrary proprietary DLLs — discovery is path/file presence only.
 */
struct VegasSharedFxEntry {
    QString dllFileName;          ///< e.g. sftrkfx1_x64.dll
    QString packProductName;      ///< VERSIONINFO ProductName
    QString vegasFxName;          ///< Name as shown in Vegas Plug-In Chooser
    QString openvegasBuiltinName; ///< Builtin display name (empty if Unmapped)
    VegasSharedReplacementStatus status = VegasSharedReplacementStatus::Unmapped;
};

struct VegasSharedInstalledPack {
    QString dllFileName;
    QString packProductName;
    QString absolutePath; ///< Full path to DLL when found
    QString rootPath;     ///< Shared Plug-Ins root that owned it
};

/**
 * Catalog + discovery for
 *   C:\Program Files (x86)\VEGAS\Shared Plug-Ins
 *   C:\Program Files (x86)\Sony\Shared Plug-Ins
 *
 * Purpose: substitute Vegas Shared FX with OpenVegas builtins, and provide a
 * stable map for future golden / comparative DSP tests vs Vegas Pro.
 */
class VegasSharedAudioCatalog {
public:
    /** Prefer VEGAS Shared, then Sony Shared (legacy). */
    static QStringList defaultSharedRoots();

    /** Current substitution policy (persisted in Preferences). */
    static VegasSharedSubstitution substitutionPolicy();
    static void setSubstitutionPolicy(VegasSharedSubstitution policy);

    /** Full static map (independent of install). */
    static QVector<VegasSharedFxEntry> catalog();

    /** Packs whose Audio_x64 DLL exists under roots (VEGAS preferred over Sony). */
    static QVector<VegasSharedInstalledPack> discoverInstalled(
        const QStringList &roots = {});

    /** True if at least one known Shared Audio_x64 DLL is present. */
    static bool anyInstalled(const QStringList &roots = {});

    /**
     * Resolve a Vegas / VEG / chooser name to OpenVegas builtin display name.
     * Empty if unmapped. Strips "VEGAS " brand and common aliases.
     */
    static QString resolveBuiltinName(const QString &vegasOrAliasName);

    /** Builtin FxSlot for a Vegas FX name; empty pluginId if unmapped. */
    static FxSlot replacementSlot(const QString &vegasOrAliasName);

    /** Chooser descriptors for mapped FX (Implemented + CatalogOnly). */
    static QVector<AudioPluginDesc> chooserDescriptors(bool onlyIfInstalled = false,
                                                       const QStringList &roots = {});

    /** Entries with status == Implemented (for DSP / golden tests). */
    static QVector<VegasSharedFxEntry> implementedEntries();

    static QString statusLabel(VegasSharedReplacementStatus s);
};

} // namespace openvegas
