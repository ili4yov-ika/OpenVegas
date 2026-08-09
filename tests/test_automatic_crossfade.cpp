#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "model/ProjectModel.h"

using namespace openvegas;

namespace {

TrackEvent makeAudioEvent(int id, double startSec, double lengthSec)
{
    TrackEvent ev;
    ev.id = id;
    ev.name = QStringLiteral("clip%1").arg(id);
    ev.mediaKind = EventMediaKind::Audio;
    ev.startSec = startSec;
    ev.lengthSec = lengthSec;
    return ev;
}

} // namespace

TEST_CASE("automaticCrossfades defaults on and is settable", "[crossfade]")
{
    ProjectModel model;
    CHECK(model.automaticCrossfades());
    model.setAutomaticCrossfades(false);
    CHECK_FALSE(model.automaticCrossfades());
}

TEST_CASE("applyAutomaticCrossfade sizes fades to the overlap when a clip is dragged over its neighbor",
         "[crossfade]")
{
    ProjectModel model;
    const int tid = model.addTrack(TrackKind::Audio);
    // A: [0, 3)   B dragged left to start at 2 → overlap = 1s
    model.tracks()[tid].events.push_back(makeAudioEvent(1, 0.0, 3.0));
    model.tracks()[tid].events.push_back(makeAudioEvent(2, 2.0, 3.0));

    const bool changed = model.applyAutomaticCrossfade(2);
    REQUIRE(changed);

    const TrackEvent *a = model.findEvent(1);
    const TrackEvent *b = model.findEvent(2);
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->fadeOutSec == Catch::Approx(1.0).margin(1e-6));
    CHECK(b->fadeInSec == Catch::Approx(1.0).margin(1e-6));
    // Untouched edges stay at zero.
    CHECK(a->fadeInSec == Catch::Approx(0.0).margin(1e-6));
    CHECK(b->fadeOutSec == Catch::Approx(0.0).margin(1e-6));
}

TEST_CASE("applyAutomaticCrossfade sizes both edges when dragged between two neighbors", "[crossfade]")
{
    ProjectModel model;
    const int tid = model.addTrack(TrackKind::Audio);
    // Left [0,2), Mid dragged to [1.5, 4.5) overlapping left by 0.5s, Right [4,6) overlapping mid by 0.5s
    model.tracks()[tid].events.push_back(makeAudioEvent(1, 0.0, 2.0));
    model.tracks()[tid].events.push_back(makeAudioEvent(2, 1.5, 3.0));
    model.tracks()[tid].events.push_back(makeAudioEvent(3, 4.0, 2.0));

    REQUIRE(model.applyAutomaticCrossfade(2));

    const TrackEvent *left = model.findEvent(1);
    const TrackEvent *mid = model.findEvent(2);
    const TrackEvent *right = model.findEvent(3);
    REQUIRE(left);
    REQUIRE(mid);
    REQUIRE(right);
    CHECK(left->fadeOutSec == Catch::Approx(0.5).margin(1e-6));
    CHECK(mid->fadeInSec == Catch::Approx(0.5).margin(1e-6));
    CHECK(mid->fadeOutSec == Catch::Approx(0.5).margin(1e-6));
    CHECK(right->fadeInSec == Catch::Approx(0.5).margin(1e-6));
}

TEST_CASE("applyAutomaticCrossfade does nothing when clips don't overlap", "[crossfade]")
{
    ProjectModel model;
    const int tid = model.addTrack(TrackKind::Audio);
    model.tracks()[tid].events.push_back(makeAudioEvent(1, 0.0, 2.0));
    model.tracks()[tid].events.push_back(makeAudioEvent(2, 5.0, 2.0));

    CHECK_FALSE(model.applyAutomaticCrossfade(2));
    CHECK(model.findEvent(1)->fadeOutSec == Catch::Approx(0.0).margin(1e-6));
    CHECK(model.findEvent(2)->fadeInSec == Catch::Approx(0.0).margin(1e-6));
}

TEST_CASE("applyAutomaticCrossfade clamps fades that would exceed the shorter clip's length",
         "[crossfade]")
{
    ProjectModel model;
    const int tid = model.addTrack(TrackKind::Audio);
    // B is fully dragged inside A's span → overlap == B's full length (1s).
    model.tracks()[tid].events.push_back(makeAudioEvent(1, 0.0, 5.0));
    model.tracks()[tid].events.push_back(makeAudioEvent(2, 2.0, 1.0));

    REQUIRE(model.applyAutomaticCrossfade(2));

    const TrackEvent *b = model.findEvent(2);
    REQUIRE(b);
    // fadeIn alone can't exceed the clip's own length, and fadeIn+fadeOut must
    // never exceed lengthSec (clampFades scales both edges down together).
    CHECK(b->fadeInSec <= b->lengthSec + 1e-6);
    CHECK(b->fadeInSec + b->fadeOutSec <= b->lengthSec + 1e-6);
}
