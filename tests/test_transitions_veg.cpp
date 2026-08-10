#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "io/SamplePaths.h"
#include "io/VegReader.h"
#include "model/ProjectModel.h"
#include "video/TransitionApply.h"

#include <QDir>
#include <QFile>
#include <QSet>

using namespace openvegas;

// --- Recovery from a real Vegas project -------------------------------------------
// SAMPLES/veg_project/project_transitions_3d-blinds.veg is the user's own test bed:
// all four presets placed on a fade-in, a fade-out and a crossfade (12 instances). The
// binary layout parseTransitions() reads was reverse-engineered from this file, so the
// recovered values double as a cross-check of the screenshot-derived preset defaults.

TEST_CASE("VegReader recovers every 3D Blinds transition from the sample project",
         "[video][transitions][veg]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    const QString path =
        QDir(root).filePath(QStringLiteral("project_transitions_3d-blinds.veg"));
    if (!QFile::exists(path)) {
        SKIP("transitions sample .veg missing");
    }

    QString err;
    const VegOpenResult veg = VegReader::open(path, &err);
    CHECK(err.isEmpty());
    REQUIRE(veg.transitions.size() == 12);

    int fadeOutCount = 0;
    QSet<QString> presets;
    for (const VegTransitionInfo &t : veg.transitions) {
        presets.insert(t.presetName);
        if (t.fadeOut) {
            ++fadeOutCount;
        }
        CHECK(t.eventStartSec >= 0.0); // every one resolved to an owning event
    }
    // 4 presets x (fade-in, fade-out, crossfade); the crossfade is stored fade-in side.
    CHECK(presets.size() == 4);
    CHECK(fadeOutCount == 4);
}

TEST_CASE("Recovered 3D Blinds parameters match the catalog defaults for every preset",
         "[video][transitions][veg]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    const QString path =
        QDir(root).filePath(QStringLiteral("project_transitions_3d-blinds.veg"));
    if (!QFile::exists(path)) {
        SKIP("transitions sample .veg missing");
    }
    QString err;
    const VegOpenResult veg = VegReader::open(path, &err);
    REQUIRE_FALSE(veg.transitions.isEmpty());

    for (const VegTransitionInfo &t : veg.transitions) {
        const TransitionPresetInfo *preset =
            transitionPreset(transition3dBlindsId(), t.presetName);
        INFO("preset: " << t.presetName.toStdString());
        REQUIRE(preset);
        CHECK(t.divisions == preset->params.value(QStringLiteral("divisions")).toInt());
        CHECK(t.extraSpins == preset->params.value(QStringLiteral("extraSpins")).toInt());
        CHECK(t.stagger
              == Catch::Approx(preset->params.value(QStringLiteral("stagger")).toDouble()));
        CHECK(t.specularLight
              == Catch::Approx(preset->params.value(QStringLiteral("specularLight")).toDouble()));
        CHECK(t.direction == preset->params.value(QStringLiteral("direction")).toInt());
    }
}

TEST_CASE("Opening the sample project puts transitions on the events' fades",
         "[video][transitions][veg]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    const QString path =
        QDir(root).filePath(QStringLiteral("project_transitions_3d-blinds.veg"));
    if (!QFile::exists(path)) {
        SKIP("transitions sample .veg missing");
    }
    QString err;
    const VegOpenResult veg = VegReader::open(path, &err);

    ProjectModel model;
    model.applyVegImport(veg, path);

    int withIn = 0;
    int withOut = 0;
    for (const Track &track : model.tracks()) {
        for (const TrackEvent &ev : track.events) {
            if (ev.transitionIn.isValid()) {
                ++withIn;
                CHECK(ev.transitionIn.pluginId == transition3dBlindsId());
            }
            if (ev.transitionOut.isValid()) {
                ++withOut;
                CHECK(ev.transitionOut.pluginId == transition3dBlindsId());
            }
        }
    }
    // This is the regression the user hit: opening the project showed no transitions
    // at all because nothing parsed or applied them.
    CHECK(withIn > 0);
    CHECK(withOut > 0);
    CHECK(withIn + withOut == 12);
}
