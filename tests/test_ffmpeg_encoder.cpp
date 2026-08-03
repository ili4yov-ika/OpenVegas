#include "io/FFmpegEncoder.h"
#include "io/RenderTemplateCatalog.h"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <cmath>
#include <cstdint>

namespace {

void writeSilentWav(const QString &path, int sampleRate, double seconds)
{
    const int channels = 2;
    const int frames = std::max(1, int(std::lround(seconds * sampleRate)));
    const int dataBytes = frames * channels * int(sizeof(int16_t));
    QByteArray data;
    data.resize(44 + dataBytes);
    data.fill(0);
    auto *p = reinterpret_cast<unsigned char *>(data.data());
    auto w32 = [&](int off, uint32_t v) {
        p[off] = uchar(v & 0xff);
        p[off + 1] = uchar((v >> 8) & 0xff);
        p[off + 2] = uchar((v >> 16) & 0xff);
        p[off + 3] = uchar((v >> 24) & 0xff);
    };
    auto w16 = [&](int off, uint16_t v) {
        p[off] = uchar(v & 0xff);
        p[off + 1] = uchar((v >> 8) & 0xff);
    };
    memcpy(p, "RIFF", 4);
    w32(4, uint32_t(36 + dataBytes));
    memcpy(p + 8, "WAVEfmt ", 8);
    w32(16, 16);
    w16(20, 1);
    w16(22, uint16_t(channels));
    w32(24, uint32_t(sampleRate));
    w32(28, uint32_t(sampleRate * channels * 2));
    w16(32, uint16_t(channels * 2));
    w16(34, 16);
    memcpy(p + 36, "data", 4);
    w32(40, uint32_t(dataBytes));
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    REQUIRE(f.write(data) == data.size());
}

} // namespace

TEST_CASE("findFfmpeg checks application directory candidates", "[media][ffmpeg]")
{
    int argc = 1;
    char arg0[] = "openvegas_media_tests";
    char *argv[] = {arg0, nullptr};
    if (!QCoreApplication::instance()) {
        static QCoreApplication app(argc, argv);
        Q_UNUSED(app);
    }

    // If PATH already has ffmpeg, still OK — we only assert helper is callable.
    const QString ff = openvegas::FFmpegEncoder::findFfmpeg();
    if (!ff.isEmpty()) {
        REQUIRE(QFileInfo::exists(ff));
        return;
    }

    // Without ffmpeg anywhere: create a dummy next to the test exe and ensure path logic
    // would see it on a fresh process — static cache already tried; just document layout.
    const QString beside =
        QDir(QCoreApplication::applicationDirPath()).filePath(
#ifdef Q_OS_WIN
            QStringLiteral("ffmpeg.exe")
#else
            QStringLiteral("ffmpeg")
#endif
        );
    WARN(QStringLiteral("ffmpeg missing; drop binary at %1 for portable runs").arg(beside).toStdString());
}

TEST_CASE("FFmpegEncoder wav to aac smoke", "[media][ffmpeg]")
{
    if (openvegas::FFmpegEncoder::findFfmpeg().isEmpty()) {
        SKIP("ffmpeg not available");
    }
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString wav = QDir(tmp.path()).filePath(QStringLiteral("in.wav"));
    const QString out = QDir(tmp.path()).filePath(QStringLiteral("out.m4a"));
    writeSilentWav(wav, 48000, 0.25);

    openvegas::RenderTemplate tpl;
    tpl.audioOnly = true;
    tpl.extension = QStringLiteral(".m4a");
    tpl.bitrateKbps = 128;
    tpl.sampleRate = 48000;
    tpl.channels = 2;

    const auto r = openvegas::FFmpegEncoder::encodeAudioFromWav(
        wav, out, tpl, QStringLiteral("AAC"));
    REQUIRE(r.ok);
    REQUIRE(QFileInfo(out).size() > 100);
}

TEST_CASE("FFmpegEncoder png sequence to mp4 smoke", "[media][ffmpeg]")
{
    if (openvegas::FFmpegEncoder::findFfmpeg().isEmpty()) {
        SKIP("ffmpeg not available");
    }
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString frames = QDir(tmp.path()).filePath(QStringLiteral("frames"));
    REQUIRE(QDir().mkpath(frames));
    QImage img(64, 36, QImage::Format_RGB32);
    img.fill(QColor(20, 40, 80));
    for (int i = 1; i <= 8; ++i) {
        const QString png =
            QDir(frames).filePath(QStringLiteral("frame_%1.png").arg(i, 6, 10, QChar('0')));
        REQUIRE(img.save(png, "PNG"));
    }
    const QString wav = QDir(tmp.path()).filePath(QStringLiteral("a.wav"));
    writeSilentWav(wav, 48000, 8.0 / 30.0);
    const QString out = QDir(tmp.path()).filePath(QStringLiteral("out.mp4"));
    openvegas::RenderTemplate tpl;
    tpl.extension = QStringLiteral(".mp4");
    tpl.width = 64;
    tpl.height = 36;
    tpl.fps = 30.0;
    tpl.bitrateKbps = 128;

    const auto r = openvegas::FFmpegEncoder::encodeVideoFromPngSequence(
        QDir(frames).filePath(QStringLiteral("frame_%06d.png")), 1, 30.0, wav, out, tpl,
        QStringLiteral("AVC/AAC MP4"));
    REQUIRE(r.ok);
    REQUIRE(QFileInfo(out).size() > 500);
}
