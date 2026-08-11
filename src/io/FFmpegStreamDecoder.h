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

    /** True when Preferences / env allow hardware (VLD) decode on the CLI path. */
    static bool hwAccelEnabled();

    /**
     * Preference `media/hwDecoder`: "auto" (default) or an explicit ffmpeg -hwaccel
     * name — on Windows typically d3d11va / dxva2 / cuda / qsv. Env OPENVGAS_HWDECODER
     * overrides it. Returns "auto" whenever the stored value is not offered by this
     * ffmpeg build, so a stale setting degrades instead of breaking decode.
     */
    static QString hwDecodeMethod();

    /** -hwaccel names this ffmpeg reports (`ffmpeg -hwaccels`), probed once. */
    static QStringList availableHwDecodeMethods();

    /**
     * Decode @p count frames starting at @p startSec at @p fps into @p outSize.
     * Uses linked libav when available, else one ffmpeg CLI rawvideo pipe.
     * @return number of non-null frames written into @p out (size == count).
     */
    static int decodeSequence(const QString &path, double startSec, double fps, int count,
                              const QSize &outSize, QVector<QImage> *out);

    /** Single frame helper (may still use a short continuous pipe). */
    static QImage decodeFrame(const QString &path, double timeSec, const QSize &outSize);

    /**
     * Probe ffmpeg -encoders once; true if the name is *listed* (e.g. h264_nvenc).
     *
     * Necessary but NOT sufficient: full builds (Gyan, BtbN) list nvenc, qsv and amf
     * unconditionally, because the list is baked in at ffmpeg build time and says
     * nothing about the machine. Use encoderUsable() before choosing an encoder.
     */
    static bool encoderAvailable(const QString &encoderName);

    /**
     * True when a 3-frame trial encode of that codec actually succeeds here — i.e.
     * the hardware exists and its runtime initialises. Costs one short ffmpeg run
     * (~60–200 ms) the first time; the verdict is cached in QSettings and reused
     * until the ffmpeg binary changes.
     */
    static bool encoderUsable(const QString &encoderName);

    /** Drops the cached trial-encode verdicts so the next query re-probes. */
    static void clearEncoderProbeCache();

    /** Hardware encoders this build knows how to drive, in preference order. */
    static QStringList knownHwEncoders();

    /**
     * Preference `media/hwEncoder`: "auto" (default), "nvenc", "qsv", "amf" or "cpu".
     * Env OPENVGAS_HWENCODER overrides it. "auto" picks the first *usable* one.
     */
    static QString hwEncoderPreference();

    /** Encoder name that will actually be used; empty means software libx264. */
    static QString selectedHwEncoder();

    /** Video encode codec args: hardware encoder when one really works, else libx264. */
    static QStringList videoEncodeCodecArgs(int crf = 23);
};

} // namespace openvegas
