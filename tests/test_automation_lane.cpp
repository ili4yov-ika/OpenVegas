#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "model/ProjectModel.h"

using openvegas::AutomationLane;
using openvegas::AutomationPoint;
using openvegas::AutomationPointType;

TEST_CASE("AutomationLane empty fallback", "[automation]")
{
    AutomationLane lane;
    REQUIRE(lane.evaluate(1.0, 42.0) == Catch::Approx(42.0));
}

TEST_CASE("AutomationLane linear interpolate", "[automation]")
{
    AutomationLane lane;
    lane.targetId = QStringLiteral("track.volume");
    lane.points = {
        {0.0, 0.0, AutomationPointType::Linear},
        {2.0, -12.0, AutomationPointType::Linear},
    };
    REQUIRE(lane.evaluate(1.0, 0.0) == Catch::Approx(-6.0));
    REQUIRE(lane.evaluate(-1.0, 0.0) == Catch::Approx(0.0));
    REQUIRE(lane.evaluate(3.0, 0.0) == Catch::Approx(-12.0));
}

TEST_CASE("AutomationLane hold", "[automation]")
{
    AutomationLane lane;
    lane.points = {
        {0.0, 1.0, AutomationPointType::Hold},
        {1.0, 0.0, AutomationPointType::Linear},
    };
    REQUIRE(lane.evaluate(0.5, 0.0) == Catch::Approx(1.0));
}
