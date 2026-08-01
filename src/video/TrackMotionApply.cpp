#include "video/TrackMotionApply.h"

#include <QPainter>
#include <QtMath>

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace openvegas {

QRectF trackMotionDestRect(const TrackMotionKeyframe &kf, int frameW, int frameH, int outW,
                           int outH)
{
    const double fh = double(std::max(1, frameH));
    double px = kf.positionX * fh;
    double py = kf.positionY * fh;
    double pw = kf.width * fh;
    double ph = kf.height * fh;
    if (pw < 1.0) {
        pw = double(frameW);
    }
    if (ph < 1.0) {
        ph = fh;
    }
    // Motion view: position is offset from frame center (see TrackMotionDialog::frameRect).
    const double sx = double(outW) / double(std::max(1, frameW));
    const double sy = double(outH) / fh;
    const double cx = outW * 0.5 + px * sx;
    const double cy = outH * 0.5 + py * sy;
    const double w = pw * sx;
    const double h = ph * sy;
    return QRectF(cx - w * 0.5, cy - h * 0.5, w, h);
}

void applyTrackMotion(QPainter *painter, const QImage &layer, const TrackMotionKeyframe &kf,
                      int frameW, int frameH, int outW, int outH, qreal opacity)
{
    if (!painter || layer.isNull() || outW < 1 || outH < 1) {
        return;
    }
    const QRectF dest = trackMotionDestRect(kf, frameW, frameH, outW, outH);
    const double angleDeg = kf.orientationZ * 180.0 / M_PI;

    painter->save();
    painter->setOpacity(std::clamp(opacity, 0.0, 1.0));
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->translate(dest.center());
    painter->rotate(angleDeg);
    // rotationZ (spin) — apply after orientation for MVP
    if (std::abs(kf.rotationZ) > 1e-9) {
        painter->rotate(kf.rotationZ * 180.0 / M_PI);
    }
    painter->translate(-dest.center());
    painter->drawImage(dest, layer);
    painter->restore();
}

} // namespace openvegas
