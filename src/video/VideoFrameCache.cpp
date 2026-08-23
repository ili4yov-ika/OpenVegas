#include "video/VideoFrameCache.h"

#include "video/NestedFrameHook.h"

#include "io/FFmpegStreamDecoder.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QMetaObject>
#include <QRunnable>
#include <QThreadPool>
#include <QVector>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace openvegas {
namespace {

constexpr int kMaxInflight = 6;
std::atomic<double> g_bucketSec{1.0 / 30.0};

class DecodeJob : public QRunnable {
public:
    DecodeJob(VideoFrameCache *cache, QString path, qint64 bucket, QSize size, bool still)
        : m_cache(cache)
        , m_path(std::move(path))
        , m_bucket(bucket)
        , m_size(size)
        , m_still(still)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        QImage img;
        if (!QFileInfo::exists(m_path) || m_size.width() < 2 || m_size.height() < 2) {
            if (m_cache) {
                m_cache->finishFrameJob(m_path, m_bucket, m_size, img);
            }
            return;
        }

        if (m_still) {
            QImageReader reader(m_path);
            reader.setAutoTransform(true);
            QSize src = reader.size();
            if (src.isValid()) {
                src.scale(m_size, Qt::KeepAspectRatioByExpanding);
                reader.setScaledSize(src);
            }
            img = reader.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
            if (!img.isNull() && img.size() != m_size) {
                img = img.scaled(m_size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                if (img.width() > m_size.width() || img.height() > m_size.height()) {
                    const int x = (img.width() - m_size.width()) / 2;
                    const int y = (img.height() - m_size.height()) / 2;
                    img = img.copy(x, y, m_size.width(), m_size.height());
                }
            }
        } else if (looksLikeProjectMedia(m_path)) {
            // A VEGAS project used as a clip. There is no file to decode — its picture
            // has to be composed from the project itself. Exactly this frame: a nearest
            // one would be cached below as though it were the frame for this bucket, and
            // scrubbing would keep showing it. Null just means the nested timeline's own
            // media is still decoding, and null is never cached — the next repaint asks
            // again and gets the real frame.
            img = nestedFrame(m_path, double(m_bucket) * VideoFrameCache::bucketSec(), m_size,
                              /*exact=*/true);
        } else {
            // Continuous decode: one seek + raw pipe (no PNG / no seek-per-frame).
            img = FFmpegStreamDecoder::decodeFrame(m_path, double(m_bucket) * VideoFrameCache::bucketSec(),
                                                   m_size);
        }

        if (m_cache) {
            m_cache->finishFrameJob(m_path, m_bucket, m_size, img);
        }
    }

private:
    VideoFrameCache *m_cache = nullptr;
    QString m_path;
    qint64 m_bucket = 0;
    QSize m_size;
    bool m_still = false;
};

/** One ffmpeg process → N sequential frames (keeps video near audio clock). */
class BurstDecodeJob : public QRunnable {
public:
    BurstDecodeJob(VideoFrameCache *cache, QString path, qint64 startBucket, int count, QSize size)
        : m_cache(cache)
        , m_path(std::move(path))
        , m_start(startBucket)
        , m_count(count)
        , m_size(size)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        QVector<QImage> frames;
        frames.resize(m_count);
        if (!QFileInfo::exists(m_path) || m_count < 1 || m_size.width() < 2) {
            if (m_cache) {
                m_cache->finishBurstJob(m_path, m_start, m_size, frames);
            }
            return;
        }

        const double step = VideoFrameCache::bucketSec();
        const double t0 = double(m_start) * step;
        const double fps = 1.0 / step;
        // One process: continuous raw RGB sequence (Phase 5 — no per-frame CLI seek).
        FFmpegStreamDecoder::decodeSequence(m_path, t0, fps, m_count, m_size, &frames);

        if (m_cache) {
            m_cache->finishBurstJob(m_path, m_start, m_size, frames);
        }
    }

private:
    VideoFrameCache *m_cache = nullptr;
    QString m_path;
    qint64 m_start = 0;
    int m_count = 0;
    QSize m_size;
};

} // namespace

VideoFrameCache &VideoFrameCache::instance()
{
    static VideoFrameCache cache;
    return cache;
}

VideoFrameCache::VideoFrameCache(QObject *parent)
    : QObject(parent)
{
}

double VideoFrameCache::bucketSec()
{
    return g_bucketSec.load();
}

void VideoFrameCache::setBucketFps(double fps)
{
    fps = std::clamp(fps, 1.0, 120.0);
    g_bucketSec.store(1.0 / fps);
}

QSize VideoFrameCache::cappedSize(const QSize &requested)
{
    const int w = std::max(2, requested.width());
    const int h = std::max(2, requested.height());
    const int longSide = std::max(w, h);
    if (longSide <= kMaxLongSide) {
        return QSize(w, h);
    }
    const double s = double(kMaxLongSide) / double(longSide);
    return QSize(std::max(2, int(std::lround(w * s))), std::max(2, int(std::lround(h * s))));
}

qint64 VideoFrameCache::bucketForTime(double timeSec)
{
    const double step = bucketSec();
    return qint64(std::floor(std::max(0.0, timeSec) / step + 1e-9));
}

QString VideoFrameCache::frameKey(const QString &path, qint64 timeBucket, const QSize &size)
{
    return path + QLatin1Char('|') + QString::number(timeBucket) + QLatin1Char('|')
           + QString::number(size.width()) + QLatin1Char('x') + QString::number(size.height());
}

QString VideoFrameCache::activeBurstKey(const QString &path, const QSize &size)
{
    return path + QLatin1Char('|') + QString::number(size.width()) + QLatin1Char('x')
           + QString::number(size.height());
}

void VideoFrameCache::invalidate(const QString &path)
{
    QMutexLocker lock(&m_mutex);
    if (path.isEmpty()) {
        m_frames.clear();
        m_inflight.clear();
        m_inflightCount = 0;
        return;
    }
    const QString prefix = path + QLatin1Char('|');
    for (auto it = m_frames.begin(); it != m_frames.end();) {
        if (it.key().startsWith(prefix)) {
            it = m_frames.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_inflight.begin(); it != m_inflight.end();) {
        if (it.key().startsWith(prefix)) {
            it = m_inflight.erase(it);
        } else {
            ++it;
        }
    }
}

QImage VideoFrameCache::nearestLocked(const QString &path, qint64 bucket, const QSize &size,
                                      int maxDelta) const
{
    // Prefer exact, then slightly behind (video lag), then ahead — matches soft drop-frame.
    for (int d = 0; d <= maxDelta; ++d) {
        if (d == 0) {
            const auto it = m_frames.constFind(frameKey(path, bucket, size));
            if (it != m_frames.cend()) {
                return *it;
            }
            continue;
        }
        const qint64 behind = bucket - d;
        if (behind >= 0) {
            const auto it = m_frames.constFind(frameKey(path, behind, size));
            if (it != m_frames.cend()) {
                return *it;
            }
        }
        const auto itAhead = m_frames.constFind(frameKey(path, bucket + d, size));
        if (itAhead != m_frames.cend()) {
            return *itAhead;
        }
    }
    return {};
}

void VideoFrameCache::requestFrame(const QString &path, qint64 timeBucket, const QSize &size,
                                   bool still)
{
    const QString key = frameKey(path, timeBucket, size);
    {
        QMutexLocker lock(&m_mutex);
        if (m_inflight.value(key, false) || m_inflightCount >= kMaxInflight) {
            return;
        }
        m_inflight.insert(key, true);
        ++m_inflightCount;
    }
    QThreadPool::globalInstance()->start(new DecodeJob(this, path, timeBucket, size, still));
}

void VideoFrameCache::requestBurst(const QString &path, qint64 startBucket, int count,
                                   const QSize &size)
{
    if (count < 2) {
        requestFrame(path, startBucket, size, false);
        return;
    }
    // Reserve inflight slots for the burst range under one synthetic key.
    const QString burstKey = path + QLatin1Char('|') + QStringLiteral("burst|")
                             + QString::number(startBucket) + QLatin1Char('|')
                             + QString::number(count) + QLatin1Char('|')
                             + QString::number(size.width()) + QLatin1Char('x')
                             + QString::number(size.height());
    {
        QMutexLocker lock(&m_mutex);
        if (m_inflight.value(burstKey, false) || m_inflightCount >= kMaxInflight) {
            return;
        }
        // An already-running burst for this path+size will populate the cache soon; spawning
        // an overlapping ffmpeg process just contends with it for CPU (see m_activeBurstEnd doc).
        const QString activeKey = activeBurstKey(path, size);
        const auto activeIt = m_activeBurstEnd.constFind(activeKey);
        if (activeIt != m_activeBurstEnd.constEnd() && startBucket < activeIt.value()) {
            return;
        }
        // Skip if most of the range is already cached.
        int missing = 0;
        for (int i = 0; i < count; ++i) {
            if (!m_frames.contains(frameKey(path, startBucket + i, size))) {
                ++missing;
            }
        }
        if (missing < std::max(2, count / 3)) {
            return;
        }
        m_inflight.insert(burstKey, true);
        ++m_inflightCount;
        m_activeBurstEnd.insert(activeKey, startBucket + count);
    }
    QThreadPool::globalInstance()->start(
        new BurstDecodeJob(this, path, startBucket, count, size));
}

QImage VideoFrameCache::frameIfReady(const QString &path, double timeSec, const QSize &size,
                                     bool still, bool allowNearest)
{
    if (path.isEmpty() || size.width() < 2 || size.height() < 2) {
        return {};
    }
    const QSize target = cappedSize(size);
    const qint64 bucket = still ? 0 : bucketForTime(timeSec);
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_frames.constFind(frameKey(path, bucket, target));
        if (it != m_frames.cend()) {
            return *it;
        }
        if (allowNearest) {
            // ~6 frames @ 30fps ≈ 200ms — typical soft-realtime slack
            QImage near = nearestLocked(path, bucket, target, 6);
            if (!near.isNull()) {
                const bool needExact = !m_inflight.value(frameKey(path, bucket, target), false);
                lock.unlock();
                if (needExact) {
                    requestFrame(path, bucket, target, still);
                }
                return near;
            }
        }
        if (m_inflight.value(frameKey(path, bucket, target), false)) {
            return {};
        }
    }
    requestFrame(path, bucket, target, still);
    return {};
}

void VideoFrameCache::prefetch(const QString &path, double timeSec, const QSize &size, bool still,
                               double behindSec, double aheadSec)
{
    if (still || path.isEmpty()) {
        frameIfReady(path, 0.0, size, true, false);
        return;
    }
    const QSize target = cappedSize(size);
    const qint64 center = bucketForTime(timeSec);
    const double step = bucketSec();
    const qint64 behind = qint64(std::ceil(std::max(0.0, behindSec) / step));
    const qint64 ahead = qint64(std::ceil(std::max(0.0, aheadSec) / step));

    // Burst decode forward from current bucket (one ffmpeg process, sequential read).
    const int burstCount = int(std::min<qint64>(45, ahead + 1));
    const bool burstCoversForward = burstCount >= 4;
    if (burstCoversForward) {
        requestBurst(path, center, burstCount, target);
    }

    for (qint64 b = center - behind; b <= center + ahead; ++b) {
        if (b < 0) {
            continue;
        }
        const QString key = frameKey(path, b, target);
        {
            QMutexLocker lock(&m_mutex);
            if (m_frames.contains(key) || m_inflight.value(key, false)) {
                continue;
            }
        }
        // Individual fills cover behind (burst never reaches backwards); forward-of-center
        // only when there was no burst to cover it (small aheadSec) — otherwise this would
        // spawn a redundant ffmpeg process racing the burst for the same frames every tick.
        if (b < center || !burstCoversForward) {
            requestFrame(path, b, target, false);
        }
    }
}

void VideoFrameCache::finishFrameJob(const QString &path, qint64 timeBucket, const QSize &size,
                                     const QImage &img)
{
    const QString key = frameKey(path, timeBucket, size);
    {
        QMutexLocker lock(&m_mutex);
        if (!img.isNull()) {
            m_frames.insert(key, img);
        }
        if (m_inflight.remove(key)) {
            m_inflightCount = std::max(0, m_inflightCount - 1);
        }
    }
    QMetaObject::invokeMethod(
        this, [this, path]() { emit frameReady(path); }, Qt::QueuedConnection);
}

void VideoFrameCache::finishBurstJob(const QString &path, qint64 startBucket, const QSize &size,
                                     const QVector<QImage> &frames)
{
    const QString burstKey = path + QLatin1Char('|') + QStringLiteral("burst|")
                             + QString::number(startBucket) + QLatin1Char('|')
                             + QString::number(frames.size()) + QLatin1Char('|')
                             + QString::number(size.width()) + QLatin1Char('x')
                             + QString::number(size.height());
    {
        QMutexLocker lock(&m_mutex);
        for (int i = 0; i < frames.size(); ++i) {
            if (!frames[i].isNull()) {
                m_frames.insert(frameKey(path, startBucket + i, size), frames[i]);
            }
        }
        if (m_inflight.remove(burstKey)) {
            m_inflightCount = std::max(0, m_inflightCount - 1);
        }
        // Only clear if this is still the tracked burst for this path+size (the gate in
        // requestBurst means a newer one shouldn't have started while this was active, but
        // stay defensive).
        const QString activeKey = activeBurstKey(path, size);
        const auto activeIt = m_activeBurstEnd.constFind(activeKey);
        if (activeIt != m_activeBurstEnd.constEnd()
            && activeIt.value() == startBucket + qint64(frames.size())) {
            m_activeBurstEnd.remove(activeKey);
        }
    }
    QMetaObject::invokeMethod(
        this, [this, path]() { emit frameReady(path); }, Qt::QueuedConnection);
}

} // namespace openvegas
