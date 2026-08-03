#pragma once

#include "io/RenderTemplateCatalog.h"

#include <QString>
#include <QStringList>

#include <functional>

namespace openvegas {

struct FFmpegEncodeResult {
    bool ok = false;
    bool canceled = false;
    QString error;
    QString ffmpegPath;
};

/** Return false to cancel a running ffmpeg process. */
using FFmpegCancelFn = std::function<bool()>;

/** Offline encode/mux via external ffmpeg CLI (found on PATH / common install dirs). */
class FFmpegEncoder {
public:
    static QString findFfmpeg();

    /** Encode audio-only file from a PCM WAV. */
    static FFmpegEncodeResult encodeAudioFromWav(const QString &wavPath, const QString &outputPath,
                                                 const RenderTemplate &tpl, const QString &formatName,
                                                 const FFmpegCancelFn &shouldContinue = {});

    /**
     * Mux PNG sequence (frame_%06d.png starting at 1) + optional WAV into video container.
     * @param framePattern absolute path pattern with %06d, e.g. C:/tmp/frame_%06d.png
     */
    static FFmpegEncodeResult encodeVideoFromPngSequence(const QString &framePattern, int startNumber,
                                                        double fps, const QString &wavPath,
                                                        const QString &outputPath,
                                                        const RenderTemplate &tpl,
                                                        const QString &formatName,
                                                        const FFmpegCancelFn &shouldContinue = {});

    static FFmpegEncodeResult run(const QStringList &args, const FFmpegCancelFn &shouldContinue = {});
};

} // namespace openvegas
