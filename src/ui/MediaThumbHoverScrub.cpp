#include "ui/MediaThumbHoverScrub.h"
#include "io/MediaFilmstripCache.h"
#include "io/MediaMime.h"
#include "io/MediaProbe.h"
#include "io/MediaThumbCache.h"

#include <QPainter>
#include <QPen>
#include <QPoint>

#include <algorithm>
#include <cmath>

namespace openvegas {

bool MediaThumbHoverScrub::isVideoPath(const QString &path)
{
    if (path.isEmpty() || !MediaMime::isMediaFile(path)) {
        return false;
    }
    return MediaMime::guessKind(path) == QLatin1String("video");
}

double MediaThumbHoverScrub::durationSec(const QString &path, double hintSec)
{
    if (hintSec > 0.05) {
        return hintSec;
    }
    const double d = MediaProbe::cachedDurationSec(path);
    return d > 0.05 ? d : 8.0;
}

double MediaThumbHoverScrub::timeAtX(const QRect &thumb, int mouseXInViewport, double durationSec)
{
    if (thumb.width() <= 1 || durationSec <= 0.0) {
        return 0.0;
    }
    const double frac =
        std::clamp((mouseXInViewport - thumb.left()) / double(thumb.width() - 1), 0.0, 1.0);
    // Keep seek slightly inside the last frame.
    return std::min(frac * durationSec, std::max(0.0, durationSec - 0.04));
}

void MediaThumbHoverScrub::paintThumb(QPainter *painter, const QRect &thumb, const QString &path,
                                      const QString &kind, bool scrubbing, int mouseXInViewport,
                                      double durationHintSec)
{
    if (!painter || thumb.isEmpty()) {
        return;
    }

    const QString useKind = kind.isEmpty() ? MediaMime::guessKind(path) : kind;
    const bool doScrub = scrubbing && isVideoPath(path);

    if (doScrub) {
        const double dur = durationSec(path, durationHintSec);
        const double t = timeAtX(thumb, mouseXInViewport, dur);
        // Scrub frames are plain (no film ladders); ladders only in static thumbs.
        QPixmap frame = MediaFilmstripCache::instance().frameIfReady(path, t, thumb.size());
        if (frame.isNull()) {
            frame = MediaFilmstripCache::instance().posterIfReady(path, thumb.size());
        }
        if (!frame.isNull()) {
            painter->drawPixmap(thumb, frame);
        } else {
            MediaThumbCache::instance().iconFor(path, thumb.size(), useKind).paint(painter, thumb);
        }

        const int x = std::clamp(mouseXInViewport, thumb.left(), thumb.right());
        painter->save();
        painter->setPen(QPen(QColor(0xff, 0xff, 0xff), 1));
        painter->drawLine(x, thumb.top(), x, thumb.bottom());
        painter->restore();
        return;
    }

    // Static video: frame from cache + sprocket ladders (timeline must stay plain).
    MediaThumbCache::instance().iconFor(path, thumb.size(), useKind).paint(painter, thumb);
    if (isVideoPath(path) || useKind == QLatin1String("video")) {
        MediaThumbCache::paintFilmSprockets(painter, thumb);
    }
}

} // namespace openvegas
