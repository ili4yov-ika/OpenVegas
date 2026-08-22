#pragma once

#include "plugins/AudioPluginHost.h"

#include <QHash>
#include <QString>
#include <memory>

namespace openvegas {

/**
 * Host for the VEGAS / Sound Forge Shared Plug-In audio effects.
 *
 * These are **not** VST or OFX, and not a proprietary protocol either: every effect is a
 * COM in-process class registered DirectShow-style, and the object behind the CLSID is an
 * ordinary DirectShow **transform filter** — one `Input` pin (`MEDIATYPE_Audio`), one
 * `Output` pin, exposing `IBaseFilter` / `IMediaFilter` / `IMemInputPin`, plus
 * `IPersistStream` for state and `ISpecifyPropertyPages` for the native dialog.
 * So hosting them needs no reverse engineering at all — only the documented
 * DirectShow interfaces.
 *
 * Three things about these filters were established by measurement, not assumption, and
 * the implementation depends on each of them:
 *
 *  - **They must live in a real filter graph.** Connecting pin-to-pin outside one fails
 *    with `VFW_E_TYPE_NOT_ACCEPTED`, and putting our input and output pin on a *single*
 *    filter makes the graph a cycle, which fails with `VFW_E_NO_TRANSPORT`. Hence one
 *    source filter and one separate sink filter around the effect.
 *  - **They process 32-bit float natively.** At default settings Track EQ then comes back
 *    bit-identical to its input; through 16-bit PCM the same path shows a small residue
 *    that is only quantisation noise. Float is therefore the connection format.
 *  - **They decommit the allocator while connecting the output pin,** so it has to be
 *    committed again after the graph is wired or the first `GetBuffer` fails.
 *
 * Threading: every one of these classes registers `ThreadingModel=Both`, i.e. the object
 * declares itself internally synchronised, so the audio thread may call `Receive` on the
 * pointer directly. Each thread that touches COM still needs its own `CoInitializeEx`.
 *
 * Windows-only. Elsewhere every entry point fails cleanly and `isAvailable()` is false,
 * which leaves the builtin substitutes as the only path, exactly as before.
 */
class SoundForgeDsHost : public AudioPluginHost {
public:
    static SoundForgeDsHost &instance();

    /** True on Windows when at least one Shared Plug-In effect is registered. */
    static bool isAvailable();

    /** "sfds:{CLSID}" — the pluginId form this host answers to. */
    static QString makePluginId(const QString &clsid);
    /** CLSID out of a "sfds:{…}" pluginId; empty when it is not one. */
    static QString clsidFromPluginId(const QString &pluginId);

    /** True when this slot already has a live instance (no reload needed). */
    bool hasInstance(const FxSlot &slot) const;

    bool createInstance(const AudioPluginDesc &desc, FxSlot *slot) override;
    void releaseInstance(FxSlot *slot) override;
    void prepare(FxSlot *slot, double sampleRate, int blockSize) override;
    void reset(FxSlot *slot) override;
    void process(FxSlot *slot, float **in, float **out, int channels, int frames) override;
    bool openEditor(FxSlot *slot, QWidget *parent) override;
    QByteArray getState(const FxSlot *slot) const override;
    bool setState(FxSlot *slot, const QByteArray &state) override;

    /**
     * Run one buffer through a freshly built graph and report whether the effect
     * changed it. Used by tests to prove the pipeline end-to-end without a project.
     * `meanDiffOut` receives the mean absolute difference from the input.
     */
    static bool probeProcess(const QString &clsid, double *meanDiffOut = nullptr,
                             QString *errorOut = nullptr);

private:
    struct Instance;
    std::shared_ptr<Instance> lookup(const FxSlot *slot) const;
    QHash<QString, std::shared_ptr<Instance>> m_instances;
};

} // namespace openvegas
