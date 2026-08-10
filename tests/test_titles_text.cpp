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

// Media Properties (the generator window's toolbar button) ride along inside the very
// same params blob as the text settings, so they inherit its project-archive/undo
// serialization for free — but only as long as every field is actually mapped. These
// guard that: a field added to GeneratorMediaProps but forgotten in
// titlesTextToMap/titlesTextFromMap would silently reset on save/load.

TEST_CASE("GeneratorMediaProps round-trips through titlesTextToMap/FromMap",
         "[video][titles-text][media-props]")
{
    TitlesTextParams p;
    p.media.tapeName = QStringLiteral("Reel 07");
    p.media.useCustomTimecode = true;
    p.media.customTimecodeSec = 12.5;
    p.media.frameWidth = 3840;
    p.media.frameHeight = 2160;
    p.media.fieldOrder = MediaFieldOrder::UpperFieldFirst;
    p.media.pixelAspect = 0.9091;
    p.media.alphaChannel = MediaAlphaChannel::Straight;
    p.media.backgroundColor = QColor(10, 20, 30, 255);
    p.media.rotation = MediaRotation::Deg90Clockwise;

    const TitlesTextParams back = titlesTextFromMap(titlesTextToMap(p));

    CHECK(back.media.tapeName == QStringLiteral("Reel 07"));
    CHECK(back.media.useCustomTimecode);
    CHECK(back.media.customTimecodeSec == Catch::Approx(12.5));
    CHECK(back.media.frameWidth == 3840);
    CHECK(back.media.frameHeight == 2160);
    CHECK(back.media.fieldOrder == MediaFieldOrder::UpperFieldFirst);
    CHECK(back.media.pixelAspect == Catch::Approx(0.9091));
    CHECK(back.media.alphaChannel == MediaAlphaChannel::Straight);
    CHECK(back.media.backgroundColor == QColor(10, 20, 30, 255));
    CHECK(back.media.rotation == MediaRotation::Deg90Clockwise);
}

TEST_CASE("GeneratorMediaProps defaults survive a params map with no media keys",
         "[video][titles-text][media-props]")
{
    // Older projects (and every project saved before Media Properties existed) have no
    // "media.*" keys at all — those must fall back to the struct's own defaults rather
    // than to zeroes, or a load would silently claim 0x0 frame size / Undefined alpha.
    QVariantMap legacy = titlesTextToMap(TitlesTextParams());
    for (const QString &key : legacy.keys()) {
        if (key.startsWith(QStringLiteral("media."))) {
            legacy.remove(key);
        }
    }
    const TitlesTextParams back = titlesTextFromMap(legacy);
    const GeneratorMediaProps def;
    CHECK(back.media.frameWidth == def.frameWidth);
    CHECK(back.media.frameHeight == def.frameHeight);
    CHECK(back.media.pixelAspect == Catch::Approx(def.pixelAspect));
    CHECK(back.media.alphaChannel == def.alphaChannel);
    CHECK(back.media.rotation == def.rotation);
    CHECK(back.media.backgroundColor == def.backgroundColor);
}

TEST_CASE("titlesTextFromMap clamps out-of-range media enum values",
         "[video][titles-text][media-props]")
{
    // Hand-edited / corrupted state must not produce an out-of-range enum that would
    // index past the dialog's combo boxes.
    QVariantMap m = titlesTextToMap(TitlesTextParams());
    m[QStringLiteral("media.fieldOrder")] = 99;
    m[QStringLiteral("media.alphaChannel")] = -5;
    m[QStringLiteral("media.rotation")] = 42;
    const TitlesTextParams back = titlesTextFromMap(m);
    CHECK(back.media.fieldOrder == MediaFieldOrder::LowerFieldFirst);
    CHECK(back.media.alphaChannel == MediaAlphaChannel::Undefined);
    CHECK(back.media.rotation == MediaRotation::Deg90CounterClockwise);
}

// Parameter keyframes (the generator window's keyframe pane). titlesTextAtTime() is what
// VideoCompositor actually renders through, so these guard the evaluation contract as
// much as the storage one.

TEST_CASE("titlesTextAtTime returns the params unchanged when nothing is animated",
         "[video][titles-text][keyframes]")
{
    TitlesTextParams p;
    p.scale = 1.5;
    p.locationX = 0.25;
    const TitlesTextParams at = titlesTextAtTime(p, 3.0);
    CHECK(at.scale == Catch::Approx(1.5));
    CHECK(at.locationX == Catch::Approx(0.25));
    CHECK(at.lanes.isEmpty());
}

TEST_CASE("titlesTextAtTime interpolates linearly between two keyframes",
         "[video][titles-text][keyframes]")
{
    TitlesTextParams p;
    titlesTextSetKeyframe(&p, QStringLiteral("scale"), 0.0, 1.0, VideoKeyframeType::Linear);
    titlesTextSetKeyframe(&p, QStringLiteral("scale"), 4.0, 3.0, VideoKeyframeType::Linear);

    CHECK(titlesTextAtTime(p, 0.0).scale == Catch::Approx(1.0));
    CHECK(titlesTextAtTime(p, 2.0).scale == Catch::Approx(2.0));
    CHECK(titlesTextAtTime(p, 4.0).scale == Catch::Approx(3.0));
}

TEST_CASE("titlesTextAtTime holds the end values outside the keyframe range",
         "[video][titles-text][keyframes]")
{
    // Before the first and after the last keyframe Vegas holds, it does not extrapolate —
    // the curve drawn in the pane has that same flat tail.
    TitlesTextParams p;
    titlesTextSetKeyframe(&p, QStringLiteral("locationX"), 2.0, 0.2);
    titlesTextSetKeyframe(&p, QStringLiteral("locationX"), 4.0, 0.8);

    CHECK(titlesTextAtTime(p, 0.0).locationX == Catch::Approx(0.2));
    CHECK(titlesTextAtTime(p, 100.0).locationX == Catch::Approx(0.8));
}

TEST_CASE("titlesTextAtTime animates each lane independently",
         "[video][titles-text][keyframes]")
{
    TitlesTextParams p;
    p.locationY = 0.9; // static, no lane — must survive untouched
    titlesTextSetKeyframe(&p, QStringLiteral("locationX"), 0.0, 0.0);
    titlesTextSetKeyframe(&p, QStringLiteral("locationX"), 2.0, 1.0);
    titlesTextSetKeyframe(&p, QStringLiteral("shadowBlur"), 0.0, 0.0);
    titlesTextSetKeyframe(&p, QStringLiteral("shadowBlur"), 2.0, 10.0);

    const TitlesTextParams at = titlesTextAtTime(p, 1.0);
    CHECK(at.locationX == Catch::Approx(0.5));
    CHECK(at.shadowBlur == Catch::Approx(5.0));
    CHECK(at.locationY == Catch::Approx(0.9));
}

TEST_CASE("titlesTextSetKeyframe replaces a keyframe at the same time and keeps order sorted",
         "[video][titles-text][keyframes]")
{
    TitlesTextParams p;
    titlesTextSetKeyframe(&p, QStringLiteral("scale"), 4.0, 3.0);
    titlesTextSetKeyframe(&p, QStringLiteral("scale"), 1.0, 2.0);
    titlesTextSetKeyframe(&p, QStringLiteral("scale"), 4.0, 9.0); // replaces, not appends

    const TitlesTextParamLane *lane = titlesTextFindLane(p, QStringLiteral("scale"));
    REQUIRE(lane);
    REQUIRE(lane->keys.size() == 2);
    CHECK(lane->keys[0].timeSec == Catch::Approx(1.0));
    CHECK(lane->keys[1].timeSec == Catch::Approx(4.0));
    CHECK(lane->keys[1].value == Catch::Approx(9.0));
}

TEST_CASE("titlesTextRemoveKeyframe drops the lane once its last keyframe is gone",
         "[video][titles-text][keyframes]")
{
    // A lane left behind empty would keep the parameter showing as "animated" in the UI
    // while evaluating statically — the two must never disagree.
    TitlesTextParams p;
    titlesTextSetKeyframe(&p, QStringLiteral("tracking"), 1.0, 5.0);
    REQUIRE(titlesTextFindLane(p, QStringLiteral("tracking")) != nullptr);

    CHECK(titlesTextRemoveKeyframe(&p, QStringLiteral("tracking"), 1.0));
    CHECK(titlesTextFindLane(p, QStringLiteral("tracking")) == nullptr);
    CHECK(p.lanes.isEmpty());
    // Removing again is a no-op, not a crash.
    CHECK_FALSE(titlesTextRemoveKeyframe(&p, QStringLiteral("tracking"), 1.0));
}

TEST_CASE("Keyframe lanes round-trip through titlesTextToMap/FromMap",
         "[video][titles-text][keyframes]")
{
    TitlesTextParams p;
    titlesTextSetKeyframe(&p, QStringLiteral("locationX"), 0.0, 0.1, VideoKeyframeType::Smooth);
    titlesTextSetKeyframe(&p, QStringLiteral("locationX"), 2.5, 0.9, VideoKeyframeType::Hold);
    titlesTextSetKeyframe(&p, QStringLiteral("scale"), 1.0, 2.0, VideoKeyframeType::Linear);

    const TitlesTextParams back = titlesTextFromMap(titlesTextToMap(p));

    const TitlesTextParamLane *loc = titlesTextFindLane(back, QStringLiteral("locationX"));
    REQUIRE(loc);
    REQUIRE(loc->keys.size() == 2);
    CHECK(loc->keys[0].timeSec == Catch::Approx(0.0));
    CHECK(loc->keys[0].value == Catch::Approx(0.1));
    CHECK(loc->keys[0].type == VideoKeyframeType::Smooth);
    CHECK(loc->keys[1].type == VideoKeyframeType::Hold);
    REQUIRE(titlesTextFindLane(back, QStringLiteral("scale")) != nullptr);
    // Parameters that were never keyframed must not gain an empty lane on the way back.
    CHECK(titlesTextFindLane(back, QStringLiteral("shadowBlur")) == nullptr);
}

TEST_CASE("A static generator's serialized map carries no keyframe keys at all",
         "[video][titles-text][keyframes]")
{
    const QVariantMap m = titlesTextToMap(TitlesTextParams());
    for (const QString &key : m.keys()) {
        INFO("unexpected keyframe key: " << key.toStdString());
        CHECK_FALSE(key.startsWith(QStringLiteral("kf.")));
    }
}

TEST_CASE("titlesTextParamValue/SetParamValue cover every animatable parameter",
         "[video][titles-text][keyframes]")
{
    // Guards the hand-written key dispatch: a parameter listed in the pane's tree but
    // missing from the getter/setter would silently keyframe to a constant 0.
    TitlesTextParams p;
    double probe = 1.0;
    for (const TitlesTextAnimatableParam &ap : titlesTextAnimatableParams()) {
        probe += 1.0;
        INFO("param: " << ap.key.toStdString());
        titlesTextSetParamValue(&p, ap.key, probe);
        CHECK(titlesTextParamValue(p, ap.key) == Catch::Approx(probe));
    }
}
