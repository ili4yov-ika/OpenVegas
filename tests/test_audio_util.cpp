#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "audio/AudioUtil.h"

TEST_CASE("dbToLinear unity", "[util]")
{
    REQUIRE(openvegas::dbToLinear(0.0) == Catch::Approx(1.0f));
}

TEST_CASE("dbToLinear -6 dB", "[util]")
{
    REQUIRE(openvegas::dbToLinear(-6.0) == Catch::Approx(0.501187f).margin(0.01f));
}

TEST_CASE("fader round-trip near unity", "[util]")
{
    const int pos = openvegas::dbToFaderPos(0.0);
    REQUIRE(pos == 70);
    REQUIRE(openvegas::faderPosToDb(70) == Catch::Approx(0.0).margin(0.5));
}

TEST_CASE("pan center equal power", "[util]")
{
    float l = 0, r = 0;
    openvegas::panGains(0.f, l, r);
    REQUIRE(l == Catch::Approx(r).margin(1e-5f));
    REQUIRE((l * l + r * r) == Catch::Approx(1.0f).margin(1e-4f));
}
