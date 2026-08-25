#include "capture/CaptureSources.h"

#include "io/FFmpegEncoder.h"

#include <QGuiApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QScreen>

namespace openvegas {

QVector<CaptureSource> CaptureSources::screens()
{
    QVector<CaptureSource> out;
    const QList<QScreen *> list = QGuiApplication::screens();
    for (int i = 0; i < list.size(); ++i) {
        QScreen *sc = list[i];
        if (!sc) {
            continue;
        }
        CaptureSource s;
        s.kind = CaptureSource::Kind::Screen;
        // The index is what a grabber is told to open; the name is for the user.
        s.id = QString::number(i);
        s.name = sc->name().isEmpty() ? QObject::tr("Display %1").arg(i + 1)
                                      : sc->name();
        // Device pixels, not logical ones: a scaled display records at its real size, and
        // recording a 4K monitor at its 1080p logical size would quietly lose half of it.
        const QSize logical = sc->geometry().size();
        const double dpr = sc->devicePixelRatio();
        s.nativeSize = QSize(int(std::lround(logical.width() * dpr)),
                             int(std::lround(logical.height() * dpr)));
        s.frameRate = sc->refreshRate() > 1.0 ? sc->refreshRate() : 60.0;
        out.push_back(s);
    }
    return out;
}

QVector<CaptureSource> CaptureSources::parseDshowListing(const QString &stderrText)
{
    QVector<CaptureSource> out;

    // ffmpeg prints the listing on stderr, one device per line, with its alternative name
    // on the line after. The prefix in brackets has changed between versions — older ones
    // say `[dshow @ ...]`, 8.x says `[in#0 @ ...]` — so any bracketed prefix is accepted
    // and the device's kind is taken from the `(video)` / `(audio)` suffix:
    //
    //   [in#0 @ 000001d...] "Camera (NVIDIA Broadcast)" (video)
    //   [in#0 @ 000001d...]   Alternative name "@device_sw_{860BB310-...}"
    //
    // Older versions printed no suffix and grouped devices under a heading instead, so
    // that is kept as a fallback. Matching `[dshow` alone, which is what the documented
    // format suggests, finds nothing at all on a current ffmpeg.
    //
    // A custom delimiter: the pattern itself contains `)"`, in `"(.+)"`, which would end a
    // plain raw string in the middle of the expression.
    static const QRegularExpression nameLine(
        QStringLiteral(R"RX(^\s*\[[^\]]*\]\s+"(.+)"(?:\s+\((video|audio)\))?\s*$)RX"));
    static const QRegularExpression header(
        QStringLiteral(R"RX(DirectShow\s+(video|audio)\s+devices)RX"),
        QRegularExpression::CaseInsensitiveOption);

    QString section;
    const QStringList lines = stderrText.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        const QRegularExpressionMatch head = header.match(line);
        if (head.hasMatch()) {
            section = head.captured(1).toLower();
            continue;
        }
        const QRegularExpressionMatch m = nameLine.match(raw);
        if (!m.hasMatch()) {
            continue;
        }
        const QString name = m.captured(1);
        if (name.startsWith(QStringLiteral("@device"))) {
            continue; // the alternative-name line, not a device of its own
        }
        // The per-line kind wins when ffmpeg gives it; otherwise the section heading does.
        const QString kindText = m.captured(2).isEmpty() ? section : m.captured(2).toLower();
        if (kindText.isEmpty()) {
            continue;
        }

        CaptureSource s;
        s.name = name;
        s.id = name; // what -i "video=<name>" wants
        if (kindText == QLatin1String("audio")) {
            s.kind = CaptureSource::Kind::Audio;
            // ffmpeg does not report a device's formats in the listing, so these are the
            // defaults a capture starts from; the picker lets them be raised, and the
            // plan takes the best across everything chosen.
            s.sampleRate = 48000;
            s.channels = 2;
            s.bitDepth = 16;
        } else {
            s.kind = CaptureSource::Kind::Camera;
            s.nativeSize = QSize(1920, 1080);
            s.frameRate = 30.0;
        }
        out.push_back(s);
    }
    return out;
}

QVector<CaptureSource> CaptureSources::devices()
{
    const QString ffmpeg = FFmpegEncoder::findFfmpeg();
    if (ffmpeg.isEmpty()) {
        return {}; // no ffmpeg is not an error: screen capture still works
    }
#ifdef Q_OS_WIN
    QProcess proc;
    proc.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-list_devices"),
                        QStringLiteral("true"), QStringLiteral("-f"),
                        QStringLiteral("dshow"), QStringLiteral("-i"),
                        QStringLiteral("dummy")});
    if (!proc.waitForFinished(8000)) {
        proc.kill();
        return {};
    }
    // Listing devices always "fails" — ffmpeg has nothing to open — so the exit code says
    // nothing and the output is what matters.
    return parseDshowListing(QString::fromLocal8Bit(proc.readAllStandardError()));
#else
    return {};
#endif
}

QVector<CaptureSource> CaptureSources::all()
{
    QVector<CaptureSource> out = screens();
    out += devices();
    return out;
}

} // namespace openvegas
