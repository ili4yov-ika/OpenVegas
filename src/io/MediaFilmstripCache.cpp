#include "MediaFilmstripCache.h"
#include "MediaThumbCache.h"

#include "io/FfmpegLocator.h"
#include "video/NestedFrameHook.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QProcess>
#include <QRunnable>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>

#include <algorithm>
#include <cmath>

namespace openvegas {
namespace {

constexpr int kMaxInflight = 6;
constexpr double kBucketSec = 0.25; // quantize seeks (~4 buckets/sec)
// One decode job covers this many buckets — 30 s of timeline. Chosen so the ffprobe
// interval scan stays about a second; larger runs make the first tile appear later
// without decoding any faster per frame.
constexpr int kRangeBuckets = 120;

class FrameRangeJob : public QRunnable
{
public:
    FrameRangeJob(MediaFilmstripCache *cache, QString path, qint64 startBucket, int count,
                  QSize size)
        : m_cache(cache)
        , m_path(std::move(path))
        , m_startBucket(startBucket)
        , m_count(count)
        , m_size(std::move(size))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        QHash<qint64, QPixmap> produced;
        if (looksLikeProjectMedia(m_path)) {
            // A VEGAS project used as a clip: composed, not decoded. One frame per bucket
            // would be wasteful here too, so the run is sampled — the pictures a
            // filmstrip shows are a guide, and composing every quarter second of a
            // nested timeline is far more work than reading keyframes off a file.
            produced = composeNested();
        } else {
            const QString ffmpeg = MediaFilmstripCache::findFfmpeg();
            if (!ffmpeg.isEmpty() && QFileInfo::exists(m_path) && m_size.width() > 1
                && m_size.height() > 1) {
                produced = extract(ffmpeg);
            }
        }
        // A nested project that returned nothing is still decoding, not broken.
        m_cache->finishFrameRange(m_path, m_startBucket, m_count, m_size, produced,
                                  !looksLikeProjectMedia(m_path));
    }

private:
    /** Keyframe timestamps inside [from, from+duration], in seconds. */
    QVector<double> keyframeTimes(const QString &ffmpeg, double from, double duration) const
    {
        QString ffprobe = ffmpeg;
        const int cut = ffprobe.lastIndexOf(QLatin1String("ffmpeg"));
        if (cut < 0) {
            return {};
        }
        ffprobe.replace(cut, 6, QStringLiteral("ffprobe"));
        if (!QFileInfo::exists(ffprobe)) {
            return {};
        }
        QProcess proc;
        proc.start(ffprobe,
                   {QStringLiteral("-v"), QStringLiteral("error"),
                    QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                    QStringLiteral("-skip_frame"), QStringLiteral("nokey"),
                    QStringLiteral("-show_entries"), QStringLiteral("frame=pts_time"),
                    QStringLiteral("-of"), QStringLiteral("csv=p=0"),
                    QStringLiteral("-read_intervals"),
                    QStringLiteral("%1%+%2").arg(from, 0, 'f', 3).arg(duration, 0, 'f', 3),
                    m_path});
        if (!proc.waitForFinished(20000) || proc.exitStatus() != QProcess::NormalExit) {
            return {};
        }
        QVector<double> times;
        const QList<QByteArray> lines = proc.readAllStandardOutput().split('\n');
        for (const QByteArray &line : lines) {
            bool ok = false;
            const double t = line.trimmed().toDouble(&ok);
            if (ok) {
                times.push_back(t);
            }
        }
        std::sort(times.begin(), times.end());
        return times;
    }

    /**
     * The nested project's frame at `timeSec`, waiting for it to be composed.
     *
     * The compose is asynchronous underneath: it starts decoding the nested timeline's
     * own media and returns null until those frames arrive. In a paint that means "ask
     * again next repaint", but this runs in a background job that may as well wait — and
     * must, because the alternative it used to take (accept the nearest frame already
     * decoded) hands back the same picture for every time in the run, and the strip fills
     * with one frame repeated.
     *
     * The wait gives the pool its thread back, since the decodes being waited on are
     * queued on that same pool and would otherwise never get a thread to run on.
     */
    QImage awaitNested(double timeSec) const
    {
        constexpr int kTries = 30;
        constexpr int kSleepMs = 100; // so: up to 3 s for a frame
        QImage img = nestedFrame(m_path, timeSec, m_size, /*exact=*/true);
        for (int i = 0; img.isNull() && i < kTries; ++i) {
            QThreadPool *pool = QThreadPool::globalInstance();
            pool->releaseThread();
            QThread::msleep(kSleepMs);
            pool->reserveThread();
            img = nestedFrame(m_path, timeSec, m_size, /*exact=*/true);
        }
        return img;
    }

    /** Frames of a nested project, composed at a coarser step than the bucket grid. */
    QHash<qint64, QPixmap> composeNested() const
    {
        QHash<qint64, QPixmap> out;
        if (m_size.width() < 2 || m_size.height() < 2) {
            return out;
        }
        // One composed frame per this many buckets; the rest of the block reuses it.
        // Composing a nested timeline costs about what a full preview frame costs, so a
        // tile every two seconds keeps the strip informative without stalling the pool.
        constexpr int kStride = 8;
        for (int i = 0; i < m_count; i += kStride) {
            const qint64 bucket = m_startBucket + i;
            const QImage img = awaitNested(double(bucket) * kBucketSec);
            if (img.isNull()) {
                // Leave the block empty rather than filling it with the previous one:
                // the buckets stay unclaimed misses and are asked for again.
                continue;
            }
            const QPixmap pm = QPixmap::fromImage(img);
            for (int j = i; j < std::min(i + kStride, m_count); ++j) {
                out.insert(m_startBucket + j, pm);
            }
        }
        return out;
    }

    QHash<qint64, QPixmap> extract(const QString &ffmpeg) const
    {
        QHash<qint64, QPixmap> out;
        const double from = double(m_startBucket) * kBucketSec;
        const double duration = double(m_count) * kBucketSec;

        const QVector<double> times = keyframeTimes(ffmpeg, from, duration);
        if (times.isEmpty()) {
            return out;
        }
        QTemporaryDir tmp;
        if (!tmp.isValid()) {
            return out;
        }
        QProcess proc;
        proc.start(ffmpeg,
                   {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                    QStringLiteral("error"), QStringLiteral("-skip_frame"),
                    QStringLiteral("nokey"), QStringLiteral("-ss"),
                    QString::number(from, 'f', 3), QStringLiteral("-t"),
                    QString::number(duration, 'f', 3), QStringLiteral("-i"), m_path,
                    QStringLiteral("-vsync"), QStringLiteral("0"), QStringLiteral("-vf"),
                    QStringLiteral("scale=%1:%2:force_original_aspect_ratio=increase,crop=%1:%2")
                        .arg(m_size.width())
                        .arg(m_size.height()),
                    QStringLiteral("-q:v"), QStringLiteral("4"), QStringLiteral("-y"),
                    tmp.path() + QStringLiteral("/f_%04d.jpg")});
        if (!proc.waitForFinished(60000) || proc.exitStatus() != QProcess::NormalExit) {
            return out;
        }

        // The i-th image is the i-th keyframe of the interval, so the two lists line up.
        QVector<QPixmap> images;
        for (int i = 1; i <= times.size() + 4; ++i) {
            const QString file =
                tmp.path() + QStringLiteral("/f_%1.jpg").arg(i, 4, 10, QLatin1Char('0'));
            if (!QFileInfo::exists(file)) {
                break;
            }
            images.push_back(QPixmap(file));
        }
        if (images.isEmpty()) {
            return out;
        }

        for (int i = 0; i < m_count; ++i) {
            const qint64 bucket = m_startBucket + i;
            const double t = double(bucket) * kBucketSec;
            // Last keyframe at or before this bucket — what the viewer would be looking
            // at while scrubbing there.
            int pick = 0;
            for (int k = 0; k < times.size() && k < images.size(); ++k) {
                if (times[k] <= t + 1e-6) {
                    pick = k;
                } else {
                    break;
                }
            }
            if (pick < images.size() && !images[pick].isNull()) {
                out.insert(bucket, images[pick]);
            }
        }
        return out;
    }

    MediaFilmstripCache *m_cache = nullptr;
    QString m_path;
    qint64 m_startBucket = 0;
    int m_count = 1;
    QSize m_size;
};

class PosterJob : public QRunnable {
public:
    PosterJob(MediaFilmstripCache *cache, QString path, QSize size)
        : m_cache(cache)
        , m_path(std::move(path))
        , m_size(size)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        QPixmap pm;
        if (QFileInfo::exists(m_path)) {
            // Reuse Shell / image decode from MediaThumbCache (sync path).
            pm = MediaThumbCache::instance().pixmapIfReady(m_path, m_size, QStringLiteral("video"));
            if (pm.isNull()) {
                // Force a blocking Shell load via iconFor → may return placeholder; filter by ready
                // by calling finishAsyncLoad-style sync through a dedicated decode:
                // Use iconFor only as last resort after waiting briefly is not possible here —
                // MediaThumbCache::finishAsyncLoad is public, invoke it.
                MediaThumbCache::instance().finishAsyncLoad(m_path, m_size, QStringLiteral("video"));
                pm = MediaThumbCache::instance().pixmapIfReady(m_path, m_size, QStringLiteral("video"));
            }
        }
        if (m_cache) {
            m_cache->finishPosterJob(m_path, m_size, pm);
        }
    }

private:
    MediaFilmstripCache *m_cache = nullptr;
    QString m_path;
    QSize m_size;
};

} // namespace

MediaFilmstripCache &MediaFilmstripCache::instance()
{
    static MediaFilmstripCache cache;
    return cache;
}

MediaFilmstripCache::MediaFilmstripCache(QObject *parent)
    : QObject(parent)
{
}

QString MediaFilmstripCache::findFfmpeg()
{
    // Kept as a name callers already use; the search itself lives in FfmpegLocator so that
    // things which only need to run ffmpeg — capture, for one — do not have to drag the
    // filmstrip cache and everything behind it in with them.
    return FfmpegLocator::find();
}

qint64 MediaFilmstripCache::bucketForTime(double timeSec)
{
    return qint64(std::floor(std::max(0.0, timeSec) / kBucketSec + 1e-9));
}

QString MediaFilmstripCache::frameKey(const QString &path, qint64 timeBucket, const QSize &size)
{
    return path + QLatin1Char('|') + QString::number(timeBucket) + QLatin1Char('|')
           + QString::number(size.width()) + QLatin1Char('x') + QString::number(size.height());
}

QString MediaFilmstripCache::posterKey(const QString &path, const QSize &size)
{
    // "poster2" — plain frames (no Project Media sprocket ladders baked in).
    return path + QLatin1Char('|') + QStringLiteral("poster2|") + QString::number(size.width())
           + QLatin1Char('x') + QString::number(size.height());
}

void MediaFilmstripCache::invalidate(const QString &path)
{
    QMutexLocker lock(&m_mutex);
    if (path.isEmpty()) {
        m_frames.clear();
        m_frameInflight.clear();
        m_posters.clear();
        m_posterInflight.clear();
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
    for (auto it = m_frameInflight.begin(); it != m_frameInflight.end();) {
        if (it.key().startsWith(prefix)) {
            it = m_frameInflight.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_posters.begin(); it != m_posters.end();) {
        if (it.key().startsWith(prefix)) {
            it = m_posters.erase(it);
        } else {
            ++it;
        }
    }
    m_posterInflight.remove(path);
}

QPixmap MediaFilmstripCache::frameIfReady(const QString &path, double timeSec, const QSize &size)
{
    if (path.isEmpty() || size.width() < 2 || size.height() < 2) {
        return {};
    }
    if (findFfmpeg().isEmpty()) {
        return {}; // caller falls back to poster / procedural
    }
    const qint64 bucket = bucketForTime(timeSec);
    const QString key = frameKey(path, bucket, size);
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_frames.constFind(key);
        if (it != m_frames.cend()) {
            return *it;
        }
        if (m_frameInflight.value(key, false)) {
            return {};
        }
    }
    requestFrame(path, bucket, size);
    return {};
}

QPixmap MediaFilmstripCache::posterIfReady(const QString &path, const QSize &size)
{
    if (path.isEmpty() || size.width() < 2 || size.height() < 2) {
        return {};
    }
    const QString key = posterKey(path, size);
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_posters.constFind(key);
        if (it != m_posters.cend()) {
            return *it;
        }
        if (m_posterInflight.value(path, false)) {
            return {};
        }
    }
    // Fast path: already in MediaThumbCache
    QPixmap pm =
        MediaThumbCache::instance().pixmapIfReady(path, size, QStringLiteral("video"));
    if (!pm.isNull()) {
        QMutexLocker lock(&m_mutex);
        m_posters.insert(key, pm);
        return pm;
    }
    requestPoster(path, size);
    return {};
}

void MediaFilmstripCache::requestFrame(const QString &path, qint64 timeBucket, const QSize &size)
{
    // Decode a run of buckets per job, not one. Starting ffmpeg and opening a 4K file
    // costs far more than the decode itself at this scale, so asking for one thumbnail
    // at a time made a filmstrip take tens of seconds to fill in.
    const qint64 start = (timeBucket / kRangeBuckets) * kRangeBuckets;

    QVector<qint64> claimed;
    {
        QMutexLocker lock(&m_mutex);
        if (m_frameInflight.value(frameKey(path, timeBucket, size), false)
            || m_frames.contains(frameKey(path, timeBucket, size))) {
            return;
        }
        if (m_inflightCount >= kMaxInflight) {
            return; // try again on next paint / ready signal
        }
        for (int i = 0; i < kRangeBuckets; ++i) {
            const QString key = frameKey(path, start + i, size);
            if (m_frameInflight.value(key, false) || m_frames.contains(key)) {
                continue;
            }
            m_frameInflight.insert(key, true);
            claimed.push_back(start + i);
        }
        if (claimed.isEmpty()) {
            return;
        }
        ++m_inflightCount;
    }
    QThreadPool::globalInstance()->start(new FrameRangeJob(this, path, start, kRangeBuckets, size));
}

void MediaFilmstripCache::requestPoster(const QString &path, const QSize &size)
{
    {
        QMutexLocker lock(&m_mutex);
        if (m_posterInflight.value(path, false) || m_posters.contains(posterKey(path, size))) {
            return;
        }
        m_posterInflight.insert(path, true);
    }
    QThreadPool::globalInstance()->start(new PosterJob(this, path, size));
}

void MediaFilmstripCache::finishFrameRange(const QString &path, qint64 startBucket, int count,
                                          const QSize &size,
                                          const QHash<qint64, QPixmap> &frames,
                                          bool missesAreFinal)
{
    {
        QMutexLocker lock(&m_mutex);
        for (int i = 0; i < count; ++i) {
            const qint64 bucket = startBucket + i;
            const QString key = frameKey(path, bucket, size);
            const QPixmap pm = frames.value(bucket);
            if (!pm.isNull() || missesAreFinal) {
                // An empty pixmap is a miss marker: without it a bucket the decoder could
                // not fill would be requested again on every repaint.
                m_frames.insert(key, pm);
            }
            m_frameInflight.remove(key);
        }
        m_inflightCount = std::max(0, m_inflightCount - 1);
    }
    QMetaObject::invokeMethod(
        this, [this, path]() { emit frameReady(path); }, Qt::QueuedConnection);
}

void MediaFilmstripCache::finishPosterJob(const QString &path, const QSize &size, const QPixmap &pm)
{
    const QString key = posterKey(path, size);
    {
        QMutexLocker lock(&m_mutex);
        if (!pm.isNull()) {
            m_posters.insert(key, pm);
        }
        m_posterInflight.remove(path);
    }
    if (!pm.isNull()) {
        QMetaObject::invokeMethod(
            this, [this, path]() { emit frameReady(path); }, Qt::QueuedConnection);
    }
}

} // namespace openvegas
