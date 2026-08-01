#pragma once

#include "model/ProjectModel.h"

#include <QImage>
#include <QRectF>

class QPainter;

namespace openvegas {

/**
 * Place a layer (already Pan/Crop'd to out frame) onto canvas using Track Motion KF.
 * Motion units are height-normalized (× frameH → pixels), matching TrackMotionDialog.
 */
void applyTrackMotion(QPainter *painter, const QImage &layer, const TrackMotionKeyframe &kf,
                      int frameW, int frameH, int outW, int outH, qreal opacity = 1.0);

/** Destination rect of the motion quad in out-pixel space (unrotated AABB of center/size). */
QRectF trackMotionDestRect(const TrackMotionKeyframe &kf, int frameW, int frameH, int outW,
                           int outH);

} // namespace openvegas
