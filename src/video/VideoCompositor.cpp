#include "video/VideoCompositor.h"

#include "audio/FadeCurves.h"
#include "video/ColorCorrectorApply.h"
#include "video/PanCropApply.h"
#include "video/TitlesTextApply.h"
#include "video/TrackMotionApply.h"
#include "video/VideoFrameCache.h"
#include "video/VideoKeyframeEval.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace openvegas {

double VideoCompositor::eventOpacityAt(const TrackEvent &ev, double timelineSec)
{
    const double local = timelineSec - ev.startSec;
    if (local < -1e-9 || local >= ev.lengthSec) {
        return 0.0;
    }
    double fade = 1.0;
    if (ev.fadeInSec > 1e-9 && local < ev.fadeInSec) {
        fade *= fadeCurveAmplitude(ev.fadeInCurve, local / ev.fadeInSec);
    }
    const double fromEnd = ev.lengthSec - local;
    if (ev.fadeOutSec > 1e-9 && fromEnd < ev.fadeOutSec) {
        fade *= 1.0 - fadeCurveAmplitude(ev.fadeOutCurve, 1.0 - fromEnd / ev.fadeOutSec);
    }
    return std::clamp(ev.opacity * fade, 0.0, 1.0);
}

bool VideoCompositor::isVideoTrackVisible(const ProjectModel &model, int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= model.tracks().size()) {
        return false;
    }
    const Track &t = model.tracks()[trackIndex];
    if (t.kind != TrackKind::Video || t.muted) {
        return false;
    }
    bool anySolo = false;
    for (const Track &tr : model.tracks()) {
        if (tr.kind == TrackKind::Video && tr.solo) {
            anySolo = true;
            break;
        }
    }
    if (anySolo && !t.solo) {
        return false;
    }
    return true;
}

namespace {

QSize composeSize(const ProjectModel &model, const QSize &out)
{
    int w = std::max(2, out.width());
    int h = std::max(2, out.height());
    const int fw = std::max(1, model.frameWidth());
    const int fh = std::max(1, model.frameHeight());
    const double ar = double(fw) / double(fh);
    const double viewAr = double(w) / double(h);
    if (viewAr > ar) {
        w = std::max(2, int(std::lround(h * ar)));
    } else {
        h = std::max(2, int(std::lround(w / ar)));
    }
    return VideoFrameCache::cappedSize(QSize(w, h));
}

} // namespace

double VideoCompositor::quantizeToFrame(double sec, double fps)
{
    fps = std::clamp(fps, 1.0, 120.0);
    const qint64 frame = qint64(std::floor(std::max(0.0, sec) * fps + 1e-9));
    return double(frame) / fps;
}

QImage VideoCompositor::compose(const ProjectModel &model, double t, const QSize &out,
                                bool softRealtime)
{
    const QSize sz = composeSize(model, out);
    QImage canvas(sz, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::black);

    const int fw = std::max(1, model.frameWidth());
    const int fh = std::max(1, model.frameHeight());
    const double aspect = double(fw) / double(fh);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    int hitCount = 0;
    int paintedCount = 0;

    // Bottom-up: reverse track order (UI list is top→bottom).
    const auto &tracks = model.tracks();
    for (int ti = tracks.size() - 1; ti >= 0; --ti) {
        if (!isVideoTrackVisible(model, ti)) {
            continue;
        }
        const Track &track = tracks[ti];
        const TrackMotionKeyframe motionKf = evaluateTrackMotion(track.motion, t, aspect);

        // Events on a track: draw in start order so later overlap (CF) paints on top.
        QVector<const TrackEvent *> hits;
        for (const TrackEvent &ev : track.events) {
            if (!isVideoFamily(ev.mediaKind)) {
                continue;
            }
            if (t + 1e-9 < ev.startSec || t >= ev.startSec + ev.lengthSec) {
                continue;
            }
            hits.push_back(&ev);
        }
        std::sort(hits.begin(), hits.end(),
                  [](const TrackEvent *a, const TrackEvent *b) { return a->startSec < b->startSec; });

        for (const TrackEvent *evp : hits) {
            const TrackEvent &ev = *evp;
            const double opacity = eventOpacityAt(ev, t);
            if (opacity <= 1e-6) {
                continue;
            }
            ++hitCount;
            const QString path = model.mediaPathForEvent(ev);
            const double eventLocal = std::max(0.0, ev.eventLocalSec(t));
            const double mediaTime = ev.sourceTimeSec(t);

            QImage src;
            if (!path.isEmpty()) {
                const bool still = ev.mediaKind == EventMediaKind::Still
                                   || ev.mediaKind == EventMediaKind::Title;
                src = VideoFrameCache::instance().frameIfReady(path, mediaTime, sz, still,
                                                                softRealtime);
            } else {
                // No media file: a generator event synthesizes its own picture.
                for (const FxSlot &slot : ev.fxChain) {
                    if (isTitlesTextName(slot.displayName)) {
                        const double progress =
                            std::clamp(eventLocal / std::max(0.05, ev.lengthSec), 0.0, 1.0);
                        src = renderTitlesText(titlesTextFromSlot(slot), sz, progress);
                        break;
                    }
                }
            }
            if (src.isNull()) {
                continue;
            }

            EventPanCropState pan = ev.panCrop;
            pan.ensureDefault(fw, fh);
            // Pan/Crop KF are event-local (not reversed source time).
            const PanCropKeyframe pkf = evaluatePanCrop(pan, eventLocal, fw, fh);
            const MaskKeyframe *mask =
                pan.maskEnabled ? maskHoldAt(pan, eventLocal) : nullptr;

            QImage layer = applyPanCrop(src, pkf, fw, fh, sz.width(), sz.height(),
                                        pan.stretchToFillFrame, mask);
            // Event then track FX (builtins + OFX / emulated).
            applyVideoFxChain(&layer, ev.fxChain, eventLocal);
            applyVideoFxChain(&layer, track.fxChain, mediaTime);
            applyTrackMotion(&painter, layer, motionKf, fw, fh, sz.width(), sz.height(), opacity);
            ++paintedCount;
        }
    }

    painter.end();
    if (hitCount > 0 && paintedCount == 0) {
        return {}; // decoding — hold last frame in UI
    }
    return canvas;
}

void VideoCompositor::prefetchAround(const ProjectModel &model, double t, const QSize &out,
                                     double behindSec, double aheadSec)
{
    const QSize sz = composeSize(model, out);
    VideoFrameCache::setBucketFps(model.frameRate());
    for (int ti = 0; ti < model.tracks().size(); ++ti) {
        if (!isVideoTrackVisible(model, ti)) {
            continue;
        }
        for (const TrackEvent &ev : model.tracks()[ti].events) {
            if (!isVideoFamily(ev.mediaKind)) {
                continue;
            }
            const QString path = model.mediaPathForEvent(ev);
            if (path.isEmpty()) {
                continue;
            }
            const double evEnd = ev.startSec + ev.lengthSec;
            if (t + aheadSec < ev.startSec || t - behindSec > evEnd) {
                continue;
            }
            const bool still = ev.mediaKind == EventMediaKind::Still
                               || ev.mediaKind == EventMediaKind::Title;
            const double mediaTime = ev.sourceTimeSec(t);
            VideoFrameCache::instance().prefetch(path, mediaTime, sz, still, behindSec,
                                                 aheadSec);
        }
    }
}

} // namespace openvegas
