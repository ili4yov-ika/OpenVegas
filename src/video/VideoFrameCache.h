#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QSize>
#include <QString>

namespace openvegas {

/**
 * Full-ish preview frame decode for Video Preview (ARGB32).
 * FFmpeg CLI seek for video; QImageReader for stills. Async + prefetch.
 *
 * Soft realtime (Kdenlive/MLT-like): audio is the clock; video presents the nearest
 * ready frame for that time rather than blocking on exact seeks.
 */
class VideoFrameCache : public QObject {
    Q_OBJECT
public:
    static VideoFrameCache &instance();

    /** Cap long side of decode target (perf). */
    static constexpr int kMaxLongSide = 1280;

    /** Quantization step for decode buckets (~project fps). */
    static double bucketSec();
    static void setBucketFps(double fps);

    /**
     * Return decoded frame at media time if ready; otherwise queue async load.
     * @param still if true, ignore timeSec and load whole image once.
     * @param allowNearest if true, return closest cached bucket within a few frames
     *        (soft realtime — keep video moving with audio).
     */
    QImage frameIfReady(const QString &path, double timeSec, const QSize &size, bool still = false,
                        bool allowNearest = false);

    /**
     * Prefetch around timeSec. During play prefer forward window (audio clock ahead).
     * Uses a burst ffmpeg decode when possible for better A/V sync.
     */
    void prefetch(const QString &path, double timeSec, const QSize &size, bool still,
                  double behindSec = 0.25, double aheadSec = 1.25);

    void invalidate(const QString &path = {});

    void finishFrameJob(const QString &path, qint64 timeBucket, const QSize &size,
                        const QImage &img);
    void finishBurstJob(const QString &path, qint64 startBucket, const QSize &size,
                        const QVector<QImage> &frames);

    static QSize cappedSize(const QSize &requested);
    static qint64 bucketForTime(double timeSec);

signals:
    void frameReady(const QString &path);

private:
    explicit VideoFrameCache(QObject *parent = nullptr);

    static QString frameKey(const QString &path, qint64 timeBucket, const QSize &size);
    void requestFrame(const QString &path, qint64 timeBucket, const QSize &size, bool still);
    void requestBurst(const QString &path, qint64 startBucket, int count, const QSize &size);
    QImage nearestLocked(const QString &path, qint64 bucket, const QSize &size, int maxDelta) const;

    QMutex m_mutex;
    QHash<QString, QImage> m_frames;
    QHash<QString, bool> m_inflight;
    int m_inflightCount = 0;
};

} // namespace openvegas
