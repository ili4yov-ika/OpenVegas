#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <QVector>

namespace openvegas {

/**
 * Continuous / burst video decode for preview.
 * Prefer one ffmpeg process → raw RGB frames (no per-frame seek + PNG).
 * Optional linked libav when built with OPENVGAS_FFMPEG.
 */
class FFmpegStreamDecoder {
public:
    /** True when this build was linked against libav* (OPENVGAS_FFMPEG). */
    static bool linkedAvailable();

    /** True when Preferences / env allow -hwaccel auto on decode CLI. */
    static bool hwAccelEnabled();

    /**
     * Decode @p count frames starting at @p startSec at @p fps into @p outSize.
     * Uses linked libav when available, else one ffmpeg CLI rawvideo pipe.
     * @return number of non-null frames written into @p out (size == count).
     */
    static int decodeSequence(const QString &path, double startSec, double fps, int count,
                              const QSize &outSize, QVector<QImage> *out);

    /** Single frame helper (may still use a short continuous pipe). */
    static QImage decodeFrame(const QString &path, double timeSec, const QSize &outSize);

    /** Probe ffmpeg -encoders once; true if name is listed (e.g. h264_nvenc). */
    static bool encoderAvailable(const QString &encoderName);

    /** Video encode codec args: NVENC / QSV if present, else libx264 (Phase 6). */
    static QStringList videoEncodeCodecArgs(int crf = 23);
};

} // namespace openvegas
