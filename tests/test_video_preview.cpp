#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "audio/FadeCurves.h"
#include "video/ColorCorrectorApply.h"
#include "video/PanCropApply.h"
#include "video/TrackMotionApply.h"
#include "video/VideoCompositor.h"
#include "video/VideoKeyframeEval.h"

#include <QDir>
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

