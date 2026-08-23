#pragma once

#include <QImage>
#include <QSize>
#include <QString>

namespace openvegas {

/**
 * A seam so the frame caches can show a nested VEGAS project without depending on how
 * one is loaded.
 *
 * A `.veg` dropped on a timeline is a clip whose picture has to be *composed* from a
 * second project — which means ProjectModel, VegReader and the whole compositor. The
 * filmstrip and preview caches sit far below all that, and wiring them to it directly
 * dragged project loading into every target that merely draws thumbnails.
 *
 * So the caches ask here instead, and NestedProjectSource installs itself as the
 * provider at start-up. With nothing installed the caches simply get no picture, which is
 * the correct answer for a build that has no project loader in it at all.
 */
using NestedFrameFn = QImage (*)(const QString &path, double timeSec, const QSize &size);

/** Install the renderer. Passing nullptr removes it. */
void setNestedFrameProvider(NestedFrameFn fn);

/** True when a provider is installed. */
bool hasNestedFrameProvider();

/** Cheap check that does not need a provider: is this path a project rather than media? */
bool looksLikeProjectMedia(const QString &path);

/** Composed frame, or null when there is no provider or it has nothing yet. */
QImage nestedFrame(const QString &path, double timeSec, const QSize &size);

} // namespace openvegas
