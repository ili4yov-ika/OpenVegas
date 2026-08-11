#include <catch2/catch_test_macros.hpp>

#include "io/FFmpegStreamDecoder.h"
#include "io/MediaFilmstripCache.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QTemporaryDir>
#include <QVector>

using namespace openvegas;

namespace {

QString makeTinyMp4(QTemporaryDir &dir)
{
    const QString ffmpeg = MediaFilmstripCache::findFfmpeg();
    if (ffmpeg.isEmpty()) {
        return {};
    }
    const QString out = dir.filePath(QStringLiteral("tiny.mp4"));
    QProcess proc;
    proc.start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-hide_banner"),
                        QStringLiteral("-loglevel"), QStringLiteral("error"),
                        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                        QStringLiteral("color=c=red:s=64x36:d=0.5"), QStringLiteral("-c:v"),
                        QStringLiteral("libx264"), QStringLiteral("-pix_fmt"),
                        QStringLiteral("yuv420p"), QStringLiteral("-t"), QStringLiteral("0.5"),
                        out});
    if (!proc.waitForFinished(30000) || proc.exitStatus() != QProcess::NormalExit
        || proc.exitCode() != 0 || !QFileInfo::exists(out)) {
        return {};
    }
    return out;
}

} // namespace

TEST_CASE("videoEncodeCodecArgs always returns a video codec", "[ffmpeg][hw]")
{
    int argc = 0;
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, nullptr);

    const QStringList args = FFmpegStreamDecoder::videoEncodeCodecArgs(23);
    REQUIRE(args.contains(QStringLiteral("-c:v")));
    REQUIRE((args.contains(QStringLiteral("libx264")) || args.contains(QStringLiteral("h264_nvenc"))
             || args.contains(QStringLiteral("h264_qsv"))
             || args.contains(QStringLiteral("h264_amf"))));
}

TEST_CASE("decodeSequence continuous raw pipe fills frames", "[ffmpeg][decode]")
{
    int argc = 0;
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, nullptr);

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString mp4 = makeTinyMp4(dir);
    if (mp4.isEmpty()) {
        WARN("ffmpeg unavailable — skip continuous decode smoke");
        return;
    }

    QVector<QImage> frames;
    const int n =
        FFmpegStreamDecoder::decodeSequence(mp4, 0.0, 30.0, 8, QSize(64, 36), &frames);
    REQUIRE(n >= 1);
    REQUIRE(frames.size() == 8);
    REQUIRE_FALSE(frames[0].isNull());
    REQUIRE(frames[0].width() == 64);
    REQUIRE(frames[0].height() == 36);
}

TEST_CASE("linkedAvailable matches build flag", "[ffmpeg][decode]")
{
#ifdef OPENVGAS_FFMPEG
    REQUIRE(FFmpegStreamDecoder::linkedAvailable());
#else
    REQUIRE_FALSE(FFmpegStreamDecoder::linkedAvailable());
#endif
}

// The regression this whole probe exists for: `ffmpeg -encoders` is a static list
// baked into the binary at *its* build time, so a full build (Gyan, BtbN) lists
// h264_nvenc, h264_qsv and h264_amf on every machine — including ones with no such
// GPU. Selecting on that list alone means a render that dies at encoder init.
// Measured on the dev box (Ryzen 7700 + RTX 4070, no Intel part): h264_qsv is
// listed, and encoding three frames with it exits 171 with "Error creating a MFX
// session: -9".
TEST_CASE("Only an encoder that survives a trial run counts as usable",
          "[ffmpeg][hw][encoder-probe]")
{
    int argc = 0;
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, nullptr);

    if (MediaFilmstripCache::findFfmpeg().isEmpty()) {
        SKIP("ffmpeg unavailable");
    }

    int listed = 0;
    int usable = 0;
    for (const QString &name : FFmpegStreamDecoder::knownHwEncoders()) {
        INFO("encoder: " << name.toStdString());
        const bool isListed = FFmpegStreamDecoder::encoderAvailable(name);
        const bool isUsable = FFmpegStreamDecoder::encoderUsable(name);
        listed += isListed ? 1 : 0;
        usable += isUsable ? 1 : 0;
        // Usable must imply listed — never the other way round.
        if (isUsable) {
            CHECK(isListed);
        }
    }
    // Nothing can be usable that was not listed, whatever this machine has.
    CHECK(usable <= listed);
}

TEST_CASE("videoEncodeCodecArgs only names an encoder that actually runs here",
          "[ffmpeg][hw][encoder-probe]")
{
    int argc = 0;
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, nullptr);

    if (MediaFilmstripCache::findFfmpeg().isEmpty()) {
        SKIP("ffmpeg unavailable");
    }

    const QStringList args = FFmpegStreamDecoder::videoEncodeCodecArgs(23);
    const int i = args.indexOf(QStringLiteral("-c:v"));
    REQUIRE(i >= 0);
    REQUIRE(i + 1 < args.size());
    const QString codec = args[i + 1];
    if (codec != QStringLiteral("libx264")) {
        CHECK(FFmpegStreamDecoder::encoderUsable(codec));
    }
}

TEST_CASE("An explicit hardware encoder that cannot run falls back to software",
          "[ffmpeg][hw][encoder-probe]")
{
    int argc = 0;
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, nullptr);

    if (MediaFilmstripCache::findFfmpeg().isEmpty()) {
        SKIP("ffmpeg unavailable");
    }

    // Picking a codec by name must not bypass the capability check — otherwise a
    // user who selects QSV on an AMD box gets a broken render rather than libx264.
    qputenv("OPENVGAS_HWENCODER", "nosuchvendor");
    CHECK(FFmpegStreamDecoder::selectedHwEncoder().isEmpty());
    CHECK(FFmpegStreamDecoder::videoEncodeCodecArgs(23).contains(QStringLiteral("libx264")));

    qputenv("OPENVGAS_HWENCODER", "cpu");
    CHECK(FFmpegStreamDecoder::selectedHwEncoder().isEmpty());
    CHECK(FFmpegStreamDecoder::videoEncodeCodecArgs(23).contains(QStringLiteral("libx264")));

    qunsetenv("OPENVGAS_HWENCODER");
}

TEST_CASE("hwDecodeMethod rejects a method this ffmpeg does not offer",
          "[ffmpeg][hw][decode]")
{
    int argc = 0;
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, nullptr);

    if (MediaFilmstripCache::findFfmpeg().isEmpty()) {
        SKIP("ffmpeg unavailable");
    }

    // A stale setting must degrade to "auto", not make every decode call fail.
    qputenv("OPENVGAS_HWDECODER", "totally_not_a_hwaccel");
    CHECK(FFmpegStreamDecoder::hwDecodeMethod() == QStringLiteral("auto"));

    const QStringList methods = FFmpegStreamDecoder::availableHwDecodeMethods();
    if (!methods.isEmpty()) {
        qputenv("OPENVGAS_HWDECODER", methods.first().toLatin1());
        CHECK(FFmpegStreamDecoder::hwDecodeMethod() == methods.first());
    }

    qunsetenv("OPENVGAS_HWDECODER");
    CHECK(FFmpegStreamDecoder::hwDecodeMethod() == QStringLiteral("auto"));
}
