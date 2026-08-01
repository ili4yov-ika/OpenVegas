#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "audio/BuiltinDsp.h"
#include "plugins/AudioPluginTypes.h"

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
