#include "capture/CapturePreview.h"

#include "capture/CaptureInput.h"
#include "io/FfmpegLocator.h"

#include <QPointer>
#include <QProcess>
#include <QTimer>

namespace openvegas {

namespace {


/** How long a single grab may take before it is given up on. */
constexpr int kGrabTimeoutMs = 6000;

} // namespace

CapturePreview::CapturePreview(QObject *parent)
    : QObject(parent)
{
}

CapturePreview::~CapturePreview()
{
    cancel();
}

QStringList CapturePreview::argumentsFor(const CaptureSource &source, const QSize &maxSize)
{
    if (!source.isVideo()) {
        return {}; // a microphone has nothing to show
    }
    // One frame a second is all a still needs, and asking a screen grabber for its full
    // rate here would have it capturing at 165 fps to throw all but one frame away.
    const QStringList input = captureInputArguments(source, 1.0);
    if (input.isEmpty()) {
        return {};
    }

    QStringList args;
    args << QStringLiteral("-hide_banner") << QStringLiteral("-loglevel")
         << QStringLiteral("error");
    args += input;
    args << QStringLiteral("-frames:v") << QStringLiteral("1");
    if (maxSize.isValid() && !maxSize.isEmpty()) {
        // Fitted inside the box, keeping its shape. `force_original_aspect_ratio` rather
        // than an expression: a filter argument is comma-separated, so a `min(a,b)` in one
        // is read as the end of the filter and ffmpeg refuses the whole command.
        args << QStringLiteral("-vf")
             << QStringLiteral("scale=%1:%2:force_original_aspect_ratio=decrease")
                    .arg(maxSize.width())
                    .arg(maxSize.height());
    }
    // PNG down the pipe: lossless, self-delimiting, and QImage reads it without a file.
    args << QStringLiteral("-f") << QStringLiteral("image2pipe") << QStringLiteral("-c:v")
         << QStringLiteral("png") << QStringLiteral("-");
    return args;
}

bool CapturePreview::isBusy() const
{
    return m_proc != nullptr;
}

void CapturePreview::cancel()
{
    if (!m_proc) {
        return;
    }
    QProcess *proc = m_proc;
    m_proc = nullptr; // so finish() ignores what this one has to say
    proc->disconnect(this);
    proc->kill();
    proc->waitForFinished(1000);
    proc->deleteLater();
    m_buffer.clear();
}

void CapturePreview::request(const CaptureSource &source, const QSize &maxSize)
{
    // The newest request is the one the user is waiting on; an older grab of a source they
    // have already clicked past is worth nothing.
    cancel();

    const QStringList args = argumentsFor(source, maxSize);
    if (args.isEmpty()) {
        emit failed(tr("%1 has no picture to show.").arg(source.name));
        return;
    }
    const QString ffmpeg = FfmpegLocator::find();
    if (ffmpeg.isEmpty()) {
        emit failed(tr("ffmpeg was not found; previews need it."));
        return;
    }

    m_buffer.clear();
    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this]() {
        if (m_proc) {
            m_buffer += m_proc->readAllStandardOutput();
        }
    });
    connect(m_proc, &QProcess::finished, this, [this](int, QProcess::ExitStatus) { finish(); });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) { finish(); });
    m_proc->start(ffmpeg, args);

    // A device that never produces a frame — a camera another program is holding — would
    // otherwise leave the preview waiting for ever.
    //
    // The timer is tied to *this* process, not to whatever is running when it fires. Left
    // as a bare `m_proc`, an old grab's timeout kills a later one that is doing nothing
    // wrong: at one grab every two and a half seconds and a six-second limit, the third
    // preview in a row was being killed by the first one's timer, and the pane said "No
    // picture came back" about a source that had just worked twice.
    QPointer<QProcess> watched(m_proc);
    QTimer::singleShot(kGrabTimeoutMs, this, [this, watched]() {
        if (watched && watched == m_proc && watched->state() != QProcess::NotRunning) {
            watched->kill();
        }
    });
}

void CapturePreview::finish()
{
    if (!m_proc) {
        return;
    }
    QProcess *proc = m_proc;
    m_proc = nullptr;
    m_buffer += proc->readAllStandardOutput();
    const QString stderrText = QString::fromUtf8(proc->readAllStandardError()).trimmed();
    proc->deleteLater();

    QImage frame;
    if (!m_buffer.isEmpty()) {
        frame = QImage::fromData(m_buffer, "PNG");
    }
    m_buffer.clear();

    if (frame.isNull()) {
        emit failed(stderrText.isEmpty() ? tr("No picture came back.")
                                         : stderrText.section(QLatin1Char('\n'), -1));
        return;
    }
    emit frameReady(frame);
}

} // namespace openvegas
