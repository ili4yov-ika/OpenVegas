#pragma once

#include "plugins/AudioPluginTypes.h"

#include <QImage>
#include <QVariantMap>

namespace openvegas {

/** Primary color correction params (Vegas-style Color Corrector MVP). */
struct ColorCorrectorParams {
    double brightness = 0.0;  // −1…+1
    double contrast = 1.0;    // 0…2 (1 = neutral)
    double saturation = 1.0;  // 0…2 (1 = neutral)
    double gamma = 1.0;       // 0.1…3 (1 = neutral)
};

ColorCorrectorParams colorCorrectorFromMap(const QVariantMap &m);
QVariantMap colorCorrectorToMap(const ColorCorrectorParams &p);
ColorCorrectorParams colorCorrectorFromSlot(const FxSlot &slot);
void colorCorrectorSaveToSlot(FxSlot *slot, const ColorCorrectorParams &p);

bool isColorCorrectorName(const QString &displayName);
bool isColorGradingName(const QString &displayName);
bool isBrightnessContrastName(const QString &displayName);
bool isPanCropName(const QString &displayName);

/** In-place ARGB32(_Premultiplied) color correct. No-op when params are identity. */
void applyColorCorrector(QImage *img, const ColorCorrectorParams &p);

/**
 * Simplified Color Grading from FxSlot.state keys used by ColorGradingEditor:
 * lift|gamma|gain|offset .{r,g,b,y}
 */
void applyColorGrading(QImage *img, const QVariantMap &params);

/**
 * Full video FX chain: skip bypass / PanCrop; builtins (CC, Grading, B&C);
 * Soften/Blur/Invert/Sepia/Gain via OfxHost; PluginFormat::Ofx via processSlot.
 */
void applyVideoFxChain(QImage *img, const QVector<FxSlot> &chain, double timeSec = 0.0);

/** Compat wrapper → applyVideoFxChain(..., 0). */
void applyVideoColorFxChain(QImage *img, const QVector<FxSlot> &chain);

} // namespace openvegas
