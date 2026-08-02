#include "MediaThumbCache.h"
#include "MediaFilmstripCache.h"

#include <QDateTime>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QMetaObject>
#include <QPainter>
#include <QProcess>
#include <QRunnable>
#include <QTemporaryDir>
#include <QThreadPool>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shlobj.h>
#  include <shobjidl.h>
#endif

namespace openvegas {
namespace {

QString cacheKey(const QString &path, const QSize &size)
{
    // "|raw" — thumbs without baked sprockets (timeline reuses this cache as poster).
    return path + QLatin1Char('|') + QString::number(size.width()) + QLatin1Char('x')
           + QString::number(size.height()) + QStringLiteral("|raw");
}

bool isImageExt(const QString &ext)
{
    static const QStringList k = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                  QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("webp"),
                                  QStringLiteral("tif"), QStringLiteral("tiff")};
    return k.contains(ext);
}

bool isVideoExt(const QString &ext)
{
    static const QStringList k = {QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("mkv"),
                                  QStringLiteral("avi"), QStringLiteral("m4v"), QStringLiteral("wmv"),
                                  QStringLiteral("webm"), QStringLiteral("mpg"), QStringLiteral("mpeg")};
    return k.contains(ext);
}

/** Vegas-style film sprocket "ladders" on left/right edges of a video thumb. */
void drawFilmSprockets(QPainter &p, const QRect &r)
{
    if (r.width() < 16 || r.height() < 12) {
        return;
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    const int railW = qBound(3, r.width() / 22, 6);
    const int holeH = qBound(2, r.height() / 14, 5);
    const int gap = qMax(1, holeH * 2 / 3);
    const int pitch = holeH + gap;
    const int holeW = qMax(2, railW - 1);
    const int insetX = 1;
    const int topPad = qMax(1, (r.height() % pitch) / 2);

    // Slightly darker film rails behind the perforations.
    p.fillRect(QRect(r.left(), r.top(), railW + 1, r.height()), QColor(0, 0, 0, 55));
    p.fillRect(QRect(r.right() - railW, r.top(), railW + 1, r.height()), QColor(0, 0, 0, 55));

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xb8, 0xb8, 0xb8));
    for (int y = r.top() + topPad; y + holeH <= r.bottom(); y += pitch) {
        p.drawRoundedRect(QRect(r.left() + insetX, y, holeW, holeH), 1, 1);
        p.drawRoundedRect(QRect(r.right() - insetX - holeW + 1, y, holeW, holeH), 1, 1);
    }
    p.restore();
}

QString kindFromPath(const QString &path, const QString &hint)
{
    if (!hint.isEmpty())
        return hint;
    const QString ext = QFileInfo(path).suffix().toLower();
    if (isImageExt(ext))
        return QStringLiteral("still");
    if (isVideoExt(ext))
        return QStringLiteral("video");
    if (ext == QLatin1String("wav") || ext == QLatin1String("mp3") || ext == QLatin1String("flac")
        || ext == QLatin1String("aac") || ext == QLatin1String("ogg") || ext == QLatin1String("m4a"))
        return QStringLiteral("audio");
    return QStringLiteral("other");
}

#ifdef Q_OS_WIN
QPixmap pixmapFromHBITMAP(HBITMAP hbmp)
{
    BITMAP bm{};
    if (!GetObject(hbmp, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0)
        return {};

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = bm.bmWidth;
    bmi.bmiHeader.biHeight = -bm.bmHeight; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    QImage img(bm.bmWidth, bm.bmHeight, QImage::Format_ARGB32_Premultiplied);
    HDC hdc = GetDC(nullptr);
    const int ok = GetDIBits(hdc, hbmp, 0, bm.bmHeight, img.bits(), &bmi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, hdc);
    if (ok == 0)
        return {};
    return QPixmap::fromImage(img);
}
#endif

class ThumbLoadJob : public QRunnable {
public:
    ThumbLoadJob(MediaThumbCache *cache, QString path, QSize size, QString kind)
        : m_cache(cache)
        , m_path(std::move(path))
        , m_size(size)
        , m_kind(std::move(kind))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        if (!m_cache)
            return;
        m_cache->finishAsyncLoad(m_path, m_size, m_kind);
    }

private:
    MediaThumbCache *m_cache = nullptr;
    QString m_path;
    QSize m_size;
    QString m_kind;
};

} // namespace

MediaThumbCache &MediaThumbCache::instance()
{
    static MediaThumbCache cache;
    return cache;
}

MediaThumbCache::MediaThumbCache(QObject *parent)
    : QObject(parent)
{
}

void MediaThumbCache::paintFilmSprockets(QPainter *painter, const QRect &rect)
{
    if (!painter || rect.isEmpty()) {
        return;
    }
    drawFilmSprockets(*painter, rect);
}

void MediaThumbCache::invalidate(const QString &path)
{
    QMutexLocker lock(&m_mutex);
    if (path.isEmpty()) {
        m_cache.clear();
        m_inflight.clear();
        return;
    }
    const QString prefix = path + QLatin1Char('|');
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if (it.key().startsWith(prefix) || it.key().startsWith(path))
            it = m_cache.erase(it);
        else
            ++it;
    }
    m_inflight.remove(path);
}

QIcon MediaThumbCache::iconFor(const QString &path, const QSize &size, const QString &kindHint)
{
    if (path.isEmpty())
        return QIcon(placeholder(kindHint.isEmpty() ? QStringLiteral("other") : kindHint, size, 0));

    const QFileInfo fi(path);
    const qint64 mtime = fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : 0;
    const QString key = cacheKey(path, size);
    const QString kind = kindFromPath(path, kindHint);

    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_cache.constFind(key);
        if (it != m_cache.cend() && it->mtime == mtime && !it->pm.isNull() && it->ready)
            return QIcon(it->pm);
        if (it != m_cache.cend() && it->mtime == mtime && !it->pm.isNull() && !it->ready) {
            // Placeholder already shown; keep requesting only once.
            return QIcon(it->pm);
        }
    }

    // Images: try sync load (fast for PNG/JPEG thumbs).
    if (kind == QLatin1String("still") || isImageExt(fi.suffix().toLower())) {
        QPixmap pm = loadImageFile(path, size);
        if (!pm.isNull()) {
            QMutexLocker lock(&m_mutex);
            m_cache.insert(key, {pm, mtime, size, true});
            return QIcon(pm);
        }
    }

    const QPixmap ph = placeholder(kind, size, static_cast<int>(qHash(path)));
    bool needAsync = false;
    {
        QMutexLocker lock(&m_mutex);
        if (!m_cache.contains(key))
            m_cache.insert(key, {ph, mtime, size, false});
        if (!m_inflight.value(path, false)) {
            m_inflight.insert(path, true);
            needAsync = true;
        }
    }
    if (needAsync)
        requestAsync(path, size, kind);
    return QIcon(ph);
}

QPixmap MediaThumbCache::pixmapIfReady(const QString &path, const QSize &size, const QString &kindHint)
{
    if (path.isEmpty()) {
        return {};
    }

    const QFileInfo fi(path);
    const qint64 mtime = fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : 0;
    const QString key = cacheKey(path, size);
    const QString kind = kindFromPath(path, kindHint);

    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_cache.constFind(key);
        if (it != m_cache.cend() && it->mtime == mtime && it->ready && !it->pm.isNull()) {
            return it->pm;
        }
        if (it != m_cache.cend() && it->mtime == mtime && !it->ready) {
            return {};
        }
    }

    if (kind == QLatin1String("still") || isImageExt(fi.suffix().toLower())) {
        QPixmap pm = loadImageFile(path, size);
        if (!pm.isNull()) {
            QMutexLocker lock(&m_mutex);
            m_cache.insert(key, {pm, mtime, size, true});
            return pm;
        }
    }

    bool needAsync = false;
    {
        QMutexLocker lock(&m_mutex);
        if (!m_cache.contains(key)) {
            m_cache.insert(key, {QPixmap{}, mtime, size, false});
        }
        if (!m_inflight.value(path, false)) {
            m_inflight.insert(path, true);
            needAsync = true;
        }
    }
    if (needAsync) {
        requestAsync(path, size, kind);
    }
    return {};
}

void MediaThumbCache::requestAsync(const QString &path, const QSize &size, const QString &kindHint)
{
    QThreadPool::globalInstance()->start(new ThumbLoadJob(this, path, size, kindHint));
}

void MediaThumbCache::finishAsyncLoad(const QString &path, const QSize &size, const QString &kindHint)
{
    QPixmap pm = loadSync(path, size, kindHint);
    const QFileInfo fi(path);
    const qint64 mtime = fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : 0;
    const QString key = cacheKey(path, size);
    bool ready = !pm.isNull();
    {
        QMutexLocker lock(&m_mutex);
        if (ready)
            m_cache.insert(key, {pm, mtime, size, true});
        m_inflight.remove(path);
    }
    if (ready) {
        QMetaObject::invokeMethod(
            this, [this, path]() { emit thumbnailReady(path); }, Qt::QueuedConnection);
    }
}

QPixmap MediaThumbCache::loadSync(const QString &path, const QSize &size, const QString &kindHint)
{
    const QString kind = kindFromPath(path, kindHint);
    if (kind == QLatin1String("still") || isImageExt(QFileInfo(path).suffix().toLower())) {
        QPixmap pm = loadImageFile(path, size);
        if (!pm.isNull())
            return pm;
    }

    const bool video =
        kind == QLatin1String("video") || isVideoExt(QFileInfo(path).suffix().toLower());
    if (video) {
        // Plain frame only — sprocket ladders are overlaid in Project Media / Explorer UI.
        QPixmap frame = loadFfmpegVideoThumb(path, size);
        if (frame.isNull()) {
            frame = loadShellThumbnail(path, size);
        }
        return frame;
    }

    return loadShellThumbnail(path, size);
}

QPixmap MediaThumbCache::loadFfmpegVideoThumb(const QString &path, const QSize &size) const
{
    if (path.isEmpty() || size.width() < 2 || size.height() < 2 || !QFileInfo::exists(path)) {
        return {};
    }
    const QString ffmpeg = MediaFilmstripCache::findFfmpeg();
    if (ffmpeg.isEmpty()) {
        return {};
    }

    // Representative poster: ~1s in (Vegas-like), then first frame.
    const double times[] = {1.0, 0.0};
    for (double t : times) {
        QTemporaryDir tmp;
        if (!tmp.isValid()) {
            return {};
        }
        const QString out = tmp.path() + QStringLiteral("/poster.jpg");
        QProcess proc;
        QStringList args;
        args << QStringLiteral("-hide_banner") << QStringLiteral("-loglevel")
             << QStringLiteral("error");
        if (t > 0.0) {
            args << QStringLiteral("-ss") << QString::number(t, 'f', 3);
        }
        args << QStringLiteral("-i") << path << QStringLiteral("-frames:v") << QStringLiteral("1")
             << QStringLiteral("-vf")
             << QStringLiteral("scale=%1:%2:force_original_aspect_ratio=increase,crop=%1:%2")
                    .arg(size.width())
                    .arg(size.height())
             << QStringLiteral("-q:v") << QStringLiteral("4") << QStringLiteral("-y") << out;
        proc.start(ffmpeg, args);
        if (proc.waitForFinished(20000) && proc.exitStatus() == QProcess::NormalExit
            && proc.exitCode() == 0 && QFileInfo::exists(out)) {
            QPixmap pm(out);
            if (!pm.isNull()) {
                if (pm.size() == size) {
                    return pm;
                }
                return pm.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }
        }
    }
    return {};
}

QPixmap MediaThumbCache::loadImageFile(const QString &path, const QSize &size) const
{
    QImageReader reader(path);
    if (!reader.canRead())
        return {};
    const QSize orig = reader.size();
    if (orig.isValid() && !orig.isEmpty()) {
        QSize scaled = orig;
        scaled.scale(size, Qt::KeepAspectRatioByExpanding);
        reader.setScaledSize(scaled);
    } else {
        reader.setScaledSize(size);
    }
    QImage img = reader.read();
    if (img.isNull())
        return {};
    QPixmap canvas(size);
    canvas.fill(QColor(28, 30, 34));
    QPainter p(&canvas);
    QPixmap pm = QPixmap::fromImage(img);
    const QSize fit = pm.size().scaled(size, Qt::KeepAspectRatio);
    const QRect r((size.width() - fit.width()) / 2, (size.height() - fit.height()) / 2, fit.width(),
                  fit.height());
    p.drawPixmap(r, pm.scaled(fit, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    p.end();
    return canvas;
}

QPixmap MediaThumbCache::loadShellThumbnail(const QString &path, const QSize &size) const
{
#ifdef Q_OS_WIN
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    IShellItem *item = nullptr;
    const HRESULT hrItem =
        SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, IID_PPV_ARGS(&item));
    if (FAILED(hrItem) || !item) {
        if (SUCCEEDED(hrInit) || hrInit == S_FALSE)
            CoUninitialize();
        return {};
    }

    IShellItemImageFactory *factory = nullptr;
    HRESULT hr = item->QueryInterface(IID_PPV_ARGS(&factory));
    item->Release();
    if (FAILED(hr) || !factory) {
        if (SUCCEEDED(hrInit) || hrInit == S_FALSE)
            CoUninitialize();
        return {};
    }

    SIZE sz{size.width(), size.height()};
    HBITMAP hbmp = nullptr;
    hr = factory->GetImage(sz, SIIGBF_RESIZETOFIT | SIIGBF_BIGGERSIZEOK | SIIGBF_THUMBNAILONLY, &hbmp);
    if (FAILED(hr) || !hbmp)
        hr = factory->GetImage(sz, SIIGBF_RESIZETOFIT | SIIGBF_BIGGERSIZEOK, &hbmp);
    factory->Release();

    QPixmap pm;
    if (SUCCEEDED(hr) && hbmp) {
        pm = pixmapFromHBITMAP(hbmp);
        DeleteObject(hbmp);
    }

    if (SUCCEEDED(hrInit) || hrInit == S_FALSE)
        CoUninitialize();

    if (pm.isNull())
        return {};
    if (pm.size() == size)
        return pm;

    QPixmap canvas(size);
    canvas.fill(QColor(28, 30, 34));
    QPainter p(&canvas);
    const QSize fit = pm.size().scaled(size, Qt::KeepAspectRatio);
    const QRect r((size.width() - fit.width()) / 2, (size.height() - fit.height()) / 2, fit.width(),
                  fit.height());
    p.drawPixmap(r, pm.scaled(fit, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    p.end();
    return canvas;
#else
    Q_UNUSED(path);
    Q_UNUSED(size);
    return {};
#endif
}

QPixmap MediaThumbCache::placeholder(const QString &kind, const QSize &size, int variant) const
{
    QPixmap pm(size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r(0.5, 0.5, size.width() - 1.0, size.height() - 1.0);

    if (kind == QLatin1String("audio")) {
        p.fillRect(r, QColor(36, 40, 48));
        p.setPen(QPen(QColor(70, 120, 200), 1.2));
        p.drawRoundedRect(r, 4, 4);
        p.setPen(QColor(160, 190, 255));
        p.setFont(QFont(QStringLiteral("Segoe UI"), qMax(8, size.height() / 5), QFont::DemiBold));
        p.drawText(r, Qt::AlignCenter, QStringLiteral("WAV"));
        return pm;
    }

    if (kind == QLatin1String("video")) {
        // Plain placeholder; sprocket ladders are painted by MediaThumbHoverScrub.
        p.fillRect(r, QColor(0x2a, 0x30, 0x3a));
        p.setPen(QPen(QColor(0x70, 0x78, 0x84), 1));
        p.drawRoundedRect(r, 2, 2);
        p.setPen(QColor(0xd0, 0xd4, 0xda));
        p.setFont(QFont(QStringLiteral("Segoe UI"), qMax(10, size.height() / 3), QFont::Bold));
        p.drawText(QRect(0, 0, size.width(), size.height()), Qt::AlignCenter, QStringLiteral("V"));
        return pm;
    }

    const int v = qAbs(variant) % 5;
    const QColor c1 = QColor::fromHsv(200 + v * 12, 90, 55);
    const QColor c2 = QColor::fromHsv(210 + v * 8, 70, 35);
    QLinearGradient g(r.topLeft(), r.bottomRight());
    g.setColorAt(0, c1);
    g.setColorAt(1, c2);
    p.fillRect(r, g);
    p.setPen(QPen(QColor(255, 255, 255, 40), 1));
    p.drawRoundedRect(r, 3, 3);
    p.setPen(QColor(255, 255, 255, 180));
    p.setFont(QFont(QStringLiteral("Segoe UI"), qMax(10, size.height() / 4), QFont::Bold));
    p.drawText(r, Qt::AlignCenter, QStringLiteral("V"));
    return pm;
}

} // namespace openvegas
