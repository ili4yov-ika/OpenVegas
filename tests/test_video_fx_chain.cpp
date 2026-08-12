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

// A Titles & Text / Media Generator event's own content lives in fxChain[0] — it IS
// the event, not a video effect stacked on top of it. Opening Video Event FX / Pan-Crop
// on such an event used to call ensureFxFirst(), which unconditionally chain.prepend()ed
// Pan/Crop, silently shoving the generator to index 1 — corrupting every fxChain[0] /
// fxChain.first()-based lookup (TitlesTextEditorDialog, TimelineView's generator
// thumbnail, MainWindow's Fx-button routing) and making the generator show up as a
// removable/draggable node in VideoEventFxDialogExact's plugin chain UI.
TEST_CASE("ensureFxFirst inserts Pan/Crop after a generator's slot 0 instead of displacing it",
         "[video][plugins]")
{
    QVector<FxSlot> chain;
    chain.push_back(makeFxSlot(QStringLiteral("VEGAS Titles & Text"), PluginFormat::Builtin));

    ensureFxFirst(chain, QStringLiteral("Pan/Crop"), PluginFormat::Builtin);

    REQUIRE(chain.size() == 2);
    CHECK(chain[0].displayName == QStringLiteral("VEGAS Titles & Text"));
    CHECK(chain[1].displayName == QStringLiteral("Pan/Crop"));
}

TEST_CASE("ensureFxFirst on a generator event is idempotent across repeat calls",
         "[video][plugins]")
{
    QVector<FxSlot> chain;
    chain.push_back(makeFxSlot(QStringLiteral("VEGAS Titles & Text"), PluginFormat::Builtin));

    ensureFxFirst(chain, QStringLiteral("Pan/Crop"), PluginFormat::Builtin);
    ensureFxFirst(chain, QStringLiteral("Pan/Crop"), PluginFormat::Builtin);

    REQUIRE(chain.size() == 2);
    CHECK(chain[0].displayName == QStringLiteral("VEGAS Titles & Text"));
    CHECK(chain[1].displayName == QStringLiteral("Pan/Crop"));
}

TEST_CASE("ensureFxFirst still prepends normally for a plain (non-generator) video event",
         "[video][plugins]")
{
    QVector<FxSlot> chain;
    chain.push_back(makeFxSlot(QStringLiteral("Sepia"), PluginFormat::Ofx));

    ensureFxFirst(chain, QStringLiteral("Pan/Crop"), PluginFormat::Builtin);

    REQUIRE(chain.size() == 2);
    CHECK(chain[0].displayName == QStringLiteral("Pan/Crop"));
    CHECK(chain[1].displayName == QStringLiteral("Sepia"));
}

TEST_CASE("ensureFxFirst also preserves a non-text Media Generator slot at index 0",
         "[video][plugins]")
{
    QVector<FxSlot> chain;
    chain.push_back(makeFxSlot(QStringLiteral("Checkerboard"), PluginFormat::Builtin,
                               QStringLiteral("builtin:MediaGenerator:Checkerboard")));

    ensureFxFirst(chain, QStringLiteral("Pan/Crop"), PluginFormat::Builtin);

    REQUIRE(chain.size() == 2);
    CHECK(chain[0].pluginId == QStringLiteral("builtin:MediaGenerator:Checkerboard"));
    CHECK(chain[1].displayName == QStringLiteral("Pan/Crop"));
}

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

// OpenVegas's stand-in renderers are off (OPENVEGAS_EMULATED_VIDEO_FX == 0), so a VEGAS
// effect with no loadable plug-in behind it must leave the frame alone. These two used to
// assert the opposite — that a box blur stood in for Soften and a channel flip for Invert —
// which is exactly the behaviour that made the app look like it was running VEGAS's
// effects when it was running approximations. See MARKDOWN/PLAN_OFX_VIDEO_PLUGINS.md.

TEST_CASE("applyVideoFxChain leaves the frame untouched without a real plug-in",
          "[video][plugins]")
{
    QImage img(8, 8, QImage::Format_ARGB32);
    img.fill(qRgb(0, 0, 0));
    img.setPixel(3, 3, qRgb(255, 255, 255));
    const QImage before = img.copy();

    QVector<FxSlot> chain;
    chain.push_back(makeFxSlot(QStringLiteral("Soften"), PluginFormat::Ofx));
    applyVideoFxChain(&img, chain, 0.0);

    REQUIRE(img == before);
}

TEST_CASE("applyVideoFxChain does not substitute anything for an unknown effect",
          "[video][plugins]")
{
    // Deliberately a name no catalog can resolve. "Invert" would be a bad choice here:
    // VEGAS really ships com.vegascreativesoftware:invert, so where the samples are present
    // the real plug-in renders it — correctly, and this test would then be asserting the
    // wrong thing for the right reason.
    QImage img = solid(10, 20, 30);
    const QImage before = img.copy();
    QVector<FxSlot> chain;
    chain.push_back(makeFxSlot(QStringLiteral("Not A Real Effect"), PluginFormat::Ofx));
    applyVideoFxChain(&img, chain, 0.0);
    REQUIRE(img == before);
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

TEST_CASE("applyVideoFxChain walks the whole chain without a real plug-in", "[video][plugins]")
{
    // Chain order and bypass still have to be honoured — the chain is real even when
    // nothing in it can render yet. Builtins in the same chain keep working.
    QImage img = solid(128, 128, 128);
    FxSlot cc = makeFxSlot(QStringLiteral("Color Corrector"), PluginFormat::Builtin,
                           QStringLiteral("builtin:Color Corrector"));
    ColorCorrectorParams p;
    p.brightness = 0.5;
    p.contrast = 1.0;
    colorCorrectorSaveToSlot(&cc, p);

    QVector<FxSlot> chain;
    chain.push_back(makeFxSlot(QStringLiteral("Not A Real Effect"), PluginFormat::Ofx));
    FxSlot soft = makeFxSlot(QStringLiteral("Soften"), PluginFormat::Ofx);
    soft.bypass = true;
    chain.push_back(soft);
    chain.push_back(cc);
    applyVideoFxChain(&img, chain, 0.0);
    REQUIRE(qRed(img.pixel(0, 0)) > 128);
}
