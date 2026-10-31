#include "TestQtApp.h"

#include "io/SamplePaths.h"
#include "io/VegReader.h"
#include "model/ProjectModel.h"
#include "video/GeneratorPresetData.h"
#include "video/MediaGeneratorApply.h"
#include "video/TitlesTextApply.h"
#include "video/VegasGenerators.h"

#include <QColor>
#include <QXmlStreamReader>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <cmath>

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

/** Two stored values alike: numbers within 1e-6, lists and maps member by member. */
static bool sameValue(const QVariant &a, const QVariant &b)
{
    if (a.typeId() == QMetaType::QVariantList || b.typeId() == QMetaType::QVariantList) {
        const QVariantList x = a.toList(), y = b.toList();
        if (x.size() != y.size()) {
            return false;
        }
        for (int i = 0; i < x.size(); ++i) {
            if (!sameValue(x[i], y[i])) {
                return false;
            }
        }
        return true;
    }
    if (a.typeId() == QMetaType::QVariantMap || b.typeId() == QMetaType::QVariantMap) {
        const QVariantMap x = a.toMap(), y = b.toMap();
        if (x.keys() != y.keys()) {
            return false;
        }
        for (auto it = x.constBegin(); it != x.constEnd(); ++it) {
            if (!sameValue(it.value(), y.value(it.key()))) {
                return false;
            }
        }
        return true;
    }
    if (a.typeId() == QMetaType::QString || b.typeId() == QMetaType::QString) {
        return a.toString() == b.toString();
    }
    return std::abs(a.toDouble() - b.toDouble()) <= 1e-6;
}

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

    // OFX generators bring their OFX parameter record; the legacy two their own block.
    const VegPoolMedia *checker = first.value(QStringLiteral("{Svfx:com.vegascreativesoftware:checkerboard}"));
    REQUIRE(checker);
    CHECK(checker->presetName == QStringLiteral("(Default)"));
    CHECK(checker->ofx.params.size() == 13);
    const VegPoolMedia *credit = first.value(QStringLiteral("{02FD3B60-986B-11D4-9F6F-00485481BD31}"));
    REQUIRE(credit);
    CHECK(credit->displayName == QStringLiteral("VEGAS Credit Roll"));
    CHECK(credit->presetName == QStringLiteral("Plain Scrolling on Black"));
    CHECK(credit->ofx.params.isEmpty());
    // Scrolling credits, the text area a tenth in from each side, black behind, and the
    // sample text VEGAS starts every credit roll with: a header (leading space), an item
    // and two two-column lines.
    CHECK(credit->legacyParams.value(QStringLiteral("Effect")).toInt() == 0);
    CHECK(credit->legacyParams.value(QStringLiteral("Left")).toDouble() == Catch::Approx(0.1));
    CHECK(credit->legacyParams.value(QStringLiteral("Right")).toDouble() == Catch::Approx(0.9));
    CHECK(credit->legacyParams.value(QStringLiteral("Text")).toString()
          == QStringLiteral(" Title Text\r\nSubitem Text\r\nItem Left\tItem Right\r\n"
                            "Longer Item Left\tLonger Item Right\r\n"));
    CHECK(credit->legacyParams.value(QStringLiteral("Styles")).toList().size() == 5);
    const VegPoolMedia *gradient = first.value(QStringLiteral("{8B5DEABC-53BD-49F6-9803-367478307A59}"));
    REQUIRE(gradient);
    CHECK(gradient->presetName == QStringLiteral("Linear White to Black"));
    // Linear, white at the left edge to black at the right, across the middle.
    CHECK(gradient->legacyParams.value(QStringLiteral("GradientType")).toInt() == 0);
    const QVariantList points = gradient->legacyParams.value(QStringLiteral("Points")).toList();
    REQUIRE(points.size() == 2);
    CHECK(points[0].toMap().value(QStringLiteral("X")).toDouble() == Catch::Approx(0.0));
    CHECK(points[1].toMap().value(QStringLiteral("X")).toDouble() == Catch::Approx(1.0));
    CHECK(points[0].toMap().value(QStringLiteral("Color")).toList()
          == QVariantList{1.0, 1.0, 1.0, 1.0});
    CHECK(points[1].toMap().value(QStringLiteral("Color")).toList()
          == QVariantList{0.0, 0.0, 0.0, 1.0});
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

    // A project made in VEGAS 22 uses no preset name the table lacks, and the values VEGAS
    // saved are the table's values — the OFX generators' records and the legacy two's blocks.
    const QString path = samplePath(kGeneratorsSample);
    if (!QFileInfo::exists(path)) {
        SKIP("sample project not available");
    }
    const VegOpenResult veg = VegReader::open(path);
    for (const VegPoolMedia &m : veg.mediaPool) {
        INFO(m.generatorId.toStdString() << " / " << m.presetName.toStdString());
        const StockGeneratorPreset *stock = stockGeneratorPreset(m.generatorId, m.presetName);
        REQUIRE(stock);
        const QVariantMap saved = m.isLegacyGenerator() ? m.legacyParams : vegOfxParamMap(m.ofx);
        if (m.isLegacyGenerator()) {
            REQUIRE_FALSE(saved.isEmpty());
        }
        for (auto it = stock->params.constBegin(); it != stock->params.constEnd(); ++it) {
            INFO("parameter " << it.key().toStdString());
            REQUIRE(saved.contains(it.key()));
            CHECK(sameValue(saved.value(it.key()), it.value()));
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

TEST_CASE("An animated generator parameter is read off its curve at each frame",
          "[veg][generators]")
{
    const StockGeneratorPreset *def = stockGeneratorPreset(
        QStringLiteral("{Svfx:com.vegascreativesoftware:checkerboard}"), QStringLiteral("(Default)"));
    REQUIRE(def);
    MediaGeneratorParams p = mediaGeneratorFromVegas(
        QStringLiteral("{Svfx:com.vegascreativesoftware:checkerboard}"), def->name, def->params);
    // Tiles from a tenth of the frame at 1 s to half of it at 3 s; colours stay put.
    p.vegasCurves.insert(QStringLiteral("TileDimensionsWidth"), QVariantList{1.0, 0.1, 3.0, 0.5});
    p.vegasCurves.insert(QStringLiteral("Color1"), QVariantList{0.0, 0.0, 3.0, 1.0});
    auto width = [&](double t) {
        return mediaGeneratorParamsAt(p, t).value(QStringLiteral("TileDimensionsWidth")).toDouble();
    };
    CHECK(width(0.0) == Catch::Approx(0.1));
    CHECK(width(2.0) == Catch::Approx(0.3));
    CHECK(width(5.0) == Catch::Approx(0.5));
    CHECK(mediaGeneratorParamsAt(p, 2.0).value(QStringLiteral("Color1")).toList().size() == 4);
    // And the picture follows: bigger tiles at the end than at the start.
    const QImage early = renderMediaGenerator(p, QSize(200, 100), 0.0);
    const QImage late = renderMediaGenerator(p, QSize(200, 100), 3.0);
    CHECK(early != late);
}

TEST_CASE("A timed credit roll brings each group in and out with its effect", "[veg][generators]")
{
    openvegas::test::ensureQtApp(); // text needs the font database
    const QString credits = QStringLiteral("{02FD3B60-986B-11D4-9F6F-00485481BD31}");
    const StockGeneratorPreset *fade = stockGeneratorPreset(credits, QStringLiteral("Timed, Fade on Black"));
    REQUIRE(fade);
    const QSize size(160, 90);
    const QImage background = renderVegasGenerator(credits, QVariantMap{{QStringLiteral("Background"),
                                                                         QVariantList{0.0, 0.0, 0.0, 1.0}}},
                                                   size, 0.0, 9.0);
    // Three groups over nine seconds; the first fades in from nothing.
    CHECK(renderVegasGenerator(credits, fade->params, size, 0.0, 9.0) == background);
    CHECK(renderVegasGenerator(credits, fade->params, size, 1.5, 9.0) != background);
    // Wipes cover part of the group on the way in: half-way through the wipe, only some of it.
    const StockGeneratorPreset *wipe =
        stockGeneratorPreset(credits, QStringLiteral("Timed, Wipe In/Out, on Black"));
    REQUIRE(wipe);
    const QImage held = renderVegasGenerator(credits, wipe->params, size, 1.5, 9.0);
    const QImage wiping = renderVegasGenerator(credits, wipe->params, size, 0.3, 9.0);
    CHECK(wiping != held);
}

TEST_CASE("Titles & Text imports the anchor and every shadow field VEGAS's presets set",
          "[veg][titles-and-text]")
{
    // project_big--buck-bunny_titles-and-text.veg is built from VEGAS's stock presets, so each
    // instance has to carry the values its preset ships with in PresetPackage.xml. The
    // anchor was not read at all, and shadow fields past 2000 bytes into a long text's
    // record came back as their defaults.
    const QString veg = samplePath(QStringLiteral("project_big--buck-bunny_titles-and-text.veg"));
    const QString pkg = QDir(SamplePaths::samplesDir())
                            .filePath(QStringLiteral("VEGAS-PRO-22-PROGRAM-FILES/OFX Video Plug-Ins/"
                                                     "TitlesAndText.ofx.bundle/Contents/Presets/"
                                                     "PresetPackage.xml"));
    if (!QFileInfo::exists(veg) || !QFileInfo::exists(pkg)) {
        SKIP("sample project or VEGAS preset package not available");
    }
    // AnimationName -> parameter -> first value, for presets with an animation of their own.
    QHash<QString, QHash<QString, QString>> presets;
    QSet<QString> shared;
    {
        QFile f(pkg);
        REQUIRE(f.open(QIODevice::ReadOnly));
        QXmlStreamReader xml(&f);
        QHash<QString, QString> current;
        QString param;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QLatin1String("OfxPreset")) {
                current.clear();
            } else if (xml.isStartElement() && xml.name().startsWith(QLatin1String("OfxParamType"))) {
                param = xml.attributes().value(QLatin1String("name")).toString();
            } else if (xml.isStartElement() && xml.name() == QLatin1String("OfxParamValue")) {
                current.insert(param, xml.readElementText().trimmed());
            } else if (xml.isEndElement() && xml.name() == QLatin1String("OfxPreset")) {
                const QString anim = current.value(QStringLiteral("AnimationName"));
                if (presets.contains(anim)) {
                    shared.insert(anim);
                }
                presets.insert(anim, current);
            }
        }
    }
    const VegOpenResult result = VegReader::open(veg);
    int compared = 0;
    for (const VegTitleTextInfo &t : result.titlesAndText) {
        if (!presets.contains(t.animationName) || shared.contains(t.animationName)) {
            continue; // "_None" and the like: several presets, no way to tell which
        }
        const QHash<QString, QString> &ref = presets.value(t.animationName);
        INFO(t.animationName.toStdString());
        const QStringList loc = ref.value(QStringLiteral("Location")).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        CHECK(t.anchor == ref.value(QStringLiteral("Alignment"), QStringLiteral("4")).toInt());
        CHECK(t.locationX == Catch::Approx(loc.value(0).toDouble()).margin(1e-4));
        CHECK(t.locationY == Catch::Approx(loc.value(1).toDouble()).margin(1e-4));
        CHECK(t.shadowEnable == (ref.value(QStringLiteral("ShadowEnable")) == QLatin1String("true")));
        CHECK(t.shadowOffsetX == Catch::Approx(ref.value(QStringLiteral("ShadowOffsetX")).toDouble()).margin(1e-4));
        CHECK(t.shadowOffsetY == Catch::Approx(ref.value(QStringLiteral("ShadowOffsetY")).toDouble()).margin(1e-4));
        CHECK(t.shadowBlur == Catch::Approx(ref.value(QStringLiteral("ShadowBlur")).toDouble()).margin(1e-4));
        ++compared;
    }
    CHECK(compared >= 40);
}

TEST_CASE("Titles & Text presets place everything VEGAS's preset sets", "[veg][titles-and-text]")
{
    openvegas::test::ensureQtApp();
    const QVector<TitlesTextStockPreset> &stock = titlesTextStockPresets();
    // "(Default)" and the 52 presets of the package, in VEGAS's browser order.
    REQUIRE(stock.size() == 53);
    CHECK(stock.first().name == QStringLiteral("(Default)"));
    CHECK(stock.first().params.text == QStringLiteral("Sample Text"));
    CHECK(stock[1].name == QStringLiteral("Title01"));
    CHECK(stock.last().name == QStringLiteral("Subtitles"));

    const TitlesTextParams title03 = titlesTextPresetParams(QStringLiteral("Title03"));
    CHECK(title03.text == QStringLiteral("FREE CUSTOMER\nSUPPORT"));
    CHECK(title03.fontFamily == QStringLiteral("Arial"));
    CHECK(title03.locationX == Catch::Approx(0.52));
    CHECK(title03.locationY == Catch::Approx(0.589474));
    CHECK(title03.lineSpacing == Catch::Approx(0.8));
    CHECK(title03.scale == Catch::Approx(1.3));

    // Placeholder and Subtitles share "_None" with "(Default)" and differ in everything else.
    const TitlesTextParams placeholder = titlesTextPresetParams(QStringLiteral("Placeholder"));
    CHECK(placeholder.textColor == QColor(255, 0, 0));
    CHECK(placeholder.fontFamily == QStringLiteral("Tahoma"));
    const TitlesTextParams subtitles = titlesTextPresetParams(QStringLiteral("Subtitles"));
    CHECK(subtitles.shadowEnable);
    CHECK(subtitles.locationY == Catch::Approx(0.1));
    CHECK(titlesTextPresetParams(QStringLiteral("_None")).text == QStringLiteral("Sample Text"));
    // An animation key, as older drags carry, still finds its preset.
    CHECK(titlesTextPresetParams(QStringLiteral("_Bounce")).outlineWidth == Catch::Approx(6.0));

    // Location's Y counts up from the bottom: Subtitles sits low in the frame, as in VEGAS.
    const QRectF box = titlesTextBoundingBox(subtitles, QSize(1920, 1080));
    CHECK(box.center().y() > 1080 * 0.8);

    ProjectModel model;
    const int id = model.addTitlesTextEvent(QStringLiteral("Subtitles"));
    const TrackEvent *ev = model.findEvent(id);
    REQUIRE(ev);
    REQUIRE_FALSE(ev->fxChain.isEmpty());
    CHECK(ev->fxChain.first().displayName == QStringLiteral("Titles & Text"));
    const TitlesTextParams placed = titlesTextFromSlot(ev->fxChain.first());
    CHECK(placed.text == QStringLiteral("Subtitles"));
    CHECK(placed.shadowEnable);
    CHECK(placed.locationY == Catch::Approx(0.1));
    CHECK(placed.lineSpacing == Catch::Approx(0.8));
}

TEST_CASE("A Titles & Text preset opened from a project matches the same preset placed",
          "[veg][titles-and-text]")
{
    // The project is built from VEGAS's stock presets, so each instance with an animation of
    // its own should read back as that preset does from the table — text included, which the
    // importer used to break into a line per paragraph plus an empty one.
    const QString veg = samplePath(QStringLiteral("project_big--buck-bunny_titles-and-text.veg"));
    if (!QFileInfo::exists(veg)) {
        SKIP("sample project not available");
    }
    const VegOpenResult result = VegReader::open(veg);
    int compared = 0;
    for (const VegTitleTextInfo &t : result.titlesAndText) {
        if (t.animationName == QLatin1String("_None") || !t.animationName.startsWith(QLatin1Char('_'))) {
            continue;
        }
        const TitlesTextParams ref = titlesTextPresetParams(t.animationName);
        INFO(t.animationName.toStdString());
        CHECK(t.text.toStdString() == ref.text.toStdString());
        CHECK(t.fontFamily == ref.fontFamily);
        CHECK(t.fontSize == Catch::Approx(ref.fontSize));
        CHECK(t.italic == ref.italic);
        CHECK(t.lineSpacing == Catch::Approx(ref.lineSpacing).margin(1e-4));
        CHECK(t.tracking == Catch::Approx(ref.tracking).margin(1e-4));
        CHECK(t.outlineWidth == Catch::Approx(ref.outlineWidth).margin(1e-4));
        ++compared;
    }
    CHECK(compared >= 40);
}
