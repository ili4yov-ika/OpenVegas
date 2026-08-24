#pragma once

#include "model/ProjectModel.h"

#include <QString>

namespace openvegas {

/** Segment ease 0…1 → blend weight using VideoKeyframeType (Hold / Linear / fade-like curves). */
double videoKeyframeEase(VideoKeyframeType type, double t);

/** Vegas POSK/UI codes: 0=Linear, 1=Fast, 2=Slow, 3=Smooth, 4=Sharp, 5=Hold. */
VideoKeyframeType videoKeyframeTypeFromVegasCode(int code);
int videoKeyframeTypeToVegasCode(VideoKeyframeType type);
QString videoKeyframeTypeName(VideoKeyframeType type);

PanCropKeyframe evaluatePanCrop(const EventPanCropState &state, double localTimeSec, int frameW,
                                int frameH);

TrackMotionKeyframe evaluateTrackMotion(const TrackMotionState &state, double timeSec,
                                        double aspect);

/** Hold mask paths from maskIndexAt (v1 — no path interpolation). */
const MaskKeyframe *maskHoldAt(const EventPanCropState &state, double localTimeSec);

/**
 * Mask shape at `localTimeSec`, morphed between keyframes.
 *
 * Returns false when the event has no mask. Each contour is decided on its own: one whose
 * anchor count matches the next keyframe is interpolated anchor by anchor, tangents
 * included, and one that gained or lost a point holds its earlier shape. Judging the mask
 * as a whole would freeze all of it whenever any single contour changed.
 */
bool maskAt(const EventPanCropState &state, double localTimeSec, MaskKeyframe *out);

} // namespace openvegas
