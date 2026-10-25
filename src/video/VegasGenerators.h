#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <QVariantMap>

namespace openvegas {

/**
 * VEGAS's six non-text Media Generators drawn by our own code, from the parameters VEGAS
 * stores for them (a preset's values, or what a .veg says).
 *
 * The four OFX ones are measured against VEGAS's own Vfx1.ofx, rendered through our host
 * (the [generator-ref] test): Checkerboard, Solid Color and Test Pattern to the pixel level,
 * Noise Texture in kind only — its noise is the plug-in's own. Color Gradient and Credit
 * Roll are legacy DirectX plug-ins nothing here can host; they are drawn from the decoded
 * blocks (src/io/VegLegacyGenerators.h). When VEGAS is installed the OFX four are drawn by
 * the plug-in instead (GeneratorPluginHook); this is what draws them otherwise.
 *
 * @param pluginId  "{Svfx:com.vegascreativesoftware:…}" or the legacy CLSID in braces
 * @param timeSec, lengthSec  where in the event the frame is, for Credit Roll's motion
 * @return a premultiplied ARGB32 frame, or a null image for an id this does not know
 */
QImage renderVegasGenerator(const QString &pluginId, const QVariantMap &params, const QSize &size,
                            double timeSec = 0.0, double lengthSec = 10.0);

/** True for the six generator ids renderVegasGenerator() draws. */
bool isVegasGeneratorId(const QString &pluginId);

/**
 * Credit Roll laid out still, its text block at the top of its area — what a preset
 * thumbnail shows, rather than whichever moment of the roll a time would pick.
 */
QImage renderCreditRollStill(const QVariantMap &params, const QSize &size);

} // namespace openvegas
