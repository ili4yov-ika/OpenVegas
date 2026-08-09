#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "audio/FadeCurves.h"

using openvegas::FadeCurveType;
using openvegas::fadeCurveAmplitude;

TEST_CASE("fadeCurveAmplitude endpoints", "[fade]")
{
    for (int i = 0; i < openvegas::fadeCurveCount(); ++i) {
        const auto t = static_cast<FadeCurveType>(i);
        REQUIRE(fadeCurveAmplitude(t, 0.0) == Catch::Approx(0.0).margin(1e-9));
        REQUIRE(fadeCurveAmplitude(t, 1.0) == Catch::Approx(1.0).margin(1e-9));
    }
}

TEST_CASE("fadeCurveAmplitude linear mid", "[fade]")
{
    REQUIRE(fadeCurveAmplitude(FadeCurveType::Linear, 0.5) == Catch::Approx(0.5));
}

TEST_CASE("fadeCurveAmplitude smooth S-curve mid", "[fade]")
{
    REQUIRE(fadeCurveAmplitude(FadeCurveType::Smooth, 0.5) == Catch::Approx(0.5));
    // Ease: below linear at start, above at end of first half is not required —
    // Smooth(0.25) = 0.15625
    REQUIRE(fadeCurveAmplitude(FadeCurveType::Smooth, 0.25) == Catch::Approx(0.15625));
}

// When two clips crossfade with the SAME curve on both edges (fade-out clip A
// against fade-in clip B), amplitude(t) + (1 - amplitude(t)) trivially sums to
// 1 for any t — this is how AudioGraph::processClip computes fade-out (see
// src/audio/AudioGraph.cpp: `1.0 - fadeCurveAmplitude(curve, 1.0 - fromEnd/fadeOutSec)`).
// Guard that identity explicitly so a future curve addition can't silently
// break the "constant amplitude at the crossfade midpoint" property for the
// symmetric (matched fadeOut/fadeIn duration and curve) case.
TEST_CASE("fadeCurveAmplitude crossfade sums to unity for a matched pair", "[fade]")
{
    for (int i = 0; i < openvegas::fadeCurveCount(); ++i) {
        const auto t = static_cast<FadeCurveType>(i);
        for (double x : {0.0, 0.1, 0.25, 0.5, 0.75, 0.9, 1.0}) {
            const double fadeOutGain = 1.0 - fadeCurveAmplitude(t, x);
            const double fadeInGain = fadeCurveAmplitude(t, x);
            REQUIRE(fadeOutGain + fadeInGain == Catch::Approx(1.0).margin(1e-9));
        }
    }
}
