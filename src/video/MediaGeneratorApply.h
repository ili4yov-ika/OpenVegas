#pragma once

#include "plugins/AudioPluginTypes.h"

#include <QColor>
#include <QImage>
#include <QSize>
#include <QString>
#include <QVariantMap>

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
    /**
     * What a VEGAS project actually said, kept whole for the real generator: the plug-in
     * ("{Svfx:…}", or a legacy CLSID in braces), its preset, and its stored parameters and
     * curves (`vegOfxParamMap()` / `vegOfxCurveMap()` shapes). The pattern fields above
     * are only what the stand-in renderer can show of them. Empty when the generator was
     * placed from OpenVegas's own browser.
     */
    QString vegasPluginId;
    QString vegasPreset;
    QVariantMap vegasParams;
    QVariantMap vegasCurves;
};

/**
 * Render the pattern at the given size (fully opaque — these generators have no
 * alpha). tile/stripe widths are authored against a 100px-wide reference (the
 * preset thumbnail) and scale proportionally with size.
 */
QImage renderMediaGeneratorPattern(const MediaGeneratorParams &p, const QSize &size);

/**
 * One frame of the generator, drawn the way VEGAS draws it.
 *
 * VEGAS's own OFX plug-in when one is installed (GeneratorPluginHook), else our renderers
 * (VegasGenerators) from the values VEGAS stores — the slot's, or the generator's
 * "(Default)"/first stock preset for a slot that predates them. Only a slot naming no VEGAS
 * generator at all falls back to the stand-in pattern above.
 *
 * @param timeSec, lengthSec  the frame's place in the event (Credit Roll moves)
 */
QImage renderMediaGenerator(const MediaGeneratorParams &p, const QSize &size, double timeSec = 0.0,
                            double lengthSec = 10.0);

/** The VEGAS id behind a slot: its own, or the stock group of the same name. */
QString mediaGeneratorVegasId(const MediaGeneratorParams &p);

/**
 * A VEGAS generator — its id, preset and stored values — as params for this renderer.
 *
 * The one place both the .veg importer and the generator browser go through, so a preset
 * placed from the browser and the same preset opened from a project are the same thing.
 * Everything VEGAS said is kept in the vegas* fields; the pattern fields get what the
 * stand-in renderer can show of it: the generator's pattern, its colours where named, and
 * Checkerboard's tile width. Unknown ids keep `displayName` and the stand-in defaults.
 */
MediaGeneratorParams mediaGeneratorFromVegas(const QString &vegasPluginId, const QString &presetName,
                                             const QVariantMap &params,
                                             const QVariantMap &curves = {},
                                             const QString &displayName = {});

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
