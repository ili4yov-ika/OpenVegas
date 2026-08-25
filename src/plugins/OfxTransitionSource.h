#pragma once

#include <QStringList>

namespace openvegas {

/**
 * Draws transitions with VEGAS's own OFX plug-ins instead of the built-in geometry.
 *
 * Installs itself as the provider behind `video/TransitionPluginHook.h`, which is how the
 * compositor reaches a plug-in host it must not depend on directly. Call once at start-up;
 * with the VEGAS bundle absent it installs nothing and the built-in renderers keep drawing.
 */
class OfxTransitionSource {
public:
    /**
     * Install the provider. Safe to call more than once.
     *
     * @param preferredRoots VEGAS install roots to look in before the usual plug-in
     *        search. A machine can carry several VEGAS versions and the older ones do not
     *        all have the transition effects; this is how a caller says which one it means.
     */
    static void install(const QStringList &preferredRoots = {});

    /** Remove it — the built-in geometry draws again. Used by tests. */
    static void uninstall();
};

} // namespace openvegas
