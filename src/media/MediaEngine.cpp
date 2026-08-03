#include "media/MediaEngine.h"

#include "audio/AudioEngine.h"
#include "io/FFmpegEncoder.h"
#include "model/ProjectModel.h"
#include "video/VideoCompositor.h"
#include "video/VideoFrameCache.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>

#include <cmath>

namespace openvegas {
namespace {

bool isWaveFormat(const QString &formatName)
{
    return formatName.compare(QStringLiteral("Wave (Microsoft)"), Qt::CaseInsensitive) == 0;
}

bool wantsVideo(const RenderFormat &fmt, const RenderTemplate &tpl)
{
    if (fmt.audioOnly || tpl.audioOnly) {
        return false;
    }
    const QString ext = tpl.extension.toLower();
    if (ext == QLatin1String(".wav") || ext == QLatin1String(".mp3")
        || ext == QLatin1String(".flac") || ext == QLatin1String(".m4a")
        || ext == QLatin1String(".aac") || ext == QLatin1String(".aif")
        || ext == QLatin1String(".aiff")) {
        return false;
    }
    return true;
}

QImage composeWithRetries(const ProjectModel &model, double t, const QSize &out, int attempts = 8)
{
    QImage img;
    for (int i = 0; i < attempts; ++i) {
        VideoCompositor::prefetchAround(model, t, out, 0.1, 0.5);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(i == 0 ? 20 : 40);
        img = VideoCompositor::compose(model, t, out, false);
        if (!img.isNull()) {
            return img;
        }
    }
    QImage black(out, QImage::Format_ARGB32_Premultiplied);
    black.fill(Qt::black);
    return black;
}

bool report(const MediaRenderRequest &req, MediaRenderProgress::Stage stage, int current, int total,
            double timeSec = 0.0, const QString &message = {})
{
    if (!req.onProgress) {
        return true;
    }
    MediaRenderProgress p;
    p.stage = stage;
    p.current = current;
    p.total = std::max(1, total);
    p.timeSec = timeSec;
    p.message = message;
    return req.onProgress(p);
}

} // namespace

bool MediaEngine::resolveTemplate(const QString &formatName, const QString &templateName,
                                  RenderFormat *outFormat, RenderTemplate *outTpl)
{
    if (!outFormat || !outTpl) {
        return false;
    }
    const RenderFormat *fmt = RenderTemplateCatalog::findFormat(formatName);
    if (!fmt || fmt->templates.isEmpty()) {
        return false;
    }
    *outFormat = *fmt;
    if (!templateName.isEmpty()) {
        for (const RenderTemplate &t : fmt->templates) {
            if (t.name == templateName) {
                *outTpl = t;
                return true;
            }
        }
    }
    *outTpl = fmt->templates.first();
    return true;
}

MediaRenderResult MediaEngine::renderProject(ProjectModel &model, AudioEngine *audio,
                                             const MediaRenderRequest &req)
{
    MediaRenderResult result;
    if (req.outputPath.isEmpty()) {
        result.error = QStringLiteral("empty output path");
        return result;
    }
    if (!audio) {
        result.error = QStringLiteral("audio engine unavailable");
        return result;
    }

    RenderFormat fmt;
    RenderTemplate tpl;
    if (!resolveTemplate(req.formatName, req.templateName, &fmt, &tpl)) {
        result.error = QStringLiteral("unknown render format/template");
        return result;
    }

    const double start = std::max(0.0, req.startSec);
    const double len = std::max(0.05, req.lengthSec);

    auto canceled = [&]() {
        result.canceled = true;
        result.error = QStringLiteral("canceled");
        return result;
    };

    if (!report(req, MediaRenderProgress::Stage::Audio, 0, 1, start,
                QStringLiteral("Mixing audio…"))) {
        return canceled();
    }

    if (isWaveFormat(req.formatName)) {
        audio->syncGraphFromProject();
        if (!audio->renderToWav(req.outputPath, start, len)) {
            result.error = QStringLiteral("WAV render failed");
            return result;
        }
        if (!report(req, MediaRenderProgress::Stage::Done, 1, 1, start + len)) {
            return canceled();
        }
        result.ok = true;
        result.message = QStringLiteral("Wrote %1").arg(req.outputPath);
        return result;
    }

    if (FFmpegEncoder::findFfmpeg().isEmpty()) {
        result.error = QStringLiteral("ffmpeg not found — install FFmpeg or use Wave (Microsoft)");
        return result;
    }

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        result.error = QStringLiteral("temp directory unavailable");
        return result;
    }
    const QString wavPath = QDir(tmp.path()).filePath(QStringLiteral("mix.wav"));
    audio->syncGraphFromProject();
    if (!audio->renderToWav(wavPath, start, len)) {
        result.error = QStringLiteral("temp WAV render failed");
        return result;
    }
    if (!report(req, MediaRenderProgress::Stage::Audio, 1, 1, start + len,
                QStringLiteral("Audio mix ready"))) {
        return canceled();
    }

    if (!wantsVideo(fmt, tpl)) {
        if (!report(req, MediaRenderProgress::Stage::Encode, 0, 1, start,
                    QStringLiteral("Encoding audio…"))) {
            return canceled();
        }
        const FFmpegEncodeResult enc = FFmpegEncoder::encodeAudioFromWav(
            wavPath, req.outputPath, tpl, req.formatName, [&]() {
                return report(req, MediaRenderProgress::Stage::Encode, 1, 2, start,
                              QStringLiteral("Encoding audio…"));
            });
        if (enc.canceled) {
            return canceled();
        }
        if (!enc.ok) {
            result.error = enc.error;
            return result;
        }
        report(req, MediaRenderProgress::Stage::Done, 1, 1, start + len);
        result.ok = true;
        result.message = QStringLiteral("Wrote %1").arg(req.outputPath);
        return result;
    }

    int width = tpl.width > 0 ? tpl.width : model.frameWidth();
    int height = tpl.height > 0 ? tpl.height : model.frameHeight();
    width = std::max(2, width);
    height = std::max(2, height);
    const QSize outSize = VideoFrameCache::cappedSize(QSize(width, height));

    const double fps = tpl.fps > 0.1 ? tpl.fps : std::max(1.0, model.frameRate());
    const int frameCount = std::max(1, int(std::ceil(len * fps - 1e-9)));
    const QString framesDir = QDir(tmp.path()).filePath(QStringLiteral("frames"));
    QDir().mkpath(framesDir);

    for (int i = 0; i < frameCount; ++i) {
        const double t = start + (double(i) + 0.5) / fps;
        if (!report(req, MediaRenderProgress::Stage::Frames, i, frameCount, t,
                    QStringLiteral("Frame %1 / %2").arg(i + 1).arg(frameCount))) {
            return canceled();
        }

        QImage frame = composeWithRetries(model, t, outSize);
        if (frame.format() != QImage::Format_ARGB32_Premultiplied
            && frame.format() != QImage::Format_RGB32) {
            frame = frame.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        }

        if (req.onPreviewFrame) {
            req.onPreviewFrame(frame, t);
        }

        const QString png =
            QDir(framesDir).filePath(QStringLiteral("frame_%1.png").arg(i + 1, 6, 10, QChar('0')));
        if (!frame.save(png, "PNG")) {
            result.error = QStringLiteral("failed to write frame %1").arg(i + 1);
            return result;
        }
        if ((i % 2) == 0) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
    }

    if (!report(req, MediaRenderProgress::Stage::Frames, frameCount, frameCount, start + len,
                QStringLiteral("Frames ready"))) {
        return canceled();
    }
    if (!report(req, MediaRenderProgress::Stage::Encode, 0, 1, start + len,
                QStringLiteral("Encoding video…"))) {
        return canceled();
    }

    const QString pattern = QDir(framesDir).filePath(QStringLiteral("frame_%06d.png"));
    const FFmpegEncodeResult enc = FFmpegEncoder::encodeVideoFromPngSequence(
        pattern, 1, fps, wavPath, req.outputPath, tpl, req.formatName, [&]() {
            return report(req, MediaRenderProgress::Stage::Encode, 1, 2, start + len,
                          QStringLiteral("Encoding video…"));
        });
    if (enc.canceled) {
        return canceled();
    }
    if (!enc.ok) {
        result.error = enc.error;
        return result;
    }
    report(req, MediaRenderProgress::Stage::Done, 1, 1, start + len);
    result.ok = true;
    result.message = QStringLiteral("Wrote %1 (%2 frames)").arg(req.outputPath).arg(frameCount);
    return result;
}

} // namespace openvegas
