#pragma once

#include "model/ProjectModel.h"

namespace openvegas {

/** Segment ease 0…1 → blend weight using VideoKeyframeType (Hold / Linear / fade-like curves). */
double videoKeyframeEase(VideoKeyframeType type, double t);

PanCropKeyframe evaluatePanCrop(const EventPanCropState &state, double localTimeSec, int frameW,
                                int frameH);

TrackMotionKeyframe evaluateTrackMotion(const TrackMotionState &state, double timeSec,
                                        double aspect);

/** Hold mask paths from maskIndexAt (v1 — no path interpolation). */
const MaskKeyframe *maskHoldAt(const EventPanCropState &state, double localTimeSec);

} // namespace openvegas
