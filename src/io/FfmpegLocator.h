#pragma once

#include <QString>

namespace openvegas {

/**
 * Where ffmpeg is on this machine.
 *
 * One answer for the whole program: the renderer, the filmstrip cache and capture all shell
 * out to the same binary, and two ideas of where it lives would eventually disagree — the
 * one that mattered being whichever ran first. Kept in a file of its own so a target that
 * only needs to run ffmpeg does not have to link the media cache to ask.
 *
 * The search covers PATH first (a user who installed it meant that one), then the
 * application's own folder for a portable drop-in, then the usual per-platform locations.
 */
class FfmpegLocator {
public:
    /** Full path to ffmpeg, or empty when it is not installed. Cached after the first call. */
    static QString find();
};

} // namespace openvegas
