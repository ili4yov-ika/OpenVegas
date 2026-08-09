#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "video/TitlesTextApply.h"

#include <QSet>

using namespace openvegas;

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
