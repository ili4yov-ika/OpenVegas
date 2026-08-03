#include "io/FFmpegEncoder.h"
#include "io/FFmpegStreamDecoder.h"
#include "io/MediaFilmstripCache.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QProcess>

namespace openvegas {
namespace {

int audioBitrateKbps(const RenderTemplate &tpl)
{
    if (tpl.bitrateKbps > 0) {
        return tpl.bitrateKbps;
    }
    return 192;
}

QStringList audioCodecArgs(const QString &formatName, const RenderTemplate &tpl, const QString &outPath)
{
    const QString fmt = formatName.toLower();
    const QString ext = QFileInfo(outPath).suffix().toLower();
    QStringList a;
    if (fmt.contains(QStringLiteral("flac")) || ext == QLatin1String("flac")) {
        a << QStringLiteral("-c:a") << QStringLiteral("flac");
        return a;
    }
    if (fmt.contains(QStringLiteral("mp3")) || ext == QLatin1String("mp3")) {
        a << QStringLiteral("-c:a") << QStringLiteral("libmp3lame") << QStringLiteral("-b:a")
          << QStringLiteral("%1k").arg(audioBitrateKbps(tpl));
        return a;
    }
    a << QStringLiteral("-c:a") << QStringLiteral("aac") << QStringLiteral("-b:a")
      << QStringLiteral("%1k").arg(audioBitrateKbps(tpl));
    return a;
}

} // namespace

QString FFmpegEncoder::findFfmpeg()
{
    return MediaFilmstripCache::findFfmpeg();
}

FFmpegEncodeResult FFmpegEncoder::run(const QStringList &args, const FFmpegCancelFn &shouldContinue)
{
    FFmpegEncodeResult r;
    r.ffmpegPath = findFfmpeg();
    if (r.ffmpegPath.isEmpty()) {
        r.error = QStringLiteral("ffmpeg not found (install FFmpeg and add it to PATH)");
        return r;
    }
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(r.ffmpegPath, args);
    if (!proc.waitForStarted(10000)) {
        r.error = QStringLiteral("failed to start ffmpeg");
        return r;
    }

    // Poll so UI can cancel; max ~2h.
    constexpr int kMaxMs = 2 * 60 * 60 * 1000;
    int waited = 0;
    while (!proc.waitForFinished(200)) {
        waited += 200;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (shouldContinue && !shouldContinue()) {
            proc.kill();
            proc.waitForFinished(3000);
            r.canceled = true;
            r.error = QStringLiteral("canceled");
            return r;
        }
        if (waited >= kMaxMs) {
            proc.kill();
            r.error = QStringLiteral("ffmpeg timed out");
            return r;
        }
    }

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        if (shouldContinue && !shouldContinue()) {
            r.canceled = true;
            r.error = QStringLiteral("canceled");
            return r;
        }
        const QString out = QString::fromUtf8(proc.readAll()).right(800);
        r.error = QStringLiteral("ffmpeg failed (exit %1): %2").arg(proc.exitCode()).arg(out);
        return r;
    }
    r.ok = true;
    return r;
}

FFmpegEncodeResult FFmpegEncoder::encodeAudioFromWav(const QString &wavPath, const QString &outputPath,
                                                     const RenderTemplate &tpl,
                                                     const QString &formatName,
                                                     const FFmpegCancelFn &shouldContinue)
{
    QStringList args;
    args << QStringLiteral("-y") << QStringLiteral("-hide_banner") << QStringLiteral("-loglevel")
         << QStringLiteral("error") << QStringLiteral("-i") << wavPath;
    args << audioCodecArgs(formatName, tpl, outputPath);
    if (tpl.sampleRate > 0) {
        args << QStringLiteral("-ar") << QString::number(tpl.sampleRate);
    }
    if (tpl.channels > 0) {
        args << QStringLiteral("-ac") << QString::number(tpl.channels);
    }
    args << outputPath;
    return run(args, shouldContinue);
}

FFmpegEncodeResult FFmpegEncoder::encodeVideoFromPngSequence(const QString &framePattern,
                                                             int startNumber, double fps,
                                                             const QString &wavPath,
                                                             const QString &outputPath,
                                                             const RenderTemplate &tpl,
                                                             const QString &formatName,
                                                             const FFmpegCancelFn &shouldContinue)
{
    Q_UNUSED(formatName);
    const double useFps = fps > 0.1 ? fps : 30.0;

    auto buildArgs = [&](const QStringList &codecArgs) {
        QStringList args;
        args << QStringLiteral("-y") << QStringLiteral("-hide_banner") << QStringLiteral("-loglevel")
             << QStringLiteral("error") << QStringLiteral("-framerate")
             << QString::number(useFps, 'f', 3) << QStringLiteral("-start_number")
             << QString::number(startNumber) << QStringLiteral("-i") << framePattern;
        if (!wavPath.isEmpty() && QFileInfo::exists(wavPath)) {
            args << QStringLiteral("-i") << wavPath;
        }
        args << codecArgs;
        if (!codecArgs.contains(QStringLiteral("yuv420p"))) {
            args << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
        }
        if (tpl.width > 0 && tpl.height > 0) {
            const int w = tpl.width & ~1;
            const int h = tpl.height & ~1;
            if (w >= 2 && h >= 2) {
                args << QStringLiteral("-vf") << QStringLiteral("scale=%1:%2").arg(w).arg(h);
            }
        }
        if (!wavPath.isEmpty() && QFileInfo::exists(wavPath)) {
            args << QStringLiteral("-c:a") << QStringLiteral("aac") << QStringLiteral("-b:a")
                 << QStringLiteral("%1k").arg(audioBitrateKbps(tpl)) << QStringLiteral("-shortest");
        } else {
            args << QStringLiteral("-an");
        }
        args << outputPath;
        return args;
    };

    const QStringList preferred = FFmpegStreamDecoder::videoEncodeCodecArgs(23);
    FFmpegEncodeResult r = run(buildArgs(preferred), shouldContinue);
    if (r.canceled) {
        return r;
    }
    const bool hw =
        preferred.contains(QStringLiteral("h264_nvenc"))
        || preferred.contains(QStringLiteral("h264_qsv"))
        || preferred.contains(QStringLiteral("h264_amf"));
    if (!r.ok && hw) {
        const QStringList soft = {QStringLiteral("-c:v"),     QStringLiteral("libx264"),
                                  QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                                  QStringLiteral("-crf"),     QStringLiteral("23")};
        r = run(buildArgs(soft), shouldContinue);
        if (r.ok) {
            r.error.clear();
        }
    }
    return r;
}

} // namespace openvegas
