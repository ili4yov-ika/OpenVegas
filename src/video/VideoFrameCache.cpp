#include "video/VideoFrameCache.h"

#include "io/MediaFilmstripCache.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QMetaObject>
#include <QProcess>
#include <QRunnable>
#include <QTemporaryDir>
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
        } else {
            const QString ffmpeg = MediaFilmstripCache::findFfmpeg();
            if (!ffmpeg.isEmpty()) {
                const double step = VideoFrameCache::bucketSec();
                const double t = double(m_bucket) * step;
                QTemporaryDir tmp;
                if (tmp.isValid()) {
                    const QString out =
                        tmp.path() + QStringLiteral("/vf_%1.png").arg(m_bucket);
                    QProcess proc;
                    QStringList args;
                    // -ss before -i: fast seek (preview soft-realtime, like MLT drop-frame)
                    args << QStringLiteral("-hide_banner") << QStringLiteral("-loglevel")
                         << QStringLiteral("error") << QStringLiteral("-ss")
                         << QString::number(t, 'f', 3) << QStringLiteral("-i") << m_path
                         << QStringLiteral("-frames:v") << QStringLiteral("1")
                         << QStringLiteral("-vf")
                         << QStringLiteral(
                                "scale=%1:%2:force_original_aspect_ratio=increase,crop=%1:%2")
                                .arg(m_size.width())
                                .arg(m_size.height())
                         << QStringLiteral("-y") << out;
                    proc.start(ffmpeg, args);
                    if (proc.waitForFinished(25000) && proc.exitStatus() == QProcess::NormalExit
                        && proc.exitCode() == 0 && QFileInfo::exists(out)) {
                        img = QImage(out).convertToFormat(QImage::Format_ARGB32_Premultiplied);
                    }
                }
            }
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
        const QString ffmpeg = MediaFilmstripCache::findFfmpeg();
        if (ffmpeg.isEmpty() || !QFileInfo::exists(m_path) || m_count < 1) {
            if (m_cache) {
                m_cache->finishBurstJob(m_path, m_start, m_size, frames);
            }
            return;
        }

        const double step = VideoFrameCache::bucketSec();
        const double t0 = double(m_start) * step;
        const double dur = double(m_count) * step + step * 0.5;
        QTemporaryDir tmp;
        if (!tmp.isValid()) {
            if (m_cache) {
                m_cache->finishBurstJob(m_path, m_start, m_size, frames);
            }
            return;
        }

        const QString pattern = tmp.path() + QStringLiteral("/b_%05d.png");
        QProcess proc;
        QStringList args;
        args << QStringLiteral("-hide_banner") << QStringLiteral("-loglevel")
             << QStringLiteral("error") << QStringLiteral("-ss") << QString::number(t0, 'f', 3)
             << QStringLiteral("-i") << m_path << QStringLiteral("-t")
             << QString::number(dur, 'f', 3) << QStringLiteral("-vf")
             << QStringLiteral("fps=%1,scale=%2:%3:force_original_aspect_ratio=increase,crop=%2:%3")
                    .arg(1.0 / step, 0, 'f', 4)
                    .arg(m_size.width())
                    .arg(m_size.height())
             << QStringLiteral("-y") << pattern;
        proc.start(ffmpeg, args);
        if (proc.waitForFinished(60000) && proc.exitStatus() == QProcess::NormalExit
            && proc.exitCode() == 0) {
            for (int i = 0; i < m_count; ++i) {
                // ffmpeg image2 is 1-based by default
                const QString file =
                    tmp.path() + QStringLiteral("/b_%1.png").arg(i + 1, 5, 10, QLatin1Char('0'));
                if (QFileInfo::exists(file)) {
                    frames[i] = QImage(file).convertToFormat(QImage::Format_ARGB32_Premultiplied);
                }
            }
        }

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
    if (burstCount >= 4) {
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
        // Individual fills only a few behind / near; burst covers ahead.
        if (b <= center + 2) {
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
    }
    QMetaObject::invokeMethod(
        this, [this, path]() { emit frameReady(path); }, Qt::QueuedConnection);
}

} // namespace openvegas
