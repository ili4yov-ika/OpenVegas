#include <catch2/catch_test_macros.hpp>

#include "model/ProjectModel.h"

#include <algorithm>

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

TEST_CASE("selectEvent(additive=true) is a real toggle, not just add", "[selection]")
{
    ProjectModel model;
    const int t1 = model.addTrack(TrackKind::Video);
    model.tracks()[t1].events.push_back(makeVideoEvent(1, 0.0, 2.0));
    model.tracks()[t1].events.push_back(makeVideoEvent(2, 3.0, 2.0));

    model.selectEvent(1, false);
    CHECK(model.selectedEventIds() == QVector<int>{1});

    // Ctrl-click the already-selected clip again — must deselect it (exclude), not
    // leave it selected (that was the old "additive only ever adds" bug).
    model.selectEvent(1, true);
    CHECK(model.selectedEventIds().isEmpty());

    // Ctrl-click adds a not-yet-selected clip on top of the existing selection.
    model.selectEvent(1, false);
    model.selectEvent(2, true);
    auto ids = model.selectedEventIds();
    std::sort(ids.begin(), ids.end());
    CHECK(ids == QVector<int>{1, 2});

    // Ctrl-click one of the two selected clips again — only it drops out.
    model.selectEvent(1, true);
    CHECK(model.selectedEventIds() == QVector<int>{2});
}

TEST_CASE("selectEvent(additive=true) toggles a whole A/V group together", "[selection]")
{
    ProjectModel model;
    const int vi = model.addTrack(TrackKind::Video);
    const int ai = model.addTrack(TrackKind::Audio);
    TrackEvent v = makeVideoEvent(1, 0.0, 4.0);
    v.groupId = 7;
    TrackEvent a = makeVideoEvent(2, 0.0, 4.0);
    a.mediaKind = EventMediaKind::Audio;
    a.groupId = 7;
    model.tracks()[vi].events.push_back(v);
    model.tracks()[ai].events.push_back(a);

    model.selectEvent(1, true);
    auto ids = model.selectedEventIds();
    std::sort(ids.begin(), ids.end());
    CHECK(ids == QVector<int>{1, 2});

    // Toggling either member off drops the whole group.
    model.selectEvent(2, true);
    CHECK(model.selectedEventIds().isEmpty());
}

TEST_CASE("selectRange selects every event between anchor and target on same track",
         "[selection]")
{
    ProjectModel model;
    const int t1 = model.addTrack(TrackKind::Video);
    model.tracks()[t1].events.push_back(makeVideoEvent(1, 0.0, 2.0));
    model.tracks()[t1].events.push_back(makeVideoEvent(2, 3.0, 2.0));
    model.tracks()[t1].events.push_back(makeVideoEvent(3, 6.0, 2.0));

    model.selectRange(1, 3);
    auto ids = model.selectedEventIds();
    std::sort(ids.begin(), ids.end());
    CHECK(ids == QVector<int>{1, 2, 3});
}

TEST_CASE("selectRange excludes events outside the anchor..target time span", "[selection]")
{
    ProjectModel model;
    const int t1 = model.addTrack(TrackKind::Video);
    model.tracks()[t1].events.push_back(makeVideoEvent(1, 0.0, 2.0));
    model.tracks()[t1].events.push_back(makeVideoEvent(2, 3.0, 2.0));
    // Well past the anchor..target span — must stay unselected.
    model.tracks()[t1].events.push_back(makeVideoEvent(3, 20.0, 2.0));

    model.selectRange(1, 2);
    auto ids = model.selectedEventIds();
    std::sort(ids.begin(), ids.end());
    CHECK(ids == QVector<int>{1, 2});
}

TEST_CASE("selectRange spans multiple tracks between anchor and target", "[selection]")
{
    ProjectModel model;
    const int t0 = model.addTrack(TrackKind::Video);
    const int t1 = model.addTrack(TrackKind::Video);
    const int t2 = model.addTrack(TrackKind::Video);
    model.tracks()[t0].events.push_back(makeVideoEvent(1, 0.0, 2.0));
    model.tracks()[t1].events.push_back(makeVideoEvent(2, 0.0, 2.0));
    model.tracks()[t2].events.push_back(makeVideoEvent(3, 0.0, 2.0));

    model.selectRange(1, 3);
    auto ids = model.selectedEventIds();
    std::sort(ids.begin(), ids.end());
    CHECK(ids == QVector<int>{1, 2, 3});
}

TEST_CASE("selectRange excludes tracks outside the anchor..target track range", "[selection]")
{
    ProjectModel model;
    const int t0 = model.addTrack(TrackKind::Video);
    const int t1 = model.addTrack(TrackKind::Video);
    const int t2 = model.addTrack(TrackKind::Video);
    model.tracks()[t0].events.push_back(makeVideoEvent(1, 0.0, 2.0));
    model.tracks()[t1].events.push_back(makeVideoEvent(2, 0.0, 2.0));
    // Anchor/target are tracks 0 and 1 only — track 2's clip must stay unselected.
    model.tracks()[t2].events.push_back(makeVideoEvent(3, 0.0, 2.0));

    model.selectRange(1, 2);
    auto ids = model.selectedEventIds();
    std::sort(ids.begin(), ids.end());
    CHECK(ids == QVector<int>{1, 2});
}

TEST_CASE("selectRange falls back to a plain select when the anchor no longer exists",
         "[selection]")
{
    ProjectModel model;
    const int t1 = model.addTrack(TrackKind::Video);
    model.tracks()[t1].events.push_back(makeVideoEvent(2, 3.0, 2.0));

    model.selectRange(/*anchor (gone)=*/999, /*target=*/2);
    CHECK(model.selectedEventIds() == QVector<int>{2});
}
