#include "plugins/AudioPluginTypes.h"
#include "video/ColorCorrectorApply.h"

#include <QImage>

#include <catch2/catch_test_macros.hpp>

using namespace openvegas;

namespace {

QImage solid(int r, int g, int b)
{
    QImage img(16, 16, QImage::Format_ARGB32);
    img.fill(qRgb(r, g, b));
    return img;
}

} // namespace

TEST_CASE("applyVideoFxChain skips bypass and Pan/Crop", "[video][plugins]")
{
    QImage img = solid(40, 50, 60);
    const QRgb before = img.pixel(0, 0);

    QVector<FxSlot> chain;
    FxSlot pan = makeFxSlot(QStringLiteral("Pan/Crop"), PluginFormat::Builtin);
    chain.push_back(pan);

    FxSlot inv = makeFxSlot(QStringLiteral("Invert"), PluginFormat::Ofx);
    inv.bypass = true;
    chain.push_back(inv);

    applyVideoFxChain(&img, chain, 0.0);
    REQUIRE(img.pixel(0, 0) == before);
}

TEST_CASE("applyVideoFxChain Soften changes image", "[video][plugins]")
{
    QImage img(8, 8, QImage::Format_ARGB32);
    img.fill(qRgb(0, 0, 0));
    img.setPixel(3, 3, qRgb(255, 255, 255));
    const QRgb centerBefore = img.pixel(3, 3);
    const QRgb neighborBefore = img.pixel(2, 3);

    QVector<FxSlot> chain;
    chain.push_back(makeFxSlot(QStringLiteral("Soften"), PluginFormat::Ofx));
    applyVideoFxChain(&img, chain, 0.0);

    REQUIRE(img.pixel(3, 3) != centerBefore);
    REQUIRE(img.pixel(2, 3) != neighborBefore);
}

TEST_CASE("applyVideoFxChain Invert inverts", "[video][plugins]")
{
    QImage img = solid(10, 20, 30);
    QVector<FxSlot> chain;
    chain.push_back(makeFxSlot(QStringLiteral("Invert"), PluginFormat::Ofx));
    applyVideoFxChain(&img, chain, 0.0);
    REQUIRE(qRed(img.pixel(1, 1)) == 245);
    REQUIRE(qGreen(img.pixel(1, 1)) == 235);
    REQUIRE(qBlue(img.pixel(1, 1)) == 225);
}

TEST_CASE("applyVideoFxChain Color Corrector still works", "[video][plugins]")
{
    QImage img = solid(128, 128, 128);
    FxSlot cc = makeFxSlot(QStringLiteral("Color Corrector"), PluginFormat::Builtin,
                           QStringLiteral("builtin:Color Corrector"));
    ColorCorrectorParams p;
    p.brightness = 0.5;
    p.contrast = 1.0;
    colorCorrectorSaveToSlot(&cc, p);

    QVector<FxSlot> chain;
    chain.push_back(cc);
    applyVideoFxChain(&img, chain, 0.0);
    REQUIRE(qRed(img.pixel(0, 0)) > 128);
}

TEST_CASE("applyVideoFxChain order: Invert then bypass Soften", "[video][plugins]")
{
    QImage img = solid(0, 0, 0);
    QVector<FxSlot> chain;
    chain.push_back(makeFxSlot(QStringLiteral("Invert"), PluginFormat::Ofx));
    FxSlot soft = makeFxSlot(QStringLiteral("Soften"), PluginFormat::Ofx);
    soft.bypass = true;
    chain.push_back(soft);
    applyVideoFxChain(&img, chain, 0.0);
    REQUIRE(qRed(img.pixel(0, 0)) == 255);
}
