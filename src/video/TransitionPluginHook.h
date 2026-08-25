#pragma once

#include <QImage>
#include <QString>
#include <QVariantMap>

namespace openvegas {

/**
 * A seam so a transition can be drawn by VEGAS's own plug-in instead of our geometry.
 *
 * Every transition group here has a renderer written from the parameter names its presets
 * carry. That is a reading, not a measurement — and the plug-ins that do it properly are
 * sitting in the VEGAS bundle the project already loads effects from: `Vfx1.ofx` declares
 * all twenty-four groups in the OFX transition context, with two source clips and their
 * own progress parameter.
 *
 * Hosting them means OFX, and `src/video/` does not know about `src/plugins/` — the
 * compositor is linked into targets that have no plug-in host at all. So the drawing asks
 * here, and the application installs the provider at start-up, the same arrangement
 * NestedFrameHook uses for nested projects.
 *
 * With nothing installed, or when the plug-in is missing or refuses, the built-in geometry
 * draws the frame. That is the honest fallback: a transition that cannot be hosted still
 * has to produce a picture.
 */
using TransitionPluginFn = bool (*)(const QString &groupKey, const QImage &from, const QImage &to,
                                    double progress, const QVariantMap &params, QImage *out);

/** Install the provider. Passing nullptr removes it. */
void setTransitionPluginProvider(TransitionPluginFn fn);

/** True when a provider is installed. */
bool hasTransitionPluginProvider();

/**
 * Draw `groupKey` through the real plug-in.
 *
 * @param groupKey the tail of the VEGAS identifier — "pagepeel", "iris", "portals".
 * @param params   preset values under the keys the preset package uses (`peelAngle`);
 *                 the provider maps them to whatever the plug-in calls them.
 * @return false when there is no provider, no plug-in for this group, or the render
 *         failed — in which case `out` is untouched and the caller draws it itself.
 */
bool transitionPluginRender(const QString &groupKey, const QImage &from, const QImage &to,
                            double progress, const QVariantMap &params, QImage *out);

} // namespace openvegas
