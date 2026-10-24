#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "model/ProjectModel.h"

using namespace openvegas;

namespace {

TrackEvent makeVideoEvent(int id, double startSec, double lengthSec)
{
    TrackEvent ev;
    ev.id = id;
    ev.name = QStringLiteral("clip%1").arg(id);
    ev.mediaKind = EventMediaKind::Video;
    ev.startSec = startSec;
    ev.lengthSec = lengthSec;
    return ev;
}

} // namespace

TEST_CASE("splitAllAt splits every event under the cursor regardless of selection",
         "[split]")
{
    ProjectModel model;
    const int t1 = model.addTrack(TrackKind::Video);
    const int t2 = model.addTrack(TrackKind::Video);
    // Neither event is selected — this is the "position cursor, hit S" workflow.
    model.tracks()[t1].events.push_back(makeVideoEvent(1, 0.0, 4.0));
    model.tracks()[t2].events.push_back(makeVideoEvent(2, 0.0, 4.0));

    REQUIRE(model.splitAllAt(2.0));

    REQUIRE(model.tracks()[t1].events.size() == 2);
    REQUIRE(model.tracks()[t2].events.size() == 2);
    CHECK(model.tracks()[t1].events[0].lengthSec == Catch::Approx(2.0));
    CHECK(model.tracks()[t1].events[1].startSec == Catch::Approx(2.0));
    CHECK(model.tracks()[t1].events[1].lengthSec == Catch::Approx(2.0));
    CHECK(model.tracks()[t2].events[0].lengthSec == Catch::Approx(2.0));
}

TEST_CASE("splitAllAt only splits events that actually span the cursor", "[split]")
{
    ProjectModel model;
    const int t1 = model.addTrack(TrackKind::Video);
    model.tracks()[t1].events.push_back(makeVideoEvent(1, 0.0, 2.0));
    model.tracks()[t1].events.push_back(makeVideoEvent(2, 5.0, 2.0));

    // Cursor sits in the gap between the two clips — nothing to split.
    CHECK_FALSE(model.splitAllAt(3.5));
    REQUIRE(model.tracks()[t1].events.size() == 2);
}

TEST_CASE("splitAllAt keeps a grouped A/V pair split together at the same time", "[split]")
{
    ProjectModel model;
    const int vi = model.addTrack(TrackKind::Video);
    const int ai = model.addTrack(TrackKind::Audio);
    // groupId 5 is well outside the model's own group-id counter (starts at 1) so the
    // post-split "fresh group id" can't coincidentally collide with it.
    TrackEvent v = makeVideoEvent(1, 0.0, 6.0);
    v.groupId = 5;
    TrackEvent a = makeVideoEvent(2, 0.0, 6.0);
    a.mediaKind = EventMediaKind::Audio;
    a.groupId = 5;
    model.tracks()[vi].events.push_back(v);
    model.tracks()[ai].events.push_back(a);

    REQUIRE(model.splitAllAt(2.5));

    REQUIRE(model.tracks()[vi].events.size() == 2);
    REQUIRE(model.tracks()[ai].events.size() == 2);
    // Both new right halves must land in the SAME fresh group, not split twice.
    CHECK(model.tracks()[vi].events[1].groupId == model.tracks()[ai].events[1].groupId);
    CHECK(model.tracks()[vi].events[1].groupId != 5);
}

TEST_CASE("Trim start retains the source in-point in forward and reverse events", "[trim][split]")
{
    for (bool reversed : {false, true}) {
        ProjectModel model;
        const int track = model.addTrack(TrackKind::Video);
        TrackEvent event = makeVideoEvent(90, 2.0, 8.0);
        event.mediaStartSec = 1.0;
        event.mediaLengthSec = 20.0;
        event.reversed = reversed;
        model.tracks()[track].events.push_back(event);
        const double sourceBefore = event.sourceTimeSec(6.0);
        REQUIRE(model.trimEventStartTo(90, 5.0));
        const TrackEvent *trimmed = model.findEvent(90);
        REQUIRE(trimmed != nullptr);
        CHECK(trimmed->sourceTimeSec(6.0) == Catch::Approx(sourceBefore));
        CHECK(trimmed->startSec == Catch::Approx(5.0));
        CHECK(trimmed->lengthSec == Catch::Approx(5.0));
    }
}
TEST_CASE("Ruler offset, meter and beat note affect displayed time", "[ruler]")
{
    ProjectModel model;
    model.setRulerTimeFormat(RulerTimeFormat::Time);
    CHECK(model.formatRulerTime(61.125) == QStringLiteral("00:01:01.125"));
    model.setRulerStartSec(2.0);
    CHECK(model.formatRulerTime(61.125) == QStringLiteral("00:01:03.125"));
    model.setRulerTimeFormat(RulerTimeFormat::MeasuresBeats);
    model.setRulerStartSec(0.0);
    model.setTempoBpm(120.0);
    model.setBeatsPerMeasure(3);
    CHECK(model.formatRulerTime(1.5) == QStringLiteral("2.1"));
    model.setBeatNote(3);
    CHECK(model.formatRulerTime(1.5) == QStringLiteral("3.1"));
}
