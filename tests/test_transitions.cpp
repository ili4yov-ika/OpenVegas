#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "video/TransitionApply.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>

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
    // Transcribed from the *-default_set.png captures.
    const Expected table[] = {
        {"Simple", 8, 0, 0.0, 1.0, 0},
        {"Left to Right", 4, 0, 0.2, 0.7, 0},
        {"Slot Machine", 4, 4, 0.3, 1.0, 2},
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
