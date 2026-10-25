#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <QVariantMap>

namespace openvegas {

/**
 * A seam so a Media Generator can be drawn by VEGAS's own OFX plug-in instead of our code.
 *
 * Checkerboard, Noise Texture, Solid Color and Test Pattern are effects in `Vfx1.ofx`, in
 * the OFX generator context. `src/video/` does not know about `src/plugins/`, so the
 * drawing asks here and the application installs the provider at start-up — the same
 * arrangement TransitionPluginHook uses. With nothing installed, or the plug-in missing or
 * refusing, VegasGenerators draws the frame.
 */
using GeneratorPluginFn = bool (*)(const QString &effectKey, const QVariantMap &params,
                                   double timeSec, const QSize &size, QImage *out);

/** Install the provider. Passing nullptr removes it. */
void setGeneratorPluginProvider(GeneratorPluginFn fn);

/** True when a provider is installed. */
bool hasGeneratorPluginProvider();

/**
 * Draw a generator through the real plug-in.
 *
 * @param effectKey the tail of the VEGAS identifier — "checkerboard", "noisetexture".
 * @param params    the generator's stored values under the plug-in's own names.
 * @return false when there is no provider, no plug-in for it, or the render failed — in
 *         which case `out` is untouched and the caller draws it itself.
 */
bool generatorPluginRender(const QString &effectKey, const QVariantMap &params, double timeSec,
                           const QSize &size, QImage *out);

} // namespace openvegas
