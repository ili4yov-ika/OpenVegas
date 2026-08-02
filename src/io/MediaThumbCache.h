#pragma once

#include <QHash>
#include <QIcon>
#include <QMutex>
#include <QObject>
#include <QPixmap>
#include <QRect>
#include <QSize>
#include <QString>

class QPainter;

namespace openvegas {

/**
 * Cached thumbnails for Explorer / Project Media.
 * Images: QImageReader. Video: ffmpeg frame (+ Shell fallback on Windows).
 */
class MediaThumbCache : public QObject {
    Q_OBJECT
public:
    static MediaThumbCache &instance();

    /** Sync lookup; may return a placeholder and queue async load. */
    QIcon iconFor(const QString &path, const QSize &size, const QString &kindHint = {});

    /**
     * Timeline-safe lookup: returns a real pixmap only when decode finished.
     * Null while loading (or on failure); queues async load like iconFor.
     */
    QPixmap pixmapIfReady(const QString &path, const QSize &size, const QString &kindHint = {});

    /** Clear one path or everything. */
    void invalidate(const QString &path = {});

    /**
     * Vegas-style film sprocket "ladders" for Project Media / Explorer cards only.
     * Do not bake into cached pixmaps used by the timeline filmstrip.
     */
    static void paintFilmSprockets(QPainter *painter, const QRect &rect);

    /** Called from worker threads after async decode. */
    void finishAsyncLoad(const QString &path, const QSize &size, const QString &kindHint);

signals:
    void thumbnailReady(const QString &path);

private:
    explicit MediaThumbCache(QObject *parent = nullptr);

    struct Entry {
        QPixmap pm;
        qint64 mtime = 0;
        QSize size;
        bool ready = false;
    };

    QPixmap loadSync(const QString &path, const QSize &size, const QString &kindHint);
    QPixmap loadImageFile(const QString &path, const QSize &size) const;
    QPixmap loadShellThumbnail(const QString &path, const QSize &size) const;
    /** Decode a representative video frame via ffmpeg (poster for Project Media / Explorer). */
    QPixmap loadFfmpegVideoThumb(const QString &path, const QSize &size) const;
    QPixmap placeholder(const QString &kind, const QSize &size, int variant) const;
    void requestAsync(const QString &path, const QSize &size, const QString &kindHint);

    QMutex m_mutex;
    QHash<QString, Entry> m_cache;
    QHash<QString, bool> m_inflight;
};

} // namespace openvegas
