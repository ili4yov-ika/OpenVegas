#include "capture/CaptureInput.h"

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

void appendRate(QStringList *args, double frameRate)
{
    if (!args || frameRate <= 0.0) {
        return;
    }
    args->append(QStringLiteral("-framerate"));
    args->append(QString::number(frameRate, 'g', 6));
}

} // namespace

QStringList captureInputArguments(const CaptureSource &source, double frameRate)
{
    QStringList args;
    switch (source.kind) {
    case CaptureSource::Kind::Screen: {
        // A screen grabber takes the whole desktop and is told which part to keep, so a
        // second monitor is a region rather than a device of its own. Without the offset
        // and size, picking one monitor records every monitor.
#ifdef Q_OS_WIN
        args << QStringLiteral("-f") << QStringLiteral("gdigrab");
        appendRate(&args, frameRate);
        if (!source.origin.isNull()) {
            args << QStringLiteral("-offset_x") << QString::number(source.origin.x())
                 << QStringLiteral("-offset_y") << QString::number(source.origin.y());
        }
        appendSize(&args, source.nativeSize);
        args << QStringLiteral("-i") << QStringLiteral("desktop");
#else
        args << QStringLiteral("-f") << QStringLiteral("x11grab");
        appendRate(&args, frameRate);
        appendSize(&args, source.nativeSize);
        // x11grab carries the offset in the display name rather than in its own flags.
        args << QStringLiteral("-i")
             << QStringLiteral(":0.0+%1,%2").arg(source.origin.x()).arg(source.origin.y());
#endif
        break;
    }
    case CaptureSource::Kind::Window: {
#ifdef Q_OS_WIN
        args << QStringLiteral("-f") << QStringLiteral("gdigrab");
        appendRate(&args, frameRate);
        // By handle, not by title: a title changes while a take is running (a document is
        // saved, a tab is switched) and two windows can carry the same one.
        args << QStringLiteral("-i") << QStringLiteral("hwnd=%1").arg(source.id);
#else
        return {}; // no window grabber wired up off Windows yet
#endif
        break;
    }
    case CaptureSource::Kind::Camera: {
#ifdef Q_OS_WIN
        args << QStringLiteral("-f") << QStringLiteral("dshow");
        appendSize(&args, source.nativeSize);
        appendRate(&args, source.frameRate);
        args << QStringLiteral("-i") << QStringLiteral("video=%1").arg(source.id);
#else
        args << QStringLiteral("-f") << QStringLiteral("v4l2");
        appendSize(&args, source.nativeSize);
        args << QStringLiteral("-i") << source.id;
#endif
        break;
    }
    case CaptureSource::Kind::Audio: {
#ifdef Q_OS_WIN
        args << QStringLiteral("-f") << QStringLiteral("dshow");
        args << QStringLiteral("-i") << QStringLiteral("audio=%1").arg(source.id);
#else
        args << QStringLiteral("-f") << QStringLiteral("pulse") << QStringLiteral("-i")
             << (source.id.isEmpty() ? QStringLiteral("default") : source.id);
#endif
        break;
    }
    }
    return args;
}

} // namespace openvegas
