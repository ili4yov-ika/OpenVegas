#pragma once

#include <QStringList>

namespace openvegas {

/**
 * Draws VEGAS's OFX Media Generators — Checkerboard, Noise Texture, Solid Color, Test
 * Pattern — with VEGAS's own Vfx1.ofx instead of our code.
 *
 * Installs itself as the provider behind `video/GeneratorPluginHook.h`. Call once at
 * start-up; with no VEGAS on the machine the provider finds no plug-in and our own
 * renderers (video/VegasGenerators.h) keep drawing.
 */
class OfxGeneratorSource {
public:
    /** Install the provider; `preferredRoots` are VEGAS installs to look in first. */
    static void install(const QStringList &preferredRoots = {});
    /** Remove it — our own renderers draw again. Used by tests. */
    static void uninstall();
};

} // namespace openvegas
