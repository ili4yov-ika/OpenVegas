#include "io/ProjectInterchange.h"
#include "io/SamplePaths.h"
#include "model/ProjectModel.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace openvegas;

static QString samplePath(const QString &rel)
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        return {};
    }
    return QDir(root).filePath(rel);
}

TEST_CASE("importEdl auto-detects Vegas CSV samples", "[interchange]")
{
    const QString path = samplePath(QStringLiteral("edl-text-file/project_sample_for_project_pictures.txt"));
    if (path.isEmpty() || !QFile::exists(path)) {
        SKIP("SAMPLES/veg_project not available");
    }
    QString err;
    const InterchangeResult r = ProjectInterchange::importEdl(path, 60.0, &err);
    REQUIRE(err.isEmpty());
    REQUIRE(r.events.size() == 2);
    CHECK(r.events[0].kind == QLatin1String("still"));
    CHECK(r.events[0].startSec == Catch::Approx(0.0).margin(1e-3));
    CHECK(r.events[0].lengthSec == Catch::Approx(5.0).margin(1e-3));
    CHECK(r.events[1].startSec == Catch::Approx(5.0).margin(1e-3));
}

TEST_CASE("importFinalCutXml parses FCPX audio-only sample", "[interchange]")
{
    const QString path = samplePath(QStringLiteral("final-cut-pro-x/project_sample_for_project_audio.fcpxml"));
    if (path.isEmpty() || !QFile::exists(path)) {
        SKIP("SAMPLES/veg_project not available");
    }
    QString err;
    const InterchangeResult r = ProjectInterchange::importFinalCutXml(path, &err);
    REQUIRE(err.isEmpty());
    REQUIRE_FALSE(r.events.isEmpty());
    CHECK(r.events[0].kind == QLatin1String("audio"));
    CHECK(r.events[0].lengthSec > 1.0);
}

TEST_CASE("importFinalCutXml reverse uses offset 0 and timeMap", "[interchange]")
{
    const QString path =
        samplePath(QStringLiteral("final-cut-pro-x/project_big--buck-bunny_4x3-preview-reverse-fades-fx.fcpxml"));
    if (path.isEmpty() || !QFile::exists(path)) {
        SKIP("SAMPLES/veg_project not available");
    }
    QString err;
    const InterchangeResult r = ProjectInterchange::importFinalCutXml(path, &err);
    REQUIRE(err.isEmpty());
    REQUIRE_FALSE(r.events.isEmpty());
    const InterchangeEvent *video = nullptr;
    for (const InterchangeEvent &ev : r.events) {
        if (ev.kind == QLatin1String("video")) {
            video = &ev;
            break;
        }
    }
    REQUIRE(video != nullptr);
    INFO("events=" << r.events.size() << " start=" << video->startSec << " len=" << video->lengthSec
                   << " fadeIn=" << video->fadeInSec << " fadeOut=" << video->fadeOutSec
                   << " rate=" << video->playRate << " mediaStart=" << video->mediaStartSec);
    CHECK(video->startSec == Catch::Approx(0.0).margin(0.05));
    CHECK(video->fadeInSec > 1.0);
    CHECK(video->fadeOutSec > 1.0);
    CHECK(video->playRate < 0.0);
}

TEST_CASE("Vegas CSV export round-trips basic events", "[interchange]")
{
    ProjectModel model;
    model.setFrameRate(30.0);
    const int tid = model.addTrack(TrackKind::Audio);
    TrackEvent ev;
    ev.id = 1;
    ev.name = QStringLiteral("tone");
    ev.mediaKind = EventMediaKind::Audio;
    ev.startSec = 1.0;
    ev.lengthSec = 2.5;
    ev.fadeInSec = 0.25;
    ev.fadeOutSec = 0.5;
    ev.gainDb = -6.0;
    ev.mediaPath = QStringLiteral("C:/media/tone.wav");
    model.tracks()[tid].events.push_back(ev);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString out = tmp.filePath(QStringLiteral("out.txt"));
    QString err;
    REQUIRE(ProjectInterchange::exportVegasCsvEdl(model, out, &err));
    REQUIRE(err.isEmpty());

    const InterchangeResult r = ProjectInterchange::importVegasCsvEdl(out, &err);
    REQUIRE(err.isEmpty());
    REQUIRE(r.events.size() == 1);
    CHECK(r.events[0].kind == QLatin1String("audio"));
    CHECK(r.events[0].startSec == Catch::Approx(1.0).margin(1e-3));
    CHECK(r.events[0].lengthSec == Catch::Approx(2.5).margin(1e-3));
    CHECK(r.events[0].fadeInSec == Catch::Approx(0.25).margin(1e-3));
    CHECK(r.events[0].hasSustainGain);
    CHECK(r.events[0].sustainGain == Catch::Approx(0.501).margin(0.02));
}

TEST_CASE("FCPXML export includes asset and video not gap", "[interchange]")
{
    ProjectModel model;
    model.setFrameRate(30.0);
    const int tid = model.addTrack(TrackKind::Video);
    TrackEvent ev;
    ev.id = 1;
    ev.name = QStringLiteral("clip");
    ev.mediaKind = EventMediaKind::Video;
    ev.startSec = 0.0;
    ev.lengthSec = 4.0;
    ev.mediaPath = QStringLiteral("C:/media/clip.mp4");
    model.tracks()[tid].events.push_back(ev);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString out = tmp.filePath(QStringLiteral("out.fcpxml"));
    QString err;
    REQUIRE(ProjectInterchange::exportFcpxml(model, out, &err));
    QFile f(out);
    REQUIRE(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString xml = QString::fromUtf8(f.readAll());
    CHECK(xml.contains(QLatin1String("<asset")));
    CHECK(xml.contains(QLatin1String("<video")));
    CHECK_FALSE(xml.contains(QLatin1String("<gap")));
}
