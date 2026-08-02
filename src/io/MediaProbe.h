#pragma once

#include <QString>

namespace openvegas {

/** Stream / format metadata for Match Media Video Settings (and similar). */
struct MediaProbeInfo {
    bool ok = false;
    QString path;
    QString error;

    QString formatName;     // e.g. "mov,mp4,m4a,3gp,3g2,mj2" / "ISO Base Media"
    QString formatLongName; // e.g. "QuickTime / MPEG-4 / Motion JPEG 2000"
    int streamCount = 0;

    bool hasVideo = false;
    int width = 0;
    int height = 0;
    int videoBitDepth = 0; // bits; 0 → treat as 8 / display as 32 in UI when unknown
    double frameRate = 0.0;
    QString fieldOrder; // progressive, tt, bb, …
    QString pixelFormat;

    bool hasAudio = false;
    int sampleRate = 0;
    int audioBitDepth = 0;
    int channels = 0;

    double durationSec = 0.0;

    QString videoSummary() const; // "3840x2160x32"
    QString audioSummary() const; // "48000 Hz, 16 Bits, 6 Channels"
    QString timecode() const;     // "00:10:34,34"
    QString fileTypeLabel() const;
};

class MediaProbe {
public:
    /** Locate ffprobe next to ffmpeg, or on PATH. */
    static QString findFfprobe();

    /** Probe a media file (ffprobe JSON, ffmpeg -i fallback). */
    static MediaProbeInfo probe(const QString &path);

    /** Cached duration in seconds (0 if unknown). */
    static double cachedDurationSec(const QString &path);

    /**
     * Length to use when inserting media on the timeline.
     * hintSec wins when > 0.05; else probed duration; else kind defaults (still=5s, other=8s).
     */
    static double lengthForInsert(const QString &path, const QString &kind, double hintSec = 0.0);
};

} // namespace openvegas
