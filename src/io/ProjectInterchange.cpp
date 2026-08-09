#include "io/ProjectInterchange.h"
#include "io/VegReader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QHash>
#include <QTextStream>
#include <QUrl>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>
#include <cmath>

namespace openvegas {

namespace {

QString nativePathFromUrlish(QString s)
{
    s = s.trimmed();
    if (s.startsWith(QLatin1String("file://localhost"), Qt::CaseInsensitive)) {
        s = s.mid(16);
    } else if (s.startsWith(QLatin1String("file://"), Qt::CaseInsensitive)) {
        s = s.mid(7);
    }
    s = QUrl::fromPercentEncoding(s.toUtf8());
#if defined(Q_OS_WIN)
    if (s.startsWith(QLatin1Char('/')) && s.size() > 2 && s.at(2) == QLatin1Char(':')) {
        s = s.mid(1);
    }
#endif
    return QDir::fromNativeSeparators(s);
}

void addMediaUnique(InterchangeResult *r, const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    const QString norm = QDir::cleanPath(QDir::fromNativeSeparators(path));
    for (const InterchangeMediaRef &m : r->media) {
        if (QDir::cleanPath(m.path).compare(norm, Qt::CaseInsensitive) == 0) {
            return;
        }
    }
    InterchangeMediaRef ref;
    ref.path = norm;
    ref.displayName = QFileInfo(norm).fileName();
    ref.kind = ProjectInterchange::guessKind(norm);
    r->media.push_back(ref);
}

void scrapePathsFromText(const QString &text, InterchangeResult *r)
{
    // Allow spaces in paths (Premiere/Vegas often export "name - 12.mp4").
    static const QRegularExpression re(
        QStringLiteral(
            R"((?:[A-Za-z]:[\\/][^"'<>|\r\n*?]+\.(?:mp4|mov|mkv|avi|webm|mxf|m4v|wmv|wav|bwf|mp3|aif|aiff|flac|ogg|png|jpg|jpeg|tga|tif|tiff|bmp|gif))"
            R"(|/(?:Users|home|media|mnt|Volumes)[^"'<>|\r\n*?]+\.(?:mp4|mov|mkv|avi|webm|mxf|m4v|wmv|wav|bwf|mp3|aif|aiff|flac|ogg|png|jpg|jpeg|tga|tif|tiff|bmp|gif)))"),
        QRegularExpression::CaseInsensitiveOption);
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        addMediaUnique(r, nativePathFromUrlish(it.next().captured().trimmed()));
    }
}

void scrapePathsFromXml(QXmlStreamReader &xml, InterchangeResult *r)
{
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) {
            continue;
        }
        const QStringView name = xml.name();
        if (name == QLatin1String("pathurl") || name == QLatin1String("url")
            || name == QLatin1String("media-path") || name == QLatin1String("src")
            || name == QLatin1String("file-path")) {
            addMediaUnique(r, nativePathFromUrlish(xml.readElementText()));
        }
        for (const QXmlStreamAttribute &a : xml.attributes()) {
            const QString an = a.name().toString().toLower();
            const QString val = a.value().toString();
            if (an.contains(QLatin1String("path")) || an.contains(QLatin1String("url"))
                || an == QLatin1String("src")) {
                if (val.contains(QLatin1Char('.'))
                    && (val.contains(QLatin1Char('/')) || val.contains(QLatin1Char('\\'))
                        || val.contains(QLatin1String("file:")))) {
                    addMediaUnique(r, nativePathFromUrlish(val));
                }
            }
        }
    }
}

/** Prefer resolved existing path; fall back to stored mediaPath for offline export. */
QString eventExportPath(const ProjectModel &model, const TrackEvent &ev)
{
    const QString resolved = model.mediaPathForEvent(ev);
    if (!resolved.isEmpty()) {
        return resolved;
    }
    return ev.mediaPath;
}

} // namespace

QString ProjectInterchange::guessKind(const QString &pathOrName)
{
    const QString lower = pathOrName.toLower();
    if (lower.endsWith(QLatin1String(".wav")) || lower.endsWith(QLatin1String(".bwf"))
        || lower.endsWith(QLatin1String(".mp3")) || lower.endsWith(QLatin1String(".aif"))
        || lower.endsWith(QLatin1String(".aiff")) || lower.endsWith(QLatin1String(".flac"))
        || lower.endsWith(QLatin1String(".ogg"))) {
        return QStringLiteral("audio");
    }
    if (lower.endsWith(QLatin1String(".png")) || lower.endsWith(QLatin1String(".jpg"))
        || lower.endsWith(QLatin1String(".jpeg")) || lower.endsWith(QLatin1String(".tga"))
        || lower.endsWith(QLatin1String(".tif")) || lower.endsWith(QLatin1String(".tiff"))
        || lower.endsWith(QLatin1String(".bmp")) || lower.endsWith(QLatin1String(".gif"))) {
        return QStringLiteral("still");
    }
    return QStringLiteral("video");
}

QString ProjectInterchange::formatTimecode(double sec, double fps, bool dropFrame)
{
    Q_UNUSED(dropFrame);
    const double useFps = (fps > 1.0) ? fps : 30.0;
    const int totalFrames = std::max(0, static_cast<int>(std::lround(sec * useFps)));
    const int ff = totalFrames % static_cast<int>(std::lround(useFps));
    int ss = (totalFrames / static_cast<int>(std::lround(useFps))) % 60;
    int mm = (totalFrames / static_cast<int>(std::lround(useFps)) / 60) % 60;
    int hh = totalFrames / static_cast<int>(std::lround(useFps)) / 3600;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(hh, 2, 10, QLatin1Char('0'))
        .arg(mm, 2, 10, QLatin1Char('0'))
        .arg(ss, 2, 10, QLatin1Char('0'))
        .arg(ff, 2, 10, QLatin1Char('0'));
}

bool ProjectInterchange::parseTimecode(const QString &tc, double fps, double *outSec)
{
    const QStringList parts = tc.trimmed().split(QLatin1Char(':'));
    if (parts.size() != 4 || !outSec) {
        return false;
    }
    bool ok = true;
    const int hh = parts[0].toInt(&ok);
    if (!ok) {
        return false;
    }
    const int mm = parts[1].toInt(&ok);
    if (!ok) {
        return false;
    }
    const int ss = parts[2].toInt(&ok);
    if (!ok) {
        return false;
    }
    const int ff = parts[3].toInt(&ok);
    if (!ok) {
        return false;
    }
    const double useFps = (fps > 1.0) ? fps : 30.0;
    *outSec = hh * 3600.0 + mm * 60.0 + ss + (ff / useFps);
    return true;
}

InterchangeResult ProjectInterchange::importMediaFromProject(const QString &vegPath, QString *error)
{
    InterchangeResult r;
    const VegOpenResult veg = VegReader::open(vegPath, error);
    if (error && !error->isEmpty()) {
        return r;
    }
    r.title = QFileInfo(vegPath).completeBaseName();
    r.warnings = veg.warnings;
    for (const QString &p : veg.mediaPaths) {
        addMediaUnique(&r, p);
    }
    if (r.media.isEmpty()) {
        r.warnings << QStringLiteral("No media paths found in project.");
    }
    return r;
}

InterchangeResult ProjectInterchange::importEdl(const QString &path, double frameRate, QString *error)
{
    // Vegas Pro “EDL Text File” is a semicolon CSV — route before CMX3600 parse.
    {
        QFile peek(path);
        if (peek.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&peek);
            QString header;
            while (!in.atEnd() && header.trimmed().isEmpty()) {
                header = in.readLine();
            }
            if (header.contains(QLatin1Char(';'))
                && header.contains(QLatin1String("StartTime"), Qt::CaseInsensitive)) {
                return importVegasCsvEdl(path, error);
            }
        }
    }

    InterchangeResult r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Cannot open EDL: %1").arg(f.errorString());
        }
        return r;
    }
    QTextStream in(&f);
    QString pendingName;
    const double fps = frameRate > 1.0 ? frameRate : 30.0;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1String("TITLE:"), Qt::CaseInsensitive)) {
            r.title = line.mid(6).trimmed();
            continue;
        }
        if (line.startsWith(QLatin1Char('*'))) {
            const QString lower = line.toLower();
            const int idx = lower.indexOf(QLatin1String("from clip name:"));
            if (idx >= 0) {
                pendingName = line.mid(idx + 15).trimmed();
            }
            continue;
        }
        // 001  AX       V     C        00:00:00:00 00:00:08:00 00:00:00:00 00:00:08:00
        const QStringList tok = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                           Qt::SkipEmptyParts);
        if (tok.size() < 8) {
            continue;
        }
        bool isEvent = false;
        tok[0].toInt(&isEvent);
        if (!isEvent) {
            continue;
        }
        const QString track = tok[2].toUpper();
        double recIn = 0.0;
        double recOut = 0.0;
        if (!parseTimecode(tok[6], fps, &recIn) || !parseTimecode(tok[7], fps, &recOut)) {
            continue;
        }
        InterchangeEvent ev;
        ev.name = pendingName.isEmpty() ? QStringLiteral("EDL %1").arg(tok[0]) : pendingName;
        ev.kind = track.startsWith(QLatin1Char('A')) ? QStringLiteral("audio")
                                                     : QStringLiteral("video");
        ev.startSec = recIn;
        ev.lengthSec = std::max(0.05, recOut - recIn);
        r.events.push_back(ev);
        pendingName.clear();
    }
    if (r.events.isEmpty()) {
        r.warnings << QStringLiteral("No EDL edit events parsed.");
    }
    return r;
}

bool ProjectInterchange::exportEdl(const ProjectModel &model, const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Cannot write EDL: %1").arg(f.errorString());
        }
        return false;
    }
    QTextStream out(&f);
    out << "TITLE: " << model.projectTitle() << "\n";
    out << "FCM: NON-DROP FRAME\n\n";
    const double fps = model.frameRate() > 1.0 ? model.frameRate() : 30.0;
    int eventNo = 1;
    for (const Track &t : model.tracks()) {
        const QString trackCode = (t.kind == TrackKind::Audio) ? QStringLiteral("A") : QStringLiteral("V");
        for (const TrackEvent &ev : t.events) {
            const QString recIn = formatTimecode(ev.startSec, fps);
            const QString recOut = formatTimecode(ev.startSec + ev.lengthSec, fps);
            const QString srcIn = formatTimecode(0.0, fps);
            const QString srcOut = formatTimecode(ev.lengthSec, fps);
            out << QStringLiteral("%1  AX       %2     C        %3 %4 %5 %6\n")
                       .arg(eventNo, 3, 10, QLatin1Char('0'))
                       .arg(trackCode, srcIn, srcOut, recIn, recOut);
            out << "* FROM CLIP NAME: " << ev.name << "\n\n";
            ++eventNo;
        }
    }
    return true;
}

InterchangeResult ProjectInterchange::importFinalCutXml(const QString &path, QString *error)
{
    InterchangeResult r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("Cannot open XML: %1").arg(f.errorString());
        }
        return r;
    }
    const QByteArray data = f.readAll();
    f.close();

    QXmlStreamReader xml(data);
    scrapePathsFromXml(xml, &r);
    if (xml.hasError() && r.media.isEmpty()) {
        scrapePathsFromText(QString::fromUtf8(data), &r);
    } else if (r.media.isEmpty()) {
        scrapePathsFromText(QString::fromUtf8(data), &r);
    }

    auto parseTimeAttr = [](QString s) -> double {
        s = s.trimmed();
        if (s.endsWith(QLatin1Char('s'))) {
            s.chop(1);
        }
        if (s.contains(QLatin1Char('/'))) {
            const QStringList fr = s.split(QLatin1Char('/'));
            if (fr.size() == 2) {
                const double a = fr[0].toDouble();
                const double b = fr[1].toDouble();
                if (b != 0.0) {
                    return a / b;
                }
            }
        }
        bool ok = false;
        const double v = s.toDouble(&ok);
        return ok ? v : -1.0;
    };

    // Map FCPX asset id → path/name; FCP7 file id → pathurl
    QHash<QString, QString> assetIdToPath;
    QHash<QString, QString> assetIdToName;
    {
        QXmlStreamReader x(data);
        while (!x.atEnd()) {
            x.readNext();
            if (!x.isStartElement()) {
                continue;
            }
            if (x.name() == QLatin1String("asset")) {
                const QString id = x.attributes().value(QLatin1String("id")).toString();
                const QString name = x.attributes().value(QLatin1String("name")).toString();
                QString src = x.attributes().value(QLatin1String("src")).toString();
                if (src.startsWith(QLatin1String("file://localhost/"))) {
                    src = src.mid(QStringLiteral("file://localhost/").size());
                } else if (src.startsWith(QLatin1String("file:///"))) {
                    src = src.mid(QStringLiteral("file:///").size());
                }
                src = QDir::fromNativeSeparators(src);
                if (!id.isEmpty()) {
                    assetIdToPath.insert(id, src);
                    assetIdToName.insert(id, name);
                }
            } else if (x.name() == QLatin1String("file")) {
                const QString id = x.attributes().value(QLatin1String("id")).toString();
                // Self-closing <file id="…"/> may have pathurl as sibling under clipitem;
                // also capture nested pathurl when present.
                int depth = 1;
                QString pathurl;
                QString fname;
                while (depth > 0 && !x.atEnd()) {
                    x.readNext();
                    if (x.isStartElement()) {
                        ++depth;
                        if (x.name() == QLatin1String("pathurl")) {
                            pathurl = nativePathFromUrlish(x.readElementText());
                            --depth;
                        } else if (x.name() == QLatin1String("name") && fname.isEmpty()) {
                            fname = x.readElementText();
                            --depth;
                        }
                    } else if (x.isEndElement()) {
                        --depth;
                    }
                }
                if (!id.isEmpty() && !pathurl.isEmpty()) {
                    assetIdToPath.insert(id, pathurl);
                    if (!fname.isEmpty()) {
                        assetIdToName.insert(id, fname);
                    }
                }
            }
        }
    }

    // Also map pathurl media already scraped by basename for FCP7 empty <file id/> refs
    for (const InterchangeMediaRef &m : r.media) {
        const QString base = QFileInfo(m.path).completeBaseName();
        if (!base.isEmpty() && !assetIdToPath.contains(base)) {
            assetIdToPath.insert(base, m.path);
            assetIdToName.insert(base, QFileInfo(m.path).fileName());
        }
    }

    // Sequence timebase for FCP7 frame→sec (default 60 for stills sample / Vegas 4K)
    double seqFps = 60.0;
    {
        QXmlStreamReader x(data);
        bool inSequence = false;
        bool inRate = false;
        while (!x.atEnd()) {
            x.readNext();
            if (x.isStartElement()) {
                if (x.name() == QLatin1String("sequence")) {
                    inSequence = true;
                } else if (inSequence && x.name() == QLatin1String("rate")) {
                    inRate = true;
                } else if (inRate && x.name() == QLatin1String("timebase")) {
                    const double tb = x.readElementText().toDouble();
                    if (tb > 1.0) {
                        seqFps = tb;
                    }
                    inRate = false;
                }
            } else if (x.isEndElement() && x.name() == QLatin1String("sequence")) {
                break;
            }
        }
    }

    auto fillFcpxClip = [&](QXmlStreamReader &xml, const QString &elemName) {
        // Skip channel stems (lane="-1" nested under parent audio)
        if (!xml.attributes().value(QLatin1String("lane")).isEmpty()) {
            return;
        }
        InterchangeEvent ev;
        const QString ref = xml.attributes().value(QLatin1String("ref")).toString();
        ev.name = assetIdToName.value(ref);
        ev.sourcePath = assetIdToPath.value(ref);
        if (ev.name.isEmpty()) {
            ev.name = xml.attributes().value(QLatin1String("name")).toString();
        }
        if (ev.name.isEmpty()) {
            ev.name = QFileInfo(ev.sourcePath).completeBaseName();
        }
        if (ev.name.isEmpty()) {
            ev.name = QStringLiteral("Clip");
        }
        if (elemName == QLatin1String("audio")) {
            ev.kind = QStringLiteral("audio");
        } else {
            ev.kind = guessKind(ev.sourcePath.isEmpty() ? ev.name : ev.sourcePath);
        }

        const double d = parseTimeAttr(xml.attributes().value(QLatin1String("duration")).toString());
        const QString startAttr = xml.attributes().value(QLatin1String("start")).toString();
        const QString offsetAttr = xml.attributes().value(QLatin1String("offset")).toString();
        // Timeline = offset when present; otherwise spine origin (0). Never use start as
        // timeline — FCPX start is the media in-point (critical for reverse SubClips).
        if (!offsetAttr.isEmpty()) {
            const double off = parseTimeAttr(offsetAttr);
            if (off >= 0.0) {
                ev.startSec = off;
            }
        } else {
            ev.startSec = 0.0;
        }
        if (!startAttr.isEmpty()) {
            const double mediaIn = parseTimeAttr(startAttr);
            if (mediaIn >= 0.0) {
                ev.mediaStartSec = mediaIn;
            }
        }
        if (d > 0.0) {
            ev.lengthSec = d;
        }

        // Nested fades / timeMap until this element ends
        int depth = 1;
        double timeMapT0 = -1.0, timeMapT1 = -1.0;
        double timeMapV0 = 0.0, timeMapV1 = 0.0;
        int timePts = 0;
        while (depth > 0 && !xml.atEnd()) {
            xml.readNext();
            // Empty elements (<fadeIn … />) are both Start and End in one token — no depth change.
            const bool emptyElem = xml.isStartElement() && xml.isEndElement();
            if (xml.isStartElement()) {
                if (!emptyElem) {
                    ++depth;
                }
                const QString child = xml.name().toString().toLower();
                if (child == QLatin1String("fadein")) {
                    const double fd =
                        parseTimeAttr(xml.attributes().value(QLatin1String("duration")).toString());
                    if (fd > 0.0) {
                        ev.fadeInSec = fd;
                    }
                } else if (child == QLatin1String("fadeout")) {
                    const double fd =
                        parseTimeAttr(xml.attributes().value(QLatin1String("duration")).toString());
                    if (fd > 0.0) {
                        ev.fadeOutSec = fd;
                    }
                } else if (child == QLatin1String("timept")) {
                    const double t =
                        parseTimeAttr(xml.attributes().value(QLatin1String("time")).toString());
                    const double v =
                        parseTimeAttr(xml.attributes().value(QLatin1String("value")).toString());
                    if (t >= 0.0) {
                        if (timePts == 0) {
                            timeMapT0 = t;
                            timeMapV0 = v;
                        }
                        timeMapT1 = t;
                        timeMapV1 = v;
                        ++timePts;
                    }
                }
            } else if (xml.isEndElement()) {
                --depth;
            }
        }
        if (timePts >= 2 && timeMapT1 > timeMapT0) {
            // Decreasing value ⇒ reverse playback
            if (timeMapV1 < timeMapV0 - 1e-3) {
                ev.playRate = -1.0;
                ev.mediaLengthSec = std::abs(timeMapV0 - timeMapV1);
                // Reverse SubClip: media in-point on the reversed item is often 0
                if (ev.mediaStartSec > ev.mediaLengthSec + 1.0) {
                    // start attr was absolute source time; keep length from timeMap
                    ev.mediaStartSec = 0.0;
                }
            }
        }

        if (ev.lengthSec > 0.05 || (!offsetAttr.isEmpty() && ev.startSec >= 0.0)) {
            r.events.push_back(ev);
        }
    };

    QXmlStreamReader xml2(data);
    int sequenceDepth = 0;
    double pendingFadeInSec = 0.0;
    while (!xml2.atEnd()) {
        xml2.readNext();
        if (xml2.isStartElement()) {
            const QString name = xml2.name().toString().toLower();
            if (name == QLatin1String("sequence")) {
                ++sequenceDepth;
            }

            // FCPX spine clips (<video ref="r4" .../>, <asset-clip ref="..." .../>,
            // <audio ref="..." .../>) always carry a ref to the asset they place.
            // FCP7 XMEML's <video>/<audio> are unrelated same-named elements — plain
            // per-kind track containers straight under <media>, never carrying ref —
            // so gate on ref here or every FCP7 export gets its whole video/audio
            // section swallowed as one phantom 8-second "Clip" instead of parsing
            // the real <track>/<clipitem> children underneath.
            if ((name == QLatin1String("video") || name == QLatin1String("asset-clip")
                || name == QLatin1String("audio"))
                && !xml2.attributes().value(QLatin1String("ref")).isEmpty()) {
                fillFcpxClip(xml2, name);
                continue;
            }

            // FCPX wrapper <clip duration="0s"> — skip creating a phantom event
            if (name == QLatin1String("clip")) {
                const double d =
                    parseTimeAttr(xml2.attributes().value(QLatin1String("duration")).toString());
                if (d <= 1e-6) {
                    continue; // children (nested spine) still visited normally
                }
                // Non-zero FCPX compound clip: treat like asset-clip if it has ref
                if (!xml2.attributes().value(QLatin1String("ref")).isEmpty()) {
                    fillFcpxClip(xml2, name);
                }
                continue;
            }

            // FCP7: only sequence clipitems (skip master <clip ismasterclip>)
            if (name == QLatin1String("clipitem") && sequenceDepth > 0) {
                InterchangeEvent ev;
                ev.name = xml2.attributes().value(QLatin1String("name")).toString();
                if (ev.name.isEmpty()) {
                    ev.name = QStringLiteral("Clip");
                }
                ev.kind = QStringLiteral("video");

                int startFrame = -1;
                int endFrame = -1;
                int durFrame = -1;
                int inFrame = -1;
                int outFrame = -1;
                double itemFps = seqFps;
                QString fileId;
                QString mediaType;

                int depth = 1;
                while (depth > 0 && !xml2.atEnd()) {
                    xml2.readNext();
                    if (xml2.isStartElement()) {
                        ++depth;
                        const QString child = xml2.name().toString().toLower();
                        if (child == QLatin1String("name") && ev.name == QLatin1String("Clip")) {
                            const QString n = xml2.readElementText();
                            --depth;
                            if (!n.isEmpty()) {
                                ev.name = n;
                            }
                        } else if (child == QLatin1String("start")) {
                            startFrame = xml2.readElementText().toInt();
                            --depth;
                        } else if (child == QLatin1String("end")) {
                            endFrame = xml2.readElementText().toInt();
                            --depth;
                        } else if (child == QLatin1String("duration")) {
                            durFrame = xml2.readElementText().toInt();
                            --depth;
                        } else if (child == QLatin1String("in")) {
                            inFrame = xml2.readElementText().toInt();
                            --depth;
                        } else if (child == QLatin1String("out")) {
                            outFrame = xml2.readElementText().toInt();
                            --depth;
                        } else if (child == QLatin1String("timebase")) {
                            const double tb = xml2.readElementText().toDouble();
                            --depth;
                            if (tb > 1.0) {
                                itemFps = tb;
                            }
                        } else if (child == QLatin1String("file")) {
                            fileId = xml2.attributes().value(QLatin1String("id")).toString();
                        } else if (child == QLatin1String("mediatype")) {
                            mediaType = xml2.readElementText().trimmed().toLower();
                            --depth;
                        } else if (child == QLatin1String("pathurl")) {
                            ev.sourcePath = nativePathFromUrlish(xml2.readElementText());
                            --depth;
                        }
                    } else if (xml2.isEndElement()) {
                        --depth;
                    }
                }

                if (mediaType == QLatin1String("audio")) {
                    ev.kind = QStringLiteral("audio");
                } else if (guessKind(ev.name) == QLatin1String("still")) {
                    ev.kind = QStringLiteral("still");
                } else if (!ev.sourcePath.isEmpty()) {
                    ev.kind = guessKind(ev.sourcePath);
                }

                if (ev.sourcePath.isEmpty() && !fileId.isEmpty()) {
                    ev.sourcePath = assetIdToPath.value(fileId);
                    if (ev.sourcePath.isEmpty()) {
                        // id often equals basename without extension quirks
                        for (auto it = assetIdToPath.begin(); it != assetIdToPath.end(); ++it) {
                            if (it.key().startsWith(fileId, Qt::CaseInsensitive)
                                || fileId.startsWith(it.key(), Qt::CaseInsensitive)) {
                                ev.sourcePath = it.value();
                                break;
                            }
                        }
                    }
                }
                if (ev.sourcePath.isEmpty()) {
                    // Match scraped media by event name / filename
                    for (const InterchangeMediaRef &m : r.media) {
                        if (QFileInfo(m.path).fileName().compare(ev.name, Qt::CaseInsensitive) == 0
                            || m.displayName.compare(ev.name, Qt::CaseInsensitive) == 0) {
                            ev.sourcePath = m.path;
                            break;
                        }
                    }
                }

                const double fpsUse = itemFps > 1.0 ? itemFps : seqFps;
                if (startFrame >= 0 && endFrame > startFrame && fpsUse > 1.0) {
                    ev.startSec = double(startFrame) / fpsUse;
                    ev.lengthSec = double(endFrame - startFrame) / fpsUse;
                } else if ((startFrame < 0 || endFrame < 0) && durFrame > 0 && fpsUse > 1.0) {
                    // Linked to surrounding transitions (start=end=-1)
                    ev.startSec = 0.0;
                    ev.lengthSec = double(durFrame) / fpsUse;
                } else if (durFrame > 0 && fpsUse > 1.0 && ev.lengthSec < 0.05) {
                    ev.lengthSec = double(durFrame) / fpsUse;
                }
                if (inFrame >= 0 && fpsUse > 1.0) {
                    ev.mediaStartSec = double(inFrame) / fpsUse;
                }
                if (inFrame >= 0 && outFrame > inFrame && fpsUse > 1.0) {
                    ev.mediaLengthSec = double(outFrame - inFrame) / fpsUse;
                }

                if (pendingFadeInSec > 1e-6) {
                    ev.fadeInSec = pendingFadeInSec;
                    pendingFadeInSec = 0.0;
                }

                if (ev.lengthSec < 0.05 && startFrame == 0 && endFrame == 0) {
                    continue;
                }
                if (ev.lengthSec > 0.05 || ev.startSec > 0.0) {
                    r.events.push_back(ev);
                }
                continue;
            }

            // FCP7 transitions → pending fades for neighboring clipitem
            if (name == QLatin1String("transitionitem") && sequenceDepth > 0) {
                int tStart = -1;
                int tEnd = -1;
                QString alignment;
                double tFps = seqFps;
                int depth = 1;
                while (depth > 0 && !xml2.atEnd()) {
                    xml2.readNext();
                    if (xml2.isStartElement()) {
                        ++depth;
                        const QString child = xml2.name().toString().toLower();
                        if (child == QLatin1String("start")) {
                            tStart = xml2.readElementText().toInt();
                            --depth;
                        } else if (child == QLatin1String("end")) {
                            tEnd = xml2.readElementText().toInt();
                            --depth;
                        } else if (child == QLatin1String("alignment")) {
                            alignment = xml2.readElementText().trimmed().toLower();
                            --depth;
                        } else if (child == QLatin1String("timebase")) {
                            const double tb = xml2.readElementText().toDouble();
                            --depth;
                            if (tb > 1.0) {
                                tFps = tb;
                            }
                        }
                    } else if (xml2.isEndElement()) {
                        --depth;
                    }
                }
                if (tStart >= 0 && tEnd > tStart && tFps > 1.0) {
                    const double fadeSec = double(tEnd - tStart) / tFps;
                    if (alignment.contains(QLatin1String("start")) || tStart == 0) {
                        pendingFadeInSec = fadeSec;
                    } else if (!r.events.isEmpty()) {
                        // Fade-out / mid dissolve after the preceding clipitem
                        r.events.last().fadeOutSec =
                            std::max(r.events.last().fadeOutSec, fadeSec);
                    } else {
                        pendingFadeInSec = fadeSec;
                    }
                }
                continue;
            }
        } else if (xml2.isEndElement()) {
            if (xml2.name().toString().toLower() == QLatin1String("sequence")
                && sequenceDepth > 0) {
                --sequenceDepth;
            }
        }
    }

    // Prefer timeline clips over master-bin empties: if we have events with length, drop zero-length
    QVector<InterchangeEvent> kept;
    for (const InterchangeEvent &ev : r.events) {
        if (ev.lengthSec >= 0.05) {
            kept.push_back(ev);
        }
    }
    if (!kept.isEmpty()) {
        r.events = kept;
    }

    for (InterchangeEvent &ev : r.events) {
        // Do not override explicit <audio> clips just because the asset is an A/V container.
        if (ev.kind == QLatin1String("audio")) {
            continue;
        }
        if (!ev.sourcePath.isEmpty()) {
            ev.kind = guessKind(ev.sourcePath);
        } else if (guessKind(ev.name) == QLatin1String("still")) {
            ev.kind = QStringLiteral("still");
        }
    }

    r.title = QFileInfo(path).completeBaseName();
    if (r.media.isEmpty() && r.events.isEmpty()) {
        r.warnings << QStringLiteral("No media or clips found in XML.");
    } else if (!r.events.isEmpty()) {
        r.warnings << QStringLiteral("Parsed %1 timeline clip(s) from XML/FCPXML.").arg(r.events.size());
    }
    return r;
}

bool ProjectInterchange::exportFinalCutXml(const ProjectModel &model, const QString &path,
                                           QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Cannot write XML: %1").arg(f.errorString());
        }
        return false;
    }
    const double fps = model.frameRate() > 1.0 ? model.frameRate() : 30.0;
    auto toFrames = [fps](double sec) {
        return static_cast<int>(std::lround(std::max(0.0, sec) * fps));
    };
    auto pathUrl = [](const QString &native) -> QString {
        if (native.isEmpty()) {
            return {};
        }
        QString p = QDir::fromNativeSeparators(native);
#if defined(Q_OS_WIN)
        if (p.size() >= 2 && p.at(1) == QLatin1Char(':')) {
            return QStringLiteral("file://localhost/") + p;
        }
#endif
        return QStringLiteral("file://localhost") + (p.startsWith(QLatin1Char('/')) ? p : (QLatin1Char('/') + p));
    };

    QXmlStreamWriter w(&f);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("xmeml"));
    w.writeAttribute(QStringLiteral("version"), QStringLiteral("5"));
    w.writeStartElement(QStringLiteral("sequence"));
    w.writeTextElement(QStringLiteral("name"),
                        model.projectTitle().isEmpty() ? QStringLiteral("OpenVegas")
                                                       : model.projectTitle());
    w.writeStartElement(QStringLiteral("rate"));
    w.writeTextElement(QStringLiteral("timebase"), QString::number(static_cast<int>(std::lround(fps))));
    w.writeTextElement(QStringLiteral("ntsc"),
                       (std::abs(fps - 29.97) < 0.05 || std::abs(fps - 59.94) < 0.05)
                           ? QStringLiteral("TRUE")
                           : QStringLiteral("FALSE"));
    w.writeEndElement(); // rate

    w.writeStartElement(QStringLiteral("media"));
    int id = 1;

    auto writeClipItem = [&](const TrackEvent &ev, const QString &mediaType) {
        const QString mediaPath = eventExportPath(model, ev);
        const int dur = std::max(1, toFrames(ev.lengthSec));
        const int start = toFrames(ev.startSec);
        const int inF = toFrames(ev.mediaStartSec);
        const int outF = inF + dur;
        w.writeStartElement(QStringLiteral("clipitem"));
        w.writeAttribute(QStringLiteral("id"), QStringLiteral("clipitem-%1").arg(id++));
        w.writeTextElement(QStringLiteral("name"), ev.name);
        w.writeTextElement(QStringLiteral("duration"), QString::number(dur));
        w.writeStartElement(QStringLiteral("rate"));
        w.writeTextElement(QStringLiteral("timebase"), QString::number(static_cast<int>(std::lround(fps))));
        w.writeTextElement(QStringLiteral("ntsc"), QStringLiteral("FALSE"));
        w.writeEndElement();
        w.writeTextElement(QStringLiteral("start"), QString::number(start));
        w.writeTextElement(QStringLiteral("end"), QString::number(start + dur));
        w.writeTextElement(QStringLiteral("in"), QString::number(inF));
        w.writeTextElement(QStringLiteral("out"), QString::number(outF));
        if (!mediaPath.isEmpty()) {
            w.writeStartElement(QStringLiteral("file"));
            w.writeAttribute(QStringLiteral("id"), QStringLiteral("file-%1").arg(id));
            w.writeTextElement(QStringLiteral("name"), QFileInfo(mediaPath).fileName());
            w.writeTextElement(QStringLiteral("pathurl"), pathUrl(mediaPath));
            w.writeEndElement();
        }
        w.writeStartElement(QStringLiteral("sourcetrack"));
        w.writeTextElement(QStringLiteral("mediatype"), mediaType);
        w.writeTextElement(QStringLiteral("trackindex"), QStringLiteral("1"));
        w.writeEndElement();
        w.writeEndElement(); // clipitem
    };

    auto writeTransition = [&](int startFrame, int endFrame, const QString &alignment,
                               const QString &mediaType) {
        const QString effectName = mediaType == QStringLiteral("audio")
            ? QStringLiteral("Cross Fade (0dB)")
            : QStringLiteral("Cross Dissolve");
        w.writeStartElement(QStringLiteral("transitionitem"));
        w.writeTextElement(QStringLiteral("start"), QString::number(startFrame));
        w.writeTextElement(QStringLiteral("end"), QString::number(endFrame));
        w.writeTextElement(QStringLiteral("alignment"), alignment);
        w.writeStartElement(QStringLiteral("effect"));
        w.writeTextElement(QStringLiteral("name"), effectName);
        w.writeTextElement(QStringLiteral("effectid"), effectName);
        w.writeTextElement(QStringLiteral("effecttype"), QStringLiteral("transition"));
        w.writeTextElement(QStringLiteral("mediatype"), mediaType);
        w.writeEndElement();
        w.writeEndElement();
    };

    auto writeClipWithFades = [&](const TrackEvent &ev, const QString &mediaType) {
        if (ev.fadeInSec > 1e-3) {
            writeTransition(toFrames(ev.startSec), toFrames(ev.startSec + ev.fadeInSec),
                            QStringLiteral("start"), mediaType);
        }
        writeClipItem(ev, mediaType);
        if (ev.fadeOutSec > 1e-3) {
            const double fadeStart = ev.startSec + ev.lengthSec - ev.fadeOutSec;
            writeTransition(toFrames(fadeStart), toFrames(ev.startSec + ev.lengthSec),
                            QStringLiteral("end"), mediaType);
        }
    };

    w.writeStartElement(QStringLiteral("video"));
    for (const Track &t : model.tracks()) {
        if (t.kind != TrackKind::Video) {
            continue;
        }
        w.writeStartElement(QStringLiteral("track"));
        for (const TrackEvent &ev : t.events) {
            writeClipWithFades(ev, QStringLiteral("video"));
        }
        w.writeEndElement(); // track
    }
    w.writeEndElement(); // video

    w.writeStartElement(QStringLiteral("audio"));
    for (const Track &t : model.tracks()) {
        if (t.kind != TrackKind::Audio) {
            continue;
        }
        w.writeStartElement(QStringLiteral("track"));
        for (const TrackEvent &ev : t.events) {
            writeClipWithFades(ev, QStringLiteral("audio"));
        }
        w.writeEndElement();
    }
    w.writeEndElement(); // audio
    w.writeEndElement(); // media
    w.writeEndElement(); // sequence
    w.writeEndElement(); // xmeml
    w.writeEndDocument();
    return true;
}

bool ProjectInterchange::exportFcpxml(const ProjectModel &model, const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Cannot write FCPXML: %1").arg(f.errorString());
        }
        return false;
    }
    const double fps = model.frameRate() > 1.0 ? model.frameRate() : 30.0;
    const qint64 frameRateNum = static_cast<qint64>(std::lround(fps * 1000.0));
    auto toRational = [frameRateNum](double sec) {
        const qint64 frames = static_cast<qint64>(std::lround(std::max(0.0, sec) * (frameRateNum / 1000.0)));
        return QStringLiteral("%1/%2s").arg(frames * 1000).arg(frameRateNum);
    };
    auto pathUrl = [](const QString &native) -> QString {
        if (native.isEmpty()) {
            return {};
        }
        QString p = QDir::fromNativeSeparators(native);
#if defined(Q_OS_WIN)
        if (p.size() >= 2 && p.at(1) == QLatin1Char(':')) {
            return QStringLiteral("file://localhost/") + p;
        }
#endif
        return QStringLiteral("file://localhost") + (p.startsWith(QLatin1Char('/')) ? p : (QLatin1Char('/') + p));
    };

    struct AssetRef {
        QString id;
        QString path;
        QString name;
        bool hasVideo = false;
        bool hasAudio = false;
    };
    QVector<AssetRef> assets;
    QHash<QString, int> assetIndexByKey;
    auto ensureAsset = [&](const TrackEvent &ev) -> QString {
        const QString mediaPath = eventExportPath(model, ev);
        const QString key = mediaPath.isEmpty() ? (QStringLiteral("name:") + ev.name)
                                               : QDir::cleanPath(mediaPath);
        const auto it = assetIndexByKey.constFind(key);
        if (it != assetIndexByKey.cend()) {
            AssetRef &a = assets[*it];
            if (isVideoFamily(ev.mediaKind) || ev.mediaKind == EventMediaKind::Still) {
                a.hasVideo = true;
            }
            if (ev.mediaKind == EventMediaKind::Audio) {
                a.hasAudio = true;
            }
            return a.id;
        }
        AssetRef a;
        a.id = QStringLiteral("r%1").arg(assets.size() + 2); // r1 = format
        a.path = mediaPath;
        a.name = ev.name.isEmpty() ? QFileInfo(mediaPath).completeBaseName() : ev.name;
        a.hasVideo = isVideoFamily(ev.mediaKind) || ev.mediaKind == EventMediaKind::Still;
        a.hasAudio = ev.mediaKind == EventMediaKind::Audio;
        assetIndexByKey.insert(key, assets.size());
        assets.push_back(a);
        return a.id;
    };

    for (const Track &t : model.tracks()) {
        for (const TrackEvent &ev : t.events) {
            ensureAsset(ev);
        }
    }

    QXmlStreamWriter w(&f);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("fcpxml"));
    w.writeAttribute(QStringLiteral("version"), QStringLiteral("1.9"));
    w.writeStartElement(QStringLiteral("resources"));
    w.writeStartElement(QStringLiteral("format"));
    w.writeAttribute(QStringLiteral("id"), QStringLiteral("r1"));
    w.writeAttribute(QStringLiteral("frameDuration"), QStringLiteral("1000/%1s").arg(frameRateNum));
    w.writeAttribute(QStringLiteral("width"), QString::number(model.frameWidth()));
    w.writeAttribute(QStringLiteral("height"), QString::number(model.frameHeight()));
    w.writeEndElement();
    for (const AssetRef &a : assets) {
        w.writeStartElement(QStringLiteral("asset"));
        w.writeAttribute(QStringLiteral("id"), a.id);
        w.writeAttribute(QStringLiteral("name"), a.name);
        if (!a.path.isEmpty()) {
            w.writeAttribute(QStringLiteral("src"), pathUrl(a.path));
        }
        w.writeAttribute(QStringLiteral("start"), QStringLiteral("0/1s"));
        if (a.hasVideo) {
            w.writeAttribute(QStringLiteral("hasVideo"), QStringLiteral("1"));
        }
        if (a.hasAudio) {
            w.writeAttribute(QStringLiteral("hasAudio"), QStringLiteral("1"));
        }
        w.writeEndElement();
    }
    w.writeEndElement(); // resources

    const QString title =
        model.projectTitle().isEmpty() ? QStringLiteral("OpenVegas") : model.projectTitle();
    w.writeStartElement(QStringLiteral("library"));
    w.writeStartElement(QStringLiteral("event"));
    w.writeAttribute(QStringLiteral("name"), title);
    w.writeStartElement(QStringLiteral("project"));
    w.writeAttribute(QStringLiteral("name"), title);
    w.writeStartElement(QStringLiteral("sequence"));
    w.writeAttribute(QStringLiteral("format"), QStringLiteral("r1"));
    w.writeAttribute(QStringLiteral("tcFormat"), QStringLiteral("NDF"));
    w.writeStartElement(QStringLiteral("spine"));

    auto writeClip = [&](const TrackEvent &ev, bool asAudio) {
        const QString ref = ensureAsset(ev);
        if (ref.isEmpty()) {
            return;
        }
        w.writeStartElement(asAudio ? QStringLiteral("audio") : QStringLiteral("video"));
        w.writeAttribute(QStringLiteral("ref"), ref);
        w.writeAttribute(QStringLiteral("name"), ev.name);
        w.writeAttribute(QStringLiteral("offset"), toRational(ev.startSec));
        w.writeAttribute(QStringLiteral("duration"), toRational(ev.lengthSec));
        w.writeAttribute(QStringLiteral("start"), toRational(ev.mediaStartSec));
        if (ev.fadeInSec > 1e-3 || ev.fadeOutSec > 1e-3) {
            w.writeStartElement(asAudio ? QStringLiteral("adjust-volume")
                                        : QStringLiteral("adjust-blend"));
            w.writeStartElement(QStringLiteral("param"));
            w.writeAttribute(QStringLiteral("name"), QStringLiteral("amount"));
            if (ev.fadeInSec > 1e-3) {
                w.writeStartElement(QStringLiteral("fadeIn"));
                w.writeAttribute(QStringLiteral("type"), QStringLiteral("linear"));
                w.writeAttribute(QStringLiteral("duration"), toRational(ev.fadeInSec));
                w.writeEndElement();
            }
            if (ev.fadeOutSec > 1e-3) {
                w.writeStartElement(QStringLiteral("fadeOut"));
                w.writeAttribute(QStringLiteral("type"), QStringLiteral("linear"));
                w.writeAttribute(QStringLiteral("duration"), toRational(ev.fadeOutSec));
                w.writeEndElement();
            }
            w.writeEndElement(); // param
            w.writeEndElement();
        }
        if (ev.reversed) {
            w.writeStartElement(QStringLiteral("timeMap"));
            const double t0 = ev.mediaStartSec;
            const double t1 = ev.mediaStartSec + ev.lengthSec;
            const double cycle = ev.mediaLengthSec > 1e-6 ? ev.mediaLengthSec : ev.lengthSec;
            w.writeStartElement(QStringLiteral("timept"));
            w.writeAttribute(QStringLiteral("time"), toRational(t0));
            w.writeAttribute(QStringLiteral("value"), toRational(cycle));
            w.writeAttribute(QStringLiteral("interp"), QStringLiteral("linear"));
            w.writeEndElement();
            w.writeStartElement(QStringLiteral("timept"));
            w.writeAttribute(QStringLiteral("time"), toRational(t1));
            w.writeAttribute(QStringLiteral("value"), toRational(0.0));
            w.writeAttribute(QStringLiteral("interp"), QStringLiteral("linear"));
            w.writeEndElement();
            w.writeEndElement();
        }
        w.writeEndElement();
    };

    for (const Track &t : model.tracks()) {
        if (t.kind == TrackKind::Video) {
            for (const TrackEvent &ev : t.events) {
                writeClip(ev, false);
            }
        }
    }
    for (const Track &t : model.tracks()) {
        if (t.kind == TrackKind::Audio) {
            for (const TrackEvent &ev : t.events) {
                writeClip(ev, true);
            }
        }
    }

    w.writeEndElement(); // spine
    w.writeEndElement(); // sequence
    w.writeEndElement(); // project
    w.writeEndElement(); // event
    w.writeEndElement(); // library
    w.writeEndElement(); // fcpxml
    w.writeEndDocument();
    return true;
}

InterchangeResult ProjectInterchange::importPremiereProject(const QString &path, QString *error)
{
    InterchangeResult r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("Cannot open Premiere project: %1").arg(f.errorString());
        }
        return r;
    }
    const QByteArray data = f.readAll();
    f.close();
    r.title = QFileInfo(path).completeBaseName();

    // Newer .prproj is often gzip-compressed XML; try inflate via qUncompress won't work for gzip.
    // Scrape ASCII/UTF-8 and UTF-16LE media paths from raw bytes.
    scrapePathsFromText(QString::fromUtf8(data), &r);

    // UTF-16LE strings
    QString u16;
    const int n = data.size();
    for (int i = 0; i + 1 < n;) {
        const ushort c = quint8(data[i]) | (quint8(data[i + 1]) << 8);
        if (c >= 32 && c < 127) {
            QString s;
            int j = i;
            while (j + 1 < n) {
                const ushort ch = quint8(data[j]) | (quint8(data[j + 1]) << 8);
                if (ch < 32 || ch >= 127) {
                    break;
                }
                s.append(QChar(ch));
                j += 2;
            }
            if (s.size() >= 8) {
                scrapePathsFromText(s, &r);
            }
            i = j;
        } else {
            ++i;
        }
    }

    if (r.media.isEmpty()) {
        r.warnings << QStringLiteral(
            "No media paths extracted from .prproj (format may be compressed/unsupported).");
    } else {
        r.warnings << QStringLiteral("Premiere import is best-effort (media paths only).");
    }
    return r;
}

InterchangeResult ProjectInterchange::importBroadcastWave(const QStringList &paths)
{
    InterchangeResult r;
    for (const QString &p : paths) {
        addMediaUnique(&r, p);
    }
    return r;
}

FadeCurveType ProjectInterchange::fadeCurveFromVegasCode(int code)
{
    switch (code) {
    case 1:
        return FadeCurveType::Linear;
    case 2:
        return FadeCurveType::Fast;
    case -2:
        return FadeCurveType::Slow;
    case 4:
        return FadeCurveType::Smooth;
    case -4:
        return FadeCurveType::Sharp;
    default:
        return FadeCurveType::Smooth;
    }
}

int ProjectInterchange::fadeCurveToVegasCode(FadeCurveType type)
{
    switch (type) {
    case FadeCurveType::Linear:
        return 1;
    case FadeCurveType::Fast:
        return 2;
    case FadeCurveType::Slow:
        return -2;
    case FadeCurveType::Smooth:
        return 4;
    case FadeCurveType::Sharp:
        return -4;
    }
    return 4;
}

bool ProjectInterchange::exportVegasCsvEdl(const ProjectModel &model, const QString &path,
                                           QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Cannot write Vegas EDL: %1").arg(f.errorString());
        }
        return false;
    }
    QTextStream out(&f);
    out << "\"ID\";\"Track\";\"StartTime\";\"Length\";\"PlayRate\";\"Locked\";\"Normalized\";"
           "\"StretchMethod\";\"Looped\";\"OnRuler\";\"MediaType\";\"FileName\";\"Stream\";"
           "\"StreamStart\";\"StreamLength\";\"FadeTimeIn\";\"FadeTimeOut\";\"SustainGain\";"
           "\"CurveIn\";\"GainIn\";\"CurveOut\";\"GainOut\";\"Layer\";\"Color\";\"CurveInR\";"
           "\"CurveOutR\";\"PlayPitch\";\"LockPitch\";\"FirstChannel\";\"Channels\"\n";

    auto ms = [](double sec) {
        return QString::number(sec * 1000.0, 'f', 4);
    };
    auto boolTok = [](bool v) {
        return v ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
    };
    auto quotePath = [](const QString &p) {
        return QStringLiteral("\"%1\"").arg(QString(p).replace(QLatin1Char('"'), QLatin1String("\"\"")));
    };

    int id = 1;
    int videoTrackNo = 0;
    int audioTrackNo = 0;
    for (const Track &t : model.tracks()) {
        const bool isAudio = (t.kind == TrackKind::Audio);
        const int trackNo = isAudio ? audioTrackNo++ : videoTrackNo++;
        for (const TrackEvent &ev : t.events) {
            QString mediaType = QStringLiteral("VIDEO");
            if (isAudio || ev.mediaKind == EventMediaKind::Audio) {
                mediaType = QStringLiteral("AUDIO");
            } else if (ev.mediaKind == EventMediaKind::Still) {
                mediaType = QStringLiteral("VIDEO");
            }
            const QString filePath = eventExportPath(model, ev);
            double sustain = 1.0;
            if (isAudio || ev.mediaKind == EventMediaKind::Audio) {
                if (ev.gainDb <= -39.5) {
                    sustain = 0.0;
                } else {
                    sustain = std::clamp(std::pow(10.0, ev.gainDb / 20.0), 0.0, 1.0);
                }
            } else {
                sustain = std::clamp(ev.opacity, 0.0, 1.0);
            }
            const double playRate = ev.reversed ? -1.0 : 1.0;
            const double streamLen =
                ev.mediaLengthSec > 1e-6 ? ev.mediaLengthSec
                                         : (ev.looped ? ev.lengthSec : ev.lengthSec);
            out << id << "; " << trackNo << "; " << ms(ev.startSec) << "; " << ms(ev.lengthSec)
                << "; " << QString::number(playRate, 'f', 6) << "; FALSE; FALSE; 0; "
                << boolTok(ev.looped) << "; FALSE; " << mediaType << "; " << quotePath(filePath)
                << "; 0; " << ms(ev.mediaStartSec) << "; " << ms(streamLen) << "; "
                << ms(ev.fadeInSec) << "; " << ms(ev.fadeOutSec) << "; "
                << QString::number(sustain, 'f', 6) << "; "
                << fadeCurveToVegasCode(ev.fadeInCurve) << "; 0.000000; "
                << fadeCurveToVegasCode(ev.fadeOutCurve) << "; 0.000000; 0; -1; "
                << fadeCurveToVegasCode(ev.fadeInCurve) << "; "
                << fadeCurveToVegasCode(ev.fadeOutCurve) << "; 0.000000; FALSE; "
                << ev.firstChannel << "; "
                << (ev.channelCount > 0 ? ev.channelCount : (isAudio ? 2 : 0)) << "\n";
            ++id;
        }
    }
    return true;
}

InterchangeResult ProjectInterchange::importVegasCsvEdl(const QString &path, QString *error)
{
    InterchangeResult r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Cannot open Vegas EDL: %1").arg(f.errorString());
        }
        return r;
    }
    QTextStream in(&f);
    const QString headerLine = in.readLine();
    if (!headerLine.contains(QLatin1String("StartTime"), Qt::CaseInsensitive)
        || !headerLine.contains(QLatin1Char(';'))) {
        if (error) {
            *error = QStringLiteral("Not a Vegas EDL Text CSV (expected ';' header with StartTime).");
        }
        return r;
    }

    QStringList headers;
    {
        // Simple CSV split on ';' respecting quotes
        QString cur;
        bool inQ = false;
        for (QChar c : headerLine) {
            if (c == QLatin1Char('"')) {
                inQ = !inQ;
                continue;
            }
            if (c == QLatin1Char(';') && !inQ) {
                headers << cur.trimmed();
                cur.clear();
            } else {
                cur.append(c);
            }
        }
        headers << cur.trimmed();
    }

    auto col = [&](const QString &name) -> int {
        for (int i = 0; i < headers.size(); ++i) {
            if (headers[i].compare(name, Qt::CaseInsensitive) == 0) {
                return i;
            }
        }
        return -1;
    };
    const int iTrack = col(QStringLiteral("Track"));
    const int iStart = col(QStringLiteral("StartTime"));
    const int iLen = col(QStringLiteral("Length"));
    const int iRate = col(QStringLiteral("PlayRate"));
    const int iType = col(QStringLiteral("MediaType"));
    const int iFile = col(QStringLiteral("FileName"));
    const int iStreamStart = col(QStringLiteral("StreamStart"));
    const int iStreamLen = col(QStringLiteral("StreamLength"));
    const int iLooped = col(QStringLiteral("Looped"));
    const int iFadeIn = col(QStringLiteral("FadeTimeIn"));
    const int iFadeOut = col(QStringLiteral("FadeTimeOut"));
    const int iCurveIn = col(QStringLiteral("CurveIn"));
    const int iCurveOut = col(QStringLiteral("CurveOut"));
    const int iFirstCh = col(QStringLiteral("FirstChannel"));
    const int iChannels = col(QStringLiteral("Channels"));
    const int iSustain = col(QStringLiteral("SustainGain"));
    if (iStart < 0 || iLen < 0 || iType < 0) {
        if (error) {
            *error = QStringLiteral("Vegas EDL missing required columns.");
        }
        return r;
    }

    r.title = QFileInfo(path).completeBaseName();
    QSet<QString> seenKeys;

    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        QStringList cols;
        QString cur;
        bool inQ = false;
        for (QChar c : line) {
            if (c == QLatin1Char('"')) {
                inQ = !inQ;
                continue;
            }
            if (c == QLatin1Char(';') && !inQ) {
                cols << cur.trimmed();
                cur.clear();
            } else {
                cur.append(c);
            }
        }
        cols << cur.trimmed();

        auto at = [&](int i) -> QString {
            return (i >= 0 && i < cols.size()) ? cols[i] : QString();
        };

        const QString mediaType = at(iType).toUpper();
        if (mediaType != QLatin1String("VIDEO") && mediaType != QLatin1String("AUDIO")) {
            continue;
        }

        const double startMs = at(iStart).toDouble();
        const double lenMs = at(iLen).toDouble();
        if (!(lenMs > 1.0)) {
            continue;
        }

        InterchangeEvent ev;
        ev.kind = (mediaType == QLatin1String("AUDIO")) ? QStringLiteral("audio")
                                                        : QStringLiteral("video");
        ev.startSec = startMs / 1000.0;
        ev.lengthSec = lenMs / 1000.0;
        ev.trackIndex = at(iTrack).toInt();
        if (iFadeIn >= 0) {
            ev.fadeInSec = at(iFadeIn).toDouble() / 1000.0;
        }
        if (iFadeOut >= 0) {
            ev.fadeOutSec = at(iFadeOut).toDouble() / 1000.0;
        }
        if (iCurveIn >= 0) {
            ev.fadeInCurve = fadeCurveFromVegasCode(at(iCurveIn).toInt());
        }
        if (iCurveOut >= 0) {
            ev.fadeOutCurve = fadeCurveFromVegasCode(at(iCurveOut).toInt());
        }
        if (iFirstCh >= 0) {
            ev.firstChannel = std::max(0, at(iFirstCh).toInt());
        }
        if (iChannels >= 0) {
            ev.channelCount = std::max(0, at(iChannels).toInt());
        }
        if (iSustain >= 0 && !at(iSustain).isEmpty()) {
            bool ok = false;
            const double g = at(iSustain).toDouble(&ok);
            if (ok) {
                ev.hasSustainGain = true;
                ev.sustainGain = std::clamp(g, 0.0, 1.0);
            }
        }
        if (iRate >= 0 && !at(iRate).isEmpty()) {
            bool ok = false;
            const double rate = at(iRate).toDouble(&ok);
            if (ok && std::isfinite(rate) && std::abs(rate) > 1e-9) {
                ev.playRate = rate;
            }
        }
        if (iStreamStart >= 0 && !at(iStreamStart).isEmpty()) {
            bool ok = false;
            const double ms = at(iStreamStart).toDouble(&ok);
            if (ok && std::isfinite(ms) && ms >= 0.0) {
                ev.mediaStartSec = ms / 1000.0;
            }
        }
        if (iStreamLen >= 0 && !at(iStreamLen).isEmpty()) {
            bool ok = false;
            const double ms = at(iStreamLen).toDouble(&ok);
            if (ok && std::isfinite(ms) && ms > 0.0) {
                ev.mediaLengthSec = ms / 1000.0;
            }
        }
        if (iLooped >= 0 && !at(iLooped).isEmpty()) {
            const QString v = at(iLooped).trimmed();
            ev.looped = !(v.compare(QStringLiteral("FALSE"), Qt::CaseInsensitive) == 0
                          || v == QLatin1String("0"));
        }

        const QString fileName = at(iFile);
        if (!fileName.isEmpty()) {
            ev.sourcePath = QDir::fromNativeSeparators(fileName);
            addMediaUnique(&r, ev.sourcePath);
            ev.name = QFileInfo(ev.sourcePath).fileName();
            // Vegas stills are exported as MediaType=VIDEO — detect by extension
            if (ev.kind == QLatin1String("video")
                && guessKind(ev.sourcePath) == QLatin1String("still")) {
                ev.kind = QStringLiteral("still");
            }
        } else {
            ev.name = QStringLiteral("%1 %2").arg(ev.kind).arg(r.events.size() + 1);
        }

        // Dedup identical rows (Vegas often emits multi-channel audio duplicates)
        const QString key =
            QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
                .arg(ev.kind)
                .arg(ev.trackIndex)
                .arg(ev.startSec, 0, 'f', 3)
                .arg(ev.lengthSec, 0, 'f', 3)
                .arg(ev.sourcePath)
                .arg(ev.firstChannel)
                .arg(ev.channelCount);
        if (seenKeys.contains(key)) {
            continue;
        }
        seenKeys.insert(key);
        r.events.push_back(ev);
    }

    if (r.events.isEmpty()) {
        r.warnings << QStringLiteral("Vegas EDL CSV contained no VIDEO/AUDIO events.");
    } else {
        r.warnings << QStringLiteral("Timeline enriched from Vegas EDL sidecar (%1 events).")
                          .arg(r.events.size());
    }
    return r;
}

InterchangeResult ProjectInterchange::importClosedCaptions(const QString &path, QString *error)
{
    InterchangeResult r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Cannot open captions: %1").arg(f.errorString());
        }
        return r;
    }
    const QString lower = path.toLower();
    QTextStream in(&f);
    const QString content = in.readAll();
    f.close();
    r.title = QFileInfo(path).completeBaseName();

    auto parseTs = [](QString ts) -> double {
        ts = ts.trimmed();
        ts.replace(QLatin1Char(','), QLatin1Char('.'));
        // HH:MM:SS.mmm or HH:MM:SS:FF
        const QStringList parts = ts.split(QRegularExpression(QStringLiteral("[:.]")));
        if (parts.size() < 3) {
            return -1.0;
        }
        const int hh = parts[0].toInt();
        const int mm = parts[1].toInt();
        const int ss = parts[2].toInt();
        double frac = 0.0;
        if (parts.size() >= 4) {
            QString fracPart = parts[3];
            if (fracPart.size() <= 2) {
                // frames @30 as rough fallback
                frac = fracPart.toInt() / 30.0;
            } else {
                while (fracPart.size() < 3) {
                    fracPart.append(QLatin1Char('0'));
                }
                frac = fracPart.left(3).toInt() / 1000.0;
            }
        }
        return hh * 3600.0 + mm * 60.0 + ss + frac;
    };

    if (lower.endsWith(QLatin1String(".srt")) || lower.endsWith(QLatin1String(".vtt"))) {
        const QRegularExpression cueRe(
            QStringLiteral(
                R"((\d{1,2}:\d{2}:\d{2}[,.]\d{1,3})\s*-->\s*(\d{1,2}:\d{2}:\d{2}[,.]\d{1,3})\s*\n([\s\S]*?)(?=\n\s*\n|\n\d+\s*\n|\z))"),
            QRegularExpression::MultilineOption);
        auto it = cueRe.globalMatch(content);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const double start = parseTs(m.captured(1));
            if (start < 0.0) {
                continue;
            }
            InterchangeEvent ev;
            ev.startSec = start;
            ev.lengthSec = std::max(0.2, parseTs(m.captured(2)) - start);
            ev.name = m.captured(3).trimmed().replace(QLatin1Char('\n'), QLatin1Char(' '));
            ev.kind = QStringLiteral("caption");
            r.events.push_back(ev);
        }
    } else if (lower.endsWith(QLatin1String(".scc"))) {
        // Scenarist Closed Caption — collect timecodes as markers
        const QRegularExpression tcRe(QStringLiteral(R"((\d{2}:\d{2}:\d{2}[:;]\d{2}))"));
        auto it = tcRe.globalMatch(content);
        QSet<QString> seen;
        while (it.hasNext()) {
            const QString tc = it.next().captured(1);
            if (seen.contains(tc)) {
                continue;
            }
            seen.insert(tc);
            double sec = 0.0;
            QString norm = tc;
            norm.replace(QLatin1Char(';'), QLatin1Char(':'));
            if (!parseTimecode(norm, 30.0, &sec)) {
                continue;
            }
            InterchangeEvent ev;
            ev.startSec = sec;
            ev.lengthSec = 1.0;
            ev.name = QStringLiteral("CC %1").arg(tc);
            ev.kind = QStringLiteral("caption");
            r.events.push_back(ev);
        }
    } else {
        r.warnings << QStringLiteral("Unsupported caption format (use .srt / .vtt / .scc).");
    }

    if (r.events.isEmpty()) {
        r.warnings << QStringLiteral("No caption cues found.");
    }
    return r;
}

namespace {

QJsonObject fxSlotToJson(const FxSlot &s)
{
    QJsonObject o;
    o.insert(QStringLiteral("pluginId"), s.pluginId);
    o.insert(QStringLiteral("displayName"), s.displayName);
    o.insert(QStringLiteral("format"), static_cast<int>(s.format));
    o.insert(QStringLiteral("bypass"), s.bypass);
    // Opaque per-plugin parameter blob (e.g. Titles & Text / Media Generator params,
    // OFX/VST state) — preserved byte-for-byte via base64, never interpreted here.
    o.insert(QStringLiteral("state"), QString::fromLatin1(s.state.toBase64()));
    o.insert(QStringLiteral("hostKey"), s.hostKey);
    return o;
}

QJsonArray fxChainToJson(const QVector<FxSlot> &chain)
{
    QJsonArray arr;
    for (const FxSlot &s : chain) {
        arr.append(fxSlotToJson(s));
    }
    return arr;
}

// v1 archive scope covers position keyframes (the common case) but not mask keyframes —
// see MARKDOWN doc for the full list of fields not yet in the archive format.
QJsonObject eventPanCropToJson(const EventPanCropState &pc)
{
    QJsonObject o;
    QJsonArray kfs;
    for (const PanCropKeyframe &k : pc.positionKeyframes) {
        QJsonObject ko;
        ko.insert(QStringLiteral("timeSec"), k.timeSec);
        ko.insert(QStringLiteral("width"), k.width);
        ko.insert(QStringLiteral("height"), k.height);
        ko.insert(QStringLiteral("xCenter"), k.xCenter);
        ko.insert(QStringLiteral("yCenter"), k.yCenter);
        ko.insert(QStringLiteral("angleDeg"), k.angleDeg);
        ko.insert(QStringLiteral("rotationXCenter"), k.rotationXCenter);
        ko.insert(QStringLiteral("rotationYCenter"), k.rotationYCenter);
        ko.insert(QStringLiteral("smoothness"), k.smoothness);
        ko.insert(QStringLiteral("type"), static_cast<int>(k.type));
        kfs.append(ko);
    }
    o.insert(QStringLiteral("positionKeyframes"), kfs);
    o.insert(QStringLiteral("maintainAspectRatio"), pc.maintainAspectRatio);
    o.insert(QStringLiteral("stretchToFillFrame"), pc.stretchToFillFrame);
    return o;
}

EventPanCropState eventPanCropFromJson(const QJsonObject &o)
{
    EventPanCropState pc;
    for (const QJsonValue &v : o.value(QStringLiteral("positionKeyframes")).toArray()) {
        const QJsonObject ko = v.toObject();
        PanCropKeyframe k;
        k.timeSec = ko.value(QStringLiteral("timeSec")).toDouble();
        k.width = ko.value(QStringLiteral("width")).toDouble(1920.0);
        k.height = ko.value(QStringLiteral("height")).toDouble(1080.0);
        k.xCenter = ko.value(QStringLiteral("xCenter")).toDouble(960.0);
        k.yCenter = ko.value(QStringLiteral("yCenter")).toDouble(540.0);
        k.angleDeg = ko.value(QStringLiteral("angleDeg")).toDouble();
        k.rotationXCenter = ko.value(QStringLiteral("rotationXCenter")).toDouble(k.xCenter);
        k.rotationYCenter = ko.value(QStringLiteral("rotationYCenter")).toDouble(k.yCenter);
        k.smoothness = ko.value(QStringLiteral("smoothness")).toDouble();
        k.type = static_cast<VideoKeyframeType>(ko.value(QStringLiteral("type")).toInt());
        pc.positionKeyframes.push_back(k);
    }
    pc.maintainAspectRatio = o.value(QStringLiteral("maintainAspectRatio")).toBool(true);
    pc.stretchToFillFrame = o.value(QStringLiteral("stretchToFillFrame")).toBool(true);
    return pc;
}

FxSlot fxSlotFromJson(const QJsonObject &o)
{
    FxSlot s;
    s.pluginId = o.value(QStringLiteral("pluginId")).toString();
    s.displayName = o.value(QStringLiteral("displayName")).toString();
    s.format = static_cast<PluginFormat>(o.value(QStringLiteral("format")).toInt());
    s.bypass = o.value(QStringLiteral("bypass")).toBool();
    s.state = QByteArray::fromBase64(o.value(QStringLiteral("state")).toString().toLatin1());
    s.hostKey = o.value(QStringLiteral("hostKey")).toString();
    return s;
}

QVector<FxSlot> fxChainFromJson(const QJsonArray &arr)
{
    QVector<FxSlot> chain;
    chain.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        chain.push_back(fxSlotFromJson(v.toObject()));
    }
    return chain;
}

} // namespace

bool ProjectInterchange::exportProjectArchive(const ProjectModel &model, const QString &dirPath,
                                              bool copyMedia, QString *error)
{
    QDir dir(dirPath);
    if (!dir.exists() && !QDir().mkpath(dirPath)) {
        if (error) {
            *error = QStringLiteral("Cannot create archive folder: %1").arg(dirPath);
        }
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("OpenVegasArchive"));
    // v2: full TrackEvent/Track/FxSlot round-trip (fxChain, media path, mediaKind, pan/crop
    // position keyframes, markers) — v1 only kept timing, so re-opening a v1 archive lost
    // every generator/effect. See MARKDOWN/PROJECT_ARCHIVE_FORMAT.md for exact coverage and
    // known gaps (track motion, mixer console, automation lanes, mask keyframes: not yet).
    root.insert(QStringLiteral("version"), 2);
    root.insert(QStringLiteral("title"), model.projectTitle());
    root.insert(QStringLiteral("frameRate"), model.frameRate());
    root.insert(QStringLiteral("sampleRate"), static_cast<qint64>(model.sampleRate()));
    root.insert(QStringLiteral("tempoBpm"), model.tempoBpm());
    root.insert(QStringLiteral("width"), model.frameWidth());
    root.insert(QStringLiteral("height"), model.frameHeight());
    root.insert(QStringLiteral("sourceProject"), model.projectPath());

    QJsonArray markersArr;
    for (const TimelineMarker &mk : model.markers()) {
        QJsonObject mko;
        mko.insert(QStringLiteral("number"), mk.number);
        mko.insert(QStringLiteral("timeSec"), mk.timeSec);
        mko.insert(QStringLiteral("label"), mk.label);
        markersArr.append(mko);
    }
    root.insert(QStringLiteral("markers"), markersArr);

    QJsonArray mediaArr;
    QStringList mediaLines;
    const QString mediaDirPath = QDir(dirPath).filePath(QStringLiteral("Media"));
    if (copyMedia) {
        QDir().mkpath(mediaDirPath);
    }
    for (const MediaItem &m : model.mediaPool()) {
        QJsonObject mo;
        mo.insert(QStringLiteral("path"), m.path);
        mo.insert(QStringLiteral("displayName"), m.displayName);
        mo.insert(QStringLiteral("kind"), m.kind);
        mo.insert(QStringLiteral("missing"), m.missing);
        QString archivedPath = m.path;
        if (copyMedia && QFileInfo::exists(m.path)) {
            const QString dest = QDir(mediaDirPath).filePath(m.displayName);
            if (QFile::copy(m.path, dest) || QFileInfo::exists(dest)) {
                archivedPath = QStringLiteral("Media/") + m.displayName;
                mo.insert(QStringLiteral("archivedPath"), archivedPath);
            }
        }
        mediaArr.append(mo);
        mediaLines << QStringLiteral("%1\t%2\t%3").arg(m.kind, m.displayName, archivedPath);
    }
    root.insert(QStringLiteral("media"), mediaArr);

    QJsonArray tracksArr;
    for (const Track &t : model.tracks()) {
        QJsonObject to;
        to.insert(QStringLiteral("id"), t.id);
        to.insert(QStringLiteral("name"), t.name);
        to.insert(QStringLiteral("kind"),
                  t.kind == TrackKind::Audio ? QStringLiteral("audio") : QStringLiteral("video"));
        to.insert(QStringLiteral("height"), t.height);
        to.insert(QStringLiteral("muted"), t.muted);
        to.insert(QStringLiteral("solo"), t.solo);
        to.insert(QStringLiteral("volumeDb"), t.volumeDb);
        to.insert(QStringLiteral("pan"), double(t.pan));
        to.insert(QStringLiteral("busId"), t.busId);
        to.insert(QStringLiteral("automationMode"), static_cast<int>(t.automationMode));
        if (t.displayColor.isValid()) {
            to.insert(QStringLiteral("displayColor"), t.displayColor.name(QColor::HexArgb));
        }
        to.insert(QStringLiteral("fxChain"), fxChainToJson(t.fxChain));

        QJsonArray evArr;
        for (const TrackEvent &ev : t.events) {
            QJsonObject eo;
            eo.insert(QStringLiteral("id"), ev.id);
            eo.insert(QStringLiteral("name"), ev.name);
            eo.insert(QStringLiteral("mediaPath"), ev.mediaPath);
            eo.insert(QStringLiteral("startSec"), ev.startSec);
            eo.insert(QStringLiteral("lengthSec"), ev.lengthSec);
            eo.insert(QStringLiteral("mediaStartSec"), ev.mediaStartSec);
            eo.insert(QStringLiteral("mediaLengthSec"), ev.mediaLengthSec);
            eo.insert(QStringLiteral("looped"), ev.looped);
            eo.insert(QStringLiteral("reversed"), ev.reversed);
            eo.insert(QStringLiteral("fadeInSec"), ev.fadeInSec);
            eo.insert(QStringLiteral("fadeOutSec"), ev.fadeOutSec);
            eo.insert(QStringLiteral("fadeInCurve"), static_cast<int>(ev.fadeInCurve));
            eo.insert(QStringLiteral("fadeOutCurve"), static_cast<int>(ev.fadeOutCurve));
            eo.insert(QStringLiteral("opacity"), ev.opacity);
            eo.insert(QStringLiteral("gainDb"), ev.gainDb);
            eo.insert(QStringLiteral("mediaKind"), static_cast<int>(ev.mediaKind));
            eo.insert(QStringLiteral("groupId"), ev.groupId);
            eo.insert(QStringLiteral("firstChannel"), ev.firstChannel);
            eo.insert(QStringLiteral("channelCount"), ev.channelCount);
            eo.insert(QStringLiteral("fxChain"), fxChainToJson(ev.fxChain));
            eo.insert(QStringLiteral("panCrop"), eventPanCropToJson(ev.panCrop));
            evArr.append(eo);
        }
        to.insert(QStringLiteral("events"), evArr);
        tracksArr.append(to);
    }
    root.insert(QStringLiteral("tracks"), tracksArr);

    QFile jsonFile(QDir(dirPath).filePath(QStringLiteral("project.json")));
    if (!jsonFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Cannot write project.json");
        }
        return false;
    }
    jsonFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    jsonFile.close();

    QFile listFile(QDir(dirPath).filePath(QStringLiteral("media_list.txt")));
    if (listFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&listFile);
        out << "# kind\tname\tpath\n";
        for (const QString &line : mediaLines) {
            out << line << "\n";
        }
    }

    // Also write EDL next to archive for interchange convenience
    exportEdl(model, QDir(dirPath).filePath(model.projectTitle() + QStringLiteral(".edl")), nullptr);
    exportVegasCsvEdl(model,
                      QDir(dirPath).filePath(model.projectTitle() + QStringLiteral(".txt")),
                      nullptr);
    return true;
}

bool ProjectInterchange::isProjectArchive(const QString &dirPath)
{
    QFile f(QDir(dirPath).filePath(QStringLiteral("project.json")));
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    return root.value(QStringLiteral("format")).toString() == QLatin1String("OpenVegasArchive");
}

bool ProjectInterchange::importProjectArchive(const QString &dirPath, ProjectModel *model,
                                              QString *error)
{
    if (!model) {
        if (error) {
            *error = QStringLiteral("No project model to populate");
        }
        return false;
    }
    QFile f(QDir(dirPath).filePath(QStringLiteral("project.json")));
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("Cannot open project.json in %1").arg(dirPath);
        }
        return false;
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) {
            *error = QStringLiteral("Invalid project.json: %1").arg(perr.errorString());
        }
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("OpenVegasArchive")) {
        if (error) {
            *error = QStringLiteral("Not an OpenVegas project archive");
        }
        return false;
    }

    model->loadEmptyProject();
    model->setFrameRate(root.value(QStringLiteral("frameRate")).toDouble(29.97));
    model->setSampleRate(static_cast<quint32>(
        root.value(QStringLiteral("sampleRate")).toVariant().toLongLong()));
    model->setTempoBpm(root.value(QStringLiteral("tempoBpm")).toDouble(120.0));
    model->setFrameSize(root.value(QStringLiteral("width")).toInt(1920),
                        root.value(QStringLiteral("height")).toInt(1080));

    for (const QJsonValue &v : root.value(QStringLiteral("media")).toArray()) {
        const QJsonObject mo = v.toObject();
        MediaItem item;
        // archivedPath (relative to dirPath) wins when the original path no longer
        // resolves — same "portable archive" intent as copyMedia in the exporter.
        const QString archived = mo.value(QStringLiteral("archivedPath")).toString();
        const QString original = mo.value(QStringLiteral("path")).toString();
        item.path = (!archived.isEmpty() && !QFileInfo::exists(original))
                       ? QDir(dirPath).filePath(archived)
                       : original;
        item.displayName = mo.value(QStringLiteral("displayName")).toString();
        item.kind = mo.value(QStringLiteral("kind")).toString();
        item.missing = !QFileInfo::exists(item.path);
        model->mediaPool().push_back(item);
    }

    for (const QJsonValue &v : root.value(QStringLiteral("markers")).toArray()) {
        const QJsonObject mko = v.toObject();
        TimelineMarker mk;
        mk.number = mko.value(QStringLiteral("number")).toInt();
        mk.timeSec = mko.value(QStringLiteral("timeSec")).toDouble();
        mk.label = mko.value(QStringLiteral("label")).toString();
        model->markers().push_back(mk);
    }

    for (const QJsonValue &tv : root.value(QStringLiteral("tracks")).toArray()) {
        const QJsonObject to = tv.toObject();
        const TrackKind kind = to.value(QStringLiteral("kind")).toString() == QLatin1String("audio")
                                   ? TrackKind::Audio
                                   : TrackKind::Video;
        const int ti = model->addTrack(kind);
        Track &t = model->tracks()[ti];
        t.name = to.value(QStringLiteral("name")).toString();
        t.height = to.value(QStringLiteral("height")).toInt(t.height);
        t.muted = to.value(QStringLiteral("muted")).toBool();
        t.solo = to.value(QStringLiteral("solo")).toBool();
        t.volumeDb = to.value(QStringLiteral("volumeDb")).toDouble();
        t.pan = float(to.value(QStringLiteral("pan")).toDouble());
        t.busId = to.value(QStringLiteral("busId")).toInt(-1);
        t.automationMode =
            static_cast<AutomationWriteMode>(to.value(QStringLiteral("automationMode")).toInt());
        if (to.contains(QStringLiteral("displayColor"))) {
            t.displayColor = QColor(to.value(QStringLiteral("displayColor")).toString());
        }
        t.fxChain = fxChainFromJson(to.value(QStringLiteral("fxChain")).toArray());

        for (const QJsonValue &ev2 : to.value(QStringLiteral("events")).toArray()) {
            const QJsonObject eo = ev2.toObject();
            TrackEvent ev;
            ev.id = eo.value(QStringLiteral("id")).toInt();
            ev.name = eo.value(QStringLiteral("name")).toString();
            ev.mediaPath = eo.value(QStringLiteral("mediaPath")).toString();
            ev.startSec = eo.value(QStringLiteral("startSec")).toDouble();
            ev.lengthSec = eo.value(QStringLiteral("lengthSec")).toDouble(1.0);
            ev.mediaStartSec = eo.value(QStringLiteral("mediaStartSec")).toDouble();
            ev.mediaLengthSec = eo.value(QStringLiteral("mediaLengthSec")).toDouble();
            ev.looped = eo.value(QStringLiteral("looped")).toBool(true);
            ev.reversed = eo.value(QStringLiteral("reversed")).toBool();
            ev.fadeInSec = eo.value(QStringLiteral("fadeInSec")).toDouble();
            ev.fadeOutSec = eo.value(QStringLiteral("fadeOutSec")).toDouble();
            ev.fadeInCurve =
                static_cast<FadeCurveType>(eo.value(QStringLiteral("fadeInCurve")).toInt());
            ev.fadeOutCurve =
                static_cast<FadeCurveType>(eo.value(QStringLiteral("fadeOutCurve")).toInt());
            ev.opacity = eo.value(QStringLiteral("opacity")).toDouble(1.0);
            ev.gainDb = eo.value(QStringLiteral("gainDb")).toDouble();
            ev.mediaKind = static_cast<EventMediaKind>(eo.value(QStringLiteral("mediaKind")).toInt());
            ev.groupId = eo.value(QStringLiteral("groupId")).toInt();
            ev.firstChannel = eo.value(QStringLiteral("firstChannel")).toInt();
            ev.channelCount = eo.value(QStringLiteral("channelCount")).toInt();
            ev.fxChain = fxChainFromJson(eo.value(QStringLiteral("fxChain")).toArray());
            ev.panCrop = eventPanCropFromJson(eo.value(QStringLiteral("panCrop")).toObject());
            t.events.push_back(ev);
        }
    }

    model->setProjectPath(QDir(dirPath).filePath(QStringLiteral("project.json")));
    return true;
}

} // namespace openvegas
