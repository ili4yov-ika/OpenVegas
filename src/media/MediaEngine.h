#pragma once

#include "io/RenderTemplateCatalog.h"

#include <QImage>
#include <QString>

#include <functional>

namespace openvegas {

class ProjectModel;
class AudioEngine;

struct MediaRenderProgress {
    enum class Stage { Audio, Frames, Encode, Done };
    Stage stage = Stage::Audio;
    int current = 0;
    int total = 1;
    double timeSec = 0.0;
    QString message;
};

/** Return false to cancel. */
using MediaRenderProgressFn = std::function<bool(const MediaRenderProgress &)>;
/** Optional: show composed export frame in Video Preview. */
using MediaRenderPreviewFn = std::function<void(const QImage &frame, double timeSec)>;

struct MediaRenderRequest {
    QString outputPath;
    QString formatName;
    QString templateName;
    double startSec = 0.0;
    double lengthSec = 1.0;
    MediaRenderProgressFn onProgress;
    MediaRenderPreviewFn onPreviewFrame;
};

struct MediaRenderResult {
    bool ok = false;
    bool canceled = false;
    QString error;
    QString message;
};

/**
 * Offline render facade: AudioEngine → (optional VideoCompositor frames) → FFmpegEncoder.
 * Playback remains on AudioEngine + MainWindow preview path.
 */
class MediaEngine {
public:
    static MediaRenderResult renderProject(ProjectModel &model, AudioEngine *audio,
                                           const MediaRenderRequest &req);

    /** Resolve catalog template by format + template name (empty → first template). */
    static bool resolveTemplate(const QString &formatName, const QString &templateName,
                                RenderFormat *outFormat, RenderTemplate *outTpl);
};

} // namespace openvegas
