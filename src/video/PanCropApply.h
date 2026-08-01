#pragma once

#include "model/ProjectModel.h"

#include <QImage>

namespace openvegas {

/**
 * Fit source into project frame, apply Pan/Crop (crop/scale/rotate/flip) and optional mask hold.
 * Output is ARGB premul at (outW × outH), typically the preview/project frame size.
 */
QImage applyPanCrop(const QImage &source, const PanCropKeyframe &kf, int frameW, int frameH,
                    int outW, int outH, bool stretchToFill, const MaskKeyframe *maskHold);

/** Rasterize mask paths into alpha (0…255) over out size; paths in project-frame pixels. */
void applyMaskAlpha(QImage *layerArgbPremul, const MaskKeyframe &mask, int frameW, int frameH);

} // namespace openvegas
