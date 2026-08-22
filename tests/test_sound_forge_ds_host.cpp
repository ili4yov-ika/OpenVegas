#include <catch2/catch_test_macros.hpp>

#include "audio/SoundForgeDsHost.h"
#include "plugins/SoundForgeHost.h"
#include "plugins/VegasSharedAudioCatalog.h"

#include <QSet>
#include <QString>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace openvegas;

namespace {

/** A 440 Hz tone, the same signal the DirectShow probe pushes. */
std::vector<float> tone(int frames)
{
    std::vector<float> samples(size_t(frames), 0.0f);
    double phase = 0.0;
    for (int i = 0; i < frames; ++i) {
        samples[size_t(i)] = float(0.5 * std::sin(phase));
        phase += 2.0 * 3.14159265358979 * 440.0 / 44100.0;
    }
    return samples;
}

} // namespace

TEST_CASE("Sound Forge pluginId round-trips through the CLSID scheme",
          "[plugins][soundforge][dshost]")
{
    const QString clsid = QStringLiteral("{607682E0-6E21-11D0-AEBC-00A0C9053912}");
    const QString id = SoundForgeHost::pluginId(clsid);

    CHECK(id == QStringLiteral("sfds:{607682E0-6E21-11D0-AEBC-00A0C9053912}"));
    CHECK(SoundForgeHost::clsidFromPluginId(id) == clsid);
    // The host used by the audio graph must agree with the discovery layer, or slots
    // saved by one would not resolve through the other.
    CHECK(SoundForgeDsHost::makePluginId(clsid) == id);
    CHECK(SoundForgeDsHost::clsidFromPluginId(id) == clsid);

    // Anything that is not one of ours must not be claimed.
    CHECK(SoundForgeHost::clsidFromPluginId(QStringLiteral("builtin:Reverb")).isEmpty());
    CHECK(SoundForgeHost::clsidFromPluginId(QStringLiteral("vst3:C:/x.vst3")).isEmpty());
    CHECK(SoundForgeHost::pluginId(QString()).isEmpty());
}

TEST_CASE("Every registered effect yields a well-formed descriptor",
          "[plugins][soundforge][dshost]")
{
    const QVector<AudioPluginDesc> descs = SoundForgeHost::pluginDescriptors();
    if (descs.isEmpty()) {
        WARN("No Shared Plug-Ins registered — descriptor checks skipped");
        return;
    }

    QSet<QString> ids;
    for (const AudioPluginDesc &d : descs) {
        INFO(d.name.toStdString() << " / " << d.id.toStdString());
        CHECK(d.format == PluginFormat::DirectShow);
        CHECK(d.id.startsWith(QStringLiteral("sfds:{")));
        CHECK_FALSE(d.name.isEmpty());
        CHECK_FALSE(SoundForgeHost::clsidFromPluginId(d.id).isEmpty());
        // Ids address a COM class, so a duplicate would mean two entries fighting over
        // one instance in the host's map.
        CHECK_FALSE(ids.contains(d.id));
        ids.insert(d.id);
    }
}

TEST_CASE("Chooser offers hosted effects without duplicating them as builtins",
          "[plugins][soundforge][dshost]")
{
    const QVector<AudioPluginDesc> chooser = VegasSharedAudioCatalog::chooserDescriptors();
    if (!SoundForgeHost::anyRegistered()) {
        WARN("No Shared Plug-Ins registered — chooser checks skipped");
        return;
    }

    QSet<QString> hostedNames;
    int hosted = 0;
    for (const AudioPluginDesc &d : chooser) {
        if (d.format == PluginFormat::DirectShow) {
            ++hosted;
            hostedNames.insert(normalizeVegasPluginKey(d.name));
        }
    }
    CHECK(hosted > 0);

    // A name the real plug-in covers must not also appear as a builtin substitute:
    // the two would be indistinguishable in the chooser.
    for (const AudioPluginDesc &d : chooser) {
        if (d.format == PluginFormat::Builtin) {
            INFO(d.name.toStdString() << " duplicates a hosted effect");
            CHECK_FALSE(hostedNames.contains(normalizeVegasPluginKey(d.name)));
        }
    }
}

TEST_CASE("A hosted effect actually transforms audio", "[plugins][soundforge][dshost]")
{
    if (!SoundForgeDsHost::isAvailable()) {
        WARN("Sound Forge hosting unavailable on this platform — DSP check skipped");
        return;
    }

    // Chorus modulates unconditionally, so it changes any input at default settings —
    // unlike an EQ, which is deliberately transparent until a band is moved.
    QString chorus;
    for (const SoundForgeClass &c : SoundForgeHost::discoverEffects()) {
        if (c.name.compare(QStringLiteral("Chorus"), Qt::CaseInsensitive) == 0) {
            chorus = c.clsid;
            break;
        }
    }
    if (chorus.isEmpty()) {
        WARN("Chorus not registered — DSP check skipped");
        return;
    }

    double meanDiff = 0.0;
    QString error;
    const bool ok = SoundForgeDsHost::probeProcess(chorus, &meanDiff, &error);
    INFO(error.toStdString());
    REQUIRE(ok);
    // Real modulation moves the signal well clear of both silence and pass-through.
    CHECK(meanDiff > 1e-4);
    CHECK(meanDiff < 2.0);
}

TEST_CASE("A chooser descriptor drives audio through the production process() path",
          "[plugins][soundforge][dshost]")
{
    if (!SoundForgeDsHost::isAvailable()) {
        WARN("Sound Forge hosting unavailable on this platform — pipeline check skipped");
        return;
    }
    // Take the effect the way the chooser hands it over, not by CLSID: this covers the
    // descriptor -> FxSlot -> instance -> process chain that the mixer actually uses.
    AudioPluginDesc chosen;
    for (const AudioPluginDesc &d : SoundForgeHost::pluginDescriptors()) {
        if (d.name.compare(QStringLiteral("Chorus"), Qt::CaseInsensitive) == 0) {
            chosen = d;
            break;
        }
    }
    if (chosen.id.isEmpty()) {
        WARN("Chorus not registered — pipeline check skipped");
        return;
    }

    auto &host = SoundForgeDsHost::instance();
    FxSlot slot;
    REQUIRE(host.createInstance(chosen, &slot));
    CHECK(slot.format == PluginFormat::DirectShow);
    CHECK(host.hasInstance(slot));

    host.prepare(&slot, 44100.0, 512);

    const int frames = 512;
    const int blocks = 8;
    // One continuous tone sliced into blocks. Re-sending the same block instead would
    // restart the sine every 512 samples, and the resulting discontinuities — not the
    // effect — would dominate whatever the output energy came out as.
    const std::vector<float> continuous = tone(frames * blocks);
    std::vector<float> left(size_t(frames), 0.0f);
    std::vector<float> right(size_t(frames), 0.0f);
    std::vector<float> outL(size_t(frames), 0.0f);
    std::vector<float> outR(size_t(frames), 0.0f);
    float *in[2] = {left.data(), right.data()};
    float *out[2] = {outL.data(), outR.data()};

    // Measure on the last block only: by then the effect's delay line is primed, so a
    // low reading would mean a real problem rather than start-up.
    double meanDiff = 0.0;
    double energy = 0.0;
    double energyR = 0.0;
    for (int block = 0; block < blocks; ++block) {
        const size_t offset = size_t(block) * size_t(frames);
        std::copy(continuous.begin() + long(offset),
                  continuous.begin() + long(offset) + frames, left.begin());
        // Copy into the existing buffer: assigning the vector could reallocate and
        // leave the in[] pointers taken above dangling.
        std::copy(left.begin(), left.end(), right.begin());
        host.process(&slot, in, out, 2, frames);

        if (block == blocks - 1) {
            for (int i = 0; i < frames; ++i) {
                meanDiff += std::fabs(double(outL[size_t(i)])
                                      - double(continuous[offset + size_t(i)]));
                energy += std::fabs(double(outL[size_t(i)]));
                energyR += std::fabs(double(outR[size_t(i)]));
            }
            meanDiff /= frames;
            energy /= frames;
            energyR /= frames;
        }
    }

    INFO("meanDiff=" << meanDiff << " energy=" << energy);
    CHECK(energy > 1e-3);   // not silence
    CHECK(meanDiff > 1e-5); // not a pass-through
    // Both channels must be filled: a de-interleave slip would leave one at zero.
    CHECK(energyR > 1e-3);

    host.releaseInstance(&slot);
    CHECK_FALSE(host.hasInstance(slot));
}

TEST_CASE("Effect settings survive the trip through the project file",
          "[plugins][soundforge][dshost]")
{
    if (!SoundForgeDsHost::isAvailable()) {
        WARN("Sound Forge hosting unavailable on this platform — state check skipped");
        return;
    }
    AudioPluginDesc chosen;
    for (const AudioPluginDesc &d : SoundForgeHost::pluginDescriptors()) {
        if (d.name.compare(QStringLiteral("Chorus"), Qt::CaseInsensitive) == 0) {
            chosen = d;
            break;
        }
    }
    if (chosen.id.isEmpty()) {
        WARN("Chorus not registered — state check skipped");
        return;
    }

    auto &host = SoundForgeDsHost::instance();
    FxSlot slot;
    REQUIRE(host.createInstance(chosen, &slot));
    // Force the graph up: the effect object only exists once it is built, and there is
    // nothing to read settings from before that.
    host.prepare(&slot, 44100.0, 512);
    std::vector<float> left(512, 0.0f);
    std::vector<float> right(512, 0.0f);
    std::vector<float> outL(512, 0.0f);
    std::vector<float> outR(512, 0.0f);
    float *in[2] = {left.data(), right.data()};
    float *out[2] = {outL.data(), outR.data()};
    host.process(&slot, in, out, 2, 512);

    const QByteArray saved = host.getState(&slot);
    INFO("state blob is " << saved.size() << " bytes");
    // An empty blob would mean the project stores nothing and every reload silently
    // resets the effect to its defaults.
    REQUIRE_FALSE(saved.isEmpty());

    // The project file carries this blob as base64 and never interprets it.
    const QByteArray throughFile = QByteArray::fromBase64(saved.toBase64());
    CHECK(throughFile == saved);

    // Loading it back must be accepted and must reproduce the same bytes.
    REQUIRE(host.setState(&slot, throughFile));
    CHECK(host.getState(&slot) == saved);

    // A sample-rate change replaces the effect object. Settings have to be carried over,
    // or switching project properties would silently reset the plug-in to its defaults.
    host.prepare(&slot, 48000.0, 512);
    host.process(&slot, in, out, 2, 512);
    CHECK(host.getState(&slot) == saved);

    host.releaseInstance(&slot);
}

TEST_CASE("Slots without an instance pass audio through untouched",
          "[plugins][soundforge][dshost]")
{
    // An unresolved slot must never silence a track: the chain still has to carry audio.
    FxSlot slot = makeFxSlot(QStringLiteral("Not A Registered Effect"),
                             PluginFormat::DirectShow,
                             QStringLiteral("sfds:{00000000-0000-0000-0000-000000000000}"));
    CHECK_FALSE(SoundForgeDsHost::instance().hasInstance(slot));

    const int frames = 256;
    std::vector<float> left = tone(frames);
    std::vector<float> right = tone(frames);
    const std::vector<float> expected = left;
    std::vector<float> outL(size_t(frames), 0.0f);
    std::vector<float> outR(size_t(frames), 0.0f);

    float *in[2] = {left.data(), right.data()};
    float *out[2] = {outL.data(), outR.data()};
    SoundForgeDsHost::instance().process(&slot, in, out, 2, frames);

    for (int i = 0; i < frames; ++i) {
        CHECK(outL[size_t(i)] == expected[size_t(i)]);
        CHECK(outR[size_t(i)] == expected[size_t(i)]);
    }
}
