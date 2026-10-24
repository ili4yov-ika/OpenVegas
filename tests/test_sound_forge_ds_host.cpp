#include <catch2/catch_test_macros.hpp>

#include "audio/SoundForgeDsHost.h"
#include "plugins/SoundForgeHost.h"
#include "plugins/VegasSharedAudioCatalog.h"

#include <QApplication>
#include <QWidget>
#include <QRegularExpression>
#include <QHash>
#include <QSet>
#include <QString>

#include <algorithm>
#include <cmath>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

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

TEST_CASE("Property pages are not offered as effects", "[plugins][soundforge][dshost]")
{
    const QVector<SoundForgeClass> all = SoundForgeHost::discoverRegistered();
    if (all.isEmpty()) {
        WARN("No Shared Plug-Ins registered — page filter check skipped");
        return;
    }

    // Every effect on this machine hosts; a dialog page slipping through shows up as an
    // entry that cannot even be created. "VEGAS Resonant Filter Prop Page" did exactly
    // that, because it is the one page not spelled "… Property Page".
    static const QRegularExpression pageName(
        QStringLiteral(R"(\bProp(erty)? Page\s*\d*$)"),
        QRegularExpression::CaseInsensitiveOption);
    for (const SoundForgeClass &c : SoundForgeHost::discoverEffects()) {
        INFO(c.name.toStdString() << " is a dialog page, not an effect");
        CHECK_FALSE(pageName.match(c.name).hasMatch());
    }

    // The pages far outnumber nothing: they should account for roughly half the classes.
    int pages = 0;
    for (const SoundForgeClass &c : all) {
        if (c.isPropertyPage) {
            ++pages;
        }
    }
    CHECK(pages > 0);
    CHECK(pages + SoundForgeHost::discoverEffects().size() == all.size());
}

TEST_CASE("Effects with several dialog pages expose all of them",
          "[plugins][soundforge][dshost]")
{
    if (!SoundForgeDsHost::isAvailable()) {
        WARN("Sound Forge hosting unavailable on this platform — page check skipped");
        return;
    }

    // Wave Hammer Surround splits into Compressor / Volume Maximizer / Routing. Hosting
    // only the first page — which is what the editor did at first — hides two thirds of
    // its controls, so the count is what this guards.
    QString waveHammer;
    for (const SoundForgeClass &c : SoundForgeHost::discoverEffects()) {
        if (c.name.compare(QStringLiteral("Wave Hammer Surround"), Qt::CaseInsensitive) == 0) {
            waveHammer = c.clsid;
            break;
        }
    }
    if (waveHammer.isEmpty()) {
        WARN("Wave Hammer Surround not registered — page check skipped");
        return;
    }

    const QStringList titles = SoundForgeDsHost::propertyPageTitles(waveHammer);
    INFO("pages: " << titles.join(QStringLiteral(" | ")).toStdString());
    CHECK(titles.size() >= 3);
    CHECK_FALSE(titles.contains(QString()));

    // A single-page effect must still report exactly one, or the editor would build a
    // pointless tab bar around it.
    QString chorus;
    for (const SoundForgeClass &c : SoundForgeHost::discoverEffects()) {
        if (c.name.compare(QStringLiteral("Chorus"), Qt::CaseInsensitive) == 0) {
            chorus = c.clsid;
            break;
        }
    }
    if (!chorus.isEmpty()) {
        CHECK(SoundForgeDsHost::propertyPageTitles(chorus).size() == 1);
    }
}

TEST_CASE("Every look-alike dialog that was retired has a real plug-in behind it",
          "[plugins][soundforge][dshost]")
{
    if (!SoundForgeDsHost::isAvailable()) {
        WARN("Sound Forge hosting unavailable on this platform — replacement check skipped");
        return;
    }

    // These are the effects whose VEGAS dialogs OpenVegas used to redraw in Qt
    // (OPENVEGAS_EMULATED_AUDIO_FX_UI, now off). Retiring a copy is only an improvement
    // while the genuine plug-in is there to take its place, so that is what is checked:
    // each name must be registered *and* actually host audio.
    const QStringList redrawn = {
        QStringLiteral("Chorus"),          QStringLiteral("Reverb"),
        QStringLiteral("Simple Delay"),    QStringLiteral("Noise Gate"),
        QStringLiteral("Track EQ"),        QStringLiteral("Track Compressor"),
    };

    QHash<QString, QString> byName;
    for (const SoundForgeClass &c : SoundForgeHost::discoverEffects()) {
        byName.insert(normalizeVegasPluginKey(c.name), c.clsid);
    }

    for (const QString &name : redrawn) {
        const QString clsid = byName.value(normalizeVegasPluginKey(name));
        INFO(name.toStdString() << " has no registered counterpart");
        if (clsid.isEmpty()) {
            WARN(name.toStdString() + " not registered on this machine — skipped");
            continue;
        }
        double meanDiff = 0.0;
        QString error;
        INFO(error.toStdString());
        // Building the graph is the point; several of these are deliberately transparent
        // at default settings, so the amount of change is not what makes them valid.
        CHECK(SoundForgeDsHost::probeProcess(clsid, &meanDiff, &error));
    }
}

#ifdef Q_OS_WIN
TEST_CASE("Native ExpressFX EQ controls change the processed signal",
          "[plugins][soundforge][dshost][native-editor]")
{
    AudioPluginDesc chosen;
    for (const auto &desc : SoundForgeHost::pluginDescriptors()) {
        if (normalizeVegasPluginKey(desc.name) == QStringLiteral("expressfx graphic eq")) {
            chosen = desc;
            break;
        }
    }
    if (chosen.id.isEmpty()) {
        WARN("ExpressFX Graphic EQ not registered — native editor check skipped");
        return;
    }
    int argc = 1;
    char name[] = "soundforge-editor-test";
    char *argv[] = {name, nullptr};
    qputenv("QT_QPA_PLATFORM", "windows");
    QApplication app(argc, argv);
    QWidget embed;
    embed.setAttribute(Qt::WA_DontShowOnScreen);
    auto &host = SoundForgeDsHost::instance();
    FxSlot slot;
    REQUIRE(host.createInstance(chosen, &slot));
    struct Release {
        SoundForgeDsHost &host; FxSlot &slot;
        ~Release() { host.releaseInstance(&slot); }
    } release{host, slot};
    host.prepare(&slot, 48000.0, 512);
    REQUIRE(host.openEditor(&slot, &embed));
    embed.show();
    app.processEvents();
    std::vector<HWND> sliders;
    EnumChildWindows(reinterpret_cast<HWND>(embed.winId()),
        [](HWND window, LPARAM data) -> BOOL {
            wchar_t className[128]{};
            GetClassNameW(window, className, 128);
            if (QString::fromWCharArray(className).endsWith(QStringLiteral("FaderClass")))
                reinterpret_cast<std::vector<HWND> *>(data)->push_back(window);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&sliders));
    REQUIRE(sliders.size() >= 11);
    const QByteArray before = host.getState(&slot);
    REQUIRE_FALSE(before.isEmpty());
    // The final custom fader is output gain. Drag to its attenuated end;
    // SetState alone cannot detect an editor which displays unapplied edits.
    HWND outputGain = sliders.back();
    RECT rect{}; GetClientRect(outputGain, &rect);
    const LPARAM point = MAKELPARAM(rect.right / 2, rect.bottom * 3 / 4);
    SendMessageW(outputGain, WM_LBUTTONDOWN, MK_LBUTTON, point);
    SendMessageW(outputGain, WM_MOUSEMOVE, MK_LBUTTON, point);
    SendMessageW(outputGain, WM_LBUTTONUP, 0, point);
    app.processEvents();
    wchar_t gainText[128]{}; GetDlgItemTextW(GetParent(outputGain), 2039, gainText, 128);
    INFO("native gain=" << QString::fromWCharArray(gainText).toStdString() << " height=" << rect.bottom);
    const QByteArray edited = host.getState(&slot);
    CHECK(edited != before);
    // The native page, without a manual SetState call, must update its live DSP.
    constexpr int frames = 512;
    auto signal = tone(frames);
    std::vector<float> left(frames), right(frames);
    float *in[] = {signal.data(), signal.data()};
    float *out[] = {left.data(), right.data()};
    double energy = 0.0, inputEnergy = 0.0;
    for (int block = 0; block < 8; ++block) host.process(&slot, in, out, 2, frames);
    for (int i = 0; i < frames; ++i) {
        energy += left[i] * left[i]; inputEnergy += signal[i] * signal[i];
    }
    INFO("output/input RMS=" << std::sqrt(energy / inputEnergy));
    CHECK(std::sqrt(energy / inputEnergy) < 0.95);
    CHECK(IsWindow(outputGain)); // applying must retain the editor/filter object
    auto rmsRatio = [&](FxSlot &target) {
        for (int block = 0; block < 8; ++block) host.process(&target, in, out, 2, frames);
        double result = 0.0;
        for (float value : left) result += value * value;
        return std::sqrt(result / inputEnergy);
    };
    // A graph rebuild before the UI poll must keep pending native edits.
    REQUIRE(host.synchronizeState(&slot));
    CHECK(rmsRatio(slot) < 0.95);
    slot.state = edited;
    REQUIRE(host.synchronizeState(&slot));
    slot.state = before; // document Undo restores a different blob under the same key
    REQUIRE(host.synchronizeState(&slot));
    CHECK(rmsRatio(slot) > 0.99);
    slot.state = edited;
    REQUIRE(host.synchronizeState(&slot));
    CHECK(rmsRatio(slot) < 0.95);

    FxSlot reopened;
    REQUIRE(host.createInstance(chosen, &reopened));
    Release reopenedRelease{host, reopened};
    REQUIRE(host.setState(&reopened, edited));
    host.prepare(&reopened, 44100.0, frames);
    CHECK(rmsRatio(reopened) < 0.95);
    CHECK(host.getState(&reopened) == edited);
    host.prepare(&reopened, 48000.0, frames);
    CHECK(rmsRatio(reopened) < 0.95);
}
#endif

TEST_CASE("Registered Shared effects accept audio through the production host",
          "[plugins][soundforge][dshost][catalog-dsp]")
{
    const auto descriptors = SoundForgeHost::pluginDescriptors();
    if (descriptors.isEmpty()) {
        SKIP("No Shared Plug-Ins registered");
    }
    auto &host = SoundForgeDsHost::instance();
    auto input = tone(512);
    std::vector<float> left(512), right(512);
    float *in[] = {input.data(), input.data()};
    float *out[] = {left.data(), right.data()};
    for (const auto &descriptor : descriptors) {
        INFO(descriptor.name.toStdString());
        FxSlot slot;
        REQUIRE(host.createInstance(descriptor, &slot));
        host.prepare(&slot, 48000.0, 512);
        for (int block = 0; block < 8; ++block) host.process(&slot, in, out, 2, 512);
        const auto error = host.processingError(slot);
        INFO(error.toStdString());
        CHECK(error.isEmpty());
        CHECK_FALSE(host.getState(&slot).isEmpty());
        CHECK(std::all_of(left.begin(), left.end(), [](float value) { return std::isfinite(value); }));
        CHECK(std::all_of(right.begin(), right.end(), [](float value) { return std::isfinite(value); }));
        host.releaseInstance(&slot);
    }
}
