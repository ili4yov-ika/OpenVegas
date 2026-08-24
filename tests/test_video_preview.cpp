#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "audio/FadeCurves.h"
#include "video/ColorCorrectorApply.h"
#include "video/PanCropApply.h"
#include "video/TrackMotionApply.h"
#include "video/VideoCompositor.h"
#include "video/VideoKeyframeEval.h"

#include <QDir>
#include <QPointF>
#include <QFileInfo>
#include <QImage>
#include <QPainter>

#include <cmath>

using namespace openvegas;

namespace {

QString fixturePath(const char *name)
{
#ifdef OPENVGAS_TEST_FIXTURES_DIR
    const QString p =
        QStringLiteral(OPENVGAS_TEST_FIXTURES_DIR) + QLatin1Char('/') + QLatin1String(name);
    if (QFileInfo::exists(p)) {
        return p;
    }
#endif
    const QString rel = QStringLiteral("tests/fixtures/video/") + QLatin1String(name);
    if (QFileInfo::exists(rel)) {
        return QDir::cleanPath(rel);
    }
    return {};
}

QImage makeSolid(int w, int h, QRgb c)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(c);
    return img;
}

} // namespace

TEST_CASE("videoKeyframeEase hold stays at start", "[video][kf]")
{
    REQUIRE(videoKeyframeEase(VideoKeyframeType::Hold, 0.0) == Catch::Approx(0.0));
    REQUIRE(videoKeyframeEase(VideoKeyframeType::Hold, 0.5) == Catch::Approx(0.0));
    REQUIRE(videoKeyframeEase(VideoKeyframeType::Hold, 1.0) == Catch::Approx(0.0));
}

TEST_CASE("videoKeyframeEase linear and smooth match fade curves", "[video][kf]")
{
    REQUIRE(videoKeyframeEase(VideoKeyframeType::Linear, 0.5) == Catch::Approx(0.5));
    REQUIRE(videoKeyframeEase(VideoKeyframeType::Smooth, 0.25)
            == Catch::Approx(fadeCurveAmplitude(FadeCurveType::Smooth, 0.25)));
}

TEST_CASE("evaluatePanCrop interpolates centers", "[video][kf]")
{
    EventPanCropState st;
    PanCropKeyframe a = EventPanCropState::identityKeyframe(100, 100);
    a.timeSec = 0.0;
    a.xCenter = 0.0;
    a.type = VideoKeyframeType::Linear;
    PanCropKeyframe b = a;
    b.timeSec = 1.0;
    b.xCenter = 100.0;
    st.positionKeyframes = {a, b};
    const PanCropKeyframe mid = evaluatePanCrop(st, 0.5, 100, 100);
    REQUIRE(mid.xCenter == Catch::Approx(50.0));
}

TEST_CASE("mask keyframes morph, and only a changed contour holds", "[video][kf][mask]")
{
    auto square = [](double x, double y, double s, int extraAnchor = 0) {
        MaskPath p;
        p.closed = true;
        p.mode = MaskPathMode::Positive;
        for (const QPointF &d : {QPointF(-1, -1), QPointF(1, -1), QPointF(1, 1), QPointF(-1, 1)}) {
            MaskAnchor a;
            a.x = x + d.x() * s;
            a.y = y + d.y() * s;
            p.anchors.push_back(a);
        }
        for (int i = 0; i < extraAnchor; ++i) {
            MaskAnchor a;
            a.x = x;
            a.y = y - s;
            p.anchors.push_back(a);
        }
        return p;
    };

    EventPanCropState st;
    st.maskEnabled = true;

    MaskKeyframe k0;
    k0.timeSec = 0.0;
    k0.type = VideoKeyframeType::Linear;
    k0.paths = {square(100, 100, 10), square(500, 500, 10)};

    MaskKeyframe k1;
    k1.timeSec = 2.0;
    k1.type = VideoKeyframeType::Linear;
    // First contour moves; second gains an anchor, so it cannot be put in correspondence.
    k1.paths = {square(300, 100, 10), square(900, 500, 10, /*extraAnchor=*/1)};
    st.maskKeyframes = {k0, k1};

    MaskKeyframe mid;
    REQUIRE(maskAt(st, 1.0, &mid));
    REQUIRE(mid.paths.size() == 2);

    // Halfway the moving contour is halfway. Holding the whole keyframe until the next
    // one — which is what this did before — left it at 90 and made the mask jump.
    CHECK(mid.paths[0].anchors[0].x == Catch::Approx(190.0));
    CHECK(mid.paths[0].anchors[1].x == Catch::Approx(210.0));

    // The contour whose anchor count changed keeps its earlier shape rather than
    // freezing the rest of the mask along with it.
    REQUIRE(mid.paths[1].anchors.size() == 4);
    CHECK(mid.paths[1].anchors[0].x == Catch::Approx(490.0));

    // Before the first keyframe and after the last, the ends hold.
    MaskKeyframe edge;
    REQUIRE(maskAt(st, -1.0, &edge));
    CHECK(edge.paths[0].anchors[0].x == Catch::Approx(90.0));
    REQUIRE(maskAt(st, 9.0, &edge));
    CHECK(edge.paths[0].anchors[0].x == Catch::Approx(290.0));

    // A Hold keyframe stays put for its whole segment.
    st.maskKeyframes[0].type = VideoKeyframeType::Hold;
    REQUIRE(maskAt(st, 1.0, &mid));
    CHECK(mid.paths[0].anchors[0].x == Catch::Approx(90.0));

    // No mask at all is reported as such rather than as an empty shape.
    EventPanCropState none;
    MaskKeyframe unused;
    CHECK_FALSE(maskAt(none, 0.0, &unused));
}

TEST_CASE("evaluateTrackMotion interpolates position", "[video][kf]")
{
    TrackMotionState st;
    TrackMotionKeyframe a = TrackMotionState::identityKeyframe(16.0 / 9.0);
    a.timeSec = 0.0;
    a.positionX = 0.0;
    a.type = VideoKeyframeType::Linear;
    TrackMotionKeyframe b = a;
    b.timeSec = 2.0;
    b.positionX = 1.0;
    st.motionKeyframes = {a, b};
    const TrackMotionKeyframe mid = evaluateTrackMotion(st, 1.0, 16.0 / 9.0);
    REQUIRE(mid.positionX == Catch::Approx(0.5));
}

TEST_CASE("eventOpacityAt respects fade and opacity", "[video][fade]")
{
    TrackEvent ev;
    ev.startSec = 0.0;
    ev.lengthSec = 2.0;
    ev.opacity = 0.5;
    ev.fadeInSec = 1.0;
    ev.fadeInCurve = FadeCurveType::Linear;
    ev.fadeOutSec = 0.0;
    REQUIRE(VideoCompositor::eventOpacityAt(ev, 0.0) == Catch::Approx(0.0));
    REQUIRE(VideoCompositor::eventOpacityAt(ev, 0.5) == Catch::Approx(0.25));
    REQUIRE(VideoCompositor::eventOpacityAt(ev, 1.5) == Catch::Approx(0.5));
}

TEST_CASE("pan-crop negative width flips horizontally", "[video][pancrop]")
{
    QImage src(4, 4, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            src.setPixel(x, y, x < 2 ? qRgba(255, 0, 0, 255) : qRgba(0, 255, 0, 255));
        }
    }
    PanCropKeyframe kf = EventPanCropState::identityKeyframe(4, 4);
    kf.width = -4.0;
    kf.height = 4.0;
    const QImage out = applyPanCrop(src, kf, 4, 4, 4, 4, true, nullptr);
    REQUIRE_FALSE(out.isNull());
    // After horizontal flip, left should be green-ish, right red-ish.
    const QRgb left = out.pixel(0, 1);
    const QRgb right = out.pixel(3, 1);
    REQUIRE(qGreen(left) > qRed(left));
    REQUIRE(qRed(right) > qGreen(right));
}

namespace {

/** Four coloured quadrants, so a zoom, a pan and a mirror are all visible. */
QImage quadrantCard(int w, int h)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool right = x >= w / 2;
            const bool bottom = y >= h / 2;
            QRgb c = qRgba(200, 60, 60, 255);          // top-left red
            if (right && !bottom) {
                c = qRgba(60, 200, 60, 255);           // top-right green
            } else if (!right && bottom) {
                c = qRgba(60, 60, 200, 255);           // bottom-left blue
            } else if (right && bottom) {
                c = qRgba(200, 200, 60, 255);          // bottom-right yellow
            }
            img.setPixel(x, y, c);
        }
    }
    return img;
}

} // namespace

TEST_CASE("pan-crop zooms: a smaller rectangle fills the frame", "[video][pancrop]")
{
    // The rectangle frames a region of the source and that region is stretched over the
    // whole output. Drawing it back at its own coordinates instead — which is what this
    // did — leaves the picture exactly where it was and merely cuts away the rest, so a
    // "zoom" changed nothing but the edges. The identity case matched either way, which
    // is why it went unnoticed.
    const int n = 64;
    const QImage src = quadrantCard(n, n);

    PanCropKeyframe kf = EventPanCropState::identityKeyframe(n, n);
    // Frame only the top-left quadrant.
    kf.width = n / 2.0;
    kf.height = n / 2.0;
    kf.xCenter = n / 4.0;
    kf.yCenter = n / 4.0;
    kf.rotationXCenter = kf.xCenter;
    kf.rotationYCenter = kf.yCenter;

    const QImage out = applyPanCrop(src, kf, n, n, n, n, true, nullptr);
    REQUIRE_FALSE(out.isNull());

    // That quadrant is red, so the whole frame must now be red — corners included.
    for (const QPoint &pt : {QPoint(2, 2), QPoint(n - 3, 2), QPoint(2, n - 3),
                             QPoint(n - 3, n - 3), QPoint(n / 2, n / 2)}) {
        INFO("at " << pt.x() << "," << pt.y());
        const QRgb c = out.pixel(pt);
        CHECK(qRed(c) > 150);
        CHECK(qGreen(c) < 110);
        CHECK(qBlue(c) < 110);
    }
}

TEST_CASE("pan-crop zooms out: a larger rectangle shrinks the picture", "[video][pancrop]")
{
    const int n = 64;
    const QImage src = quadrantCard(n, n);

    PanCropKeyframe kf = EventPanCropState::identityKeyframe(n, n);
    kf.width = n * 2.0;
    kf.height = n * 2.0;
    // Centre stays at the frame centre, so the picture lands in the middle at half size.
    const QImage out = applyPanCrop(src, kf, n, n, n, n, true, nullptr);
    REQUIRE_FALSE(out.isNull());

    // Outside the shrunken picture nothing was drawn. A rectangle bigger than the frame
    // used to be "normalised" back to frame size, which quietly turned zoom-out into no
    // zoom at all — the sample project has exactly such a keyframe.
    CHECK(qAlpha(out.pixel(2, 2)) == 0);
    CHECK(qAlpha(out.pixel(n - 3, n - 3)) == 0);
    // The middle still carries the picture, and the quadrant boundary is still centred.
    CHECK(qAlpha(out.pixel(n / 2 - 6, n / 2 - 6)) > 200);
    const QRgb tl = out.pixel(n / 2 - 6, n / 2 - 6);
    const QRgb br = out.pixel(n / 2 + 6, n / 2 + 6);
    CHECK(qRed(tl) > qBlue(tl));          // top-left of the picture is red
    CHECK(qRed(br) > 150);                // bottom-right is yellow
    CHECK(qGreen(br) > 150);
}

TEST_CASE("pan-crop pans: moving the rectangle moves what is seen", "[video][pancrop]")
{
    const int n = 64;
    const QImage src = quadrantCard(n, n);

    PanCropKeyframe kf = EventPanCropState::identityKeyframe(n, n);
    // Same size as the frame, shifted a quarter-frame to the right: the right half of the
    // source moves into the middle, and the left quarter of the output falls off the
    // source and stays empty.
    kf.xCenter = n * 0.75;
    kf.rotationXCenter = kf.xCenter;

    const QImage out = applyPanCrop(src, kf, n, n, n, n, true, nullptr);
    REQUIRE_FALSE(out.isNull());

    // Middle of the output now shows what was at three-quarters across: green on top.
    const QRgb mid = out.pixel(n / 2, n / 4);
    CHECK(qGreen(mid) > qRed(mid));
    // Beyond the right edge of the source there is nothing.
    CHECK(qAlpha(out.pixel(n - 2, n / 2)) == 0);
}

TEST_CASE("track motion identity covers full frame", "[video][motion]")
{
    const QImage layer = makeSolid(32, 18, qRgba(200, 50, 50, 255));
    TrackMotionKeyframe kf = TrackMotionState::identityKeyframe(32.0 / 18.0);
    const QRectF dest = trackMotionDestRect(kf, 32, 18, 32, 18);
    REQUIRE(dest.width() == Catch::Approx(32.0).margin(0.5));
    REQUIRE(dest.height() == Catch::Approx(18.0).margin(0.5));
    REQUIRE(dest.center().x() == Catch::Approx(16.0).margin(0.5));
    REQUIRE(dest.center().y() == Catch::Approx(9.0).margin(0.5));
}

TEST_CASE("two-layer 50% opacity blend golden pixel", "[video][compose]")
{
    QImage bottom = makeSolid(8, 8, qRgba(255, 0, 0, 255));
    QImage top = makeSolid(8, 8, qRgba(0, 0, 255, 255));
    QImage canvas(8, 8, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::black);
    QPainter p(&canvas);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setOpacity(1.0);
    p.drawImage(0, 0, bottom);
    p.setOpacity(0.5);
    p.drawImage(0, 0, top);
    p.end();
    const QRgb c = canvas.pixel(4, 4);
    // SourceOver premul: approx mid between red and blue → purple-ish
    REQUIRE(qRed(c) > 80);
    REQUIRE(qBlue(c) > 80);
    REQUIRE(qGreen(c) < 40);
}

TEST_CASE("fixtures still PNGs load", "[video][fixtures]")
{
    const QString redPath = fixturePath("solid_red.png");
    if (redPath.isEmpty()) {
        WARN("fixtures not found — skip");
        return;
    }
    QImage red(redPath);
    REQUIRE_FALSE(red.isNull());
    REQUIRE(red.width() == 4);
    const QRgb px = red.convertToFormat(QImage::Format_ARGB32).pixel(1, 1);
    REQUIRE(qRed(px) > 200);
    REQUIRE(qBlue(px) < 40);
}

TEST_CASE("color corrector desaturates and brightens", "[video][color]")
{
    QImage img(8, 8, QImage::Format_ARGB32);
    img.fill(qRgba(200, 40, 40, 255));
    ColorCorrectorParams p;
    p.saturation = 0.0;
    p.brightness = 0.2;
    applyColorCorrector(&img, p);
    const QRgb c = img.pixel(2, 2);
    REQUIRE(qRed(c) == qGreen(c));
    REQUIRE(qGreen(c) == qBlue(c));
    REQUIRE(qRed(c) > 50);
}

TEST_CASE("color corrector identity is no-op", "[video][color]")
{
    QImage img(4, 4, QImage::Format_ARGB32);
    img.fill(qRgba(10, 20, 30, 255));
    const QRgb before = img.pixel(0, 0);
    applyColorCorrector(&img, ColorCorrectorParams{});
    REQUIRE(img.pixel(0, 0) == before);
}

TEST_CASE("fx chain applies Color Corrector slot", "[video][color]")
{
    QImage img(4, 4, QImage::Format_ARGB32);
    img.fill(qRgba(0, 255, 0, 255));
    FxSlot slot = makeFxSlot(QStringLiteral("Color Corrector"), PluginFormat::Builtin);
    ColorCorrectorParams p;
    p.saturation = 0.0;
    colorCorrectorSaveToSlot(&slot, p);
    applyVideoColorFxChain(&img, {slot});
    const QRgb c = img.pixel(1, 1);
    REQUIRE(qRed(c) == qGreen(c));
    REQUIRE(qGreen(c) == qBlue(c));
}

