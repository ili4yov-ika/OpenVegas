#pragma once

#include "model/ProjectModel.h"

#include <QImage>
#include <QSize>

namespace openvegas {

class VideoCompositor {
public:
    /**
     * Compose program monitor frame at timeline time t into out size (project AR content).
     * Returns null image when layers exist but source frames are still decoding (caller holds last).
     * @param softRealtime if true, use nearest cached source frames (A/V sync during play).
     */
    static QImage compose(const ProjectModel &model, double t, const QSize &out,
                          bool softRealtime = false);

    /** Event opacity × fade in/out (same curves as AudioGraph). */
    static double eventOpacityAt(const TrackEvent &ev, double timelineSec);

    /** Mute + solo among video tracks (mirror of isTrackAudible scoped to video). */
    static bool isVideoTrackVisible(const ProjectModel &model, int trackIndex);

    /** Prefetch decode for video events near t (forward-biased during play). */
    static void prefetchAround(const ProjectModel &model, double t, const QSize &out,
                               double behindSec = 0.25, double aheadSec = 1.25);

    /** Snap timeline seconds to project frame index (audio-clock presentation). */
    static double quantizeToFrame(double sec, double fps);
};

} // namespace openvegas
