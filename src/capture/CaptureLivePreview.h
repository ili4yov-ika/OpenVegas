#pragma once

#include "capture/CaptureSource.h"

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QStringList>

class QProcess;

namespace openvegas {

/**
 * A source shown moving, for as long as the picker is open.
 *
 * `CapturePreview` grabs one frame and lets the device go, which is the right trade for a
 * thumbnail nobody is watching. It is the wrong one for a picker where every source is on
 * screen at once: two monitors called "Display 1" and "Display 2" are told apart by what
 * is happening on them, and a still from four seconds ago is not that.
 *
 * One ffmpeg per source, kept running, emitting frames as they arrive. That costs a
 * decoder per visible card, so the rate is chosen per source rather than fixed —
 * see `frameRateFor()` — and everything stops the moment a take starts, because a camera
 * that is already open cannot be opened again by the recorder.
 */
class CaptureLivePreview : public QObject {
    Q_OBJECT
public:
    explicit CaptureLivePreview(QObject *parent = nullptr);
    ~CaptureLivePreview() override;

    /**
     * How often this kind of source is worth re-drawing.
     *
     * A monitor or a camera is being watched to see what is on it, and at a few frames a
     * second that reads as a slideshow. A window is being watched to confirm it is the
     * right window, which one frame every third of a second settles — and windows are the
     * numerous ones, so this is where the cost is.
     */
    static double frameRateFor(CaptureSource::Kind kind);

    /**
     * ffmpeg arguments that stream `source` as PNG frames on stdout.
     *
     * The device is opened through `captureInputArguments()`, the same call the recorder
     * and the still preview use, so all three see the same picture. Empty for a source
     * with nothing to show, or one this platform cannot open.
     */
    static QStringList argumentsFor(const CaptureSource &source, const QSize &maxSize,
                                    double frameRate);

    /** Start streaming `source`. Replaces whatever was running. */
    void start(const CaptureSource &source, const QSize &maxSize);

    /** Stop and release the device. Safe to call when nothing is running. */
    void stop();

    bool isRunning() const;

    /** What is being streamed, or an empty source when nothing is. */
    CaptureSource source() const { return m_source; }

signals:
    /** One frame off the stream. */
    void frameReady(const QImage &frame);
    /** The stream ended or never started; `reason` is ffmpeg's last words. */
    void failed(const QString &reason);

private:
    void drainFrames();

    QProcess *m_proc = nullptr;
    QByteArray m_buffer;
    CaptureSource m_source;
};

} // namespace openvegas
