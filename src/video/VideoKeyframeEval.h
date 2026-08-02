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

} // namespace openvegas
