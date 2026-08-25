#pragma once

#include <QPoint>
#include <QSize>
#include <QString>
#include <QVector>

namespace openvegas {

/**
 * One thing OpenVegas Capture can record from.
 *
 * Deliberately a plain description with no capture machinery attached: what a source *is*
 * — a monitor, a window, a camera, a microphone — and what it can give, separately from
 * whatever ends up doing the grabbing. The rules the recording follows (which resolution
 * everything lands at, which audio format the file gets) are decided from these numbers
 * alone, so they can be settled and tested without a capture device in the room.
 */
struct CaptureSource {
    enum class Kind {
        Screen,   ///< A whole monitor.
        Window,   ///< One window on screen.
        Camera,   ///< A camera or capture card.
        Audio,    ///< A microphone, line in, or loopback.
    };

    Kind kind = Kind::Screen;
    /** What the backend needs to open it — a display index, window handle, device name. */
    QString id;
    /** What to show the user. */
    QString name;

    // Video sources only.
    QSize nativeSize;
    /**
     * Where a screen starts on the virtual desktop, in device pixels.
     *
     * Screen grabbers take the whole desktop and are told which part of it to keep, so a
     * second monitor is a region rather than a device of its own — without this, picking
     * it records everything.
     */
    QPoint origin;
    double frameRate = 0.0;

    // Audio sources only.
    int sampleRate = 0;
    int channels = 0;
    /** Bits per sample the device offers; 0 when unknown. */
    int bitDepth = 0;

    bool isVideo() const { return kind != Kind::Audio; }
    bool isAudio() const { return kind == Kind::Audio; }
};

/** How a video source that is not the reference one is made to fit. */
enum class CaptureFit {
    /** Scale it to the project resolution, keeping its shape and padding the rest. */
    Letterbox,
    /** Scale and crop to fill, losing the edges that do not fit. */
    Crop,
    /** Leave it at its own size — each track keeps whatever the source gave. */
    Native,
};

/** One recorded file: a single source, its own track when the take is imported. */
struct CaptureOutput {
    CaptureSource source;
    /** What this source will be recorded at; equals the source size under Native. */
    QSize size;
    double frameRate = 0.0;
    int sampleRate = 0;
    int channels = 0;
    /** Where it is written, relative to the take folder. */
    QString fileName;
};

} // namespace openvegas
