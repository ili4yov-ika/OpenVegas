#pragma once

#include "plugins/AudioPluginTypes.h"

#include <QColor>
#include <QImage>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVariantMap>
#include <QVector>

namespace openvegas {

enum class TitlesTextAlignment { Left, Center, Right };

enum class TitlesTextAnchor {
    TopLeft,
    TopCenter,
    TopRight,
    MiddleLeft,
    MiddleCenter,
    MiddleRight,
    BottomLeft,
    BottomCenter,
    BottomRight
};

/**
 * VEGAS Titles & Text generator params (Vegas-style MVP: single uniform text run).
 */
struct TitlesTextParams {
    QString text = QStringLiteral("Sample Text");
    QString fontFamily = QStringLiteral("Verdana");
    double fontSize = 48.0; // points, at project frame height 1080
    bool bold = false;
    bool italic = false;
    TitlesTextAlignment alignment = TitlesTextAlignment::Left;

    QColor textColor = QColor(255, 255, 255, 255);
    /** Preset key (e.g. "_None", "_SlideLeft", "_Title08") — see titlesTextMotionForPreset(). */
    QString animationName = QStringLiteral("_None");

    double scale = 1.0;
    double locationX = 0.5; // normalized, 0..1 (anchor position in frame)
    double locationY = 0.5;
    TitlesTextAnchor anchor = TitlesTextAnchor::MiddleCenter;

    bool cropBackgroundToText = false;
    QColor backgroundColor = QColor(0, 0, 0, 0); // transparent by default
    double tracking = 0.0;                       // extra letter spacing, px at 1080p
    double lineSpacing = 1.0;                    // multiplier on natural line height

    double outlineWidth = 0.0; // px at 1080p
    QColor outlineColor = QColor(255, 255, 255, 255);

    bool shadowEnable = false;
    QColor shadowColor = QColor(0, 0, 0, 255);
    double shadowOffsetX = 0.2; // px at 1080p
    double shadowOffsetY = 0.2;
    double shadowBlur = 0.4; // approximate box-blur radius, px at 1080p

    /** Persisted section expand/collapse UI state (Vegas's *Group flags). */
    bool textColorExpanded = false;
    bool locationExpanded = false;
    bool advancedExpanded = false;
    bool outlineExpanded = false;
    bool shadowExpanded = false;
};

TitlesTextParams titlesTextFromMap(const QVariantMap &m);
QVariantMap titlesTextToMap(const TitlesTextParams &p);
TitlesTextParams titlesTextFromSlot(const FxSlot &slot);
void titlesTextSaveToSlot(FxSlot *slot, const TitlesTextParams &p);

/**
 * Vegas's real per-preset motion curves aren't reverse-engineerable without the actual
 * plugin binary (same "functional approximation" status as the color picker) — these
 * are hand-crafted stand-ins grouped into a small set of recognizable motion families.
 */
enum class TitlesTextMotion { None, FadeIn, SlideIn, PopIn, DropIn, TwistIn, Shake, Scroll, Flicker };
enum class TitlesTextDirection { None, Left, Right, Up, Down };

struct TitlesTextMotionSpec {
    TitlesTextMotion kind = TitlesTextMotion::None;
    TitlesTextDirection direction = TitlesTextDirection::None;
};

/**
 * Maps an animationName key (e.g. "_SlideLeft", "_Earthquake", "_Title05") to a motion
 * spec. Every real Vegas preset name resolves to *some* motion (the 25 Title-N presets
 * round-robin over the named kinds below, since their individual real motions are
 * unknown) rather than silently doing nothing.
 */
TitlesTextMotionSpec titlesTextMotionForPreset(const QString &animationKey);

struct TitlesTextPresetEntry {
    QString label;
    QString key;
};

/**
 * Every real Vegas Titles & Text animation preset name, in Vegas's own dropdown order,
 * paired with the key titlesTextMotionForPreset() understands. Single source of truth
 * shared by the Animation dropdown and the Media Generator preset grid so the two can
 * never drift out of sync.
 */
QVector<TitlesTextPresetEntry> titlesTextAnimationPresets();

/** Starting text/background color / scale for a known preset key, reverse-engineered
 *  from a real Vegas sample project (SAMPLES/veg_project/project_titles-and-text.veg —
 *  51 distinct AnimationName values recovered and cross-checked against their
 *  TextColor/Background/Scale properties). Applied when a preset is placed on the
 *  timeline (drag/drop or double-click) so the result matches what real Vegas starts
 *  you with. Unknown keys (including "_None") fall back to TitlesTextParams's own
 *  defaults (white text, transparent background, scale 1.0) — true for 48 of the 51
 *  presets; only Drop Split/Menace/Rough Day have a real opaque background. */
struct TitlesTextPresetVisuals {
    QColor textColor = QColor(255, 255, 255, 255);
    QColor backgroundColor = QColor(0, 0, 0, 0);
    double scale = 1.0;
};
TitlesTextPresetVisuals titlesTextPresetVisuals(const QString &animationKey);

/** Offsets/scale/opacity/rotation added on top of the static layout for one instant. */
struct TitlesTextAnimKeyframe {
    double offsetX = 0.0; // normalized, added to locationX
    double offsetY = 0.0; // normalized, added to locationY
    double scaleMul = 1.0;
    double opacity = 1.0;
    double rotationDeg = 0.0;
};

/**
 * progress = clamp(localTimeSec / lengthSec, 0, 1). Entrance-style kinds (SlideIn,
 * PopIn, DropIn, TwistIn, FadeIn, Flicker) animate over roughly the first quarter of
 * progress then hold at rest; Shake/Scroll run across the whole span.
 */
TitlesTextAnimKeyframe evaluateTitlesTextAnimation(const TitlesTextMotionSpec &spec, double progress);

/**
 * Render text into an image of the given size. progress=1.0 (default) renders fully
 * settled/static; pass the event's local-time fraction to play the preset's animation.
 */
QImage renderTitlesText(const TitlesTextParams &p, const QSize &size, double progress = 1.0);

/**
 * Bounding box (frame pixel coordinates, origin top-left, size = the render target) of
 * the text block at its settled/static pose (progress=1.0, no animation offset) — the
 * same block-layout math renderTitlesText() uses internally, kept here so callers that
 * need the geometry without rendering pixels (e.g. an on-canvas move/resize overlay)
 * don't duplicate it. Empty rect when there's no text to lay out.
 */
QRectF titlesTextBoundingBox(const TitlesTextParams &p, const QSize &size);

/**
 * Tileable alpha checkerboard — light gray / dark gray, matching the real Vegas Pro
 * Media Generator browser's own transparency checkerboard. Shared backdrop for any
 * Titles & Text preview (preset tiles, timeline thumbnails) whose background is
 * transparent or semi-transparent, so what you see honestly reflects what compositing
 * will actually show through.
 */
QImage checkerboardBackground(const QSize &size);

} // namespace openvegas
