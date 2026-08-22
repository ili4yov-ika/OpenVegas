#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "audio/BuiltinDsp.h"
#include "plugins/AudioPluginTypes.h"

#include <chrono>
#include <cmath>
#include <vector>

using namespace openvegas;

TEST_CASE("pack/unpack FxSlot params round-trip", "[dsp]")
{
    QVariantMap m{{QStringLiteral("gainDb"), 3.5}, {QStringLiteral("wet"), 0.25}};
    const QByteArray ba = packFxParams(m);
    const QVariantMap back = unpackFxParams(ba);
    REQUIRE(back.value(QStringLiteral("gainDb")).toDouble() == Catch::Approx(3.5));
    REQUIRE(back.value(QStringLiteral("wet")).toDouble() == Catch::Approx(0.25));
}

TEST_CASE("builtin EQ gain boost increases RMS", "[dsp]")
{
    BuiltinDspState st;
    st.prepare(48000.0);
    FxSlot slot = makeFxSlot(QStringLiteral("Track EQ"), PluginFormat::Builtin,
                             QStringLiteral("builtin:Track EQ"));
    QVariantMap p{
        {QStringLiteral("band0.enabled"), false},
        {QStringLiteral("band1.enabled"), true},
        {QStringLiteral("band1.type"), 1},
        {QStringLiteral("band1.freq"), 1000.0},
        {QStringLiteral("band1.gain"), 12.0},
        {QStringLiteral("band1.rolloff"), 12.0},
        {QStringLiteral("band2.enabled"), false},
        {QStringLiteral("band3.enabled"), false},
    };
    slot.state = packFxParams(p);

    constexpr int N = 2048;
    std::vector<float> L(N), R(N);
    for (int i = 0; i < N; ++i) {
        const float s = 0.1f * std::sin(2.f * 3.14159265f * 1000.f * float(i) / 48000.f);
        L[i] = R[i] = s;
    }
    double rmsIn = 0.0;
    for (float v : L) {
        rmsIn += double(v) * v;
    }
    rmsIn = std::sqrt(rmsIn / N);

    processBuiltinFx(&slot, &st, L.data(), R.data(), N);

    double rmsOut = 0.0;
    for (float v : L) {
        rmsOut += double(v) * v;
    }
    rmsOut = std::sqrt(rmsOut / N);
    REQUIRE(rmsOut > rmsIn * 1.5);
}

TEST_CASE("builtin gate silence below threshold", "[dsp]")
{
    BuiltinDspState st;
    st.prepare(48000.0);
    FxSlot slot = makeFxSlot(QStringLiteral("Track Noise Gate"), PluginFormat::Builtin,
                             QStringLiteral("builtin:Track Noise Gate"));
    slot.state = packFxParams({
        {QStringLiteral("thresholdDb"), -20.0},
        {QStringLiteral("attackMs"), 1.0},
        {QStringLiteral("releaseMs"), 5.0},
    });
    constexpr int N = 4096;
    std::vector<float> L(N, 0.001f), R(N, 0.001f);
    processBuiltinFx(&slot, &st, L.data(), R.data(), N);
    float peak = 0.f;
    for (int i = N / 2; i < N; ++i) {
        peak = std::max(peak, std::abs(L[i]));
    }
    REQUIRE(peak < 0.0005f);
}

TEST_CASE("builtin Delay pack/unpack and wet changes energy", "[dsp][builtin-dsp]")
{
    QVariantMap m{{QStringLiteral("delayMs"), 120.0},
                  {QStringLiteral("feedback"), 0.4},
                  {QStringLiteral("mix"), 0.7}};
    const QVariantMap back = unpackFxParams(packFxParams(m));
    REQUIRE(back.value(QStringLiteral("delayMs")).toDouble() == Catch::Approx(120.0));
    REQUIRE(back.value(QStringLiteral("mix")).toDouble() == Catch::Approx(0.7));

    BuiltinDspState st;
    st.prepare(48000.0);
    FxSlot slot = makeFxSlot(QStringLiteral("Delay"), PluginFormat::Builtin,
                             QStringLiteral("builtin:Delay"));
    slot.state = packFxParams(m);

    constexpr int N = 8192;
    std::vector<float> L(N, 0.f), R(N, 0.f);
    // Impulse then silence — wet delay should leave energy after impulse
    L[0] = R[0] = 1.f;
    processBuiltinFx(&slot, &st, L.data(), R.data(), N);
    double energyTail = 0.0;
    for (int i = 100; i < N; ++i) {
        energyTail += double(L[i]) * L[i] + double(R[i]) * R[i];
    }
    REQUIRE(energyTail > 1e-4);

    // Bypass ≈ identity on new impulse block
    BuiltinDspState st2;
    st2.prepare(48000.0);
    FxSlot bypassed = slot;
    bypassed.bypass = true;
    std::vector<float> Lb(64, 0.25f), Rb(64, 0.25f);
    processBuiltinFx(&bypassed, &st2, Lb.data(), Rb.data(), 64);
    REQUIRE(Lb[0] == Catch::Approx(0.25f));
}

TEST_CASE("builtin Reverb wet increases late energy vs dry", "[dsp][builtin-dsp]")
{
    BuiltinDspState stWet;
    stWet.prepare(48000.0);
    FxSlot wet = makeFxSlot(QStringLiteral("Reverb"), PluginFormat::Builtin,
                            QStringLiteral("builtin:Reverb"));
    wet.state = packFxParams({{QStringLiteral("roomSize"), 0.8},
                             {QStringLiteral("damp"), 0.3},
                             {QStringLiteral("mix"), 0.8}});

    BuiltinDspState stDry;
    stDry.prepare(48000.0);
    FxSlot dry = wet;
    dry.state = packFxParams({{QStringLiteral("roomSize"), 0.8},
                             {QStringLiteral("damp"), 0.3},
                             {QStringLiteral("mix"), 0.0}});

    constexpr int N = 8192;
    std::vector<float> Lw(N, 0.f), Rw(N, 0.f), Ld(N, 0.f), Rd(N, 0.f);
    Lw[0] = Rw[0] = Ld[0] = Rd[0] = 1.f;
    processBuiltinFx(&wet, &stWet, Lw.data(), Rw.data(), N);
    processBuiltinFx(&dry, &stDry, Ld.data(), Rd.data(), N);

    double eWet = 0.0, eDry = 0.0;
    for (int i = 500; i < N; ++i) {
        eWet += double(Lw[i]) * Lw[i];
        eDry += double(Ld[i]) * Ld[i];
    }
    REQUIRE(eWet > eDry + 1e-6);
}

TEST_CASE("builtin Delay+Reverb processBlock under soft budget", "[dsp][builtin-dsp][perf]")
{
    BuiltinDspState stD;
    stD.prepare(48000.0);
    FxSlot delay = makeFxSlot(QStringLiteral("Simple Delay"), PluginFormat::Builtin);
    delay.state = packFxParams({{QStringLiteral("delayMs"), 200.0},
                               {QStringLiteral("feedback"), 0.3},
                               {QStringLiteral("mix"), 0.5}});
    BuiltinDspState stR;
    stR.prepare(48000.0);
    FxSlot reverb = makeFxSlot(QStringLiteral("eFX Reverb"), PluginFormat::Builtin);
    reverb.state = packFxParams({{QStringLiteral("roomSize"), 0.6},
                                {QStringLiteral("damp"), 0.4},
                                {QStringLiteral("mix"), 0.4}});

    constexpr int N = 512;
    std::vector<float> L(N, 0.1f), R(N, 0.1f);
    const auto t0 = std::chrono::steady_clock::now();
    for (int iter = 0; iter < 200; ++iter) {
        processBuiltinFx(&delay, &stD, L.data(), R.data(), N);
        processBuiltinFx(&reverb, &stR, L.data(), R.data(), N);
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count() / 200.0;
    // Soft budget: Debug may be slower; 512@48k ≈ 10.6 ms buffer — stay well under.
    REQUIRE(ms < 5.0);
}

namespace {

/** A tone that steps between quiet and loud, so a compressor's behaviour shows. */
std::vector<float> steppedTone(int frames, double sr)
{
    std::vector<float> out(size_t(frames), 0.0f);
    double phase = 0.0;
    for (int i = 0; i < frames; ++i) {
        const double t = double(i) / sr;
        const double amp = (t < 0.25) ? 0.05 : (t < 0.5 ? 0.9 : (t < 0.75 ? 0.2 : 0.7));
        out[size_t(i)] = float(amp * std::sin(phase));
        phase += 2.0 * 3.14159265358979 * 440.0 / sr;
    }
    return out;
}

double runBuiltin(const QString &name, const QVariantMap &params, std::vector<float> *signal,
                  double sr = 44100.0)
{
    FxSlot slot = makeFxSlot(name, PluginFormat::Builtin, QStringLiteral("builtin:") + name);
    slot.state = packFxParams(params);
    BuiltinDspState st;
    st.sampleRate = sr;
    st.prepare(sr);
    std::vector<float> right = *signal;
    const int frames = int(signal->size());
    const int block = 512;
    for (int i = 0; i < frames; i += block) {
        const int n = std::min(block, frames - i);
        processBuiltinFx(&slot, &st, signal->data() + i, right.data() + i, n);
    }
    double sum = 0.0;
    for (float x : *signal) {
        sum += std::fabs(double(x));
    }
    return frames ? sum / frames : 0.0;
}

} // namespace

TEST_CASE("Standard track FX are transparent at their dialog defaults",
          "[dsp][builtin-dsp][track-fx]")
{
    // The VEGAS originals are bit-transparent at the settings their dialogs open with —
    // measured against the real plug-ins. Ours must be too, or every audio track quietly
    // colours its own signal: these three sit in the default chain of every track.
    const double sr = 44100.0;
    const int frames = 44100;
    const std::vector<float> src = steppedTone(frames, sr);

    SECTION("Track Compressor")
    {
        // Amount 1,0 is 1:1 — the dialog's own reading of "no compression".
        std::vector<float> sig = src;
        runBuiltin(QStringLiteral("Track Compressor"),
                   {
                       {QStringLiteral("threshold"), 0.0},
                       {QStringLiteral("amount"), 1.0},
                       {QStringLiteral("attackMs"), 15.0},
                       {QStringLiteral("releaseMs"), 250.0},
                       {QStringLiteral("autoGain"), true},
                   },
                   &sig, sr);
        double diff = 0.0;
        for (int i = 0; i < frames; ++i) {
            diff = std::max(diff, std::fabs(double(sig[size_t(i)]) - double(src[size_t(i)])));
        }
        INFO("peak deviation " << diff);
        CHECK(diff < 1e-6);
    }

    SECTION("Track Noise Gate")
    {
        // The fader's bottom reads "-Inf", which means the gate does nothing at all.
        std::vector<float> sig = src;
        runBuiltin(QStringLiteral("Track Noise Gate"),
                   {
                       {QStringLiteral("thresholdDb"), -60.0},
                       {QStringLiteral("attackMs"), 3.0},
                       {QStringLiteral("releaseMs"), 100.0},
                   },
                   &sig, sr);
        double diff = 0.0;
        for (int i = 0; i < frames; ++i) {
            diff = std::max(diff, std::fabs(double(sig[size_t(i)]) - double(src[size_t(i)])));
        }
        INFO("peak deviation " << diff);
        CHECK(diff < 1e-6);
    }

    SECTION("Track EQ")
    {
        // Every band opens flat, so the curve is a straight line at 0 dB.
        std::vector<float> sig = src;
        runBuiltin(QStringLiteral("Track EQ"), {}, &sig, sr);
        double diff = 0.0;
        for (int i = 0; i < frames; ++i) {
            diff = std::max(diff, std::fabs(double(sig[size_t(i)]) - double(src[size_t(i)])));
        }
        INFO("peak deviation " << diff);
        CHECK(diff < 1e-6);
    }
}

TEST_CASE("Track Compressor honours Amount as a ratio", "[dsp][builtin-dsp][track-fx]")
{
    const double sr = 44100.0;
    const int frames = 44100;
    const std::vector<float> src = steppedTone(frames, sr);

    auto levelFor = [&](double amount) {
        std::vector<float> sig = src;
        return runBuiltin(QStringLiteral("Track Compressor"),
                          {
                              {QStringLiteral("threshold"), -20.0},
                              {QStringLiteral("amount"), amount},
                              {QStringLiteral("attackMs"), 15.0},
                              {QStringLiteral("releaseMs"), 250.0},
                              {QStringLiteral("autoGain"), false},
                          },
                          &sig, sr);
    };

    double reference = 0.0;
    for (float x : src) {
        reference += std::fabs(double(x));
    }
    reference /= frames;

    // 1:1 must leave the signal alone even with the threshold well down — this is what
    // the old `ratio = 1 + amount` mapping got wrong, compressing 2:1 at the default.
    CHECK(levelFor(1.0) == Catch::Approx(reference).epsilon(1e-6));
    // Higher ratios must reduce progressively, not arbitrarily.
    const double at2 = levelFor(2.0);
    const double at4 = levelFor(4.0);
    CHECK(at2 < reference);
    CHECK(at4 < at2);
}

TEST_CASE("Auto gain compensation restores level the ratio took away",
          "[dsp][builtin-dsp][track-fx]")
{
    const double sr = 44100.0;
    const int frames = 44100;
    const std::vector<float> src = steppedTone(frames, sr);

    auto levelFor = [&](bool autoGain) {
        std::vector<float> sig = src;
        return runBuiltin(QStringLiteral("Track Compressor"),
                          {
                              {QStringLiteral("threshold"), -20.0},
                              {QStringLiteral("amount"), 4.0},
                              {QStringLiteral("attackMs"), 15.0},
                              {QStringLiteral("releaseMs"), 250.0},
                              {QStringLiteral("autoGain"), autoGain},
                          },
                          &sig, sr);
    };

    // The dialog ships this checkbox on; before it did nothing whatsoever, so turning
    // Amount up simply made the track quieter.
    CHECK(levelFor(true) > levelFor(false) * 1.5);
}
