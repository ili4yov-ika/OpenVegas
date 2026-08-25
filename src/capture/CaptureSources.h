#pragma once

#include "capture/CaptureSource.h"

#include <QSize>
#include <QString>
#include <QVector>

namespace openvegas {

/**
 * What decides whether a window is worth offering.
 *
 * Every field is something the OS is asked about a window; kept apart from the asking so
 * the rules can be tested without a desktop. The rules themselves are OBS's
 * (`libobs/util/windows/window-helpers.c`, `check_window_valid` / `add_window`), which is
 * the same set of exclusions any screen recorder converges on after enough bug reports.
 */
struct WindowFacts {
    QString exeName;   ///< Lower-case file name of the owning process.
    QString title;     ///< The window's caption, empty when it has none.
    bool visible = true;
    bool minimized = false;
    /** Hidden by the desktop compositor: UWP apps that are "open" but not on screen. */
    bool cloaked = false;
    /** `WS_EX_TOOLWINDOW`: palettes and tooltips, not windows a user would pick. */
    bool toolWindow = false;
    /** `WS_CHILD`: part of another window, not one of its own. */
    bool child = false;
    QSize clientSize;
};

/**
 * What this machine can be recorded from.
 *
 * Monitors come from the OS: on Windows `EnumDisplayMonitors`, whose rectangles are already
 * in the physical pixels a grabber is told to cut out; elsewhere from Qt, converted. Windows
 * are enumerated with OBS's rules for what counts as one. Cameras, capture cards
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
     * Windows worth offering: on screen, not minimised, not a tool window, with something
     * in them. Empty off Windows, where no window grabber is wired up.
     */
    static QVector<CaptureSource> windows();

    /**
     * Whether a window with these facts should be offered.
     *
     * Split from the enumeration for the same reason the ffmpeg listing parser is: the
     * rules are what break, and they can be checked without a desktop full of windows.
     */
    static bool shouldOfferWindow(const WindowFacts &facts);

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
