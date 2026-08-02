#pragma once

#include <QString>

class QPainter;
class QRect;

namespace openvegas {

/** Shared hover-scrub painting for Project Media / Explorer video thumbs. */
class MediaThumbHoverScrub {
public:
    static bool isVideoPath(const QString &path);

    /** Duration in seconds (cached). hintSec used when > 0.05 before/instead of probe. */
    static double durationSec(const QString &path, double hintSec = 0.0);

    /**
     * Paint a media thumbnail. When scrubbing a video, show frame at mouse X
     * (thumb width = full duration) and a white vertical scrub line.
     */
    static void paintThumb(QPainter *painter, const QRect &thumb, const QString &path,
                           const QString &kind, bool scrubbing, int mouseXInViewport,
                           double durationHintSec = 0.0);

    /** Map mouse X (viewport coords) inside thumb → media time. */
    static double timeAtX(const QRect &thumb, int mouseXInViewport, double durationSec);
};

} // namespace openvegas
