#include "capture/CaptureLivePreview.h"

#include "capture/CaptureInput.h"
#include "io/FfmpegLocator.h"

#include <QProcess>

namespace openvegas {

namespace {

/** The eight bytes every PNG starts with. */
const QByteArray kPngSignature = QByteArray::fromHex("89504e470d0a1a0a");
/** The IEND chunk, which ends one — length 0, type IEND, and its fixed CRC. */
const QByteArray kPngEnd = QByteArray::fromHex("0000000049454e44ae426082");

/**
 * Enough to keep a frame or two while one arrives, and no more.
 *
 * A preview that falls behind must drop frames rather than grow: the picker can sit open
 * for as long as someone is deciding what to record, and a buffer that only ever grows
 * turns that into a leak measured in monitors per minute.
 */
constexpr int kMaxBufferBytes = 32 * 1024 * 1024;

} // namespace

CaptureLivePreview::CaptureLivePreview(QObject *parent)
    : QObject(parent)
{
}

CaptureLivePreview::~CaptureLivePreview()
{
    stop();
}

double CaptureLivePreview::frameRateFor(CaptureSource::Kind kind)
{
    switch (kind) {
    case CaptureSource::Kind::Window:
        return 3.0;
    case CaptureSource::Kind::Screen:
    case CaptureSource::Kind::Camera:
        return 12.0;
    case CaptureSource::Kind::Audio:
        break;
    }
    return 0.0;
}

QStringList CaptureLivePreview::argumentsFor(const CaptureSource &source, const QSize &maxSize,
                                             double frameRate)
{
    if (!source.isVideo() || frameRate <= 0.0) {
        return {}; // a microphone has nothing to show
    }
    // Asked for at the device, not thinned afterwards: a screen grabber told to run at its
    // own 165 Hz would capture every one of those frames to throw all but a twelfth away.
    const QStringList input = captureInputArguments(source, frameRate);
    if (input.isEmpty()) {
        return {};
    }

    QStringList args;
    args << QStringLiteral("-hide_banner") << QStringLiteral("-loglevel")
         << QStringLiteral("error");
    args += input;
    if (maxSize.isValid() && !maxSize.isEmpty()) {
        // Fitted inside the box, keeping its shape. `force_original_aspect_ratio` rather
        // than an expression: a filter argument is comma-separated, so a `min(a,b)` in one
        // is read as the end of the filter and ffmpeg refuses the whole command.
        args << QStringLiteral("-vf")
             << QStringLiteral("scale=%1:%2:force_original_aspect_ratio=decrease")
                    .arg(maxSize.width())
                    .arg(maxSize.height());
    }
    // Scaling down to a thumbnail is most of the work, so it is worth saying out loud that
    // the output rate is the same as the input one — otherwise ffmpeg is free to duplicate
    // frames up to the stream's nominal rate and we decode pictures nobody asked for.
    args << QStringLiteral("-r") << QString::number(frameRate, 'g', 6);
    // PNG down the pipe: lossless, self-delimiting, and QImage reads it without a file.
    args << QStringLiteral("-f") << QStringLiteral("image2pipe") << QStringLiteral("-c:v")
         << QStringLiteral("png") << QStringLiteral("-");
    return args;
}

void CaptureLivePreview::start(const CaptureSource &source, const QSize &maxSize)
{
    stop();

    const double rate = frameRateFor(source.kind);
    const QStringList args = argumentsFor(source, maxSize, rate);
    if (args.isEmpty()) {
        emit failed(tr("%1 has no picture to show.").arg(source.name));
        return;
    }
    const QString ffmpeg = FfmpegLocator::find();
    if (ffmpeg.isEmpty()) {
        emit failed(tr("ffmpeg was not found."));
        return;
    }

    m_source = source;
    m_buffer.clear();
    m_proc = new QProcess(this);
    m_proc->setProgram(ffmpeg);
    m_proc->setArguments(args);
    m_proc->setProcessChannelMode(QProcess::SeparateChannels);

    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this]() {
        if (!m_proc) {
            return;
        }
        m_buffer += m_proc->readAllStandardOutput();
        if (m_buffer.size() > kMaxBufferBytes) {
            // Whatever is in there is no longer a frame boundary we can trust.
            m_buffer.clear();
        }
        drainFrames();
    });
    connect(m_proc, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        if (!m_proc) {
            return;
        }
        const QString err = QString::fromLocal8Bit(m_proc->readAllStandardError()).trimmed();
        // A preview that ends on its own has failed: nothing here asks it to stop except
        // stop(), which disconnects first.
        emit failed(err.isEmpty() ? tr("The preview of %1 ended.").arg(m_source.name) : err);
    });

    m_proc->start(QIODevice::ReadOnly);
}

void CaptureLivePreview::stop()
{
    if (!m_proc) {
        return;
    }
    QProcess *proc = m_proc;
    m_proc = nullptr;
    // Disconnected first: killing it is not a failure, and the finished handler would
    // otherwise report the take's own device release as one.
    proc->disconnect(this);
    if (proc->state() != QProcess::NotRunning) {
        proc->kill();
        proc->waitForFinished(1500);
    }
    proc->deleteLater();
    m_buffer.clear();
    m_source = CaptureSource();
}

bool CaptureLivePreview::isRunning() const
{
    return m_proc && m_proc->state() != QProcess::NotRunning;
}

void CaptureLivePreview::drainFrames()
{
    // Only the newest complete frame is worth drawing: if several arrived while the UI was
    // busy, showing them in turn would just replay the backlog late.
    QImage newest;
    while (true) {
        const int start = m_buffer.indexOf(kPngSignature);
        if (start < 0) {
            break;
        }
        const int end = m_buffer.indexOf(kPngEnd, start + kPngSignature.size());
        if (end < 0) {
            if (start > 0) {
                m_buffer.remove(0, start); // drop whatever preceded a frame we can use
            }
            break;
        }
        const int frameEnd = end + kPngEnd.size();
        QImage frame;
        if (frame.loadFromData(
                reinterpret_cast<const uchar *>(m_buffer.constData() + start),
                frameEnd - start, "PNG")) {
            newest = frame;
        }
        m_buffer.remove(0, frameEnd);
    }
    if (!newest.isNull()) {
        emit frameReady(newest);
    }
}

} // namespace openvegas
