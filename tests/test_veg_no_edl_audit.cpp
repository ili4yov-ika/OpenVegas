#include <catch2/catch_test_macros.hpp>

#include "io/SamplePaths.h"
#include "io/VegReader.h"
#include "model/ProjectModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cmath>

using namespace openvegas;

// Audits every sample .veg project against ProjectModel::applyVegImport(..., allowEdlSidecar=false)
// — i.e. exactly what a real user's own footage would hit, since almost nobody has a Vegas
// "EDL Text File" sidecar sitting next to their .veg. Real Vegas users are the target here,
// not just this repo's own sample set — a bug that only reproduces without an EDL sidecar
// stays invisible as long as every test (and every manual check) is done against files that
// happen to have one, which is exactly what let the "2 video tracks" bug from earlier this
// week ship unnoticed. General assertions (no crash, sane timing, resolvable media) run
// against all of them; the video-track-count assertion is the specific regression guard for
// that bug and only applies to Titles & Text projects (see ProjectModel.cpp's
// isGeneratorSlot lambda for why that one needed the fix).

namespace {

const QStringList &sampleProjectNames()
{
    static const QStringList names = {
        QStringLiteral("project_big--buck-bunny.veg"),
        QStringLiteral("project_big--buck-bunny_4x3-preview-and-fades.veg"),
        QStringLiteral("project_big--buck-bunny_4x3-preview-and-fades_color-grading.veg"),
        QStringLiteral("project_big--buck-bunny_4x3-preview-reverse-fades-fx.veg"),
        QStringLiteral("project_big--buck-bunny_4x3-preview-reverse-fades-fx1.veg"),
        QStringLiteral("project_big--buck-bunny_576x1024-preview-and-fades.veg"),
        QStringLiteral("project_big--buck-bunny_colors-tracks.veg"),
        QStringLiteral("project_big--buck-bunny_fades.veg"),
        QStringLiteral("project_big--buck-bunny_markers.veg"),
        QStringLiteral("project_big--buck-bunny_mix-console-2.veg"),
        QStringLiteral("project_big--buck-bunny_mix-console.veg"),
        QStringLiteral("project_big--buck-bunny_opacity-gain.veg"),
        QStringLiteral("project_big--buck-bunny_pan-crop.veg"),
        QStringLiteral("project_big--buck-bunny_pan-crop_mask.veg"),
        QStringLiteral("project_big--buck-bunny_titles-and-text.veg"),
        QStringLiteral("project_big--buck-bunny_track-motion.veg"),
        QStringLiteral("project_big--buck-bunny_trims-and-crossfade.veg"),
        QStringLiteral("project_sample_for_project_audio.veg"),
        QStringLiteral("project_sample_for_project_audio_trims-and-crossfade.veg"),
        QStringLiteral("project_sample_for_project_pictures.veg"),
        QStringLiteral("project_titles-and-text.veg"),
    };
    return names;
}

} // namespace

TEST_CASE("Every sample .veg opens sanely without an EDL sidecar", "[interchange][veg][no-edl]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }

    int checked = 0;
    for (const QString &name : sampleProjectNames()) {
        const QString path = QDir(root).filePath(name);
        if (!QFile::exists(path)) {
            WARN("missing sample: " << name.toStdString());
            continue;
        }
        INFO("project: " << name.toStdString());

        QString err;
        const VegOpenResult veg = VegReader::open(path, &err);
        CHECK(err.isEmpty());

        ProjectModel model;
        const bool usedEdl = model.applyVegImport(veg, path, /*allowEdlSidecar=*/false);
        CHECK_FALSE(usedEdl);

        int trackCount = 0;
        int videoTrackCount = 0;
        int eventCount = 0;
        for (const Track &tr : model.tracks()) {
            ++trackCount;
            if (tr.kind == TrackKind::Video) {
                ++videoTrackCount;
            }
            for (const TrackEvent &ev : tr.events) {
                ++eventCount;
                CHECK(std::isfinite(ev.startSec));
                CHECK(std::isfinite(ev.lengthSec));
                CHECK(ev.startSec >= 0.0);
                CHECK(ev.lengthSec > 0.0);
            }
        }
        CHECK(trackCount > 0);
        CHECK(eventCount > 0);

        // The specific regression this audit exists to catch: a Titles & Text project
        // must land on exactly one video track, matching real Vegas, whether or not an
        // EDL sidecar is available (see ProjectModel::applyTitlesTextFromVeg's
        // provenance comments for the two independent bugs that broke this).
        if (!veg.titlesAndText.isEmpty()) {
            CHECK(videoTrackCount == 1);
        }

        ++checked;
    }
    REQUIRE(checked >= 15); // guards against the sample list silently going stale
}

// Regression guard: the binary-timeline (no-EDL) import path used to only recover
// crossfades (fadeIn/fadeOut from clip overlap) for video events — grouped-audio
// partners inherited the video-derived fade, but standalone (audio-only) events
// were left at fadeIn=fadeOut=0 even when they visibly overlapped a neighbor on
// the same track. project_sample_for_project_audio_* is an audio-only project
// (no video track at all), so every event here goes through that standalone path.
TEST_CASE("No-EDL veg import recovers crossfades on standalone (audio-only) overlapping clips",
         "[interchange][veg][no-edl][crossfade]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    const QString path =
        QDir(root).filePath(QStringLiteral("project_sample_for_project_audio_trims-and-crossfade.veg"));
    if (!QFile::exists(path)) {
        SKIP("audio trims-and-crossfade sample missing");
    }

    QString err;
    const VegOpenResult veg = VegReader::open(path, &err);
    CHECK(err.isEmpty());

    ProjectModel model;
    model.applyVegImport(veg, path, /*allowEdlSidecar=*/false);

    // Collect all audio events per track, sorted by start time, and check that
    // every pair which geometrically overlaps also has a matching fade pair —
    // i.e. the crossfade was actually reconstructed, not just left implicit.
    bool sawOverlap = false;
    for (const Track &tr : model.tracks()) {
        if (tr.kind != TrackKind::Audio) {
            continue;
        }
        QVector<const TrackEvent *> sorted;
        for (const TrackEvent &ev : tr.events) {
            sorted.push_back(&ev);
        }
        std::sort(sorted.begin(), sorted.end(),
                 [](const TrackEvent *a, const TrackEvent *b) { return a->startSec < b->startSec; });
        for (int i = 0; i + 1 < sorted.size(); ++i) {
            const TrackEvent &a = *sorted[i];
            const TrackEvent &b = *sorted[i + 1];
            const double overlap =
                std::min(a.startSec + a.lengthSec, b.startSec + b.lengthSec) - b.startSec;
            if (overlap > 0.05) {
                sawOverlap = true;
                CHECK(a.fadeOutSec > 0.0);
                CHECK(b.fadeInSec > 0.0);
            }
        }
    }
    CHECK(sawOverlap); // the sample is named "trims-and-crossfade" — it must contain one
}
