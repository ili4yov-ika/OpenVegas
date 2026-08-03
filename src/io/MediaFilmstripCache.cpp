#include "MediaFilmstripCache.h"
#include "MediaThumbCache.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QProcess>
#include <QRunnable>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThreadPool>

#include <algorithm>
#include <cmath>

namespace openvegas {
namespace {

constexpr int kMaxInflight = 6;
constexpr double kBucketSec = 0.25; // quantize seeks (~4 buckets/sec)

class FrameJob : public QRunnable {
public:
    FrameJob(MediaFilmstripCache *cache, QString path, qint64 bucket, QSize size)
        : m_cache(cache)
        , m_path(std::move(path))
        , m_bucket(bucket)
        , m_size(size)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        QPixmap pm;
        const QString ffmpeg = MediaFilmstripCache::findFfmpeg();
        if (!ffmpeg.isEmpty() && QFileInfo::exists(m_path) && m_size.width() > 1
            && m_size.height() > 1) {
            const double t = double(m_bucket) * kBucketSec;
            QTemporaryDir tmp;
            if (tmp.isValid()) {
                const QString out =
                    tmp.path() + QStringLiteral("/f_%1.jpg").arg(m_bucket);
                QProcess proc;
                QStringList args;
                args << QStringLiteral("-hide_banner") << QStringLiteral("-loglevel")
                     << QStringLiteral("error") << QStringLiteral("-ss")
                     << QString::number(t, 'f', 3) << QStringLiteral("-i") << m_path
                     << QStringLiteral("-frames:v") << QStringLiteral("1")
                     << QStringLiteral("-vf")
                     << QStringLiteral("scale=%1:%2:force_original_aspect_ratio=increase,crop=%1:%2")
                            .arg(m_size.width())
                            .arg(m_size.height())
                     << QStringLiteral("-q:v") << QStringLiteral("4") << QStringLiteral("-y")
                     << out;
                proc.start(ffmpeg, args);
                if (proc.waitForFinished(20000) && proc.exitStatus() == QProcess::NormalExit
                    && proc.exitCode() == 0 && QFileInfo::exists(out)) {
                    pm = QPixmap(out);
                }
            }
        }
        if (m_cache) {
            m_cache->finishFrameJob(m_path, m_bucket, m_size, pm);
        }
    }

private:
    MediaFilmstripCache *m_cache = nullptr;
    QString m_path;
    qint64 m_bucket = 0;
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
    static QString cached;
    static bool tried = false;
    if (tried) {
        return cached;
    }
    tried = true;

    cached = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (!cached.isEmpty()) {
        return cached;
    }

#ifdef Q_OS_WIN
    const QString exeName = QStringLiteral("ffmpeg.exe");
#else
    const QString exeName = QStringLiteral("ffmpeg");
#endif

    // Bundled next to the application (installer / portable drop-in).
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        const QStringList besideApp = {
            QDir(appDir).filePath(exeName),
            QDir(appDir).filePath(QStringLiteral("ffmpeg/") + exeName),
            QDir(appDir).filePath(QStringLiteral("ffmpeg/bin/") + exeName),
            QDir(appDir).filePath(QStringLiteral("bin/") + exeName),
            QDir(appDir).filePath(QStringLiteral("tools/") + exeName),
            QDir(appDir).filePath(QStringLiteral("tools/ffmpeg/") + exeName),
            QDir(appDir).filePath(QStringLiteral("tools/ffmpeg/bin/") + exeName),
        };
        for (const QString &p : besideApp) {
            if (QFileInfo::exists(p)) {
                cached = QFileInfo(p).absoluteFilePath();
                return cached;
            }
        }
    }

    const QStringList extras = {
#ifdef Q_OS_WIN
        QStringLiteral("C:/ffmpeg/bin/ffmpeg.exe"),
        QStringLiteral("C:/ProgramData/chocolatey/bin/ffmpeg.exe"),
#else
        QStringLiteral("/usr/bin/ffmpeg"),
        QStringLiteral("/usr/local/bin/ffmpeg"),
#endif
    };
    for (const QString &p : extras) {
        if (QFileInfo::exists(p)) {
            cached = p;
            return cached;
        }
    }

#ifdef Q_OS_WIN
    // WinGet Gyan.FFmpeg layout
    const QString wingetRoot =
        QDir::homePath() + QStringLiteral("/AppData/Local/Microsoft/WinGet/Packages");
    const QDir wd(wingetRoot);
    if (wd.exists()) {
        const QFileInfoList packs =
            wd.entryInfoList({QStringLiteral("Gyan.FFmpeg*")}, QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &pack : packs) {
            const QStringList bins =
                QDir(pack.absoluteFilePath())
                    .entryList({QStringLiteral("ffmpeg-*-full_build")}, QDir::Dirs);
            for (const QString &b : bins) {
                const QString cand =
                    pack.absoluteFilePath() + QLatin1Char('/') + b + QStringLiteral("/bin/ffmpeg.exe");
                if (QFileInfo::exists(cand)) {
                    cached = cand;
                    return cached;
                }
            }
        }
    }
#endif
    return cached;
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
    const QString key = frameKey(path, timeBucket, size);
    {
        QMutexLocker lock(&m_mutex);
        if (m_frameInflight.value(key, false) || m_frames.contains(key)) {
            return;
        }
        if (m_inflightCount >= kMaxInflight) {
            return; // try again on next paint / ready signal
        }
        m_frameInflight.insert(key, true);
        ++m_inflightCount;
    }
    QThreadPool::globalInstance()->start(new FrameJob(this, path, timeBucket, size));
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

void MediaFilmstripCache::finishFrameJob(const QString &path, qint64 timeBucket, const QSize &size,
                                        const QPixmap &pm)
{
    const QString key = frameKey(path, timeBucket, size);
    {
        QMutexLocker lock(&m_mutex);
        if (!pm.isNull()) {
            m_frames.insert(key, pm);
        } else {
            // Cache miss marker (empty) to avoid hammering ffmpeg on broken seeks
            m_frames.insert(key, QPixmap());
        }
        m_frameInflight.remove(key);
        m_inflightCount = std::max(0, m_inflightCount - 1);
    }
    if (!pm.isNull()) {
        QMetaObject::invokeMethod(
            this, [this, path]() { emit frameReady(path); }, Qt::QueuedConnection);
    } else {
        // Still notify so timeline can refill queue for other buckets
        QMetaObject::invokeMethod(
            this, [this, path]() { emit frameReady(path); }, Qt::QueuedConnection);
    }
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
