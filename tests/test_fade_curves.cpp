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
