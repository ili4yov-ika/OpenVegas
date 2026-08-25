#include "capture/CaptureRecorder.h"

#include "io/FfmpegLocator.h"

#include <QDir>
#include <QFileInfo>

namespace openvegas {

namespace {

/** `-video_size WxH`, the pair of flags ffmpeg wants for a sized input. */
void appendSize(QStringList *args, const QSize &size)
{
    if (!args || !size.isValid() || size.isEmpty()) {
        return;
    }
    args->append(QStringLiteral("-video_size"));
    args->append(QStringLiteral("%1x%2").arg(size.width()).arg(size.height()));
}

} // namespace

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
    switch (src.kind) {
    case CaptureSource::Kind::Screen: {
        // A screen grabber takes the whole desktop and is told which part to keep, so a
        // second monitor is a region rather than a device of its own. Without the offset
        // and size, picking one monitor records every monitor.
#ifdef Q_OS_WIN
        args << QStringLiteral("-f") << QStringLiteral("gdigrab");
        if (output.frameRate > 0.0) {
            args << QStringLiteral("-framerate")
                 << QString::number(output.frameRate, 'g', 6);
        }
        if (!src.origin.isNull()) {
            args << QStringLiteral("-offset_x") << QString::number(src.origin.x())
                 << QStringLiteral("-offset_y") << QString::number(src.origin.y());
        }
        appendSize(&args, src.nativeSize);
        args << QStringLiteral("-i") << QStringLiteral("desktop");
#else
        args << QStringLiteral("-f") << QStringLiteral("x11grab");
        if (output.frameRate > 0.0) {
            args << QStringLiteral("-framerate")
                 << QString::number(output.frameRate, 'g', 6);
        }
        appendSize(&args, src.nativeSize);
        // x11grab carries the offset in the display name rather than in its own flags.
        args << QStringLiteral("-i")
             << QStringLiteral(":0.0+%1,%2").arg(src.origin.x()).arg(src.origin.y());
#endif
        break;
    }
    case CaptureSource::Kind::Window: {
#ifdef Q_OS_WIN
        args << QStringLiteral("-f") << QStringLiteral("gdigrab");
        if (output.frameRate > 0.0) {
            args << QStringLiteral("-framerate")
                 << QString::number(output.frameRate, 'g', 6);
        }
        // By handle, not by title: a title changes while a take is running (a document is
        // saved, a tab is switched) and two windows can carry the same one.
        args << QStringLiteral("-i") << QStringLiteral("hwnd=%1").arg(src.id);
#else
        return {}; // no window grabber wired up off Windows yet
#endif
        break;
    }
    case CaptureSource::Kind::Camera: {
#ifdef Q_OS_WIN
        args << QStringLiteral("-f") << QStringLiteral("dshow");
        appendSize(&args, src.nativeSize);
        if (src.frameRate > 0.0) {
            args << QStringLiteral("-framerate") << QString::number(src.frameRate, 'g', 6);
        }
        args << QStringLiteral("-i") << QStringLiteral("video=%1").arg(src.id);
#else
        args << QStringLiteral("-f") << QStringLiteral("v4l2");
        appendSize(&args, src.nativeSize);
        args << QStringLiteral("-i") << src.id;
#endif
        break;
    }
    case CaptureSource::Kind::Audio: {
#ifdef Q_OS_WIN
        args << QStringLiteral("-f") << QStringLiteral("dshow");
        args << QStringLiteral("-i") << QStringLiteral("audio=%1").arg(src.id);
#else
        args << QStringLiteral("-f") << QStringLiteral("pulse") << QStringLiteral("-i")
             << (src.id.isEmpty() ? QStringLiteral("default") : src.id);
#endif
        break;
    }
    }

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
