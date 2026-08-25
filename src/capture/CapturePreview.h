#pragma once

#include "capture/CaptureSource.h"

#include <QImage>
#include <QObject>
#include <QStringList>

class QProcess;

namespace openvegas {

/**
 * One still frame of a source, so it can be seen before it is recorded.
 *
 * A list of monitors and windows by name is not enough to pick from: two displays called
 * "Display 1" and "Display 2" say nothing about which is which, and a window's title tells
 * you the document, not what is on screen. A picture settles it in one glance.
 *
 * A still rather than a live feed on purpose. A running preview means a second capture of
 * everything on the list — for a camera that is the same device the take will want, and
 * some cameras only open once. Grabbing one frame, releasing the device and asking again
 * a moment later costs nothing while nothing is being recorded, and is easy to stop while
 * something is.
 */
class CapturePreview : public QObject {
    Q_OBJECT
public:
    explicit CapturePreview(QObject *parent = nullptr);
    ~CapturePreview() override;

    /**
     * ffmpeg arguments that grab a single frame of `source` as a PNG on stdout.
     *
     * The device is opened exactly the way the recorder opens it (`captureInputArguments`),
     * so what is previewed is what would be recorded. Empty for a source with no picture —
     * a microphone — or one this platform cannot open.
     */
    static QStringList argumentsFor(const CaptureSource &source, const QSize &maxSize);

    /** Ask for a frame. A request already in flight is dropped in favour of this one. */
    void request(const CaptureSource &source, const QSize &maxSize);

    /** Abandon any request in flight — a take is starting and the device is wanted. */
    void cancel();

    bool isBusy() const;

signals:
    /** A frame arrived for the source last requested. */
    void frameReady(const QImage &frame);
    /** No frame could be had; `reason` is ffmpeg's last words. */
    void failed(const QString &reason);

private:
    void finish();

    QProcess *m_proc = nullptr;
    QByteArray m_buffer;
};

} // namespace openvegas
