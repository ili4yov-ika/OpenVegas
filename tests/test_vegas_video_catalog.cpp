#include <catch2/catch_test_macros.hpp>

#include "io/SamplePaths.h"
#include "plugins/AudioPluginTypes.h"
#include "plugins/OfxHost.h"
#include "audio/BuiltinDsp.h"
#include "io/MediaMime.h"
#include "plugins/VegasVideoPluginCatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMimeData>

#include <memory>

using namespace openvegas;

namespace {

void ensureQtApp(int &argc, char **argv)
{
    if (!QCoreApplication::instance()) {
        static QCoreApplication app(argc, argv);
        Q_UNUSED(app);
    }
}

QString samplesVegasRoot()
{
    const QString rel = QStringLiteral("SAMPLES/VEGAS-PRO-22-PROGRAM-FILES");
    const QDir cwd(QDir::currentPath());
    const QString fromCwd = cwd.absoluteFilePath(rel);
    if (QDir(fromCwd).exists()) {
        return fromCwd;
    }
    const QString fromRepo = QDir(QCoreApplication::applicationDirPath())
                                 .absoluteFilePath(QStringLiteral("../../") + rel);
    if (QDir(fromRepo).exists()) {
        return fromRepo;
    }
    return SamplePaths::resolveProjectPath(rel);
}

} // namespace

TEST_CASE("Vegas video catalog parses Vfx1 bundle from samples", "[video-fx][vegas-video]")
{
    int argc = 1;
    char arg0[] = "test";
    char *argv[] = {arg0, nullptr};
    ensureQtApp(argc, argv);

    const QString root = samplesVegasRoot();
    if (root.isEmpty() || !QDir(root).exists()) {
        SKIP("SAMPLES/VEGAS-PRO-22-PROGRAM-FILES not available");
    }

    VegasVideoPluginCatalog::invalidateCache();
    const auto entries = VegasVideoPluginCatalog::discover({root});
    REQUIRE(entries.size() >= 50);

    bool hasChromaBlur = false;
    bool hasGlint = false;
    bool hasGaussian = false;
    for (const VegasVideoPluginEntry &e : entries) {
        if (e.displayName == QLatin1String("Chroma Blur")) {
            hasChromaBlur = true;
            REQUIRE(e.effectId.contains(QLatin1String("chromablur"), Qt::CaseInsensitive));
        }
        if (e.displayName == QLatin1String("Glint")) {
            hasGlint = true;
            REQUIRE(e.effectId.contains(QLatin1String("glint"), Qt::CaseInsensitive));
        }
        if (e.displayName == QLatin1String("Gaussian Blur")) {
            hasGaussian = true;
        }
    }
    REQUIRE(hasChromaBlur);
    REQUIRE(hasGlint);
    REQUIRE(hasGaussian);
}

TEST_CASE("resolveVideoFxSlot maps Svfx chromablur to catalog entry", "[video-fx][vegas-video]")
{
    int argc = 1;
    char arg0[] = "test";
    char *argv[] = {arg0, nullptr};
    ensureQtApp(argc, argv);

    const QString root = samplesVegasRoot();
    if (root.isEmpty()) {
        SKIP("Vegas samples not available");
    }
    VegasVideoPluginCatalog::invalidateCache();
    VegasVideoPluginCatalog::discover({root});

    FxSlot raw = fxSlotFromVegName(
        QStringLiteral("{Svfx:com.vegascreativesoftware:chromablur}"));
    REQUIRE(raw.displayName == QStringLiteral("Chroma Blur"));
    const FxSlot resolved = VegasVideoPluginCatalog::resolveVideoFxSlot(raw);
    REQUIRE((resolved.pluginId.startsWith(QLatin1String("ofx-id:"))
             || resolved.pluginId.startsWith(QLatin1String("ofx:"))));
    REQUIRE(resolved.pluginId.contains(QLatin1String("chromablur"), Qt::CaseInsensitive));
}

TEST_CASE("OfxHost parsePluginId round-trip", "[video-fx][ofx]")
{
    const OfxPluginIdParts p =
        OfxHost::parsePluginId(QStringLiteral("ofx:C:/fx/Vfx1.ofx#12#com.vegascreativesoftware:glintvelvetmatter"));
    REQUIRE(p.path == QStringLiteral("C:/fx/Vfx1.ofx"));
    REQUIRE(p.index == 12);
    REQUIRE(p.effectId == QStringLiteral("com.vegascreativesoftware:glintvelvetmatter"));

    const OfxPluginIdParts idOnly =
        OfxHost::parsePluginId(QStringLiteral("ofx-id:com.vegascreativesoftware:invert"));
    REQUIRE(idOnly.path.isEmpty());
    REQUIRE(idOnly.effectId == QStringLiteral("com.vegascreativesoftware:invert"));
}

TEST_CASE("videoFxSlotFromName Color Corrector stays builtin", "[video-fx][vegas-video]")
{
    const FxSlot cc = videoFxSlotFromName(QStringLiteral("Color Corrector"));
    REQUIRE(cc.format == PluginFormat::Builtin);
    REQUIRE(cc.pluginId == QStringLiteral("builtin:Color Corrector"));
}

TEST_CASE("Glint fallback params match the real com.vegascreativesoftware:glintvelvetmatter set",
         "[video-fx][vegas-video]")
{
    // Without the real Vegas samples discovered, paramsInfoForSlot() falls back to the
    // approximate table (OfxHost::paramsForSlot() has nothing to resolve against). Locks in
    // the full <Glint> VEG XML schema — was Threshold/Boost/Gain (Gain isn't even a real
    // param) before this fix; see MARKDOWN/ISSUES_AND_PLANS.md.
    int argc = 1;
    char arg0[] = "test";
    char *argv[] = {arg0, nullptr};
    ensureQtApp(argc, argv);

    const FxSlot glint = videoFxSlotFromName(QStringLiteral("Glint"));
    const QVector<OfxParamInfo> params = VegasVideoPluginCatalog::paramsInfoForSlot(glint);

    QStringList names;
    for (const OfxParamInfo &p : params) {
        names << p.name;
    }
    const QStringList expected = {QStringLiteral("Threshold"),   QStringLiteral("Boost"),
                                  QStringLiteral("HorizontalRadius"), QStringLiteral("VerticalRadius"),
                                  QStringLiteral("Hue"),          QStringLiteral("HueSweep"),
                                  QStringLiteral("Saturation"),   QStringLiteral("Rotation"),
                                  QStringLiteral("Streaks"),      QStringLiteral("ReduceFlicker"),
                                  QStringLiteral("EffectOnly")};
    for (const QString &key : expected) {
        REQUIRE(names.contains(key));
    }
    REQUIRE_FALSE(names.contains(QStringLiteral("gain")));
}

TEST_CASE("Real VEGAS Chroma Blur OFX plugin loads and processes a frame", "[video-fx][ofx][vegas-video]")
{
    int argc = 1;
    char arg0[] = "test";
    char *argv[] = {arg0, nullptr};
    ensureQtApp(argc, argv);

    const QString root = samplesVegasRoot();
    if (root.isEmpty() || !QDir(root).exists()) {
        SKIP("SAMPLES/VEGAS-PRO-22-PROGRAM-FILES not available");
    }

    VegasVideoPluginCatalog::invalidateCache();
    VegasVideoPluginCatalog::discover({root});

    const FxSlot raw = videoFxSlotFromName(QStringLiteral("Chroma Blur"));
    REQUIRE(raw.format == PluginFormat::Ofx);
    const FxSlot resolved = VegasVideoPluginCatalog::resolveVideoFxSlot(raw);
    REQUIRE(resolved.pluginId.startsWith(QLatin1String("ofx:")));

    QImage frame(64, 64, QImage::Format_ARGB32);
    frame.fill(qRgba(200, 100, 50, 255));
    const QImage before = frame.copy();

    FxSlot slot = resolved;
    const bool ok = OfxHost::instance().processSlot(slot, &frame, 0.0);
    REQUIRE(ok);
    REQUIRE(frame.size() == before.size());
}

TEST_CASE("OfxHost::effectIndexMap resolves real effect identifiers in Vfx1.ofx",
         "[video-fx][ofx][vegas-video]")
{
    int argc = 1;
    char arg0[] = "test";
    char *argv[] = {arg0, nullptr};
    ensureQtApp(argc, argv);
    const QString root = samplesVegasRoot();
    if (root.isEmpty() || !QDir(root).exists()) {
        SKIP("SAMPLES/VEGAS-PRO-22-PROGRAM-FILES not available");
    }
    VegasVideoPluginCatalog::invalidateCache();
    VegasVideoPluginCatalog::discover({root});
    const VegasVideoPluginEntry *e = VegasVideoPluginCatalog::findByEffectId(
        QStringLiteral("com.vegascreativesoftware:chromablur"));
    REQUIRE(e != nullptr);
    REQUIRE(e->hasBinary);
    // Regression guard: this used to come back empty because LoadLibrary couldn't
    // resolve Vfx1.ofx's dependencies (sharedk.dll / OpenColorIO_2_0.dll) sitting
    // in the VEGAS install root rather than next to the .ofx binary itself.
    const QHash<QString, int> idxMap = OfxHost::effectIndexMap(e->binaryPath);
    REQUIRE_FALSE(idxMap.isEmpty());
    REQUIRE(idxMap.contains(QStringLiteral("com.vegascreativesoftware:chromablur")));
    REQUIRE(e->pluginIndex == idxMap.value(QStringLiteral("com.vegascreativesoftware:chromablur")));
}

TEST_CASE("OfxHost::load Load+Describe succeed for a real Vegas OFX plug-in",
         "[video-fx][ofx][vegas-video]")
{
    int argc = 1;
    char arg0[] = "test";
    char *argv[] = {arg0, nullptr};
    ensureQtApp(argc, argv);
    const QString root = samplesVegasRoot();
    if (root.isEmpty() || !QDir(root).exists()) {
        SKIP("SAMPLES/VEGAS-PRO-22-PROGRAM-FILES not available");
    }
    VegasVideoPluginCatalog::invalidateCache();
    VegasVideoPluginCatalog::discover({root});
    const VegasVideoPluginEntry *e = VegasVideoPluginCatalog::findByEffectId(
        QStringLiteral("com.vegascreativesoftware:chromablur"));
    REQUIRE(e != nullptr);
    REQUIRE(e->hasBinary);

    OfxPluginDesc desc;
    desc.path = e->binaryPath;
    desc.bundlePath = e->bundlePath;
    desc.effectId = e->effectId;
    desc.pluginIndex = e->pluginIndex;
    desc.hasBinary = true;

    QString err;
    // Regression guard: this used to answer kOfxActionLoad with
    // kOfxStatErrMissingHostFeature because OfxMultiThreadSuite and several OFX 1.4 host
    // properties (kOfxPropVersion, kOfxImageEffectPropSupportsOverlays,
    // kOfxImageEffectInstancePropSequentialRender, the param animation-support flags) were
    // never declared. Both kOfxActionLoad and kOfxActionDescribe now succeed against this
    // host — see MARKDOWN/ISSUES_AND_PLANS.md for what's still blocked past this point
    // (kOfxImageEffectActionDescribeInContext).
    REQUIRE(OfxHost::load(desc, &err));
    REQUIRE_FALSE(err.contains(QStringLiteral("OFX Load failed")));
    REQUIRE_FALSE(err.contains(QStringLiteral("OFX Describe failed")));
}

TEST_CASE("OfxHost::paramsForSlot returns the real declared params of a Vegas OFX plug-in",
         "[video-fx][ofx][vegas-video]")
{
    int argc = 1;
    char arg0[] = "test";
    char *argv[] = {arg0, nullptr};
    ensureQtApp(argc, argv);

    const QString root = samplesVegasRoot();
    if (root.isEmpty() || !QDir(root).exists()) {
        SKIP("SAMPLES/VEGAS-PRO-22-PROGRAM-FILES not available");
    }

    VegasVideoPluginCatalog::invalidateCache();
    VegasVideoPluginCatalog::discover({root});

    const FxSlot raw = videoFxSlotFromName(QStringLiteral("Chroma Blur"));
    const QVector<OfxParamInfo> params = OfxHost::instance().paramsForSlot(raw);

    // These names come out of the plug-in binary's own DescribeInContext, not from any
    // table of ours, and they match what VEGAS Pro's UI shows for Chroma Blur. Getting
    // here at all took three host fixes, each of which this guards:
    //   * clip/param/effect descriptors are handed to the plug-in fully populated —
    //     without that, defineClip("Source") was followed by a propGetDimension the host
    //     failed, which the OFX support library reports as kOfxStatErrMissingHostFeature;
    //   * the VEGAS-only kOfxImageEffectPropVegasContext in-arg is supplied;
    //   * the effectId is re-resolved against the binary instead of trusting the index,
    //     so this is Chroma Blur and not whatever sits at index 0 of Vfx1.ofx.
    // See MARKDOWN/PLAN_OFX_VIDEO_PLUGINS.md.
    REQUIRE_FALSE(params.isEmpty());
    QStringList names;
    for (const OfxParamInfo &p : params) {
        names << p.name;
    }
    REQUIRE(names.contains(QStringLiteral("HorizontalPixels")));
    REQUIRE(names.contains(QStringLiteral("VerticalPixels")));
}

TEST_CASE("Real VEGAS Chroma Blur OFX plug-in actually changes pixels",
         "[video-fx][ofx][vegas-video]")
{
    int argc = 1;
    char arg0[] = "test";
    char *argv[] = {arg0, nullptr};
    ensureQtApp(argc, argv);

    const QString root = samplesVegasRoot();
    if (root.isEmpty() || !QDir(root).exists()) {
        SKIP("SAMPLES/VEGAS-PRO-22-PROGRAM-FILES not available");
    }

    VegasVideoPluginCatalog::invalidateCache();
    VegasVideoPluginCatalog::discover({root});

    const VegasVideoPluginEntry *entry = VegasVideoPluginCatalog::findByEffectId(
        QStringLiteral("com.vegascreativesoftware:chromablur"));
    REQUIRE(entry != nullptr);
    REQUIRE(entry->hasBinary);

    OfxPluginDesc desc;
    desc.path = entry->binaryPath;
    desc.bundlePath = entry->bundlePath;
    desc.effectId = entry->effectId;
    desc.pluginIndex = entry->pluginIndex;
    desc.hasBinary = true;

    QString err;
    const int id = OfxHost::instance().createInstance(desc, &err);
    REQUIRE(id > 0);

    // A hard vertical edge: a blur has to bleed one half into the other.
    QImage frame(512, 512, QImage::Format_ARGB32);
    for (int y = 0; y < frame.height(); ++y) {
        for (int x = 0; x < frame.width(); ++x) {
            frame.setPixel(x, y, x < frame.width() / 2 ? qRgb(255, 0, 0) : qRgb(0, 0, 255));
        }
    }
    const QImage before = frame.copy();

    QVariantMap params;
    params.insert(QStringLiteral("HorizontalPixels"), 8.0);
    params.insert(QStringLiteral("VerticalPixels"), 8.0);
    REQUIRE(OfxHost::instance().processFrame(id, &frame, 0.0, params, &err));
    // A fallback would have reported itself in err; a real render clears it.
    REQUIRE(err.isEmpty());
    REQUIRE(frame.size() == before.size());
    REQUIRE(frame != before);

    // Small frames are the case that used to corrupt the heap: the plug-in splits the
    // render window by threadIndex/threadMax itself and misbehaves once a band gets close
    // to its kernel radius. The host caps threads by frame height to keep bands large —
    // without that cap this call takes the process down rather than failing.
    QImage tiny(64, 64, QImage::Format_ARGB32);
    tiny.fill(qRgb(255, 0, 0));
    for (int y = 0; y < tiny.height(); ++y) {
        for (int x = 32; x < tiny.width(); ++x) {
            tiny.setPixel(x, y, qRgb(0, 0, 255));
        }
    }
    const QImage tinyBefore = tiny.copy();
    REQUIRE(OfxHost::instance().processFrame(id, &tiny, 0.0, params, &err));
    REQUIRE(err.isEmpty());
    REQUIRE(tiny != tinyBefore);

    OfxHost::instance().destroyInstance(id);
}

TEST_CASE("Video FX drag payload round-trips through the timeline's MIME format",
          "[video-fx][vegas-video][dnd]")
{
    int argc = 1;
    char arg0[] = "test";
    char *argv[] = {arg0, nullptr};
    ensureQtApp(argc, argv);

    // What VideoFxPane puts on the drag: name = plug-in, extra = preset. The timeline
    // dispatches on the kind, so a change to either side that breaks this contract turns
    // the drop into a silently ignored no-op.
    std::unique_ptr<QMimeData> md(MediaMime::fromSynthetic(
        QStringLiteral("videofx"), QStringLiteral("Chroma Blur"), QStringLiteral("Sparkle")));
    REQUIRE(md);
    REQUIRE(MediaMime::hasMediaPayload(md.get()));

    QStringList names;
    QStringList kinds;
    QStringList paths;
    QVector<double> lengths;
    QStringList extras;
    MediaMime::parse(md.get(), &names, &kinds, &paths, &lengths, &extras);
    REQUIRE(kinds.value(0) == QStringLiteral("videofx"));
    REQUIRE(names.value(0) == QStringLiteral("Chroma Blur"));
    REQUIRE(extras.value(0) == QStringLiteral("Sparkle"));

    // A plug-in row drags with no preset — the extra is empty, not missing.
    std::unique_ptr<QMimeData> plain(MediaMime::fromSynthetic(QStringLiteral("videofx"),
                                                              QStringLiteral("Glint")));
    MediaMime::parse(plain.get(), &names, &kinds, &paths, &lengths, &extras);
    REQUIRE(kinds.value(0) == QStringLiteral("videofx"));
    REQUIRE(names.value(0) == QStringLiteral("Glint"));
    REQUIRE(extras.value(0).isEmpty());
}

TEST_CASE("A dropped Video FX becomes a real slot carrying its preset",
          "[video-fx][vegas-video][dnd]")
{
    int argc = 1;
    char arg0[] = "test";
    char *argv[] = {arg0, nullptr};
    ensureQtApp(argc, argv);

    const FxSlot slot = VegasVideoPluginCatalog::slotFromDisplayName(QStringLiteral("Glint"));
    REQUIRE_FALSE(slot.displayName.isEmpty());
    REQUIRE_FALSE(slot.hostKey.isEmpty()); // instances are keyed by it; a blank one collides

    QVariantMap params = unpackFxParams(slot.state);
    params.insert(fxVegasPresetStateKey(), QStringLiteral("Sparkle"));
    FxSlot withPreset = slot;
    withPreset.state = packFxParams(params);
    REQUIRE(fxVegasPresetName(withPreset) == QStringLiteral("Sparkle"));
    // An unknown name must not silently produce an empty slot the timeline would append.
    REQUIRE_FALSE(
        VegasVideoPluginCatalog::slotFromDisplayName(QStringLiteral("No Such Effect"))
            .displayName.isEmpty());
}

TEST_CASE("Transitions and generators are not video effects", "[video-fx][vegas-video]")
{
    int argc = 1;
    char arg0[] = "test";
    char *argv[] = {arg0, nullptr};
    ensureQtApp(argc, argv);
    const QString root = samplesVegasRoot();
    if (root.isEmpty() || !QDir(root).exists()) {
        SKIP("SAMPLES/VEGAS-PRO-22-PROGRAM-FILES not available");
    }
    VegasVideoPluginCatalog::invalidateCache();
    const auto entries = VegasVideoPluginCatalog::discover({root});
    VegasVideoPluginCatalog::invalidateCache();
    REQUIRE_FALSE(entries.isEmpty());

    auto find = [&](const QString &effectId) -> const VegasVideoPluginEntry * {
        for (const VegasVideoPluginEntry &e : entries) {
            if (e.effectId.compare(effectId, Qt::CaseInsensitive) == 0) {
                return &e;
            }
        }
        return nullptr;
    };

    // VEGAS groups Page Roll under a bare "VEGAS" — exactly like a real effect — so only
    // the declared OFX context separates them. Without this the Video FX pane listed every
    // transition in the bundle (Page Roll, Push, Slide, Swap, …) as an effect.
    const VegasVideoPluginEntry *pageRoll = find(QStringLiteral("com.vegascreativesoftware:pageroll"));
    REQUIRE(pageRoll != nullptr);
    REQUIRE(pageRoll->isTransition());
    REQUIRE_FALSE(pageRoll->isVideoFx());

    const VegasVideoPluginEntry *blur = find(QStringLiteral("com.vegascreativesoftware:chromablur"));
    REQUIRE(blur != nullptr);
    REQUIRE(blur->isVideoFx());
    REQUIRE_FALSE(blur->isTransition());

    // A generator belongs to the Media Generator pane, not Video FX.
    const VegasVideoPluginEntry *checker =
        find(QStringLiteral("com.vegascreativesoftware:checkerboard"));
    REQUIRE(checker != nullptr);
    REQUIRE_FALSE(checker->isVideoFx());

    // Category chips are the pane's own labels, not the raw grouping tail.
    const VegasVideoPluginEntry *stab =
        find(QStringLiteral("com.magix.ofx.vr.stabilization"));
    if (stab) {
        REQUIRE(stab->grouping == QStringLiteral(R"(VEGAS\360)"));
        REQUIRE(stab->categories == QStringList{QStringLiteral("360°")});
        // Read from the manifest named after the bundle, not whichever XML sorted first.
        REQUIRE(stab->displayName == QStringLiteral("360° Stabilization"));
    }
    // Chroma Blur is filed under VEGAS\Utility, not VEGAS\Blur — the chip follows the
    // manifest, not the effect's name.
    REQUIRE(blur->grouping == QStringLiteral(R"(VEGAS\Utility)"));
    REQUIRE(blur->categories == QStringList{QStringLiteral("Utility")});
    REQUIRE_FALSE(blur->description.isEmpty());
}
