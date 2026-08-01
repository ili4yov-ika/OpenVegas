#pragma once

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QPixmap>
#include <QSize>
#include <QString>

namespace openvegas {

/**
 * Time-accurate video frame tiles for timeline filmstrips (Vegas-style).
 * Prefers ffmpeg seek+scale; falls back to a single Shell/poster thumb.
 */
class MediaFilmstripCache : public QObject {
    Q_OBJECT
public:
    static MediaFilmstripCache &instance();

    /**
     * Return a decoded frame for media time if ready; otherwise queue async load
     * and return null (or optional poster while waiting).
     */
    QPixmap frameIfReady(const QString &path, double timeSec, const QSize &size);

    /** Single poster (Shell) used as interim / ffmpeg-missing fallback. */
    QPixmap posterIfReady(const QString &path, const QSize &size);

    void invalidate(const QString &path = {});

    void finishFrameJob(const QString &path, qint64 timeBucket, const QSize &size, const QPixmap &pm);
    void finishPosterJob(const QString &path, const QSize &size, const QPixmap &pm);

    static QString findFfmpeg();

signals:
    void frameReady(const QString &path);

private:
    explicit MediaFilmstripCache(QObject *parent = nullptr);

    static QString frameKey(const QString &path, qint64 timeBucket, const QSize &size);
    static QString posterKey(const QString &path, const QSize &size);
    static qint64 bucketForTime(double timeSec);

    void requestFrame(const QString &path, qint64 timeBucket, const QSize &size);
    void requestPoster(const QString &path, const QSize &size);

    QMutex m_mutex;
    QHash<QString, QPixmap> m_frames;
    QHash<QString, bool> m_frameInflight;
    QHash<QString, QPixmap> m_posters;
    QHash<QString, bool> m_posterInflight;
    int m_inflightCount = 0;
};

} // namespace openvegas
