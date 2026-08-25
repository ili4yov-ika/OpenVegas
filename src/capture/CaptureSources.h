#pragma once

#include "capture/CaptureSource.h"

#include <QString>
#include <QVector>

namespace openvegas {

/**
 * What this machine can be recorded from.
 *
 * Monitors come from Qt, which already knows them and their sizes. Cameras, capture cards
 * and audio inputs come from ffmpeg's own device listing — the same ffmpeg the renderer
 * already depends on, so recording adds no new dependency and no second idea of what a
 * device is called. Names have to match what the recorder will later be told to open, and
 * asking the tool that will open it is the only way to be sure they do.
 */
class CaptureSources {
public:
    /** Every monitor attached, sized in device pixels. */
    static QVector<CaptureSource> screens();

    /**
     * Cameras, capture cards and audio inputs, as ffmpeg names them.
     *
     * Empty when ffmpeg is missing or the platform has no device backend, which is not an
     * error: screen capture still works, and the caller can say so rather than failing.
     */
    static QVector<CaptureSource> devices();

    /** Screens and devices together, in the order a picker should show them. */
    static QVector<CaptureSource> all();

    /**
     * Parse an ffmpeg device listing into sources.
     *
     * Split out from running ffmpeg so the parsing — the part that actually breaks when a
     * version changes its wording — can be tested against captured output without a
     * camera present.
     */
    static QVector<CaptureSource> parseDshowListing(const QString &stderrText);
};

} // namespace openvegas
