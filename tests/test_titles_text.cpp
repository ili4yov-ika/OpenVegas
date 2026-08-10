#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "video/TitlesTextApply.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QSet>

using namespace openvegas;

namespace {

// titlesTextBoundingBox() touches QFont/QFontMetricsF, which qFatal()-crash the whole
// process if used before any QGuiApplication exists — this test binary is otherwise a
// plain Catch2 console app with no Qt application instance at all (nothing else in it
// calls into the font database). -platform offscreen keeps it headless-safe.
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

} // namespace

// Regression coverage for a real bug: titlesTextMotionForPreset() used to derive its
// lookup key from the display label ("Drop Split" -> "_DropSplit"), which does NOT match
// the real Vegas AnimationName format recovered from SAMPLES/veg_project/
// project_titles-and-text.veg ("_Drop_split" — Vegas mixes Title_Case and lower_case
// words per preset, e.g. "_Fly_in_from_Right", "_Rolling_Glow_and_Enlarge"). Every
// multi-word named preset except the always-matching single-word ones silently fell back
// to TitlesTextMotion::None on VEG import. See ISSUES_AND_PLANS.md 2026-08-08.

TEST_CASE("Real multi-word Vegas preset keys resolve to a real motion, not None",
          "[video][titles-text]")
{
    // Every one of these is a real AnimationName value recovered from the sample project —
    // the exact bug: none of them equal presetKeyFromLabel's old "_RemoveSpaces" guess.
    static const QVector<QString> realKeys = {
        QStringLiteral("_Action_Flip"),          QStringLiteral("_Coming_at_You"),
        QStringLiteral("_Double_Flash_Glow"),    QStringLiteral("_Drop_split"),
        QStringLiteral("_Dropping_words"),       QStringLiteral("_Fall_Down"),
        QStringLiteral("_Float_and_pop"),        QStringLiteral("_Fly_in"),
        QStringLiteral("_Fly_in_from_Right"),    QStringLiteral("_Rolling_Glow_and_Enlarge"),
        QStringLiteral("_Rough_Day"),            QStringLiteral("_Scroll_Left"),
        QStringLiteral("_Slide_Down"),           QStringLiteral("_Slide_Left"),
        QStringLiteral("_Slide_Right"),          QStringLiteral("_Slide_Up"),
        QStringLiteral("_Twist_In"),
    };
    for (const QString &key : realKeys) {
        const TitlesTextMotionSpec spec = titlesTextMotionForPreset(key);
        INFO("key = " << key.toStdString());
        CHECK(spec.kind != TitlesTextMotion::None);
    }
}

TEST_CASE("titlesTextAnimationPresets() keys match the real Vegas AnimationName format",
          "[video][titles-text]")
{
    const QVector<TitlesTextPresetEntry> presets = titlesTextAnimationPresets();
    // None + 25 named + 25 Title-N, per the real Media Generator catalog (see
    // SAMPLES/veg_project/project_titles-and-text.veg — 51 distinct AnimationName values).
    REQUIRE(presets.size() == 51);

    QSet<QString> keys;
    for (const TitlesTextPresetEntry &e : presets) {
        REQUIRE_FALSE(keys.contains(e.key)); // no duplicate/colliding keys
        keys.insert(e.key);
        REQUIRE(e.key.startsWith(QLatin1Char('_')));
    }
    REQUIRE(keys.contains(QStringLiteral("_None")));
    REQUIRE(keys.contains(QStringLiteral("_Drop_split")));
    REQUIRE(keys.contains(QStringLiteral("_Fly_in_from_Right")));
    REQUIRE(keys.contains(QStringLiteral("_Title01")));
    REQUIRE(keys.contains(QStringLiteral("_Title25")));
    REQUIRE_FALSE(keys.contains(QStringLiteral("_Title26")));
}

TEST_CASE("titlesTextPresetVisuals returns real recovered colors/scale", "[video][titles-text]")
{
    // Real TextColor/Scale recovered from the sample project (see presetTable() in
    // video/TitlesTextApply.cpp for the full provenance note).
    const TitlesTextPresetVisuals bounce = titlesTextPresetVisuals(QStringLiteral("_Bounce"));
    REQUIRE(bounce.textColor == QColor(0, 128, 0));
    REQUIRE(bounce.scale == Catch::Approx(1.0));

    const TitlesTextPresetVisuals slide = titlesTextPresetVisuals(QStringLiteral("_Slide"));
    REQUIRE(slide.textColor == QColor(0, 255, 255));
    REQUIRE(slide.scale == Catch::Approx(0.5));

    const TitlesTextPresetVisuals title03 = titlesTextPresetVisuals(QStringLiteral("_Title03"));
    REQUIRE(title03.scale == Catch::Approx(1.3));
    // Title-N presets are real-transparent (Background alpha 0 in every recovered
    // instance) — not just "unset"/default, an explicit real value.
    REQUIRE(title03.backgroundColor.alpha() == 0);

    // Real recovered Background — the only 3 of 51 presets with an opaque fill (see
    // presetTable()'s provenance note on the VegReader substring-collision bug that used
    // to hide this on VEG import).
    const TitlesTextPresetVisuals dropSplit = titlesTextPresetVisuals(QStringLiteral("_Drop_split"));
    REQUIRE(dropSplit.backgroundColor == QColor(0, 255, 255, 255));
    const TitlesTextPresetVisuals menace = titlesTextPresetVisuals(QStringLiteral("_Menace"));
    REQUIRE(menace.backgroundColor == QColor(255, 255, 255, 255));
    const TitlesTextPresetVisuals roughDay = titlesTextPresetVisuals(QStringLiteral("_Rough_Day"));
    REQUIRE(roughDay.backgroundColor == QColor(255, 255, 0, 255));
    // Every other named preset stays genuinely transparent.
    REQUIRE(bounce.backgroundColor.alpha() == 0);
    REQUIRE(slide.backgroundColor.alpha() == 0);

    // Unknown / "_None" keys fall back to TitlesTextParams's own defaults, not a stale value.
    const TitlesTextPresetVisuals none = titlesTextPresetVisuals(QStringLiteral("_None"));
    REQUIRE(none.textColor == QColor(255, 255, 255, 255));
    REQUIRE(none.backgroundColor.alpha() == 0);
    REQUIRE(none.scale == Catch::Approx(1.0));
    const TitlesTextPresetVisuals unknown = titlesTextPresetVisuals(QStringLiteral("_NotARealKey"));
    REQUIRE(unknown.textColor == QColor(255, 255, 255, 255));
}

// titlesTextBoundingBox() backs the on-canvas Video Preview move/resize overlay —
// these guard the exact geometry the overlay's hit-test/drag math depends on.

TEST_CASE("titlesTextBoundingBox is empty for blank text", "[video][titles-text]")
{
    ensureQtGuiApp();
    TitlesTextParams p;
    p.text = QStringLiteral("   \n  ");
    CHECK(titlesTextBoundingBox(p, QSize(1920, 1080)).isEmpty());
}

TEST_CASE("titlesTextBoundingBox centers on locationX/Y for MiddleCenter anchor",
         "[video][titles-text]")
{
    ensureQtGuiApp();
    TitlesTextParams p;
    p.text = QStringLiteral("Sample Text");
    p.anchor = TitlesTextAnchor::MiddleCenter;
    p.locationX = 0.5;
    p.locationY = 0.5;
    const QSize size(1920, 1080);
    const QRectF box = titlesTextBoundingBox(p, size);
    REQUIRE_FALSE(box.isEmpty());
    CHECK(box.center().x() == Catch::Approx(size.width() * 0.5).margin(0.5));
    CHECK(box.center().y() == Catch::Approx(size.height() * 0.5).margin(0.5));
}

TEST_CASE("titlesTextBoundingBox anchors its top-left corner for TopLeft anchor",
         "[video][titles-text]")
{
    ensureQtGuiApp();
    TitlesTextParams p;
    p.text = QStringLiteral("Sample Text");
    p.anchor = TitlesTextAnchor::TopLeft;
    p.locationX = 0.2;
    p.locationY = 0.3;
    const QSize size(1920, 1080);
    const QRectF box = titlesTextBoundingBox(p, size);
    REQUIRE_FALSE(box.isEmpty());
    CHECK(box.left() == Catch::Approx(size.width() * 0.2).margin(0.5));
    CHECK(box.top() == Catch::Approx(size.height() * 0.3).margin(0.5));
}

TEST_CASE("titlesTextBoundingBox moves 1:1 with locationX/Y — the on-canvas drag formula",
         "[video][titles-text]")
{
    ensureQtGuiApp();
    TitlesTextParams p;
    p.text = QStringLiteral("Move Me");
    p.anchor = TitlesTextAnchor::MiddleCenter;
    p.locationX = 0.5;
    p.locationY = 0.5;
    const QSize size(1920, 1080);
    const QRectF box1 = titlesTextBoundingBox(p, size);
    p.locationX = 0.6;
    p.locationY = 0.4;
    const QRectF box2 = titlesTextBoundingBox(p, size);
    CHECK(box2.left() - box1.left() == Catch::Approx(0.1 * size.width()).margin(0.5));
    CHECK(box2.top() - box1.top() == Catch::Approx(-0.1 * size.height()).margin(0.5));
}

TEST_CASE("titlesTextBoundingBox scales around a fixed anchor point", "[video][titles-text]")
{
    ensureQtGuiApp();
    TitlesTextParams p;
    p.text = QStringLiteral("Resize Me");
    p.anchor = TitlesTextAnchor::MiddleCenter;
    p.locationX = 0.5;
    p.locationY = 0.5;
    p.scale = 1.0;
    const QSize size(1920, 1080);
    const QRectF box1 = titlesTextBoundingBox(p, size);
    p.scale = 2.0;
    const QRectF box2 = titlesTextBoundingBox(p, size);
    // MiddleCenter anchor sits at the box's own center, so the center must stay put
    // while the box grows around it — exactly what the corner-handle uniform resize
    // (scale about the anchor) on the Video Preview overlay depends on.
    CHECK(box2.center().x() == Catch::Approx(box1.center().x()).margin(0.5));
    CHECK(box2.center().y() == Catch::Approx(box1.center().y()).margin(0.5));
    CHECK(box2.width() > box1.width());
    CHECK(box2.height() > box1.height());
}
