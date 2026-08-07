#include <catch2/catch_test_macros.hpp>

#include "io/SamplePaths.h"
#include "plugins/AudioPluginTypes.h"
#include "plugins/OfxHost.h"
#include "plugins/VegasVideoPluginCatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>

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

TEST_CASE("OfxHost::paramsForSlot fails soft when a real Vegas OFX plug-in can't fully Load",
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
    // Vfx1.ofx answers kOfxActionLoad with kOfxStatErrMissingHostFeature against
    // this minimal host, so Describe never runs and no real params are available
    // (see MARKDOWN/ISSUES_AND_PLANS.md). This locks in fail-soft behavior — no
    // crash, just empty — so callers (VideoEventFxDialogExact) must keep an
    // approximate fallback. If this ever starts failing, the host gained the
    // missing feature and paramsInfoForSlot()'s real-vs-fallback branch should be
    // re-verified against the new real param set.
    REQUIRE(params.isEmpty());
}
