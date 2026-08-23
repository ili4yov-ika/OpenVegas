#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/AudioDecodeCache.h"
#include "io/SamplePaths.h"
#include "video/NestedFrameHook.h"
#include "video/VideoFrameCache.h"
#include "video/NestedProjectSource.h"

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QThread>
#include <QFile>

#include <cmath>

using namespace openvegas;

// A VEGAS project can be dropped on a timeline as a clip. The "media" is then a .veg,
// which no decoder can open — so the track played silent while its waveform drew fine
// from the .sfk beside it. VEGAS mixes such a project down into "<name>.veg.sfap0" and
// plays that; this covers reading it. Format notes: MARKDOWN/VEG_SFAP0_FORMAT.md.

TEST_CASE("A nested VEGAS project plays from its .sfap0 mixdown",
          "[audio][nested-project]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    const QString veg = QDir(root).filePath(QStringLiteral("project_big--buck-bunny.veg"));
    if (!QFile::exists(veg) || !QFile::exists(veg + QStringLiteral(".sfap0"))) {
        SKIP("nested project sample or its .sfap0 sidecar missing");
    }

    REQUIRE(AudioDecodeCache::sfap0Beside(veg) == veg + QStringLiteral(".sfap0"));
    // Only a .veg claims the sidecar; an ordinary media file must not.
    CHECK(AudioDecodeCache::sfap0Beside(QDir(root).filePath(QStringLiteral("x.mp4"))).isEmpty());

    auto buf = AudioDecodeCache::instance().get(veg, 48000);
    REQUIRE(buf);
    REQUIRE(buf->ready);
    CHECK(buf->sampleRate == 48000);
    CHECK(buf->channels == 2);

    // Header says 634.57 s; a wrong chunk walk lands on a wildly different length, which
    // is exactly how the first attempt failed.
    const double seconds = double(buf->frameCount()) / buf->sampleRate;
    INFO("decoded " << seconds << " s");
    CHECK(seconds > 600.0);
    CHECK(seconds < 700.0);

    // And it must be signal, not a buffer of zeros parsed from the wrong offset.
    double peak = 0.0;
    int probesWithSignal = 0;
    const qint64 frames = buf->frameCount();
    for (int probe = 0; probe < 8; ++probe) {
        const qint64 start = frames * probe / 8;
        double sum = 0.0;
        int n = 0;
        for (qint64 i = start; i < std::min<qint64>(start + 48000, frames); ++i) {
            for (int c = 0; c < buf->channels; ++c) {
                const double v = std::fabs(double(buf->samples[int(i * buf->channels + c)]));
                peak = std::max(peak, v);
                sum += v;
                ++n;
            }
        }
        if (n > 0 && sum / n > 1e-5) {
            ++probesWithSignal;
        }
    }
    INFO("peak " << peak << ", probes with signal " << probesWithSignal);
    CHECK(peak > 0.05);
    CHECK(probesWithSignal >= 6);
}

// --- picture ------------------------------------------------------------------------
// Audio for a nested project comes from a sidecar VEGAS wrote; picture has no such
// sidecar, so the frames are composed from the nested project itself.

TEST_CASE("A nested VEGAS project composes picture", "[video][nested-project]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    const QString veg = QDir(root).filePath(QStringLiteral("project_big--buck-bunny.veg"));
    if (!QFile::exists(veg)) {
        SKIP("nested project sample missing");
    }

    REQUIRE(NestedProjectSource::isProjectMedia(veg));
    // A media file must not be mistaken for a project.
    CHECK_FALSE(NestedProjectSource::isProjectMedia(
        QDir(root).filePath(QStringLiteral("big-buck-bunny_video-60fps-4k.mp4"))));

    // The nested timeline's own length, not the outer project's.
    const double duration = NestedProjectSource::instance().durationOf(veg);
    INFO("nested duration " << duration);
    CHECK(duration > 60.0);

    const QSize size(160, 90);
    auto composeAt = [&](double t) {
        QImage img;
        // Composing pulls the nested project's media through the async decode cache, so
        // the first ask can legitimately come back empty.
        for (int attempt = 0; attempt < 40 && img.isNull(); ++attempt) {
            img = NestedProjectSource::instance().frameAt(veg, t, size);
            if (img.isNull()) {
                QThread::msleep(250);
                QCoreApplication::processEvents();
            }
        }
        return img;
    };

    const QImage early = composeAt(20.0);
    const QImage later = composeAt(240.0);
    REQUIRE_FALSE(early.isNull());
    REQUIRE_FALSE(later.isNull());
    CHECK(early.size() == size);

    // Not a blank canvas, and not the same frame twice — either would pass a mere
    // "returned an image" check while showing nothing useful.
    auto meanLuma = [](const QImage &img) {
        double sum = 0.0;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                sum += qGray(img.pixel(x, y));
            }
        }
        return sum / (img.width() * img.height());
    };
    INFO("luma " << meanLuma(early) << " vs " << meanLuma(later));
    CHECK(meanLuma(early) > 4.0);
    CHECK(std::fabs(meanLuma(early) - meanLuma(later)) > 1.0);
}

TEST_CASE("The frame caches reach a nested project only once the provider is installed",
          "[video][nested-project]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    const QString veg = QDir(root).filePath(QStringLiteral("project_big--buck-bunny.veg"));
    if (!QFile::exists(veg)) {
        SKIP("nested project sample missing");
    }

    // The seam exists so the filmstrip and preview caches need not know how a project is
    // loaded. Forgetting to install the provider would leave those clips silently black,
    // with nothing failing anywhere, so the wiring is worth pinning down.
    setNestedFrameProvider(nullptr);
    CHECK_FALSE(hasNestedFrameProvider());
    CHECK(nestedFrame(veg, 20.0, QSize(64, 36)).isNull());

    // The cheap classification works with or without a provider.
    CHECK(looksLikeProjectMedia(veg));
    CHECK_FALSE(looksLikeProjectMedia(QStringLiteral("clip.mp4")));

    NestedProjectSource::installAsFrameProvider();
    REQUIRE(hasNestedFrameProvider());

    QImage img;
    for (int attempt = 0; attempt < 40 && img.isNull(); ++attempt) {
        img = nestedFrame(veg, 20.0, QSize(64, 36));
        if (img.isNull()) {
            QThread::msleep(250);
            QCoreApplication::processEvents();
        }
    }
    CHECK_FALSE(img.isNull());
}

TEST_CASE("The preview cache itself returns a nested project's frame",
          "[video][nested-project]")
{
    const QString root = SamplePaths::vegProjectDir();
    if (root.isEmpty()) {
        SKIP("SAMPLES/veg_project not available");
    }
    const QString veg = QDir(root).filePath(QStringLiteral("project_big--buck-bunny.veg"));
    if (!QFile::exists(veg)) {
        SKIP("nested project sample missing");
    }
    NestedProjectSource::installAsFrameProvider();

    // The last link: the cache the program monitor actually asks. Everything below it is
    // covered above, but the branch that routes a .veg away from ffmpeg lives here.
    const QSize size(160, 90);
    QImage img;
    for (int attempt = 0; attempt < 60 && img.isNull(); ++attempt) {
        img = VideoFrameCache::instance().frameIfReady(veg, 20.0, size);
        if (img.isNull()) {
            QThread::msleep(250);
            QCoreApplication::processEvents();
        }
    }
    REQUIRE_FALSE(img.isNull());
    CHECK(img.size() == size);
}
