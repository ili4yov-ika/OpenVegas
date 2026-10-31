#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "io/MediaProbe.h"
#include "io/MediaMime.h"
#include "model/ProjectModel.h"
#include "plugins/AudioPluginTypes.h"
#include "video/MediaGeneratorApply.h"

#include <cstdlib>
#include <memory>
#include <QMimeData>

using namespace openvegas;

namespace {
// Qt's paint pipeline (gradients, premultiplied alpha) can round a channel by a level or
// two — compare with a small tolerance rather than demanding bit-exact pixels.
bool colorsClose(const QColor &a, const QColor &b, int tol = 2)
{
    return std::abs(a.red() - b.red()) <= tol && std::abs(a.green() - b.green()) <= tol
           && std::abs(a.blue() - b.blue()) <= tol;
}
} // namespace

// Non-text Media Generator plug-ins (Checkerboard, Color Gradient, Solid Color, …) — the
// path-less drag'n'drop timeline placement that Titles & Text already had, generalized to
// every generator in MediaGeneratorPane's catalog (see ISSUES_AND_PLANS.md 2026-08-08).

TEST_CASE("MediaGeneratorParams round-trips through the drag payload encoding",
          "[video][media-generator]")
{
    MediaGeneratorParams p;
    p.pluginName = QStringLiteral("Checkerboard");
    p.pattern = MediaGeneratorPattern::Checker;
    p.c0 = QColor(0x11, 0x22, 0x33);
    p.c1 = QColor(0xaa, 0xbb, 0xcc);
    p.tile = 20;

    const QString payload = mediaGeneratorParamsToPayload(p);
    const MediaGeneratorParams back = mediaGeneratorParamsFromPayload(payload);

    REQUIRE(back.pluginName == p.pluginName);
    REQUIRE(back.pattern == p.pattern);
    REQUIRE(back.c0 == p.c0);
    REQUIRE(back.c1 == p.c1);
    REQUIRE(back.tile == p.tile);
}

TEST_CASE("Titles & Text drag payload preserves multiline sample and preset name",
          "[video][media-generator][titles-and-text][dnd]")
{
    const QString sample = QStringLiteral("UNIQUE\nTYPOGRAPHY");
    std::unique_ptr<QMimeData> md(MediaMime::fromSynthetic(QStringLiteral("titles"), sample,
                                                            QStringLiteral("Title01")));
    QStringList names;
    QStringList kinds;
    QStringList paths;
    QVector<double> lengths;
    QStringList extras;
    MediaMime::parse(md.get(), &names, &kinds, &paths, &lengths, &extras);

    REQUIRE(names == QStringList{sample});
    REQUIRE(kinds == QStringList{QStringLiteral("titles")});
    REQUIRE(paths == QStringList{QString()});
    REQUIRE(extras == QStringList{QStringLiteral("Title01")});
}

TEST_CASE("MediaGeneratorParams round-trips through an FxSlot's state chunk",
          "[video][media-generator]")
{
    MediaGeneratorParams p;
    p.pluginName = QStringLiteral("Color Gradient");
    p.pattern = MediaGeneratorPattern::Gradient;
    p.c0 = QColor(0x20, 0x40, 0x80);
    p.c1 = QColor(0xc0, 0x60, 0x30);
    p.tile = 8;
    p.media.tapeName = QStringLiteral("Generated gradient");
    p.media.useCustomTimecode = true;
    p.media.customTimecodeSec = 12.5;
    p.media.frameWidth = 1280;
    p.media.frameHeight = 720;
    p.media.fieldOrder = MediaFieldOrder::UpperFieldFirst;
    p.media.pixelAspect = 1.2121;
    p.media.alphaChannel = MediaAlphaChannel::Straight;
    p.media.backgroundColor = QColor(12, 34, 56, 255);
    p.media.rotation = MediaRotation::Deg90Clockwise;

    const FxSlot slot = mediaGeneratorSlotFor(p);
    REQUIRE(slot.displayName == p.pluginName);
    REQUIRE(isMediaGeneratorPluginId(slot.pluginId));
    REQUIRE_FALSE(isTitlesTextName(slot.displayName));

    const MediaGeneratorParams back = mediaGeneratorFromSlot(slot);
    REQUIRE(back.pluginName == p.pluginName);
    REQUIRE(back.pattern == p.pattern);
    REQUIRE(back.c0 == p.c0);
    REQUIRE(back.c1 == p.c1);
    REQUIRE(back.tile == p.tile);
    REQUIRE(back.media.tapeName == p.media.tapeName);
    REQUIRE(back.media.useCustomTimecode == p.media.useCustomTimecode);
    REQUIRE(back.media.customTimecodeSec == Catch::Approx(p.media.customTimecodeSec));
    REQUIRE(back.media.frameWidth == p.media.frameWidth);
    REQUIRE(back.media.frameHeight == p.media.frameHeight);
    REQUIRE(back.media.fieldOrder == p.media.fieldOrder);
    REQUIRE(back.media.pixelAspect == Catch::Approx(p.media.pixelAspect));
    REQUIRE(back.media.alphaChannel == p.media.alphaChannel);
    REQUIRE(back.media.backgroundColor == p.media.backgroundColor);
    REQUIRE(back.media.rotation == p.media.rotation);
}

TEST_CASE("renderMediaGeneratorPattern renders the requested size with the right colors",
          "[video][media-generator]")
{
    MediaGeneratorParams p;
    p.pattern = MediaGeneratorPattern::SplitScreen;
    p.c0 = QColor(0x20, 0x60, 0xa0);
    p.c1 = QColor(0xa0, 0x40, 0x20);

    const QImage img = renderMediaGeneratorPattern(p, QSize(200, 100));
    REQUIRE(img.size() == QSize(200, 100));
    REQUIRE(colorsClose(QColor(img.pixel(10, 50)), p.c0));
    REQUIRE(colorsClose(QColor(img.pixel(190, 50)), p.c1));

    // Solid Color plug-in reuses Gradient with c0 == c1 — should read as a flat fill.
    MediaGeneratorParams solid;
    solid.pattern = MediaGeneratorPattern::Gradient;
    solid.c0 = solid.c1 = QColor(0x00, 0x78, 0xd7);
    const QImage flat = renderMediaGeneratorPattern(solid, QSize(64, 36));
    REQUIRE(colorsClose(QColor(flat.pixel(0, 0)), solid.c0));
    REQUIRE(colorsClose(QColor(flat.pixel(63, 35)), solid.c0));
}

TEST_CASE("Media Generator media properties affect generated output",
          "[video][media-generator]")
{
    MediaGeneratorParams p;
    p.pluginName = QStringLiteral("Solid Color");
    p.vegasPluginId = QStringLiteral("{Svfx:com.vegascreativesoftware:solidcolor}");
    p.vegasParams.insert(QStringLiteral("Color"), QVariantList{1.0, 0.0, 0.0, 1.0});
    p.media.frameWidth = 32;
    p.media.frameHeight = 16;
    p.media.rotation = MediaRotation::Deg90Clockwise;
    const QImage out = renderMediaGenerator(p, QSize(64, 64));
    REQUIRE(out.size() == QSize(64, 64));
    REQUIRE(QColor(out.pixel(32, 32)).red() > 200);
}

TEST_CASE("MediaProbe defaults a path-less generator drop to 10 seconds",
          "[io][media-generator]")
{
    REQUIRE(MediaProbe::lengthForInsert(QString(), QStringLiteral("generator"), 0.0)
            == Catch::Approx(10.0));
    // An explicit drag-ghost hint still wins.
    REQUIRE(MediaProbe::lengthForInsert(QString(), QStringLiteral("generator"), 4.0)
            == Catch::Approx(4.0));
}

TEST_CASE("addMediaAt(generator) on empty timeline creates a new 10s video event",
          "[model][media-generator]")
{
    ProjectModel model;
    REQUIRE(model.tracks().isEmpty());

    MediaGeneratorParams p;
    p.pluginName = QStringLiteral("Checkerboard");
    p.pattern = MediaGeneratorPattern::Checker;
    p.tile = 12;
    const QString payload = mediaGeneratorParamsToPayload(p);

    const int id = model.addMediaAt(QStringLiteral("Large Tiles"), QStringLiteral("generator"),
                                    2.0, 0.0, kDropCreateNewTracks, {}, payload);

    REQUIRE(model.tracks().size() == 1);
    REQUIRE(model.tracks().first().kind == TrackKind::Video);
    REQUIRE(model.tracks().first().events.size() == 1);

    const TrackEvent &ev = model.tracks().first().events.first();
    REQUIRE(ev.id == id);
    REQUIRE(ev.name == QStringLiteral("Large Tiles"));
    REQUIRE(ev.startSec == Catch::Approx(2.0));
    REQUIRE(ev.lengthSec == Catch::Approx(10.0)); // Vegas generator default
    REQUIRE(ev.mediaKind == EventMediaKind::Title);
    REQUIRE(ev.mediaPath.isEmpty());
    REQUIRE(ev.fxChain.size() == 1);
    REQUIRE(isMediaGeneratorPluginId(ev.fxChain.first().pluginId));

    const MediaGeneratorParams back = mediaGeneratorFromSlot(ev.fxChain.first());
    REQUIRE(back.pluginName == p.pluginName);
    REQUIRE(back.pattern == p.pattern);
    REQUIRE(back.tile == p.tile);
}

TEST_CASE("addMediaAt(generator) on an existing video track reuses it instead of creating one",
          "[model][media-generator]")
{
    ProjectModel model;
    const int existingVideo = model.addTrack(TrackKind::Video);
    model.addTrack(TrackKind::Audio); // decoy — must not be picked

    const MediaGeneratorParams p{QStringLiteral("Solid Color"), MediaGeneratorPattern::Gradient,
                                 QColor(0, 0x78, 0xd7), QColor(0, 0x78, 0xd7), 8};
    const int id = model.addMediaAt(QString(), QStringLiteral("generator"), 5.0, 0.0,
                                    existingVideo, {}, mediaGeneratorParamsToPayload(p));

    REQUIRE(model.tracks().size() == 2); // no new track created
    const TrackEvent *ev = model.findEvent(id);
    REQUIRE(ev != nullptr);
    REQUIRE(ev->startSec == Catch::Approx(5.0));
    REQUIRE(ev->lengthSec == Catch::Approx(10.0));
    // Bare plugin-row drop (no preset name) falls back to the plugin name.
    REQUIRE(ev->name == QStringLiteral("Solid Color"));

    bool foundOnExistingTrack = false;
    for (const TrackEvent &e : model.tracks()[existingVideo].events) {
        if (e.id == id) {
            foundOnExistingTrack = true;
        }
    }
    REQUIRE(foundOnExistingTrack);
}
