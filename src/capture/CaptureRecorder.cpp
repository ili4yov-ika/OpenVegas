#include "capture/CaptureRecorder.h"

#include "capture/CaptureInput.h"
#include "io/FfmpegLocator.h"

#include <QDir>
#include <QFileInfo>

namespace openvegas {

CaptureRecorder::CaptureRecorder(QObject *parent)
    : QObject(parent)
{
}

CaptureRecorder::~CaptureRecorder()
{
    stop();
}

QStringList CaptureRecorder::argumentsFor(const CapturePlan &plan, const CaptureOutput &output,
                                          const QString &filePath)
{
    QStringList args;
    args << QStringLiteral("-hide_banner") << QStringLiteral("-y");

    const CaptureSource &src = output.source;
    // How the device is opened is shared with the preview, so the two cannot drift apart:
    // a preview that opened a window by its caption while the take opened it by handle
    // would show one window and record another.
    const QStringList input =
        captureInputArguments(src, src.isVideo() ? output.frameRate : 0.0);
    if (input.isEmpty()) {
        return {}; // not openable on this platform; the caller reports rather than runs
    }
    args += input;

    if (output.source.isVideo()) {
        // Scaling happens here rather than in the source: a capture device asked for a
        // size it does not have simply fails to open, while ffmpeg will scale anything.
        // Under Native the output already carries the source's own size, so this is a
        // no-op rather than a special case.
        if (output.size.isValid() && !output.size.isEmpty()
            && output.size != src.nativeSize) {
            const QString scale =
                plan.fit == CaptureFit::Crop
                    ? QStringLiteral("scale=%1:%2:force_original_aspect_ratio=increase,"
                                     "crop=%1:%2")
                          .arg(output.size.width())
                          .arg(output.size.height())
                    : QStringLiteral("scale=%1:%2:force_original_aspect_ratio=decrease,"
                                     "pad=%1:%2:(ow-iw)/2:(oh-ih)/2")
                          .arg(output.size.width())
                          .arg(output.size.height());
            args << QStringLiteral("-vf") << scale;
        }
        // Recording has to keep up with the source in real time, so this trades file size
        // for speed — the take is an intermediate, not a deliverable.
        args << QStringLiteral("-c:v") << QStringLiteral("libx264")
             << QStringLiteral("-preset") << QStringLiteral("ultrafast")
             << QStringLiteral("-crf") << QStringLiteral("18")
             << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
    } else {
        // Every audio file in the take gets the same format — the best any chosen source
        // offered — so they can sit together in one project without resampling later.
        const int depth = plan.bitDepth();
        args << QStringLiteral("-c:a")
             << (depth >= 24 ? QStringLiteral("pcm_s24le") : QStringLiteral("pcm_s16le"));
        if (output.sampleRate > 0) {
            args << QStringLiteral("-ar") << QString::number(output.sampleRate);
        }
        if (output.channels > 0) {
            args << QStringLiteral("-ac") << QString::number(output.channels);
        }
    }

    args << QDir::toNativeSeparators(filePath);
    return args;
}

void CaptureRecorder::wireProcess(QProcess *proc, const QString &sourceName)
{
    connect(proc, &QProcess::finished, this,
            [this, proc, sourceName](int code, QProcess::ExitStatus status) {
                // A recorder ending before stop() was asked for means its device went
                // away or ffmpeg refused the settings — worth saying which one.
                if (m_processes.contains(proc) && status == QProcess::CrashExit) {
                    emit sourceFailed(sourceName, tr("recording stopped unexpectedly"));
                } else if (m_processes.contains(proc) && code != 0) {
                    emit sourceFailed(sourceName,
                                      QString::fromLocal8Bit(proc->readAllStandardError())
                                          .section(QLatin1Char('\n'), -3));
                }
            });
}

bool CaptureRecorder::start(const CapturePlan &plan, const QString &folder, QString *error)
{
    if (isRecording()) {
        if (error) {
            *error = tr("Already recording.");
        }
        return false;
    }
    const QString why = plan.validate();
    if (!why.isEmpty()) {
        if (error) {
            *error = why;
        }
        return false;
    }
    const QString ffmpeg = FfmpegLocator::find();
    if (ffmpeg.isEmpty()) {
        if (error) {
            *error = tr("ffmpeg was not found; capture needs it.");
        }
        return false;
    }
    if (!QDir().mkpath(folder)) {
        if (error) {
            *error = tr("Cannot create %1").arg(folder);
        }
        return false;
    }

    const QVector<CaptureOutput> outputs = plan.outputs();

    // Everything is checked before anything starts: a take with half its sources running
    // is worse than one that refused, because the half that recorded looks like a
    // complete take until it is opened.
    for (const CaptureOutput &o : outputs) {
        if (argumentsFor(plan, o, QDir(folder).filePath(o.fileName)).isEmpty()) {
            if (error) {
                *error = tr("%1 cannot be recorded on this system.").arg(o.source.name);
            }
            return false;
        }
    }

    m_files.clear();
    m_names.clear();
    for (const CaptureOutput &o : outputs) {
        const QString path = QDir(folder).filePath(o.fileName);
        auto *proc = new QProcess(this);
        wireProcess(proc, o.source.name);
        proc->start(ffmpeg, argumentsFor(plan, o, path));
        if (!proc->waitForStarted(5000)) {
            proc->deleteLater();
            stop(); // roll back the ones already going
            if (error) {
                *error = tr("Could not start recording %1.").arg(o.source.name);
            }
            return false;
        }
        m_processes.push_back(proc);
        m_files.push_back(path);
        m_names.push_back(o.source.name);
    }
    return true;
}

void CaptureRecorder::stop()
{
    if (m_processes.isEmpty()) {
        return;
    }
    const QVector<QProcess *> running = m_processes;
    m_processes.clear(); // so the finished handler does not report these as failures

    for (QProcess *proc : running) {
        if (proc->state() == QProcess::NotRunning) {
            continue;
        }
        // "q" on stdin is how ffmpeg is asked to finish: it writes the container's index
        // and closes cleanly. Killing it leaves a file that may not play back at all.
        proc->write("q\n");
        proc->closeWriteChannel();
    }
    for (QProcess *proc : running) {
        if (!proc->waitForFinished(8000)) {
            proc->terminate();
            proc->waitForFinished(2000);
            proc->kill();
        }
        proc->deleteLater();
    }
    emit finished();
}

} // namespace openvegas
