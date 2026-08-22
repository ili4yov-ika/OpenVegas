#pragma once

#include "plugins/AudioPluginTypes.h"

#include <QString>
#include <QVector>

namespace openvegas {

/**
 * One class registered by a VEGAS / Sound Forge Shared Plug-In DLL.
 *
 * These are **not** VST or OFX. Each pack DLL is a COM in-process server whose only
 * public entry point is `DllGetClassObject`, and every effect inside it is a separate
 * CLSID registered DirectShow-style (the key carries `Merit` and a `Pins` subkey).
 * Nothing effect-specific is exported, so the class list cannot be read from the binary:
 * the CLSIDs live there as raw 16-byte GUIDs, and the registry is the only place they
 * appear by name.
 */
struct SoundForgeClass {
    QString clsid;        ///< "{607682E0-6E21-11D0-AEBC-00A0C9053912}"
    QString name;         ///< Registry default value, e.g. "Reverb"
    QString dllFileName;  ///< "sfppack1_x64.dll"
    QString dllPath;      ///< Full InprocServer32 path
    /**
     * True for the "… Property Page" companion classes. Each effect ships one (or more)
     * to draw its dialog; they are listed for completeness but are not effects.
     */
    bool isPropertyPage = false;
};

/**
 * Discovery for the Sound Forge / VEGAS Shared audio plug-in packs.
 *
 * **Scope: discovery and naming only.** Instantiating one of these means calling
 * `CoCreateInstance` and then talking to a proprietary, undocumented interface — the
 * vtable has not been reverse engineered, so nothing here loads or runs a plug-in.
 * What it does give is the authoritative list: real effect names straight from the
 * registration the VEGAS installer wrote, instead of a hand-maintained guess.
 *
 * Windows-only by construction (COM registry). Returns empty elsewhere, which leaves
 * VegasSharedAudioCatalog's static map as the only source, exactly as before.
 */
class SoundForgeHost {
public:
    /** Every class whose InprocServer32 lives under a "Shared Plug-Ins" directory. */
    static QVector<SoundForgeClass> discoverRegistered();

    /** Effects only — property pages filtered out, sorted by name. */
    static QVector<SoundForgeClass> discoverEffects();

    /** True when at least one effect is registered on this machine. */
    static bool anyRegistered();

    /** Drop the cached scan (tests, or after installing VEGAS). */
    static void invalidateCache();

    /**
     * pluginId scheme for a registered effect: "sfds:{CLSID}".
     * The CLSID is the identity — these effects have no file of their own to point at,
     * several of them share one pack DLL, and the COM registration already resolves the
     * class to its server.
     */
    static QString pluginId(const QString &clsid);
    /** CLSID out of a "sfds:{…}" pluginId; empty when it is not one. */
    static QString clsidFromPluginId(const QString &pluginId);

    /** Chooser descriptors for every registered effect (PluginFormat::DirectShow). */
    static QVector<AudioPluginDesc> pluginDescriptors();
};

} // namespace openvegas
