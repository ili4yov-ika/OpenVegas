#include "io/ProjectInterchange.h"
#include "io/SamplePaths.h"
#include "io/VegReader.h"
#include "model/ProjectModel.h"
#include "video/TitlesTextApply.h"
#include "video/TransitionApply.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

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

TEST_CASE("FCP7 XMEML export writes audio fades as transitionitem", "[interchange]")
{
    // Guards against regressing the audio-track fade loss fixed alongside the
    // video path: writeClipWithFades() must be used for BOTH <video> and
    // <audio> tracks, not just <video>.
    ProjectModel model;
    model.setFrameRate(30.0);
    const int tid = model.addTrack(TrackKind::Audio);
    TrackEvent ev;
    ev.id = 1;
    ev.name = QStringLiteral("tone");
    ev.mediaKind = EventMediaKind::Audio;
    ev.startSec = 1.0;
    ev.lengthSec = 3.0;
    ev.fadeInSec = 0.5;
    ev.fadeOutSec = 0.75;
    ev.mediaPath = QStringLiteral("C:/media/tone.wav");
    model.tracks()[tid].events.push_back(ev);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString out = tmp.filePath(QStringLiteral("out.xml"));
    QString err;
    REQUIRE(ProjectInterchange::exportFinalCutXml(model, out, &err));
    REQUIRE(err.isEmpty());

    QFile f(out);
    REQUIRE(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString xml = QString::fromUtf8(f.readAll());
    const int audioStart = xml.indexOf(QLatin1String("<audio>"));
    REQUIRE(audioStart >= 0);
    const QString audioSection = xml.mid(audioStart);
    CHECK(audioSection.contains(QLatin1String("<transitionitem>")));
    CHECK(audioSection.contains(QLatin1String("Cross Fade")));

    const InterchangeResult r = ProjectInterchange::importFinalCutXml(out, &err);
    REQUIRE(err.isEmpty());
    REQUIRE(r.events.size() == 1);
    CHECK(r.events[0].fadeInSec > 0.0);
    CHECK(r.events[0].fadeOutSec > 0.0);
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

namespace {

// Every sample name shares a base across all four interchange sample directories, just
// with a different subdirectory + extension per format.
const QStringList &sampleBaseNames()
{
    static const QStringList names = {
        QStringLiteral("project_big--buck-bunny"),
        QStringLiteral("project_big--buck-bunny_4x3-preview-and-fades"),
        QStringLiteral("project_big--buck-bunny_4x3-preview-and-fades_color-grading"),
        QStringLiteral("project_big--buck-bunny_4x3-preview-reverse-fades-fx"),
        QStringLiteral("project_big--buck-bunny_4x3-preview-reverse-fades-fx1"),
        QStringLiteral("project_big--buck-bunny_576x1024-preview-and-fades"),
        QStringLiteral("project_big--buck-bunny_fades"),
        QStringLiteral("project_big--buck-bunny_markers"),
        QStringLiteral("project_big--buck-bunny_mix-console-2"),
        QStringLiteral("project_big--buck-bunny_mix-console"),
        QStringLiteral("project_big--buck-bunny_opacity-gain"),
        QStringLiteral("project_big--buck-bunny_pan-crop"),
        QStringLiteral("project_big--buck-bunny_pan-crop_mask"),
        QStringLiteral("project_big--buck-bunny_titles-and-text"),
        QStringLiteral("project_big--buck-bunny_track-motion"),
        QStringLiteral("project_sample_for_project_audio"),
        QStringLiteral("project_sample_for_project_audio_trims-and-crossfade"),
        QStringLiteral("project_sample_for_project_pictures"),
        QStringLiteral("project_titles-and-text"),
        QStringLiteral("project_transitions_3d-blinds"),
    };
    return names;
}

} // namespace

TEST_CASE("importFinalCutXml parses every FCP7/Resolve XML sample", "[interchange]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    int checked = 0;
    for (const QString &base : sampleBaseNames()) {
        const QString path =
            samplePath(QStringLiteral("final-cut-pro-7_davinci-resolve/%1.xml").arg(base));
        if (!QFile::exists(path)) {
            WARN("missing FCP7 sample: " << base.toStdString());
            continue;
        }
        INFO("sample: " << base.toStdString());
        QString err;
        const InterchangeResult r = ProjectInterchange::importFinalCutXml(path, &err);
        CHECK(err.isEmpty());
        CHECK_FALSE(r.media.isEmpty());
        for (const InterchangeEvent &ev : r.events) {
            CHECK(std::isfinite(ev.startSec));
            CHECK(std::isfinite(ev.lengthSec));
            CHECK(ev.lengthSec > 0.0);
        }
        ++checked;
    }
    REQUIRE(checked >= 15);
}

TEST_CASE("importFinalCutXml parses every FCPX sample", "[interchange]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    int checked = 0;
    for (const QString &base : sampleBaseNames()) {
        const QString path = samplePath(QStringLiteral("final-cut-pro-x/%1.fcpxml").arg(base));
        if (!QFile::exists(path)) {
            WARN("missing FCPX sample: " << base.toStdString());
            continue;
        }
        INFO("sample: " << base.toStdString());
        QString err;
        const InterchangeResult r = ProjectInterchange::importFinalCutXml(path, &err);
        CHECK(err.isEmpty());
        CHECK_FALSE(r.media.isEmpty());
        for (const InterchangeEvent &ev : r.events) {
            CHECK(std::isfinite(ev.startSec));
            CHECK(std::isfinite(ev.lengthSec));
            CHECK(ev.lengthSec > 0.0);
        }
        ++checked;
    }
    REQUIRE(checked >= 15);
}

TEST_CASE("importPremiereProject parses every .prproj sample", "[interchange]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    int checked = 0;
    for (const QString &base : sampleBaseNames()) {
        const QString path = samplePath(QStringLiteral("premiere_after-effect/%1.prproj").arg(base));
        if (!QFile::exists(path)) {
            WARN("missing Premiere sample: " << base.toStdString());
            continue;
        }
        INFO("sample: " << base.toStdString());
        QString err;
        const InterchangeResult r = ProjectInterchange::importPremiereProject(path, &err);
        CHECK(err.isEmpty());
        CHECK_FALSE(r.media.isEmpty());
        ++checked;
    }
    REQUIRE(checked >= 15);
}

TEST_CASE("importVegasCsvEdl parses every EDL Text File sample", "[interchange]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    int checked = 0;
    for (const QString &base : sampleBaseNames()) {
        const QString path = samplePath(QStringLiteral("edl-text-file/%1.txt").arg(base));
        if (!QFile::exists(path)) {
            WARN("missing EDL sample: " << base.toStdString());
            continue;
        }
        INFO("sample: " << base.toStdString());
        QString err;
        const InterchangeResult r = ProjectInterchange::importVegasCsvEdl(path, &err);
        CHECK(err.isEmpty());
        CHECK_FALSE(r.events.isEmpty());
        for (const InterchangeEvent &ev : r.events) {
            CHECK(std::isfinite(ev.startSec));
            CHECK(std::isfinite(ev.lengthSec));
            CHECK(ev.lengthSec > 0.0);
        }
        ++checked;
    }
    REQUIRE(checked >= 15);
}

TEST_CASE("Vegas .veg round-trips through export → re-import for every format",
          "[interchange][roundtrip]")
{
    // Exercises the OTHER direction from the import audits above: open a real project,
    // export it through each of the four writer functions, then feed that output straight
    // back through the matching reader and check it still describes a real timeline. Picks
    // a project with real track/event variety (fades) rather than a trivial one.
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    const QString vegPath =
        QDir(root).filePath(QStringLiteral("project_big--buck-bunny_fades.veg"));
    if (!QFile::exists(vegPath)) {
        SKIP("sample .veg missing");
    }
    QString err;
    const VegOpenResult veg = VegReader::open(vegPath, &err);
    REQUIRE(err.isEmpty());
    ProjectModel model;
    model.applyVegImport(veg, vegPath);
    REQUIRE_FALSE(model.tracks().isEmpty());

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    SECTION("Vegas EDL CSV")
    {
        const QString out = tmp.filePath(QStringLiteral("out.txt"));
        REQUIRE(ProjectInterchange::exportVegasCsvEdl(model, out, &err));
        REQUIRE(err.isEmpty());
        const InterchangeResult back = ProjectInterchange::importVegasCsvEdl(out, &err);
        CHECK(err.isEmpty());
        CHECK_FALSE(back.events.isEmpty());
    }
    SECTION("generic EDL")
    {
        const QString out = tmp.filePath(QStringLiteral("out_generic.txt"));
        REQUIRE(ProjectInterchange::exportEdl(model, out, &err));
        REQUIRE(err.isEmpty());
        const InterchangeResult back = ProjectInterchange::importEdl(out, model.frameRate(), &err);
        CHECK(err.isEmpty());
        CHECK_FALSE(back.events.isEmpty());
    }
    SECTION("Final Cut Pro 7 XML")
    {
        const QString out = tmp.filePath(QStringLiteral("out.xml"));
        REQUIRE(ProjectInterchange::exportFinalCutXml(model, out, &err));
        REQUIRE(err.isEmpty());
        const InterchangeResult back = ProjectInterchange::importFinalCutXml(out, &err);
        CHECK(err.isEmpty());
        CHECK_FALSE(back.media.isEmpty());
    }
    SECTION("FCPXML")
    {
        const QString out = tmp.filePath(QStringLiteral("out.fcpxml"));
        REQUIRE(ProjectInterchange::exportFcpxml(model, out, &err));
        REQUIRE(err.isEmpty());
        const InterchangeResult back = ProjectInterchange::importFinalCutXml(out, &err);
        CHECK(err.isEmpty());
        CHECK_FALSE(back.media.isEmpty());
    }
}

TEST_CASE("OpenVegas project archive round-trips full state (fxChain, panCrop, markers)",
          "[interchange][archive][roundtrip]")
{
    // This is the fix for "Save does nothing" (MARKDOWN/UI_STUBS_AUDIT.md): the v1 archive
    // format only kept clip timing, so opening a "saved" project back up silently dropped
    // every generator/effect. Exercises exactly the state that was previously lost —
    // Titles & Text fxChain params, event Pan/Crop keyframes, project markers, track
    // display color/mute/solo — round-tripped through export -> import into a fresh model.
    ProjectModel model;
    model.setFrameRate(29.97);
    model.setSampleRate(48000);
    model.setTempoBpm(126.0);
    model.setFrameSize(1920, 1080);
    model.markers().push_back(TimelineMarker{0, 1, 3.5, QStringLiteral("Marker One"), false});

    const int vid = model.addTrack(TrackKind::Video);
    Track &vtrack = model.tracks()[vid];
    vtrack.name = QStringLiteral("Titles & Text");
    vtrack.muted = true;
    vtrack.displayColor = QColor(0x1a, 0x8a, 0x4a);

    TrackEvent tev;
    tev.id = 1;
    tev.name = QStringLiteral("Sample Text");
    tev.startSec = 0.0;
    tev.lengthSec = 5.0;
    tev.mediaKind = EventMediaKind::Title;
    TitlesTextParams tp;
    tp.text = QStringLiteral("Round-trip me");
    tp.animationName = QStringLiteral("_Bounce");
    tp.textColor = QColor(0, 128, 0, 255);
    tp.scale = 1.3;
    FxSlot titlesSlot = makeFxSlot(QStringLiteral("VEGAS Titles & Text"), PluginFormat::Builtin,
                                   QStringLiteral("{Svfx:com.vegascreativesoftware:titlesandtext}"));
    titlesTextSaveToSlot(&titlesSlot, tp);
    tev.fxChain = {titlesSlot};
    tev.panCrop.positionKeyframes.push_back(PanCropKeyframe{});
    tev.panCrop.positionKeyframes[0].timeSec = 0.0;
    tev.panCrop.positionKeyframes[0].xCenter = 500.0;
    tev.fadeInSec = 1.0;
    tev.transitionIn =
        makeTransitionInstance(transition3dBlindsId(), QStringLiteral("Slot Machine"));
    transitionSetParamValue(&tev.transitionIn, QStringLiteral("stagger"), 0.55);
    vtrack.events.push_back(tev);

    const int aid = model.addTrack(TrackKind::Audio);
    Track &atrack = model.tracks()[aid];
    atrack.name = QStringLiteral("Audio 1");
    atrack.volumeDb = -6.0;
    TrackEvent aev;
    aev.id = 2;
    aev.name = QStringLiteral("clip.wav");
    aev.mediaPath = QStringLiteral("C:/media/clip.wav");
    aev.startSec = 1.0;
    aev.lengthSec = 4.0;
    aev.gainDb = -3.0;
    aev.mediaKind = EventMediaKind::Audio;
    atrack.events.push_back(aev);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString archiveDir = tmp.filePath(QStringLiteral("MyProject"));
    QString err;
    REQUIRE(ProjectInterchange::exportProjectArchive(model, archiveDir, /*copyMedia=*/false, &err));
    REQUIRE(err.isEmpty());
    REQUIRE(ProjectInterchange::isProjectArchive(archiveDir));

    ProjectModel back;
    REQUIRE(ProjectInterchange::importProjectArchive(archiveDir, &back, &err));
    REQUIRE(err.isEmpty());

    CHECK(back.frameRate() == Catch::Approx(29.97));
    CHECK(back.sampleRate() == 48000);
    CHECK(back.tempoBpm() == Catch::Approx(126.0));
    CHECK(back.frameWidth() == 1920);
    CHECK(back.frameHeight() == 1080);
    REQUIRE(back.markers().size() == 1);
    CHECK(back.markers()[0].label == QStringLiteral("Marker One"));
    CHECK(back.markers()[0].timeSec == Catch::Approx(3.5));

    REQUIRE(back.tracks().size() == 2);
    const Track &backV = back.tracks()[0];
    CHECK(backV.kind == TrackKind::Video);
    CHECK(backV.muted);
    CHECK(backV.displayColor == QColor(0x1a, 0x8a, 0x4a));
    REQUIRE(backV.events.size() == 1);
    const TrackEvent &backTev = backV.events[0];
    CHECK(backTev.mediaKind == EventMediaKind::Title);
    REQUIRE(backTev.fxChain.size() == 1);
    const TitlesTextParams backTp = titlesTextFromSlot(backTev.fxChain.first());
    CHECK(backTp.text == QStringLiteral("Round-trip me"));
    CHECK(backTp.animationName == QStringLiteral("_Bounce"));
    CHECK(backTp.textColor == QColor(0, 128, 0, 255));
    CHECK(backTp.scale == Catch::Approx(1.3));
    REQUIRE(backTev.panCrop.positionKeyframes.size() == 1);
    CHECK(backTev.panCrop.positionKeyframes[0].xCenter == Catch::Approx(500.0));
    // A transition on the fade must survive the archive — otherwise every transition a
    // user places would silently vanish on the next save/load.
    REQUIRE(backTev.transitionIn.isValid());
    CHECK(backTev.transitionIn.pluginId == transition3dBlindsId());
    CHECK(transitionParamValue(backTev.transitionIn, QStringLiteral("stagger"))
          == Catch::Approx(0.55));
    CHECK(transitionParamValue(backTev.transitionIn, QStringLiteral("extraSpins"))
          == Catch::Approx(4.0));
    CHECK_FALSE(backTev.transitionOut.isValid());

    const Track &backA = back.tracks()[1];
    CHECK(backA.kind == TrackKind::Audio);
    CHECK(backA.volumeDb == Catch::Approx(-6.0));
    REQUIRE(backA.events.size() == 1);
    CHECK(backA.events[0].mediaPath == QStringLiteral("C:/media/clip.wav"));
    CHECK(backA.events[0].gainDb == Catch::Approx(-3.0));
}

TEST_CASE("Interchange formats carry a transition's fade but not its identity",
         "[interchange][transitions]")
{
    // Verified against Vegas's own exports of SAMPLES/veg_project/
    // project_transitions_3d-blinds.veg: even real Vegas degrades a 3D Blinds
    // transition to a generic dissolve — FCP7 XML gets "Cross Dissolve" /
    // "Fade In Fade Out Dissolve", FCPXML gets <transition name="Cross Dissolve">, the
    // EDL CSV has no transition column at all, and the .prproj only mentions "3D Blinds"
    // inside user marker labels. So losing the plug-in identity here is the format's
    // limit, not a bug — but the fade the transition rides on must still survive, and
    // the project archive (our own format) must keep the whole thing.
    ProjectModel model;
    model.setFrameRate(30.0);
    const int tid = model.addTrack(TrackKind::Video);
    TrackEvent ev;
    ev.id = 1;
    ev.name = QStringLiteral("clip");
    ev.mediaKind = EventMediaKind::Video;
    ev.startSec = 0.0;
    ev.lengthSec = 6.0;
    ev.fadeInSec = 1.5;
    ev.mediaPath = QStringLiteral("C:/media/clip.mp4");
    ev.transitionIn = makeTransitionInstance(transition3dBlindsId(), QStringLiteral("Spin"));
    model.tracks()[tid].events.push_back(ev);

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    QString err;

    const QString fcp7 = tmp.filePath(QStringLiteral("t.xml"));
    REQUIRE(ProjectInterchange::exportFinalCutXml(model, fcp7, &err));
    REQUIRE(err.isEmpty());
    const InterchangeResult backFcp7 = ProjectInterchange::importFinalCutXml(fcp7, &err);
    CHECK(err.isEmpty());
    REQUIRE(backFcp7.events.size() == 1);
    CHECK(backFcp7.events[0].fadeInSec > 0.0); // the fade survives …
    QFile f(fcp7);
    REQUIRE(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString xml = QString::fromUtf8(f.readAll());
    CHECK(xml.contains(QLatin1String("Cross Dissolve"))); // … as a generic dissolve
    CHECK_FALSE(xml.contains(QLatin1String("3D Blinds")));

    // Our own archive is the format that must not lose it.
    const QString archive = tmp.filePath(QStringLiteral("Archive"));
    REQUIRE(ProjectInterchange::exportProjectArchive(model, archive, /*copyMedia=*/false, &err));
    ProjectModel back;
    REQUIRE(ProjectInterchange::importProjectArchive(archive, &back, &err));
    REQUIRE_FALSE(back.tracks().isEmpty());
    REQUIRE_FALSE(back.tracks()[0].events.isEmpty());
    const TransitionInstance &backT = back.tracks()[0].events[0].transitionIn;
    REQUIRE(backT.isValid());
    CHECK(backT.presetName == QStringLiteral("Spin"));
    CHECK(transitionParamValue(backT, QStringLiteral("divisions")) == Catch::Approx(1.0));
}
