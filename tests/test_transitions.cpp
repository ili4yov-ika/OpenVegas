#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "video/TransitionApply.h"
#include "video/TransitionPresetData.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSet>

#include <algorithm>

using namespace openvegas;

namespace {

// renderTransitionPreview() draws text, which needs a QGuiApplication before the font
// database is touched (same reason test_titles_text.cpp has this helper).
void ensureQtGuiApp()
{
    if (QCoreApplication::instance()) {
        return;
    }
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    static int argc = 1;
    static char appName[] = "openvegas_video_tests";
    static char *argv[] = {appName, nullptr};
    static QGuiApplication app(argc, argv);
    Q_UNUSED(app);
}

QImage solid(const QSize &size, QRgb color)
{
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(color);
    return img;
}

/** Share of fully opaque pixels equal to `color` — the transition's own gaps are
 *  transparent, so this measures "how much of that clip is still showing". */
double coverage(const QImage &img, QRgb color)
{
    int hits = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (img.pixel(x, y) == color) {
                ++hits;
            }
        }
    }
    return double(hits) / double(std::max(1, img.width() * img.height()));
}

} // namespace

TEST_CASE("3D Blinds is in the catalog with its four Vegas presets", "[video][transitions]")
{
    const TransitionPluginInfo *info = transitionPluginById(transition3dBlindsId());
    REQUIRE(info);
    CHECK(info->name == QStringLiteral("3D Blinds"));
    REQUIRE(info->presets.size() == 4);
    CHECK(info->presets[0].name == QStringLiteral("Simple"));
    CHECK(info->presets[1].name == QStringLiteral("Left to Right"));
    CHECK(info->presets[2].name == QStringLiteral("Slot Machine"));
    CHECK(info->presets[3].name == QStringLiteral("Spin"));
}

TEST_CASE("3D Blinds parameter ranges match the reference screenshots",
         "[video][transitions]")
{
    // Read off the extreme-value captures in
    // SAMPLES/screenshots/Transitions/3D_Blinds/: Divisions 1…16, Extra spins 0…10,
    // Stagger and Specular light 0…1.
    const TransitionPluginInfo *info = transitionPluginById(transition3dBlindsId());
    REQUIRE(info);
    QHash<QString, const TransitionParamInfo *> byKey;
    for (const TransitionParamInfo &p : info->params) {
        byKey.insert(p.key, &p);
    }
    REQUIRE(byKey.contains(QStringLiteral("divisions")));
    CHECK(byKey[QStringLiteral("divisions")]->minValue == Catch::Approx(1.0));
    CHECK(byKey[QStringLiteral("divisions")]->maxValue == Catch::Approx(16.0));
    CHECK(byKey[QStringLiteral("extraSpins")]->minValue == Catch::Approx(0.0));
    CHECK(byKey[QStringLiteral("extraSpins")]->maxValue == Catch::Approx(10.0));
    CHECK(byKey[QStringLiteral("stagger")]->maxValue == Catch::Approx(1.0));
    CHECK(byKey[QStringLiteral("specularLight")]->maxValue == Catch::Approx(1.0));
    // Direction is a choice row, not a slider.
    REQUIRE_FALSE(byKey[QStringLiteral("direction")]->choices.isEmpty());
}

TEST_CASE("Each 3D Blinds preset carries its documented default values",
         "[video][transitions]")
{
    struct Expected {
        const char *preset;
        double divisions;
        double extraSpins;
        double stagger;
        double specular;
        int direction; // 0 = Left to Right, 2 = Top to Bottom
    };
    // From VEGAS's shipped PresetPackage.xml, which the catalog is now generated from.
    // It corrected one number that had been transcribed off a *-default_set.png capture:
    // Slot Machine's Direction is 0, not 2. All four stock presets use 0 — the screenshot
    // evidently showed an instance someone had rotated, which a picture cannot
    // distinguish from a preset default.
    const Expected table[] = {
        {"Simple", 8, 0, 0.0, 1.0, 0},
        {"Left to Right", 4, 0, 0.2, 0.7, 0},
        {"Slot Machine", 4, 4, 0.3, 1.0, 0},
        {"Spin", 1, 0, 0.0, 1.0, 0},
    };
    for (const Expected &e : table) {
        const TransitionInstance t =
            makeTransitionInstance(transition3dBlindsId(), QString::fromLatin1(e.preset));
        INFO("preset: " << e.preset);
        REQUIRE(t.isValid());
        CHECK(t.presetName == QString::fromLatin1(e.preset));
        CHECK(transitionParamValue(t, QStringLiteral("divisions")) == Catch::Approx(e.divisions));
        CHECK(transitionParamValue(t, QStringLiteral("extraSpins")) == Catch::Approx(e.extraSpins));
        CHECK(transitionParamValue(t, QStringLiteral("stagger")) == Catch::Approx(e.stagger));
        CHECK(transitionParamValue(t, QStringLiteral("specularLight"))
              == Catch::Approx(e.specular));
        CHECK(transitionParamValue(t, QStringLiteral("direction")) == Catch::Approx(e.direction));
    }
}

TEST_CASE("makeTransitionInstance rejects an unknown plugin id", "[video][transitions]")
{
    const TransitionInstance t =
        makeTransitionInstance(QStringLiteral("builtin:Transition:NotReal"), QStringLiteral("x"));
    CHECK_FALSE(t.isValid());
}

TEST_CASE("Editing a parameter takes the instance off its preset", "[video][transitions]")
{
    // Vegas stops claiming a stock preset the moment a slider moves; the properties
    // window's preset combo relies on that to clear its selection.
    TransitionInstance t =
        makeTransitionInstance(transition3dBlindsId(), QStringLiteral("Simple"));
    REQUIRE(t.presetName == QStringLiteral("Simple"));
    transitionSetParamValue(&t, QStringLiteral("divisions"), 12);
    CHECK(t.presetName.isEmpty());
    CHECK(transitionParamValue(t, QStringLiteral("divisions")) == Catch::Approx(12.0));
}

TEST_CASE("transitionParamValue falls back to the catalog for a missing key",
         "[video][transitions]")
{
    // A project saved before a parameter existed must not render it as 0.
    TransitionInstance t;
    t.pluginId = transition3dBlindsId();
    CHECK(transitionParamValue(t, QStringLiteral("divisions")) == Catch::Approx(8.0));
}

TEST_CASE("TransitionInstance round-trips through transitionToMap/FromMap",
         "[video][transitions]")
{
    TransitionInstance t =
        makeTransitionInstance(transition3dBlindsId(), QStringLiteral("Slot Machine"));
    transitionSetParamValue(&t, QStringLiteral("stagger"), 0.75);
    const TransitionInstance back = transitionFromMap(transitionToMap(t));
    CHECK(back.pluginId == t.pluginId);
    CHECK(back.presetName == t.presetName);
    CHECK(transitionParamValue(back, QStringLiteral("stagger")) == Catch::Approx(0.75));
    CHECK(transitionParamValue(back, QStringLiteral("extraSpins")) == Catch::Approx(4.0));
}

TEST_CASE("An invalid transition serializes to an empty map", "[video][transitions]")
{
    CHECK(transitionToMap(TransitionInstance()).isEmpty());
}

TEST_CASE("renderTransition shows only the outgoing clip at progress 0 and only the "
         "incoming one at progress 1",
         "[video][transitions]")
{
    const QSize size(64, 48);
    const QRgb red = qRgb(255, 0, 0);
    const QRgb blue = qRgb(0, 0, 255);
    const QImage a = solid(size, red);
    const QImage b = solid(size, blue);
    const TransitionInstance t =
        makeTransitionInstance(transition3dBlindsId(), QStringLiteral("Simple"));

    const QImage atStart = renderTransition(a, b, 0.0, t);
    REQUIRE(atStart.size() == size);
    CHECK(coverage(atStart, red) > 0.95);
    CHECK(coverage(atStart, blue) < 0.02);

    const QImage atEnd = renderTransition(a, b, 1.0, t);
    CHECK(coverage(atEnd, blue) > 0.95);
    CHECK(coverage(atEnd, red) < 0.02);
}

TEST_CASE("renderTransition mid-way shows neither clip fully — the blinds are turning",
         "[video][transitions]")
{
    const QSize size(64, 48);
    const QRgb red = qRgb(255, 0, 0);
    const QRgb blue = qRgb(0, 0, 255);
    const TransitionInstance t =
        makeTransitionInstance(transition3dBlindsId(), QStringLiteral("Simple"));
    const QImage mid = renderTransition(solid(size, red), solid(size, blue), 0.5, t);
    CHECK(coverage(mid, red) < 0.9);
    CHECK(coverage(mid, blue) < 0.9);
}

TEST_CASE("An unknown transition still renders, as a plain cross-dissolve",
         "[video][transitions]")
{
    // Fail soft: an unsupported group must not blank the frame.
    const QSize size(32, 24);
    TransitionInstance t;
    t.pluginId = QStringLiteral("builtin:Transition:NotReal");
    const QImage out = renderTransition(solid(size, qRgb(255, 0, 0)),
                                        solid(size, qRgb(0, 0, 255)), 0.0, t);
    REQUIRE_FALSE(out.isNull());
    CHECK(coverage(out, qRgb(255, 0, 0)) > 0.95);
}

TEST_CASE("renderTransition tolerates a missing side (a fade rather than a crossfade)",
         "[video][transitions]")
{
    const QSize size(32, 24);
    const TransitionInstance t =
        makeTransitionInstance(transition3dBlindsId(), QStringLiteral("Simple"));
    const QImage fadeOut = renderTransition(solid(size, qRgb(255, 0, 0)), QImage(), 1.0, t);
    REQUIRE_FALSE(fadeOut.isNull());
    // Fully transitioned away with nothing to reveal — the frame is empty, not the clip.
    CHECK(coverage(fadeOut, qRgb(255, 0, 0)) < 0.02);
}

TEST_CASE("renderTransitionPreview produces a fully opaque tile of the requested size",
         "[video][transitions]")
{
    ensureQtGuiApp();
    const TransitionInstance t =
        makeTransitionInstance(transition3dBlindsId(), QStringLiteral("Spin"));
    const QImage tile = renderTransitionPreview(t, QSize(130, 78), 0.45);
    REQUIRE(tile.size() == QSize(130, 78));
    // The checkerboard backdrop must cover every gap, so no pixel stays transparent.
    CHECK(qAlpha(tile.pixel(0, 0)) == 255);
    CHECK(qAlpha(tile.pixel(129 / 2, 78 / 2)) == 255);
}

TEST_CASE("Every transition offers the presets VEGAS ships", "[video][transitions]")
{
    // The stock set is generated from VEGAS's own PresetPackage.xml, so a group that
    // came back with three invented "Variant" entries would show up here immediately.
    const QVector<StockTransitionGroup> &stock = stockTransitionPresets();
    REQUIRE(stock.size() >= 20);

    int totalPresets = 0;
    for (const StockTransitionGroup &g : stock) {
        INFO(g.key.toStdString());
        CHECK_FALSE(g.key.isEmpty());
        CHECK_FALSE(g.presets.isEmpty());
        totalPresets += g.presets.size();
        for (const StockTransitionPreset &p : g.presets) {
            CHECK_FALSE(p.name.isEmpty());
        }
    }
    // 215 across 24 groups at the time of writing; the bound guards against a generator
    // run that silently produced almost nothing.
    CHECK(totalPresets > 180);

    // Zoom is the group whose full set was missing: fourteen presets, not three.
    const QVector<StockTransitionPreset> *zoom = stockPresetsFor(QStringLiteral("zoom"));
    REQUIRE(zoom);
    CHECK(zoom->size() == 14);

    // And the catalog must actually carry them through, not just hold the table.
    const TransitionPluginInfo *info = transitionPluginById(transitionZoomId());
    REQUIRE(info);
    CHECK(info->presets.size() == 14);

    // A named border preset must carry a colour, or the name is a lie: this is the
    // mapping that was wrong at first, when the generator emitted "borderColorRed" while
    // the renderer read "borderRed" and every border came out black.
    const TransitionInstance red =
        makeTransitionInstance(transitionZoomId(), QStringLiteral("Zoom Out, Center, Red Border"));
    REQUIRE(red.isValid());
    CHECK(transitionParamValue(red, QStringLiteral("borderRed")) == Catch::Approx(1.0));
    CHECK(transitionParamValue(red, QStringLiteral("borderGreen")) == Catch::Approx(0.0));
    CHECK(transitionParamValue(red, QStringLiteral("borderSize")) > 0.0);
}

TEST_CASE("Zoom grows the incoming clip and shrinks the outgoing one",
          "[video][transitions]")
{
    const QSize size(120, 90);
    QImage a(size, QImage::Format_ARGB32_Premultiplied);
    a.fill(QColor(0, 0, 0));
    QImage b(size, QImage::Format_ARGB32_Premultiplied);
    b.fill(QColor(255, 255, 255));

    auto centreLuma = [&](const QString &preset, double progress) {
        const TransitionInstance t = makeTransitionInstance(transitionZoomId(), preset);
        const QImage img = renderTransition(a, b, progress, t);
        return qRed(img.pixel(size.width() / 2, size.height() / 2));
    };

    // "Zoom In" brings B up from nothing at the centre; "Zoom Out" takes A away and
    // leaves B behind. Both end on B and start on A.
    CHECK(centreLuma(QStringLiteral("Zoom In, Center"), 0.0) < 8);
    CHECK(centreLuma(QStringLiteral("Zoom In, Center"), 1.0) > 247);
    CHECK(centreLuma(QStringLiteral("Zoom Out, Center"), 0.0) < 8);
    CHECK(centreLuma(QStringLiteral("Zoom Out, Center"), 1.0) > 247);

    // Halfway, a corner-anchored zoom must differ from a centred one — otherwise the
    // Center parameter is being ignored and all ten position presets look alike.
    const TransitionInstance centre =
        makeTransitionInstance(transitionZoomId(), QStringLiteral("Zoom In, Center"));
    const TransitionInstance corner =
        makeTransitionInstance(transitionZoomId(), QStringLiteral("Zoom In, Top-Left"));
    const QImage midCentre = renderTransition(a, b, 0.5, centre);
    const QImage midCorner = renderTransition(a, b, 0.5, corner);
    int differing = 0;
    for (int y = 0; y < size.height(); y += 3) {
        for (int x = 0; x < size.width(); x += 3) {
            if (midCentre.pixel(x, y) != midCorner.pixel(x, y)) {
                ++differing;
            }
        }
    }
    INFO("pixels differing between centred and corner zoom: " << differing);
    CHECK(differing > 50);
}

TEST_CASE("Every rendered group resolves from the project side and is not also a stub",
          "[video][transitions]")
{
    // Two places used to keep their own list of which "{Svfx:…:key}" groups have a
    // renderer: the importer, and the catalog that fills in cross-fading stubs for the
    // rest. They drifted — Zoom was left out of the stubs but never added to the
    // importer, so a project's Zoom resolved to a stub id nothing answered to and came
    // back nameless. One table feeds both now, and this is what that table owes.
    const QVector<QPair<QString, QString>> &groups = renderedOfxGroups();
    REQUIRE(groups.size() >= 11);

    for (const auto &pair : groups) {
        INFO(pair.first.toStdString());
        // The importer must land on the group's own id, not on a stub.
        const QString svfx =
            QStringLiteral("{Svfx:com.vegascreativesoftware:%1}").arg(pair.first);
        CHECK(transitionIdForOfxPlugin(svfx) == pair.second);

        // And that id must be a real catalog entry, with the shipped presets on it.
        const TransitionPluginInfo *info = transitionPluginById(pair.second);
        REQUIRE(info);
        CHECK_FALSE(info->name.isEmpty());
        CHECK_FALSE(info->presets.isEmpty());

        // Nothing may answer to the stub id for the same key: two entries with one name
        // in the dock, one of which quietly cross-fades, is the failure being prevented.
        CHECK(transitionPluginById(transitionOfxId(pair.first)) == nullptr);
    }
}

TEST_CASE("Presets of a rendered group never draw the same picture",
          "[video][transitions]")
{
    ensureQtGuiApp();
    const QSize size(96, 54);

    // Structure on both axes and no repeat. A grid of identical stripes hides a
    // translation that lands on a multiple of its pitch, which is exactly how a first
    // pass at this check reported Push Up and Push In, Up as identical when they are not.
    auto source = [&](bool second) {
        QImage img(size, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < size.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < size.width(); ++x) {
                const int u = 255 * x / (size.width() - 1);
                const int v = 255 * y / (size.height() - 1);
                row[x] = second ? qRgb(255 - u, v, 200) : qRgb(u, 255 - v, 40);
            }
        }
        return img;
    };
    const QImage a = source(false);
    const QImage b = source(true);
    const QVector<double> steps = {0.2, 0.35, 0.5, 0.65, 0.8};

    for (const auto &group : renderedOfxGroups()) {
        const TransitionPluginInfo *info = transitionPluginById(group.second);
        REQUIRE(info);
        QVector<QVector<QImage>> frames;
        for (const TransitionPresetInfo &preset : info->presets) {
            TransitionInstance t;
            t.pluginId = info->id;
            t.presetName = preset.name;
            t.params = preset.params;
            QVector<QImage> row;
            for (double p : steps) {
                row.push_back(renderTransition(a, b, p, t));
            }
            frames.push_back(row);
        }

        for (int i = 0; i < frames.size(); ++i) {
            for (int j = i + 1; j < frames.size(); ++j) {
                double best = 0.0;
                for (int k = 0; k < steps.size(); ++k) {
                    int diff = 0;
                    for (int y = 0; y < size.height(); ++y) {
                        const auto *r1 =
                            reinterpret_cast<const QRgb *>(frames[i][k].constScanLine(y));
                        const auto *r2 =
                            reinterpret_cast<const QRgb *>(frames[j][k].constScanLine(y));
                        for (int x = 0; x < size.width(); ++x) {
                            if (r1[x] != r2[x]) {
                                ++diff;
                            }
                        }
                    }
                    best = std::max(best, 100.0 * diff / (size.width() * size.height()));
                }
                // Two presets that differ only in how fast the alpha channel crosses
                // cannot look different over opaque pictures, here or in VEGAS. Dissolve
                // ships such a pair for bleed and another for morph.
                bool alphaOnly = true;
                const QVariantMap &pi = info->presets[i].params;
                const QVariantMap &pj = info->presets[j].params;
                QSet<QString> keys;
                for (auto it = pi.cbegin(); it != pi.cend(); ++it) {
                    keys.insert(it.key());
                }
                for (auto it = pj.cbegin(); it != pj.cend(); ++it) {
                    keys.insert(it.key());
                }
                for (const QString &k : keys) {
                    if (pi.value(k) == pj.value(k)) {
                        continue;
                    }
                    if (!k.contains(QStringLiteral("Alpha"), Qt::CaseInsensitive)) {
                        alphaOnly = false;
                        break;
                    }
                }
                if (alphaOnly) {
                    continue;
                }
                INFO(info->name.toStdString() << ": " << info->presets[i].name.toStdString()
                                              << " vs " << info->presets[j].name.toStdString());
                // A preset that reads the same as another is one whose parameter is being
                // ignored — the whole point of a preset list is that the entries differ.
                CHECK(best >= 2.0);
            }
        }
    }
}

TEST_CASE("Squeeze travels the way its preset is named", "[video][transitions]")
{
    const QSize size(80, 80);
    QImage a(size, QImage::Format_ARGB32_Premultiplied);
    a.fill(QColor(0, 0, 0));
    QImage b(size, QImage::Format_ARGB32_Premultiplied);
    b.fill(QColor(255, 255, 255));

    auto lumaAt = [&](const QString &preset, double progress, int x, int y) {
        const TransitionInstance t = makeTransitionInstance(transitionSqueezeId(), preset);
        REQUIRE(t.isValid());
        return qRed(renderTransition(a, b, progress, t).pixel(x, y));
    };

    // "Squeeze Down": the old clip is pushed down, so halfway it holds the bottom and the
    // new one has taken the top.
    CHECK(lumaAt(QStringLiteral("Squeeze Down"), 0.5, 40, 4) > 200);
    CHECK(lumaAt(QStringLiteral("Squeeze Down"), 0.5, 40, 76) < 55);

    // "Squeeze In, Down" moves the same way — down — but it is the new clip that does the
    // moving, opening out from the top. Anchoring both cases to the same edge sent this
    // one upwards, which is the bug this pins: the name said Down, the picture went up.
    CHECK(lumaAt(QStringLiteral("Squeeze In, Down"), 0.5, 40, 4) > 200);
    CHECK(lumaAt(QStringLiteral("Squeeze In, Down"), 0.5, 40, 76) < 55);

    // And "Squeeze In, Up" is its mirror.
    CHECK(lumaAt(QStringLiteral("Squeeze In, Up"), 0.5, 40, 4) < 55);
    CHECK(lumaAt(QStringLiteral("Squeeze In, Up"), 0.5, 40, 76) > 200);
}

TEST_CASE("Push moves the old clip only when told to", "[video][transitions]")
{
    const QSize size(80, 80);
    // A gradient, so a shift of the old clip is visible rather than hidden by flat fill.
    QImage a(size, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < size.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(a.scanLine(y));
        for (int x = 0; x < size.width(); ++x) {
            row[x] = qRgb(255 * y / (size.height() - 1), 0, 0);
        }
    }
    QImage b(size, QImage::Format_ARGB32_Premultiplied);
    b.fill(QColor(0, 255, 0));

    auto redAt = [&](const QString &preset, int x, int y) {
        const TransitionInstance t = makeTransitionInstance(transitionPushId(), preset);
        REQUIRE(t.isValid());
        return qRed(renderTransition(a, b, 0.5, t).pixel(x, y));
    };

    // Both drive upwards, so halfway the new clip owns the bottom either way.
    CHECK(qGreen(renderTransition(a, b, 0.5,
                                  makeTransitionInstance(transitionPushId(),
                                                         QStringLiteral("Push Up")))
                     .pixel(40, 76))
          > 200);

    // What separates them is the old clip. "Push Up" shoves it off, so the row that shows
    // at the top comes from halfway down the picture and is mid-grey; "Push In, Up" leaves
    // it where it is, so the top row is still the dark end of the gradient.
    CHECK(redAt(QStringLiteral("Push Up"), 40, 2) > 100);
    CHECK(redAt(QStringLiteral("Push In, Up"), 40, 2) < 30);
}

TEST_CASE("Flash burns through its tint and hides the cut", "[video][transitions]")
{
    const QSize size(64, 64);
    QImage a(size, QImage::Format_ARGB32_Premultiplied);
    a.fill(QColor(10, 20, 30));
    QImage b(size, QImage::Format_ARGB32_Premultiplied);
    b.fill(QColor(40, 50, 60));

    auto mid = [&](const QString &preset, double progress) {
        const TransitionInstance t = makeTransitionInstance(transitionFlashId(), preset);
        REQUIRE(t.isValid());
        return renderTransition(a, b, progress, t).pixel(32, 32);
    };

    // At the peak the frame is the tint, which is what hides the cut underneath it.
    const QRgb hard = mid(QStringLiteral("Hard Flash"), 0.5);
    CHECK(qRed(hard) > 250);
    CHECK(qGreen(hard) > 250);
    CHECK(qBlue(hard) > 250);

    // The yellow one is yellow, so the tint is read rather than assumed white.
    const QRgb yellow = mid(QStringLiteral("Yellow Flash"), 0.5);
    CHECK(qRed(yellow) > 250);
    CHECK(qBlue(yellow) < 40);

    // Both ends are the untouched clips.
    CHECK(qRed(mid(QStringLiteral("Hard Flash"), 0.0)) < 20);
    CHECK(qRed(mid(QStringLiteral("Hard Flash"), 1.0)) < 60);
}

TEST_CASE("Every group VEGAS ships presets for takes them from the package",
          "[video][transitions]")
{
    // The generated table is the whole shipped set. A group that hand-lists its presets
    // instead gets whatever someone transcribed — which is how Zoom ended up with three
    // invented entries against fourteen real ones, and how 3D Fly In/Out and Portals were
    // left with no parameters at all while the package named every one of them.
    struct Expect {
        const char *key;
        QString (*id)();
    };
    const Expect groups[] = {
        {"3dblinds", &transition3dBlindsId},
        {"3dcascade", &transitionCascade3dId},
        {"3dshuffle", &transitionShuffle3dId},
        {"3dflyinout", &transitionFlyInOut3dId},
        {"portals", &transitionPortalsId},
        {"venetianblinds", &transitionVenetianBlindsId},
    };

    for (const Expect &e : groups) {
        const QString key = QString::fromLatin1(e.key);
        INFO(e.key);
        const QVector<StockTransitionPreset> *stock = stockPresetsFor(key);
        REQUIRE(stock);
        REQUIRE_FALSE(stock->isEmpty());

        const TransitionPluginInfo *info = transitionPluginById(e.id());
        REQUIRE(info);
        REQUIRE(info->presets.size() == stock->size());
        for (int i = 0; i < stock->size(); ++i) {
            CHECK(info->presets[i].name == stock->at(i).name);
        }
    }

    // Every parameter a shipped preset sets must be a parameter the group declares, or
    // the value is loaded and then has nowhere to go. 3D Cascade is the case that failed:
    // its third field is Twist in the package and was declared here as "stagger".
    for (const Expect &e : groups) {
        const TransitionPluginInfo *info = transitionPluginById(e.id());
        REQUIRE(info);
        QSet<QString> declared;
        for (const TransitionParamInfo &p : info->params) {
            declared.insert(p.key);
        }
        for (const TransitionPresetInfo &preset : info->presets) {
            for (auto it = preset.params.cbegin(); it != preset.params.cend(); ++it) {
                INFO(info->name.toStdString() << " / " << preset.name.toStdString()
                                              << " / " << it.key().toStdString());
                CHECK(declared.contains(it.key()));
            }
        }
    }
}
