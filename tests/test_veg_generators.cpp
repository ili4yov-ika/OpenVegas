#include "io/SamplePaths.h"
#include "io/VegReader.h"
#include "model/ProjectModel.h"
#include "video/GeneratorPresetData.h"
#include "video/MediaGeneratorApply.h"

#include <QColor>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace openvegas;

namespace {

QString samplePath(const QString &name)
{
    return SamplePaths::resolveProjectPath(QStringLiteral("SAMPLES/veg_projects/") + name);
}

const QString kGeneratorsSample =
    QStringLiteral("project_media-generators-without-titles-and-text.veg");

} // namespace

TEST_CASE("Every event names the pool media its take plays", "[veg][generators]")
{
    // The take nested in each event carries the media-pool id; checked here on a project of
    // generators, one of files and one of Titles & Text, where order-based guessing either
    // had nothing to go on or paired the wrong clip.
    for (const QString &name : {kGeneratorsSample, QStringLiteral("project_big--buck-bunny+other-video.veg"),
                                QStringLiteral("project_titles-and-text.veg")}) {
        const QString path = samplePath(name);
        if (!QFileInfo::exists(path)) {
            WARN("missing sample: " << name.toStdString());
            continue;
        }
        const VegOpenResult veg = VegReader::open(path);
        QSet<qint64> ids;
        for (const VegPoolMedia &m : veg.mediaPool) {
            ids.insert(m.mediaId);
        }
        REQUIRE(!veg.events.isEmpty());
        for (const VegEventInfo &e : veg.events) {
            INFO(name.toStdString() << " event at " << e.startSec);
            CHECK(ids.contains(e.mediaRef));
        }
    }
}

TEST_CASE("Generated media come out of the pool with their generator and preset",
          "[veg][generators]")
{
    const QString path = samplePath(kGeneratorsSample);
    if (!QFileInfo::exists(path)) {
        SKIP("sample project not available");
    }
    const VegOpenResult veg = VegReader::open(path);
    QHash<QString, int> count;
    QHash<QString, const VegPoolMedia *> first;
    for (const VegPoolMedia &m : veg.mediaPool) {
        REQUIRE(m.isGenerated());
        ++count[m.generatorId];
        if (!first.contains(m.generatorId)) {
            first.insert(m.generatorId, &m);
        }
    }
    CHECK(count.value(QStringLiteral("{Svfx:com.vegascreativesoftware:checkerboard}")) == 13);
    CHECK(count.value(QStringLiteral("{Svfx:com.vegascreativesoftware:noisetexture}")) == 20);
    CHECK(count.value(QStringLiteral("{Svfx:com.vegascreativesoftware:solidcolor}")) == 10);
    CHECK(count.value(QStringLiteral("{Svfx:com.vegascreativesoftware:testpattern}")) == 14);
    // The two legacy DirectX generators, under their CLSIDs.
    CHECK(count.value(QStringLiteral("{8B5DEABC-53BD-49F6-9803-367478307A59}")) == 18);
    CHECK(count.value(QStringLiteral("{02FD3B60-986B-11D4-9F6F-00485481BD31}")) == 8);

    // OFX generators bring their stored parameters; the legacy two only their preset name.
    const VegPoolMedia *checker = first.value(QStringLiteral("{Svfx:com.vegascreativesoftware:checkerboard}"));
    REQUIRE(checker);
    CHECK(checker->presetName == QStringLiteral("(Default)"));
    CHECK(checker->ofx.params.size() == 13);
    const VegPoolMedia *credit = first.value(QStringLiteral("{02FD3B60-986B-11D4-9F6F-00485481BD31}"));
    REQUIRE(credit);
    CHECK(credit->displayName == QStringLiteral("VEGAS Credit Roll"));
    CHECK(credit->presetName == QStringLiteral("Plain Scrolling on Black"));
    CHECK(credit->ofx.params.isEmpty());
    const VegPoolMedia *gradient = first.value(QStringLiteral("{8B5DEABC-53BD-49F6-9803-367478307A59}"));
    REQUIRE(gradient);
    CHECK(gradient->presetName == QStringLiteral("Linear White to Black"));
}

TEST_CASE("Generator events import as generators, not as empty clips with effects",
          "[veg][generators]")
{
    const QString path = samplePath(kGeneratorsSample);
    if (!QFileInfo::exists(path)) {
        SKIP("sample project not available");
    }
    const VegOpenResult veg = VegReader::open(path);
    QHash<qint64, QString> generatorOf;
    for (const VegPoolMedia &m : veg.mediaPool) {
        generatorOf.insert(m.mediaId, m.generatorId);
    }
    QHash<QString, int> expected;
    for (const VegEventInfo &e : veg.events) {
        ++expected[generatorOf.value(e.mediaRef)];
    }

    ProjectModel model;
    model.applyVegImport(veg, path, false);
    QHash<QString, int> got;
    int events = 0;
    for (const Track &t : model.tracks()) {
        for (const TrackEvent &e : t.events) {
            ++events;
            INFO("event " << e.name.toStdString() << " at " << e.startSec);
            CHECK(e.mediaPath.isEmpty());
            CHECK(e.mediaKind == EventMediaKind::Title);
            // Before, the first event carried every OFX generator in the file as an Event FX.
            REQUIRE(e.fxChain.size() == 1);
            REQUIRE(isMediaGeneratorPluginId(e.fxChain.first().pluginId));
            const MediaGeneratorParams gp = mediaGeneratorFromSlot(e.fxChain.first());
            ++got[gp.vegasPluginId];
        }
    }
    CHECK(events == veg.events.size());
    CHECK(got == expected);

    // The first event is the Checkerboard "(Default)": black and white, tiles a tenth of
    // the frame wide — read from the project, not the browser's stand-in values.
    const TrackEvent *firstEvent = nullptr;
    for (const Track &t : model.tracks()) {
        for (const TrackEvent &e : t.events) {
            if (!firstEvent || e.startSec < firstEvent->startSec) {
                firstEvent = &e;
            }
        }
    }
    REQUIRE(firstEvent);
    const MediaGeneratorParams checker = mediaGeneratorFromSlot(firstEvent->fxChain.first());
    CHECK(checker.pluginName == QStringLiteral("Checkerboard"));
    CHECK(checker.vegasPreset == QStringLiteral("(Default)"));
    CHECK(checker.c0 == QColor(Qt::black));
    CHECK(checker.c1 == QColor(Qt::white));
    CHECK(checker.tile == 10);
    CHECK(checker.vegasParams.value(QStringLiteral("TileDimensionsWidth")).toDouble()
          == Catch::Approx(0.1));

    // Solid Color "White" fills with the colour it stores.
    bool sawWhite = false;
    for (const Track &t : model.tracks()) {
        for (const TrackEvent &e : t.events) {
            const MediaGeneratorParams gp = mediaGeneratorFromSlot(e.fxChain.first());
            if (gp.pluginName == QStringLiteral("Solid Color") && gp.vegasPreset == QStringLiteral("White")) {
                sawWhite = true;
                CHECK(gp.c0 == QColor(Qt::white));
                CHECK(gp.c1 == QColor(Qt::white));
            }
        }
    }
    CHECK(sawWhite);
}


TEST_CASE("The generator browser offers every preset VEGAS ships", "[veg][generators]")
{
    // From VEGAS's PresetPackage.xml, sftrans1.dll's string table and the "(Default)" VEGAS
    // saves — counted here so a regenerated table that lost a group shows up at once.
    QHash<QString, int> sizes;
    for (const StockGeneratorGroup &g : stockGeneratorPresets()) {
        sizes.insert(g.name, g.presets.size());
    }
    CHECK(sizes.value(QStringLiteral("Checkerboard")) == 13);
    CHECK(sizes.value(QStringLiteral("Color Gradient")) == 18);
    CHECK(sizes.value(QStringLiteral("Credit Roll")) == 8);
    CHECK(sizes.value(QStringLiteral("Noise Texture")) == 20);
    CHECK(sizes.value(QStringLiteral("Solid Color")) == 10);
    CHECK(sizes.value(QStringLiteral("Test Pattern")) == 10);

    // A project made in VEGAS 22 uses no preset name the table lacks, and for the OFX
    // generators the values VEGAS saved are the table's values.
    const QString path = samplePath(kGeneratorsSample);
    if (!QFileInfo::exists(path)) {
        SKIP("sample project not available");
    }
    const VegOpenResult veg = VegReader::open(path);
    for (const VegPoolMedia &m : veg.mediaPool) {
        INFO(m.generatorId.toStdString() << " / " << m.presetName.toStdString());
        const StockGeneratorPreset *stock = stockGeneratorPreset(m.generatorId, m.presetName);
        REQUIRE(stock);
        const QVariantMap saved = vegOfxParamMap(m.ofx);
        for (auto it = stock->params.constBegin(); it != stock->params.constEnd(); ++it) {
            INFO("parameter " << it.key().toStdString());
            REQUIRE(saved.contains(it.key()));
            const QVariantList want = it.value().toList();
            if (want.isEmpty()) {
                CHECK(saved.value(it.key()).toDouble() == Catch::Approx(it.value().toDouble()).margin(1e-6));
            } else {
                const QVariantList got = saved.value(it.key()).toList();
                REQUIRE(got.size() == want.size());
                for (int k = 0; k < want.size(); ++k) {
                    CHECK(got[k].toDouble() == Catch::Approx(want[k].toDouble()).margin(1e-6));
                }
            }
        }
    }
}

TEST_CASE("A preset dragged from the browser arrives with VEGAS's values", "[veg][generators]")
{
    const QString noise = QStringLiteral("{Svfx:com.vegascreativesoftware:noisetexture}");
    const StockGeneratorPreset *lava = stockGeneratorPreset(noise, QStringLiteral("Lava"));
    REQUIRE(lava);
    MediaGeneratorParams dragged = mediaGeneratorFromVegas(noise, lava->name, lava->params);
    const MediaGeneratorParams placed =
        mediaGeneratorParamsFromPayload(mediaGeneratorParamsToPayload(dragged));
    CHECK(placed.pluginName == QStringLiteral("Noise Texture"));
    CHECK(placed.vegasPluginId == noise);
    CHECK(placed.vegasPreset == QStringLiteral("Lava"));
    CHECK(placed.vegasParams == lava->params);
    CHECK(placed.c0 == dragged.c0);
    CHECK(placed.c1 == dragged.c1);

    // A payload from before the VEGAS fields still decodes as it did.
    const MediaGeneratorParams old =
        mediaGeneratorParamsFromPayload(QStringLiteral("Checkerboard|1|#ff111111|#ffeeeeee|12"));
    CHECK(old.pluginName == QStringLiteral("Checkerboard"));
    CHECK(old.tile == 12);
    CHECK(old.vegasPluginId.isEmpty());
}
