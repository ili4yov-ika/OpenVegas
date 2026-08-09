#pragma once

#include "plugins/AudioPluginTypes.h"

#include <QColor>
#include <QImage>
#include <QSize>
#include <QString>

namespace openvegas {

/**
 * Procedural background patterns behind OpenVegas's non-text Media Generator
 * plug-ins (Checkerboard, Color Gradient, Credit Roll, Noise Texture, Solid
 * Color, Test Pattern). Real Vegas ships an actual per-plugin renderer for
 * each of these; these are the same functional-approximation patterns already
 * used for the Media Generator preset thumbnails — single source of truth,
 * shared with MediaGeneratorPane::presetIcon() so the browser preview always
 * matches what lands on the timeline.
 */
enum class MediaGeneratorPattern {
    Gradient,
    Checker,
    HBlinds,
    VBlinds,
    Grille,
    Fence,
    Ridges,
    Bumps,
    Plaid,
    Letterbox,
    SplitScreen,
    Horizon
};

/** Params for one non-text Media Generator instance (plugin identity + preset colors). */
struct MediaGeneratorParams {
    QString pluginName = QStringLiteral("Media Generator");
    MediaGeneratorPattern pattern = MediaGeneratorPattern::Gradient;
    QColor c0 = QColor(0x20, 0x40, 0x80);
    QColor c1 = QColor(0xc0, 0x60, 0x30);
    int tile = 8;
};

/**
 * Render the pattern at the given size (fully opaque — these generators have no
 * alpha). tile/stripe widths are authored against a 100px-wide reference (the
 * preset thumbnail) and scale proportionally with size.
 */
QImage renderMediaGeneratorPattern(const MediaGeneratorParams &p, const QSize &size);

/** Build a builtin FxSlot carrying p (see isMediaGeneratorPluginId()). */
FxSlot mediaGeneratorSlotFor(const MediaGeneratorParams &p);
MediaGeneratorParams mediaGeneratorFromSlot(const FxSlot &slot);
void mediaGeneratorSaveToSlot(FxSlot *slot, const MediaGeneratorParams &p);

/**
 * Compact single-line encode/decode used for the timeline drag payload
 * (MediaMime's synthetic `extra` field — see MediaMime::fromSynthetic()).
 */
QString mediaGeneratorParamsToPayload(const MediaGeneratorParams &p);
MediaGeneratorParams mediaGeneratorParamsFromPayload(const QString &payload);

} // namespace openvegas
