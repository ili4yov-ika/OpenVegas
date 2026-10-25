#include "video/PanCropApply.h"

#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace openvegas {
namespace {

/**
 * The part of `source` that fills the frame, in source pixels: all of it when stretched,
 * otherwise the centred window with the frame's shape (the source covers the frame and
 * the overflow is cut off, as VEGAS's "Maintain aspect ratio" does).
 */
QRectF sourceWindow(const QImage &source, int frameW, int frameH, bool stretchToFill)
{
    const QRectF all(0.0, 0.0, source.width(), source.height());
    if (stretchToFill) {
        return all;
    }
    const double cover =
        std::max(double(frameW) / source.width(), double(frameH) / source.height());
    const double w = frameW / cover;
    const double h = frameH / cover;
    return QRectF((source.width() - w) * 0.5, (source.height() - h) * 0.5, w, h);
}

QPainterPath pathFromAnchors(const MaskPath &mp)
{
    QPainterPath path;
    if (mp.anchors.isEmpty()) {
        return path;
    }
    const MaskAnchor &a0 = mp.anchors.first();
    path.moveTo(a0.x, a0.y);
    for (int i = 1; i < mp.anchors.size(); ++i) {
        const MaskAnchor &prev = mp.anchors[i - 1];
        const MaskAnchor &cur = mp.anchors[i];
        path.cubicTo(prev.x + prev.outX, prev.y + prev.outY, cur.x + cur.inX, cur.y + cur.inY, cur.x,
                     cur.y);
    }
    if (mp.closed && mp.anchors.size() > 1) {
        const MaskAnchor &last = mp.anchors.last();
        const MaskAnchor &first = mp.anchors.first();
        path.cubicTo(last.x + last.outX, last.y + last.outY, first.x + first.inX,
                     first.y + first.inY, first.x, first.y);
        path.closeSubpath();
    }
    return path;
}

} // namespace

void applyMaskAlpha(QImage *layerArgbPremul, const MaskKeyframe &mask, int frameW, int frameH)
{
    if (!layerArgbPremul || layerArgbPremul->isNull() || mask.paths.isEmpty()) {
        return;
    }
    const int w = layerArgbPremul->width();
    const int h = layerArgbPremul->height();
    const double sx = frameW > 0 ? double(w) / double(frameW) : 1.0;
    const double sy = frameH > 0 ? double(h) / double(frameH) : 1.0;

    QImage maskImg(w, h, QImage::Format_ARGB32_Premultiplied);
    maskImg.fill(Qt::transparent);
    QPainter mp(&maskImg);
    mp.setRenderHint(QPainter::Antialiasing, true);
    mp.scale(sx, sy);

    bool anyPositive = false;
    for (const MaskPath &path : mask.paths) {
        if (path.mode == MaskPathMode::Positive) {
            anyPositive = true;
            break;
        }
    }
    if (!anyPositive) {
        // No positive paths → leave layer opaque (mask effectively off for v1).
        return;
    }

    mp.setCompositionMode(QPainter::CompositionMode_Source);
    for (const MaskPath &path : mask.paths) {
        if (path.mode == MaskPathMode::Disabled) {
            continue;
        }
        const QPainterPath pp = pathFromAnchors(path);
        if (pp.isEmpty()) {
            continue;
        }
        if (path.mode == MaskPathMode::Positive) {
            mp.setBrush(Qt::white);
            mp.setPen(Qt::NoPen);
            mp.drawPath(pp);
        }
    }
    // Subtract negative paths
    mp.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    for (const MaskPath &path : mask.paths) {
        if (path.mode != MaskPathMode::Negative) {
            continue;
        }
        const QPainterPath pp = pathFromAnchors(path);
        if (pp.isEmpty()) {
            continue;
        }
        mp.setBrush(Qt::white);
        mp.setPen(Qt::NoPen);
        mp.drawPath(pp);
    }
    mp.end();

    // Multiply layer alpha by mask luminance
    for (int y = 0; y < h; ++y) {
        QRgb *dst = reinterpret_cast<QRgb *>(layerArgbPremul->scanLine(y));
        const QRgb *ms = reinterpret_cast<const QRgb *>(maskImg.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const int ma = qAlpha(ms[x]);
            if (ma >= 255) {
                continue;
            }
            if (ma <= 0) {
                dst[x] = 0;
                continue;
            }
            const QRgb c = dst[x];
            const int a = (qAlpha(c) * ma + 127) / 255;
            const int r = (qRed(c) * ma + 127) / 255;
            const int g = (qGreen(c) * ma + 127) / 255;
            const int b = (qBlue(c) * ma + 127) / 255;
            dst[x] = qRgba(r, g, b, a);
        }
    }
}

QImage applyPanCrop(const QImage &source, const PanCropKeyframe &kfIn, int frameW, int frameH,
                    int outW, int outH, bool stretchToFill, const MaskKeyframe *maskHold)
{
    outW = std::max(1, outW);
    outH = std::max(1, outH);
    frameW = std::max(1, frameW);
    frameH = std::max(1, frameH);

    const PanCropKeyframe &kf = kfIn;

    const double absW = std::max(1.0, std::abs(kf.width));
    const double absH = std::max(1.0, std::abs(kf.height));
    const bool flipH = kf.width < 0.0;
    const bool flipV = kf.height < 0.0;

    // Drawn straight at the output size, so the source is resampled once, as VEGAS does.
    // This used to fit the (already output-sized) source to a frame-sized canvas, place it
    // there and scale the result back down: three resamplings, the first an upscale of a
    // 4K project's frame from 720p, which softened every Pan/Crop against VEGAS's render.
    QImage layer(outW, outH, QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);
    if (source.isNull()) {
        return layer;
    }
    QImage src = source;
    if (src.format() != QImage::Format_ARGB32_Premultiplied) {
        src = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    QRectF window = sourceWindow(src, frameW, frameH, stretchToFill);

    // The keyframe is a rectangle over the *source*, and what it frames is stretched to
    // fill the output. That is the whole of Pan/Crop: a smaller rectangle zooms in, a
    // larger one zooms out with the frame showing through around it, and moving it pans.
    //
    // Read bottom-up, a frame point is: un-rotated about the rotation centre, moved so the
    // rectangle centre is the origin, scaled until the rectangle is frame-sized (and
    // mirrored if a side is negative), then moved to the middle of the frame — and the
    // frame is then scaled to the output.
    const double sx = double(frameW) / absW;
    const double sy = double(frameH) / absH;
    QTransform frameToOut;
    frameToOut.scale(double(outW) / frameW, double(outH) / frameH);
    frameToOut.translate(frameW * 0.5, frameH * 0.5);
    frameToOut.scale(sx * (flipH ? -1.0 : 1.0), sy * (flipV ? -1.0 : 1.0));
    frameToOut.translate(-kf.xCenter, -kf.yCenter);
    frameToOut.translate(kf.rotationXCenter, kf.rotationYCenter);
    // Turning the rectangle one way turns what it frames the other way.
    frameToOut.rotate(-kf.angleDeg);
    frameToOut.translate(-kf.rotationXCenter, -kf.rotationYCenter);

    // Bilinear sampling only holds up to about a 2:1 reduction; a zoomed-out rectangle can
    // shrink the source far more. Then reduce it first by averaging, to about the size it
    // is drawn at, and draw that.
    const QPointF o = frameToOut.map(QPointF(0.0, 0.0));
    const double perFrameX = QLineF(o, frameToOut.map(QPointF(1.0, 0.0))).length();
    const double perFrameY = QLineF(o, frameToOut.map(QPointF(0.0, 1.0))).length();
    const double shrinkX = perFrameX * frameW / window.width();
    const double shrinkY = perFrameY * frameH / window.height();
    if (shrinkX < 0.5 || shrinkY < 0.5) {
        const double fx = std::min(1.0, shrinkX * 1.5);
        const double fy = std::min(1.0, shrinkY * 1.5);
        const int w = std::max(1, int(std::lround(src.width() * fx)));
        const int h = std::max(1, int(std::lround(src.height() * fy)));
        const double kx = double(w) / src.width();
        const double ky = double(h) / src.height();
        src = src.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        window = QRectF(window.x() * kx, window.y() * ky, window.width() * kx,
                        window.height() * ky);
    }

    QPainter p(&layer);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setTransform(frameToOut);
    p.drawImage(QRectF(0.0, 0.0, frameW, frameH), src, window);
    p.end();

    if (maskHold && !maskHold->paths.isEmpty()) {
        applyMaskAlpha(&layer, *maskHold, frameW, frameH);
    }
    return layer;
}

} // namespace openvegas
