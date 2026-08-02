#include "video/PanCropApply.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace openvegas {
namespace {

QImage fitSourceToFrame(const QImage &source, int frameW, int frameH, bool stretchToFill)
{
    QImage canvas(frameW, frameH, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    if (source.isNull() || frameW < 1 || frameH < 1) {
        return canvas;
    }
    QImage src = source;
    if (src.format() != QImage::Format_ARGB32_Premultiplied) {
        src = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    QPainter p(&canvas);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (stretchToFill) {
        p.drawImage(QRect(0, 0, frameW, frameH), src);
    } else {
        QImage scaled =
            src.scaled(frameW, frameH, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const int x = (scaled.width() - frameW) / 2;
        const int y = (scaled.height() - frameH) / 2;
        p.drawImage(0, 0, scaled, x, y, frameW, frameH);
    }
    p.end();
    return canvas;
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

namespace {

/**
 * VEG Event Pan/Crop for “Match Output Aspect” often stores width/height/center in
 * *media* pixels (e.g. 2880×2160 crop of 3840×2160 into a 640×480 project). Our
 * compositor works in project-frame pixels — map oversized keyframes down.
 */
PanCropKeyframe toProjectSpace(const PanCropKeyframe &kf, int frameW, int frameH)
{
    const double aw = std::abs(kf.width);
    const double ah = std::abs(kf.height);
    if (aw <= frameW * 1.25 && ah <= frameH * 1.25) {
        return kf;
    }

    PanCropKeyframe out = kf;
    const double frameAr = double(frameW) / double(std::max(1, frameH));
    const double kfAr = aw / std::max(1e-9, ah);

    // Crop AR matches project → that media rect fills the frame (Match Output Aspect).
    if (std::abs(kfAr - frameAr) < 0.05) {
        out.width = (kf.width < 0.0) ? -double(frameW) : double(frameW);
        out.height = (kf.height < 0.0) ? -double(frameH) : double(frameH);
        out.xCenter = frameW * 0.5;
        out.yCenter = frameH * 0.5;
        out.rotationXCenter = out.xCenter;
        out.rotationYCenter = out.yCenter;
        return out;
    }

    // Otherwise scale the crop into the frame, centered.
    const double s = std::min(double(frameW) / aw, double(frameH) / ah);
    out.width = kf.width * s;
    out.height = kf.height * s;
    out.xCenter = frameW * 0.5;
    out.yCenter = frameH * 0.5;
    out.rotationXCenter = out.xCenter;
    out.rotationYCenter = out.yCenter;
    return out;
}

} // namespace

QImage applyPanCrop(const QImage &source, const PanCropKeyframe &kfIn, int frameW, int frameH,
                    int outW, int outH, bool stretchToFill, const MaskKeyframe *maskHold)
{
    outW = std::max(1, outW);
    outH = std::max(1, outH);
    frameW = std::max(1, frameW);
    frameH = std::max(1, frameH);

    const PanCropKeyframe kf = toProjectSpace(kfIn, frameW, frameH);

    // Media-space Match Output Aspect must keep AR (center-crop), not stretch.
    const double aw0 = std::abs(kfIn.width);
    const double ah0 = std::abs(kfIn.height);
    const bool mediaSpace = (aw0 > frameW * 1.25 || ah0 > frameH * 1.25);
    const bool stretch = stretchToFill && !mediaSpace;

    const QImage fitted = fitSourceToFrame(source, frameW, frameH, stretch);

    const double absW = std::max(1.0, std::abs(kf.width));
    const double absH = std::max(1.0, std::abs(kf.height));
    const bool flipH = kf.width < 0.0;
    const bool flipV = kf.height < 0.0;

    // Work in project-frame pixels, then scale to out.
    QImage layer(frameW, frameH, QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);

    QPainter p(&layer);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QPointF center(kf.xCenter, kf.yCenter);
    const QPointF rotCenter(kf.rotationXCenter, kf.rotationYCenter);

    p.translate(rotCenter);
    p.rotate(kf.angleDeg);
    p.translate(-rotCenter);

    p.translate(center);
    p.scale(flipH ? -1.0 : 1.0, flipV ? -1.0 : 1.0);
    p.translate(-center);

    // Draw the crop region from fitted source into the crop rect on layer.
    // Source crop rect in fitted image = crop rect in project frame (identity mapping).
    const QRectF crop(center.x() - absW * 0.5, center.y() - absH * 0.5, absW, absH);
    p.drawImage(crop, fitted, crop);
    p.end();

    if (maskHold && !maskHold->paths.isEmpty()) {
        applyMaskAlpha(&layer, *maskHold, frameW, frameH);
    }

    if (outW == frameW && outH == frameH) {
        return layer;
    }
    return layer.scaled(outW, outH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

} // namespace openvegas
