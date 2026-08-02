#include "video/VideoKeyframeEval.h"

#include "audio/FadeCurves.h"

#include <algorithm>
#include <cmath>

namespace openvegas {
namespace {

FadeCurveType mapVideoEase(VideoKeyframeType type)
{
    switch (type) {
    case VideoKeyframeType::Slow:
        return FadeCurveType::Slow;
    case VideoKeyframeType::Fast:
        return FadeCurveType::Fast;
    case VideoKeyframeType::Smooth:
        return FadeCurveType::Smooth;
    case VideoKeyframeType::Sharp:
        return FadeCurveType::Sharp;
    case VideoKeyframeType::Linear:
    case VideoKeyframeType::Hold:
    default:
        return FadeCurveType::Linear;
    }
}

template <typename T>
double segmentT(const T &a, const T &b, double timeSec)
{
    const double dt = b.timeSec - a.timeSec;
    if (dt <= 1e-12) {
        return 1.0;
    }
    return std::clamp((timeSec - a.timeSec) / dt, 0.0, 1.0);
}

double lerpD(double a, double b, double t)
{
    return a + (b - a) * t;
}

PanCropKeyframe lerpPanCrop(const PanCropKeyframe &a, const PanCropKeyframe &b, double t)
{
    PanCropKeyframe o;
    o.timeSec = lerpD(a.timeSec, b.timeSec, t);
    o.width = lerpD(a.width, b.width, t);
    o.height = lerpD(a.height, b.height, t);
    o.xCenter = lerpD(a.xCenter, b.xCenter, t);
    o.yCenter = lerpD(a.yCenter, b.yCenter, t);
    o.angleDeg = lerpD(a.angleDeg, b.angleDeg, t);
    o.rotationXCenter = lerpD(a.rotationXCenter, b.rotationXCenter, t);
    o.rotationYCenter = lerpD(a.rotationYCenter, b.rotationYCenter, t);
    o.smoothness = lerpD(a.smoothness, b.smoothness, t);
    o.type = a.type;
    return o;
}

TrackMotionKeyframe lerpMotion(const TrackMotionKeyframe &a, const TrackMotionKeyframe &b, double t)
{
    TrackMotionKeyframe o;
    o.timeSec = lerpD(a.timeSec, b.timeSec, t);
    o.positionX = lerpD(a.positionX, b.positionX, t);
    o.positionY = lerpD(a.positionY, b.positionY, t);
    o.width = lerpD(a.width, b.width, t);
    o.height = lerpD(a.height, b.height, t);
    o.rotationZ = lerpD(a.rotationZ, b.rotationZ, t);
    o.orientationZ = lerpD(a.orientationZ, b.orientationZ, t);
    o.smoothness = lerpD(a.smoothness, b.smoothness, t);
    o.type = a.type;
    return o;
}

} // namespace

double videoKeyframeEase(VideoKeyframeType type, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    if (type == VideoKeyframeType::Hold) {
        return 0.0;
    }
    if (type == VideoKeyframeType::Linear) {
        return t;
    }
    return fadeCurveAmplitude(mapVideoEase(type), t);
}

VideoKeyframeType videoKeyframeTypeFromVegasCode(int code)
{
    switch (code) {
    case 1:
        return VideoKeyframeType::Fast;
    case 2:
        return VideoKeyframeType::Slow;
    case 3:
        return VideoKeyframeType::Smooth;
    case 4:
        return VideoKeyframeType::Sharp;
    case 5:
        return VideoKeyframeType::Hold;
    default:
        return VideoKeyframeType::Linear;
    }
}

int videoKeyframeTypeToVegasCode(VideoKeyframeType type)
{
    switch (type) {
    case VideoKeyframeType::Fast:
        return 1;
    case VideoKeyframeType::Slow:
        return 2;
    case VideoKeyframeType::Smooth:
        return 3;
    case VideoKeyframeType::Sharp:
        return 4;
    case VideoKeyframeType::Hold:
        return 5;
    case VideoKeyframeType::Linear:
    default:
        return 0;
    }
}

QString videoKeyframeTypeName(VideoKeyframeType type)
{
    switch (type) {
    case VideoKeyframeType::Fast:
        return QStringLiteral("Fast");
    case VideoKeyframeType::Slow:
        return QStringLiteral("Slow");
    case VideoKeyframeType::Smooth:
        return QStringLiteral("Smooth");
    case VideoKeyframeType::Sharp:
        return QStringLiteral("Sharp");
    case VideoKeyframeType::Hold:
        return QStringLiteral("Hold");
    case VideoKeyframeType::Linear:
    default:
        return QStringLiteral("Linear");
    }
}

PanCropKeyframe evaluatePanCrop(const EventPanCropState &state, double localTimeSec, int frameW,
                                int frameH)
{
    EventPanCropState st = state;
    st.ensureDefault(frameW, frameH);
    const auto &kfs = st.positionKeyframes;
    if (kfs.size() == 1) {
        return kfs.first();
    }
    int i1 = 0;
    for (int i = 0; i < kfs.size(); ++i) {
        if (kfs[i].timeSec <= localTimeSec + 1e-9) {
            i1 = i;
        }
    }
    if (i1 >= kfs.size() - 1) {
        return kfs.last();
    }
    const PanCropKeyframe &a = kfs[i1];
    const PanCropKeyframe &b = kfs[i1 + 1];
    if (a.type == VideoKeyframeType::Hold) {
        return a;
    }
    const double u = videoKeyframeEase(a.type, segmentT(a, b, localTimeSec));
    return lerpPanCrop(a, b, u);
}

TrackMotionKeyframe evaluateTrackMotion(const TrackMotionState &state, double timeSec,
                                        double aspect)
{
    TrackMotionState st = state;
    st.ensureDefault(aspect);
    const auto &kfs = st.motionKeyframes;
    if (kfs.size() == 1) {
        return kfs.first();
    }
    int i1 = 0;
    for (int i = 0; i < kfs.size(); ++i) {
        if (kfs[i].timeSec <= timeSec + 1e-9) {
            i1 = i;
        }
    }
    if (i1 >= kfs.size() - 1) {
        return kfs.last();
    }
    const TrackMotionKeyframe &a = kfs[i1];
    const TrackMotionKeyframe &b = kfs[i1 + 1];
    if (a.type == VideoKeyframeType::Hold) {
        return a;
    }
    const double u = videoKeyframeEase(a.type, segmentT(a, b, timeSec));
    return lerpMotion(a, b, u);
}

const MaskKeyframe *maskHoldAt(const EventPanCropState &state, double localTimeSec)
{
    const int idx = state.maskIndexAt(localTimeSec);
    if (idx < 0 || idx >= state.maskKeyframes.size()) {
        return nullptr;
    }
    return &state.maskKeyframes[idx];
}

} // namespace openvegas
