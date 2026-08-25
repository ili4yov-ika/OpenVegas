#include "capture/CapturePlan.h"
#include "capture/CaptureRecorder.h"
#include "capture/CaptureSources.h"

#include <QSet>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace openvegas;

namespace {

CaptureSource screen(const QString &name, int w, int h, double fps = 60.0)
{
    CaptureSource s;
    s.kind = CaptureSource::Kind::Screen;
    s.id = name;
    s.name = name;
    s.nativeSize = QSize(w, h);
    s.frameRate = fps;
    return s;
}

CaptureSource camera(const QString &name, int w, int h, double fps = 30.0)
{
    CaptureSource s = screen(name, w, h, fps);
    s.kind = CaptureSource::Kind::Camera;
    return s;
}

CaptureSource mic(const QString &name, int rate, int chans, int depth)
{
    CaptureSource s;
    s.kind = CaptureSource::Kind::Audio;
    s.id = name;
    s.name = name;
    s.sampleRate = rate;
    s.channels = chans;
    s.bitDepth = depth;
    return s;
}

} // namespace

TEST_CASE("The take's resolution follows the reference video source", "[capture]")
{
    CapturePlan plan;
    plan.sources = {camera(QStringLiteral("Webcam"), 1280, 720),
                    screen(QStringLiteral("Monitor"), 3840, 2160)};

    // With nothing chosen, the largest video source is what "the take's resolution" means.
    // Sizing a take by a webcam because it happened to be ticked first would surprise.
    CHECK(plan.resolution() == QSize(3840, 2160));
    CHECK(plan.frameRate() == Catch::Approx(60.0));

    // Choosing explicitly wins.
    plan.referenceIndex = 0;
    CHECK(plan.resolution() == QSize(1280, 720));
    CHECK(plan.frameRate() == Catch::Approx(30.0));

    // Pointing the reference at an audio source is meaningless, so it falls back rather
    // than leaving the take with no size at all.
    plan.sources.push_back(mic(QStringLiteral("Mic"), 48000, 2, 24));
    plan.referenceIndex = 2;
    CHECK(plan.resolution() == QSize(3840, 2160));

    // An override beats the reference outright: recording a 4K screen into a 1080p take.
    plan.referenceIndex = -1;
    plan.forcedSize = QSize(1920, 1080);
    CHECK(plan.resolution() == QSize(1920, 1080));
}

TEST_CASE("Audio is recorded at the best quality any chosen source offers", "[capture]")
{
    CapturePlan plan;
    plan.sources = {mic(QStringLiteral("Headset"), 44100, 1, 16),
                    mic(QStringLiteral("Interface"), 96000, 2, 24),
                    mic(QStringLiteral("Loopback"), 48000, 2, 16)};

    // A container will not hold three audio streams at three different rates and depths,
    // so the take takes the best on offer and brings the poorer sources up to it. Taking
    // the lowest would throw away quality that was there for nothing.
    CHECK(plan.sampleRate() == 96000);
    CHECK(plan.channels() == 2);
    CHECK(plan.bitDepth() == 24);

    for (const CaptureOutput &o : plan.outputs()) {
        INFO(o.source.name.toStdString());
        CHECK(o.sampleRate == 96000);
        CHECK(o.channels == 2);
    }
}

TEST_CASE("Every source gets its own file, and the names stay distinct", "[capture]")
{
    CapturePlan plan;
    plan.takeName = QStringLiteral("Interview");
    // Two identical capture cards, and two monitors, report the same name — which real
    // hardware regularly does.
    plan.sources = {screen(QStringLiteral("Display"), 1920, 1080),
                    screen(QStringLiteral("Display"), 1920, 1080),
                    camera(QStringLiteral("Cam Link"), 1920, 1080),
                    mic(QStringLiteral("Mic"), 48000, 2, 24)};

    const QVector<CaptureOutput> outs = plan.outputs();
    // One per source: they have to land on separate tracks when the take is imported, and
    // a single muxed file could not be pulled apart again.
    REQUIRE(outs.size() == plan.sources.size());

    QSet<QString> names;
    for (const CaptureOutput &o : outs) {
        INFO(o.fileName.toStdString());
        CHECK_FALSE(names.contains(o.fileName));
        names.insert(o.fileName);
        CHECK(o.fileName.startsWith(QStringLiteral("Interview_")));
    }
    CHECK(outs[3].fileName.endsWith(QStringLiteral(".wav")));
    CHECK(outs[0].fileName.endsWith(QStringLiteral(".mkv")));
}

TEST_CASE("Fit decides whether the other video sources are resized", "[capture]")
{
    CapturePlan plan;
    plan.sources = {screen(QStringLiteral("Monitor"), 3840, 2160),
                    camera(QStringLiteral("Webcam"), 1280, 720)};

    plan.fit = CaptureFit::Letterbox;
    QVector<CaptureOutput> outs = plan.outputs();
    CHECK(outs[0].size == QSize(3840, 2160));
    CHECK(outs[1].size == QSize(3840, 2160)); // fitted to the take

    // Native leaves each track at whatever its source gave, which is the right answer when
    // the tracks are going to be composited at different sizes anyway.
    plan.fit = CaptureFit::Native;
    outs = plan.outputs();
    CHECK(outs[0].size == QSize(3840, 2160));
    CHECK(outs[1].size == QSize(1280, 720));
}

TEST_CASE("A plan says why it cannot be recorded", "[capture]")
{
    CapturePlan plan;
    CHECK_FALSE(plan.validate().isEmpty()); // nothing chosen

    plan.sources = {screen(QStringLiteral("Monitor"), 1920, 1080)};
    CHECK(plan.validate().isEmpty());

    // An odd size fails every codec worth writing to; catching it before recording beats
    // an encoder giving up halfway through a take.
    plan.forcedSize = QSize(1921, 1080);
    CHECK_FALSE(plan.validate().isEmpty());
    plan.forcedSize = QSize(1920, 1080);
    CHECK(plan.validate().isEmpty());

    // A source that reported nothing useful is named, rather than failing anonymously.
    CaptureSource broken = mic(QStringLiteral("Ghost mic"), 0, 2, 16);
    plan.sources.push_back(broken);
    const QString why = plan.validate();
    CHECK_FALSE(why.isEmpty());
    CHECK(why.contains(QStringLiteral("Ghost mic")));
}

TEST_CASE("Device listings from a current ffmpeg are read", "[capture]")
{
    // Captured verbatim from ffmpeg 8.1 on Windows. Two details make this worth pinning:
    // the prefix is `[in#0 @ ...]`, not the `[dshow @ ...]` the documented format
    // suggests — matching only `[dshow` finds nothing at all on a current build — and a
    // virtual-camera driver interleaves its own logging, which must not be taken for
    // devices.
    const QString listing = QStringLiteral(
        "[in#0 @ 000001dcd1789e00] \"screen-capture-recorder\" (video)\n"
        "[in#0 @ 000001dcd1789e00]   Alternative name \"@device_sw_{860BB310-5D01}\"\n"
        "I2026-08-25 05:26:28.452917 (37216) [INFO] [VCAMDS] ffmpeg.exe\n"
        "I2026-08-25 05:26:28.453919 (37216) [INFO] [VCAMDS] Creating WndMsg Listener Window\n"
        "[in#0 @ 000001dcd1789e00] \"Camera (NVIDIA Broadcast)\" (video)\n"
        "[in#0 @ 000001dcd1789e00]   Alternative name \"@device_sw_{7BBFF097}\"\n"
        "[in#0 @ 000001dcd1789e00] \"Microphone (USBAudio1.0)\" (audio)\n"
        "[in#0 @ 000001dcd1789e00]   Alternative name \"@device_cm_{33D9A762}\"\n"
        "[in#0 @ 000001dcd1789e00] \"Микрофон "
        "(Realtek(R) Audio)\" (audio)\n"
        "[in#0 @ 000001dcd1789e00]   Alternative name \"@device_cm_{4C0BA29F}\"\n"
        "Error opening input file dummy.\n");

    const QVector<CaptureSource> found = CaptureSources::parseDshowListing(listing);
    REQUIRE(found.size() == 4);

    CHECK(found[0].kind == CaptureSource::Kind::Camera);
    CHECK(found[0].name == QStringLiteral("screen-capture-recorder"));
    CHECK(found[1].name == QStringLiteral("Camera (NVIDIA Broadcast)"));
    CHECK(found[2].kind == CaptureSource::Kind::Audio);
    CHECK(found[2].name == QStringLiteral("Microphone (USBAudio1.0)"));
    // A non-Latin device name survives, which a byte-wise reading would not manage.
    CHECK(found[3].kind == CaptureSource::Kind::Audio);
    CHECK(found[3].name.contains(QStringLiteral("Realtek")));

    // The alternative-name lines are not devices of their own.
    for (const CaptureSource &s : found) {
        CHECK_FALSE(s.name.startsWith(QStringLiteral("@device")));
    }

    // An audio device gets a format to start from, because ffmpeg's listing does not say
    // what it supports and a source with no numbers cannot be planned around.
    CHECK(found[2].sampleRate > 0);
    CHECK(found[2].channels > 0);
}

TEST_CASE("Older ffmpeg listings, which group by heading, still read", "[capture]")
{
    // Versions before the per-line suffix printed a heading instead. Both forms are
    // accepted rather than picking one and breaking on whichever ffmpeg the user has.
    const QString listing = QStringLiteral(
        "[dshow @ 0000021a] DirectShow video devices (some may be both video and audio)\n"
        "[dshow @ 0000021a]  \"Integrated Camera\"\n"
        "[dshow @ 0000021a]     Alternative name \"@device_pnp_x\"\n"
        "[dshow @ 0000021a] DirectShow audio devices\n"
        "[dshow @ 0000021a]  \"Microphone Array\"\n"
        "[dshow @ 0000021a]     Alternative name \"@device_cm_y\"\n");

    const QVector<CaptureSource> found = CaptureSources::parseDshowListing(listing);
    REQUIRE(found.size() == 2);
    CHECK(found[0].kind == CaptureSource::Kind::Camera);
    CHECK(found[0].name == QStringLiteral("Integrated Camera"));
    CHECK(found[1].kind == CaptureSource::Kind::Audio);
    CHECK(found[1].name == QStringLiteral("Microphone Array"));
}

TEST_CASE("Nothing at all is not an error", "[capture]")
{
    // A machine with no capture devices, or no ffmpeg, still records its screen — so an
    // empty listing has to come back empty rather than throw the picker off.
    CHECK(CaptureSources::parseDshowListing(QString()).isEmpty());
    CHECK(CaptureSources::parseDshowListing(QStringLiteral("Error opening input file dummy.\n"))
              .isEmpty());
}

namespace {

/** Index of `flag` in the argument list, or -1. */
int indexOf(const QStringList &args, const QString &flag)
{
    return int(args.indexOf(flag));
}

/** The value that follows `flag`, or an empty string. */
QString valueAfter(const QStringList &args, const QString &flag)
{
    const int at = indexOf(args, flag);
    return (at >= 0 && at + 1 < args.size()) ? args[at + 1] : QString();
}

} // namespace

TEST_CASE("A screen is recorded through the platform's screen grabber", "[capture]")
{
    CapturePlan plan;
    plan.sources = {screen(QStringLiteral("Display 1"), 1920, 1080, 60.0)};
    const CaptureOutput out = plan.outputs().first();

    const QStringList args =
        CaptureRecorder::argumentsFor(plan, out, QStringLiteral("C:/takes/a.mkv"));
    REQUIRE_FALSE(args.isEmpty());

    // Overwriting matters: a take is re-recorded until it is right, and stopping to
    // confirm each time would interrupt the thing being captured.
    CHECK(args.contains(QStringLiteral("-y")));
    CHECK(valueAfter(args, QStringLiteral("-framerate")).toDouble() == Catch::Approx(60.0));
    // The output path is last, which is where ffmpeg expects it.
    CHECK(args.last().contains(QStringLiteral("a.mkv")));
    // Recording has to keep up in real time, so the take is written fast rather than small.
    CHECK(valueAfter(args, QStringLiteral("-preset")) == QStringLiteral("ultrafast"));
}

TEST_CASE("A camera is opened by the name the listing gave", "[capture]")
{
    CapturePlan plan;
    CaptureSource cam = camera(QStringLiteral("Camera (NVIDIA Broadcast)"), 1920, 1080, 30.0);
    plan.sources = {cam};
    const CaptureOutput out = plan.outputs().first();
    const QStringList args =
        CaptureRecorder::argumentsFor(plan, out, QStringLiteral("C:/takes/cam.mkv"));

    // The name has to reach ffmpeg exactly as its own listing spelled it, brackets and
    // all — this is why enumeration asks ffmpeg rather than the OS.
    const QString input = valueAfter(args, QStringLiteral("-i"));
    CHECK(input.contains(QStringLiteral("Camera (NVIDIA Broadcast)")));
}

TEST_CASE("A source that is not the reference is scaled by ffmpeg, not by the device",
          "[capture]")
{
    CapturePlan plan;
    plan.sources = {screen(QStringLiteral("Monitor"), 3840, 2160, 60.0),
                    camera(QStringLiteral("Webcam"), 1280, 720, 30.0)};
    plan.fit = CaptureFit::Letterbox;

    const QVector<CaptureOutput> outs = plan.outputs();
    const QStringList webcam =
        CaptureRecorder::argumentsFor(plan, outs[1], QStringLiteral("C:/takes/w.mkv"));

    // Asking a capture device for a size it does not have makes it fail to open, while
    // ffmpeg will scale anything — so the fitting is a filter, and the device is still
    // opened at its own size.
    const QString vf = valueAfter(webcam, QStringLiteral("-vf"));
    CHECK(vf.contains(QStringLiteral("3840:2160")));
    CHECK(vf.contains(QStringLiteral("pad")));
    CHECK(valueAfter(webcam, QStringLiteral("-video_size")) == QStringLiteral("1280x720"));

    // Crop fills instead of padding.
    plan.fit = CaptureFit::Crop;
    const QString cropVf = valueAfter(
        CaptureRecorder::argumentsFor(plan, plan.outputs()[1], QStringLiteral("C:/t/w.mkv")),
        QStringLiteral("-vf"));
    CHECK(cropVf.contains(QStringLiteral("crop")));

    // The reference source is already the take's size, so it gets no filter at all.
    const QStringList monitor =
        CaptureRecorder::argumentsFor(plan, outs[0], QStringLiteral("C:/takes/m.mkv"));
    CHECK(indexOf(monitor, QStringLiteral("-vf")) < 0);
}

TEST_CASE("Native fit leaves every source at its own size", "[capture]")
{
    CapturePlan plan;
    plan.sources = {screen(QStringLiteral("Monitor"), 3840, 2160, 60.0),
                    camera(QStringLiteral("Webcam"), 1280, 720, 30.0)};
    plan.fit = CaptureFit::Native;

    for (const CaptureOutput &o : plan.outputs()) {
        const QStringList args =
            CaptureRecorder::argumentsFor(plan, o, QStringLiteral("C:/takes/x.mkv"));
        INFO(o.source.name.toStdString());
        // Nothing to scale: the output already carries the source's own size, so this is
        // a no-op rather than a special case in the builder.
        CHECK(indexOf(args, QStringLiteral("-vf")) < 0);
    }
}

TEST_CASE("Audio files all get the take's format", "[capture]")
{
    CapturePlan plan;
    plan.sources = {mic(QStringLiteral("Headset"), 44100, 1, 16),
                    mic(QStringLiteral("Interface"), 96000, 2, 24)};

    for (const CaptureOutput &o : plan.outputs()) {
        const QStringList args =
            CaptureRecorder::argumentsFor(plan, o, QStringLiteral("C:/takes/a.wav"));
        INFO(o.source.name.toStdString());
        // Both are written at the best format offered, so they sit together in a project
        // without anything being resampled afterwards.
        CHECK(valueAfter(args, QStringLiteral("-ar")) == QStringLiteral("96000"));
        CHECK(valueAfter(args, QStringLiteral("-ac")) == QStringLiteral("2"));
        // 24-bit was on offer, so the take keeps it rather than rounding everyone to 16.
        CHECK(valueAfter(args, QStringLiteral("-c:a")) == QStringLiteral("pcm_s24le"));
    }

    // With nothing better than 16-bit around, there is no reason to inflate the files.
    CapturePlan plain;
    plain.sources = {mic(QStringLiteral("Headset"), 48000, 2, 16)};
    const QStringList args = CaptureRecorder::argumentsFor(
        plain, plain.outputs().first(), QStringLiteral("C:/takes/a.wav"));
    CHECK(valueAfter(args, QStringLiteral("-c:a")) == QStringLiteral("pcm_s16le"));
}

TEST_CASE("A second monitor is recorded as a region of the desktop, not the whole of it",
          "[capture]")
{
    CapturePlan plan;
    CaptureSource second = screen(QStringLiteral("Display 2"), 1920, 1080, 60.0);
    second.origin = QPoint(1920, 0);
    plan.sources = {second};

    const QStringList args = CaptureRecorder::argumentsFor(
        plan, plan.outputs().first(), QStringLiteral("C:/takes/s.mkv"));

    // Screen grabbers open the whole desktop; only the offset and size say which monitor
    // is meant. Without them a two-monitor desktop records both, at double the width.
    CHECK(valueAfter(args, QStringLiteral("-video_size")) == QStringLiteral("1920x1080"));
#ifdef Q_OS_WIN
    CHECK(valueAfter(args, QStringLiteral("-offset_x")) == QStringLiteral("1920"));
    CHECK(valueAfter(args, QStringLiteral("-offset_y")) == QStringLiteral("0"));
#else
    CHECK(valueAfter(args, QStringLiteral("-i")) == QStringLiteral(":0.0+1920,0"));
#endif

    // The primary monitor starts at the origin, so it needs no offset — and the flag is
    // left out rather than passed as zero, which is what an unpatched ffmpeg expects.
    CapturePlan primary;
    primary.sources = {screen(QStringLiteral("Display 1"), 2560, 1440, 60.0)};
    const QStringList first = CaptureRecorder::argumentsFor(
        primary, primary.outputs().first(), QStringLiteral("C:/takes/p.mkv"));
    CHECK(indexOf(first, QStringLiteral("-offset_x")) < 0);
    CHECK(valueAfter(first, QStringLiteral("-video_size")) == QStringLiteral("2560x1440"));
}
