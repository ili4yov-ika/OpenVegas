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
