#include "io/MediaProbe.h"
#include "io/MediaFilmstripCache.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace openvegas {
namespace {

double parseFraction(const QString &s)
{
    const QString t = s.trimmed();
    if (t.isEmpty() || t == QLatin1String("0/0") || t == QLatin1String("N/A")) {
        return 0.0;
    }
    const int slash = t.indexOf(QLatin1Char('/'));
    if (slash > 0) {
        bool okN = false;
        bool okD = false;
        const double n = t.left(slash).toDouble(&okN);
        const double d = t.mid(slash + 1).toDouble(&okD);
        if (okN && okD && d != 0.0) {
            return n / d;
        }
        return 0.0;
    }
    bool ok = false;
    const double v = t.toDouble(&ok);
    return ok ? v : 0.0;
}

int bitsFromPixFmt(const QString &pix)
{
    const QString p = pix.toLower();
    if (p.contains(QLatin1String("p10")) || p.contains(QLatin1String("10le"))
        || p.contains(QLatin1String("10be"))) {
        return 10;
    }
    if (p.contains(QLatin1String("p12")) || p.contains(QLatin1String("12le"))) {
        return 12;
    }
    if (p.contains(QLatin1String("p16")) || p.contains(QLatin1String("16le"))) {
        return 16;
    }
    if (p.contains(QLatin1String("yuv420p")) || p.contains(QLatin1String("yuv422p"))
        || p.contains(QLatin1String("yuv444p")) || p.contains(QLatin1String("rgb24"))
        || p.contains(QLatin1String("bgr24")) || p.contains(QLatin1String("nv12"))) {
        return 8;
    }
    return 0;
}

MediaProbeInfo parseFfprobeJson(const QByteArray &json, const QString &path)
{
    MediaProbeInfo info;
    info.path = path;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        info.error = QObject::tr("Invalid ffprobe output");
        return info;
    }
    const QJsonObject root = doc.object();
    const QJsonObject format = root.value(QStringLiteral("format")).toObject();
    info.formatName = format.value(QStringLiteral("format_name")).toString();
    info.formatLongName = format.value(QStringLiteral("format_long_name")).toString();
    info.durationSec = format.value(QStringLiteral("duration")).toString().toDouble();
    if (info.durationSec <= 0.0) {
        info.durationSec = format.value(QStringLiteral("duration")).toDouble();
    }

    const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();
    info.streamCount = streams.size();
    for (const QJsonValue &sv : streams) {
        const QJsonObject s = sv.toObject();
        const QString type = s.value(QStringLiteral("codec_type")).toString();
        if (type == QLatin1String("video") && !info.hasVideo) {
            info.hasVideo = true;
            info.width = s.value(QStringLiteral("width")).toInt();
            info.height = s.value(QStringLiteral("height")).toInt();
            info.pixelFormat = s.value(QStringLiteral("pix_fmt")).toString();
            info.fieldOrder = s.value(QStringLiteral("field_order")).toString();
            info.videoBitDepth = s.value(QStringLiteral("bits_per_raw_sample")).toString().toInt();
            if (info.videoBitDepth <= 0) {
                info.videoBitDepth = s.value(QStringLiteral("bits_per_raw_sample")).toInt();
            }
            if (info.videoBitDepth <= 0) {
                info.videoBitDepth = bitsFromPixFmt(info.pixelFormat);
            }
            double fps = parseFraction(s.value(QStringLiteral("avg_frame_rate")).toString());
            if (fps < 1.0) {
                fps = parseFraction(s.value(QStringLiteral("r_frame_rate")).toString());
            }
            info.frameRate = fps;
            if (info.durationSec <= 0.0) {
                info.durationSec = s.value(QStringLiteral("duration")).toString().toDouble();
            }
        } else if (type == QLatin1String("audio") && !info.hasAudio) {
            info.hasAudio = true;
            info.sampleRate = s.value(QStringLiteral("sample_rate")).toString().toInt();
            if (info.sampleRate <= 0) {
                info.sampleRate = s.value(QStringLiteral("sample_rate")).toInt();
            }
            info.channels = s.value(QStringLiteral("channels")).toInt();
            info.audioBitDepth = s.value(QStringLiteral("bits_per_sample")).toInt();
            if (info.audioBitDepth <= 0) {
                const QString enc = s.value(QStringLiteral("bits_per_raw_sample")).toString();
                info.audioBitDepth = enc.toInt();
            }
            if (info.audioBitDepth <= 0) {
                const QString name = s.value(QStringLiteral("codec_name")).toString().toLower();
                if (name.contains(QLatin1String("pcm_s24")) || name.contains(QLatin1String("24"))) {
                    info.audioBitDepth = 24;
                } else if (name.contains(QLatin1String("pcm_f32"))
                           || name.contains(QLatin1String("pcm_s32"))) {
                    info.audioBitDepth = 32;
                } else {
                    info.audioBitDepth = 16;
                }
            }
        }
    }

    info.ok = info.hasVideo || info.hasAudio;
    if (!info.ok) {
        info.error = QObject::tr("No audio/video streams found");
    }
    return info;
}

MediaProbeInfo parseFfmpegStderr(const QString &err, const QString &path)
{
    MediaProbeInfo info;
    info.path = path;
    info.formatName = QFileInfo(path).suffix().toUpper();

    static const QRegularExpression durRe(
        QStringLiteral(R"(Duration:\s*(\d+):(\d+):(\d+[.,]\d+))"));
    const auto durM = durRe.match(err);
    if (durM.hasMatch()) {
        const int h = durM.captured(1).toInt();
        const int m = durM.captured(2).toInt();
        const double s = durM.captured(3).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
        info.durationSec = h * 3600.0 + m * 60.0 + s;
    }

    static const QRegularExpression sizeRe(QStringLiteral(R"((\d{2,5})x(\d{2,5}))"));
    static const QRegularExpression fpsRe(QStringLiteral(R"(([\d.]+)\s*(?:fps|tbr))"),
                                          QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression audRe(
        QStringLiteral(R"(Audio:\s*[^\s,]+[^,]*,\s*(\d+)\s*Hz[^,]*,\s*([^,]+))"),
        QRegularExpression::CaseInsensitiveOption);

    if (err.contains(QLatin1String("Video:"), Qt::CaseInsensitive)) {
        info.hasVideo = true;
        const auto sm = sizeRe.match(err);
        if (sm.hasMatch()) {
            info.width = sm.captured(1).toInt();
            info.height = sm.captured(2).toInt();
        }
        const auto fm = fpsRe.match(err);
        if (fm.hasMatch()) {
            info.frameRate = fm.captured(1).toDouble();
        }
        info.videoBitDepth = 8;
        if (err.contains(QLatin1String("progressive"), Qt::CaseInsensitive)) {
            info.fieldOrder = QStringLiteral("progressive");
        }
    }

    const auto am = audRe.match(err);
    if (am.hasMatch()) {
        info.hasAudio = true;
        info.sampleRate = am.captured(1).toInt();
        const QString layout = am.captured(2).toLower();
        if (layout.contains(QLatin1String("mono"))) {
            info.channels = 1;
        } else if (layout.contains(QLatin1String("stereo"))) {
            info.channels = 2;
        } else if (layout.contains(QLatin1String("5.1"))) {
            info.channels = 6;
        } else {
            info.channels = 2;
        }
        info.audioBitDepth = 16;
    }

    info.streamCount = (info.hasVideo ? 1 : 0) + (info.hasAudio ? 1 : 0);
    info.ok = info.hasVideo || info.hasAudio;
    if (!info.ok) {
        info.error = QObject::tr("Could not parse media info");
    }
    return info;
}

} // namespace

QString MediaProbeInfo::videoSummary() const
{
    if (!hasVideo || width <= 0 || height <= 0) {
        return QString();
    }
    int bits = videoBitDepth > 0 ? videoBitDepth : 8;
    // Vegas often shows 32 for 8-bit packed display depth on desktop.
    if (bits <= 8) {
        bits = 32;
    }
    return QStringLiteral("%1x%2x%3").arg(width).arg(height).arg(bits);
}

QString MediaProbeInfo::audioSummary() const
{
    if (!hasAudio || sampleRate <= 0) {
        return QString();
    }
    const int bits = audioBitDepth > 0 ? audioBitDepth : 16;
    const int ch = channels > 0 ? channels : 2;
    return QObject::tr("%1 Hz, %2 Bits, %3 Channels").arg(sampleRate).arg(bits).arg(ch);
}

QString MediaProbeInfo::timecode() const
{
    if (durationSec <= 0.0) {
        return QString();
    }
    const double t = durationSec;
    const int h = int(t / 3600.0);
    const int m = int(std::fmod(t, 3600.0) / 60.0);
    const double s = std::fmod(t, 60.0);
    const int whole = int(s);
    const int frac = int(std::round((s - whole) * 100.0));
    // Vegas-style comma decimal: 00:10:34,34
    return QStringLiteral("%1:%2:%3,%4")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(whole, 2, 10, QLatin1Char('0'))
        .arg(frac, 2, 10, QLatin1Char('0'));
}

QString MediaProbeInfo::fileTypeLabel() const
{
    if (!formatLongName.isEmpty()) {
        // Vegas shows short labels like "ISO Base"
        if (formatLongName.contains(QLatin1String("MPEG-4"), Qt::CaseInsensitive)
            || formatName.contains(QLatin1String("mp4"))) {
            return QStringLiteral("ISO Base");
        }
        return formatLongName;
    }
    if (!formatName.isEmpty()) {
        const QString first = formatName.split(QLatin1Char(',')).value(0).trimmed();
        if (first.contains(QLatin1String("mp4")) || first.contains(QLatin1String("mov"))) {
            return QStringLiteral("ISO Base");
        }
        return first;
    }
    return QFileInfo(path).suffix().toUpper();
}

QString MediaProbe::findFfprobe()
{
    static QString cached;
    static bool tried = false;
    if (tried) {
        return cached;
    }
    tried = true;

    cached = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (!cached.isEmpty()) {
        return cached;
    }

#ifdef Q_OS_WIN
    const QString exeName = QStringLiteral("ffprobe.exe");
#else
    const QString exeName = QStringLiteral("ffprobe");
#endif

    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        const QStringList besideApp = {
            QDir(appDir).filePath(exeName),
            QDir(appDir).filePath(QStringLiteral("ffmpeg/") + exeName),
            QDir(appDir).filePath(QStringLiteral("ffmpeg/bin/") + exeName),
            QDir(appDir).filePath(QStringLiteral("bin/") + exeName),
            QDir(appDir).filePath(QStringLiteral("tools/") + exeName),
            QDir(appDir).filePath(QStringLiteral("tools/ffmpeg/bin/") + exeName),
        };
        for (const QString &p : besideApp) {
            if (QFileInfo::exists(p)) {
                cached = QFileInfo(p).absoluteFilePath();
                return cached;
            }
        }
    }

    const QString ffmpeg = MediaFilmstripCache::findFfmpeg();
    if (!ffmpeg.isEmpty()) {
        QFileInfo fi(ffmpeg);
        const QString cand = fi.absoluteDir().filePath(exeName);
        if (QFileInfo::exists(cand)) {
            cached = cand;
            return cached;
        }
    }

    const QStringList extras = {
#ifdef Q_OS_WIN
        QStringLiteral("C:/ffmpeg/bin/ffprobe.exe"),
        QStringLiteral("C:/ProgramData/chocolatey/bin/ffprobe.exe"),
#else
        QStringLiteral("/usr/bin/ffprobe"),
        QStringLiteral("/usr/local/bin/ffprobe"),
#endif
    };
    for (const QString &p : extras) {
        if (QFileInfo::exists(p)) {
            cached = p;
            return cached;
        }
    }
    return cached;
}

MediaProbeInfo MediaProbe::probe(const QString &path)
{
    MediaProbeInfo info;
    info.path = path;
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        info.error = QObject::tr("File not found");
        return info;
    }

    const QString ffprobe = findFfprobe();
    if (!ffprobe.isEmpty()) {
        QProcess proc;
        proc.start(ffprobe,
                   {QStringLiteral("-v"), QStringLiteral("quiet"), QStringLiteral("-print_format"),
                    QStringLiteral("json"), QStringLiteral("-show_format"),
                    QStringLiteral("-show_streams"), path});
        if (proc.waitForFinished(15000) && proc.exitStatus() == QProcess::NormalExit
            && proc.exitCode() == 0) {
            info = parseFfprobeJson(proc.readAllStandardOutput(), path);
            if (info.ok) {
                return info;
            }
        }
    }

    const QString ffmpeg = MediaFilmstripCache::findFfmpeg();
    if (!ffmpeg.isEmpty()) {
        QProcess proc;
        proc.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-i"), path});
        proc.waitForFinished(15000);
        // ffmpeg -i writes to stderr and exits non-zero
        const QString err = QString::fromLocal8Bit(proc.readAllStandardError());
        info = parseFfmpegStderr(err, path);
        return info;
    }

    info.error = QObject::tr("ffprobe/ffmpeg not found");
    return info;
}

double MediaProbe::cachedDurationSec(const QString &path)
{
    if (path.isEmpty()) {
        return 0.0;
    }
    static QMutex mutex;
    static QHash<QString, double> cache;
    {
        QMutexLocker lock(&mutex);
        const auto it = cache.constFind(path);
        if (it != cache.cend()) {
            return *it;
        }
    }
    const MediaProbeInfo info = probe(path);
    const double d = (info.ok && info.durationSec > 0.05) ? info.durationSec : 0.0;
    QMutexLocker lock(&mutex);
    cache.insert(path, d);
    return d;
}

double MediaProbe::lengthForInsert(const QString &path, const QString &kind, double hintSec)
{
    const QString k = kind.toLower();
    if (k == QLatin1String("still") || k == QLatin1String("image")) {
        return hintSec > 0.05 ? hintSec : 5.0; // Vegas still default
    }
    // Prefer probed media duration over UI placeholders (e.g. bin card default 8s).
    if (!path.isEmpty()) {
        const double d = cachedDurationSec(path);
        if (d > 0.05) {
            return d;
        }
    }
    if (hintSec > 0.05) {
        return hintSec;
    }
    return 8.0; // fallback when probe unavailable
}

} // namespace openvegas
