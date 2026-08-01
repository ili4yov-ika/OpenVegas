#include "io/VegReader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace openvegas {

namespace {

const char kHdrConst[12] = {'\x2E', '\x91', '\xCF', '\x11', '\xA5', '\xD6',
                            '\x28', '\xDB', '\x04', '\xC1', '\x00', '\x00'};

/** VEGAS Pro uses 10 000 000 ticks per second for edit times. */
constexpr quint64 kTicksPerSec = 10000000ULL;
constexpr quint64 kMinLengthTicks = kTicksPerSec / 10;          // 0.1 s
constexpr quint64 kMaxLengthTicks = kTicksPerSec * 3600ULL * 6; // 6 h
constexpr quint64 kMaxStartTicks = kTicksPerSec * 3600ULL * 24; // 24 h

bool isPrintableUtf16Char(ushort c)
{
    return (c >= 32 && c <= 126) || (c >= 0x0400 && c <= 0x04FF) || c == 0x00A0;
}

bool isFinitePositive(double v, double lo, double hi)
{
    return std::isfinite(v) && v > lo && v < hi;
}

struct FrameSizeCandidate {
    int w = 0;
    int h = 0;
};

FrameSizeCandidate scanFrameSize(const QByteArray &data)
{
    static const FrameSizeCandidate kCommon[] = {
        {1920, 1080}, {1280, 720},  {3840, 2160}, {2560, 1440}, {1080, 1920},
        {720, 1280},  {1440, 1080}, {720, 480},   {720, 576},   {640, 480},
        {4096, 2160},
    };

    for (int off = 0x40; off + 8 < data.size(); off += 4) {
        const quint32 a = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar *>(data.constData() + off));
        const quint32 b = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar *>(data.constData() + off + 4));
        for (const FrameSizeCandidate &c : kCommon) {
            if (a == static_cast<quint32>(c.w) && b == static_cast<quint32>(c.h)) {
                return c;
            }
        }
    }
    return {1920, 1080};
}

} // namespace

bool VegReader::looksLikeVeg(const QByteArray &data)
{
    if (data.size() < 64) {
        return false;
    }
    if (data.left(4) != QByteArrayLiteral("riff")) {
        return false;
    }
    return data.mid(4, 12) == QByteArray(kHdrConst, 12);
}

QStringList VegReader::extractUtf16Strings(const QByteArray &data, int minChars)
{
    QStringList out;
    const int n = data.size();
    int i = 0;
    while (i + 3 < n) {
        const ushort c0 =
            qFromLittleEndian<ushort>(reinterpret_cast<const uchar *>(data.constData() + i));
        if (!isPrintableUtf16Char(c0)) {
            ++i;
            continue;
        }
        QString s;
        int j = i;
        while (j + 1 < n) {
            const ushort c =
                qFromLittleEndian<ushort>(reinterpret_cast<const uchar *>(data.constData() + j));
            if (!isPrintableUtf16Char(c)) {
                break;
            }
            s.append(QChar(c));
            j += 2;
        }
        if (s.size() >= minChars) {
            out.push_back(s);
            i = j;
        } else {
            ++i;
        }
    }
    return out;
}

bool VegReader::isMediaPath(const QString &s)
{
    static const QStringList exts = {
        QStringLiteral(".mp4"),  QStringLiteral(".mov"),  QStringLiteral(".avi"),
        QStringLiteral(".mkv"),  QStringLiteral(".webm"), QStringLiteral(".mpg"),
        QStringLiteral(".mpeg"), QStringLiteral(".mxf"),  QStringLiteral(".m4v"),
        QStringLiteral(".wmv"),  QStringLiteral(".wav"),  QStringLiteral(".mp3"),
        QStringLiteral(".aif"),  QStringLiteral(".aiff"), QStringLiteral(".flac"),
        QStringLiteral(".ogg"),  QStringLiteral(".png"),  QStringLiteral(".jpg"),
        QStringLiteral(".jpeg"), QStringLiteral(".tga"),  QStringLiteral(".tif"),
        QStringLiteral(".tiff"), QStringLiteral(".bmp"),  QStringLiteral(".gif"),
    };
    const QString lower = s.toLower();
    for (const QString &e : exts) {
        if (lower.endsWith(e)) {
            return s.contains(QLatin1Char(':')) || s.contains(QLatin1Char('\\'))
                   || s.contains(QLatin1Char('/'));
        }
    }
    return false;
}

bool VegReader::isProjectSelfPath(const QString &s)
{
    return s.toLower().endsWith(QLatin1String(".veg"));
}

bool VegReader::isServicePath(const QString &s)
{
    const QString lower = s.toLower();
    return lower.contains(QLatin1String("appdata")) || lower.contains(QLatin1String("\\documents"))
           || lower.endsWith(QLatin1String("\\documents"))
           || lower.contains(QLatin1String("vegas pro\\"))
           || lower.contains(QLatin1String("/vegas pro/"));
}

void VegReader::parseHeader(const QByteArray &data, VegOpenResult *result)
{
    const uchar *p = reinterpret_cast<const uchar *>(data.constData());
    result->header.fileSize = qFromLittleEndian<quint64>(p + 0x10);
    result->header.earlyBlobSize = qFromLittleEndian<quint64>(p + 0x38);
    result->header.vegasVersion = qFromLittleEndian<quint16>(p + 0x46);
    result->header.sampleRate = qFromLittleEndian<quint32>(p + 0x4C);
    result->header.frameRate = qFromLittleEndian<double>(p + 0x50);
    result->header.tempoBpm = qFromLittleEndian<double>(p + 0x58);

    if (result->header.fileSize != static_cast<quint64>(data.size())) {
        result->warnings << QStringLiteral("Header file-size field (%1) != actual size (%2)")
                                .arg(result->header.fileSize)
                                .arg(data.size());
    }

    const FrameSizeCandidate fs = scanFrameSize(data);
    result->header.width = fs.w;
    result->header.height = fs.h;

    if (result->header.vegasVersion == 0 || result->header.vegasVersion > 40) {
        result->warnings << QStringLiteral("Unexpected VEGAS version field: %1")
                                .arg(result->header.vegasVersion);
    }
    if (!isFinitePositive(result->header.frameRate, 1.0, 240.0)) {
        result->warnings << QStringLiteral("Unexpected frame rate; using 29.97");
        result->header.frameRate = 29.97;
    }
    if (result->header.sampleRate < 8000 || result->header.sampleRate > 192000) {
        result->warnings << QStringLiteral("Unexpected sample rate; using 48000");
        result->header.sampleRate = 48000;
    }
    if (!isFinitePositive(result->header.tempoBpm, 1.0, 400.0)) {
        result->header.tempoBpm = 120.0;
    }
}

void VegReader::parseUtf16Metadata(const QByteArray &data, VegOpenResult *result)
{
    QSet<QString> seenMedia;
    QSet<QString> seenLabels;
    QSet<QString> seenTrackFx;
    QSet<QString> seenEventFx;

    const QStringList utf16 = extractUtf16Strings(data, 4);
    for (const QString &s : utf16) {
        if (isProjectSelfPath(s)) {
            if (result->projectPathHint.isEmpty()) {
                result->projectPathHint = QDir::fromNativeSeparators(s);
            }
            continue;
        }

        if (isMediaPath(s) && !isServicePath(s)) {
            const QString norm = QDir::fromNativeSeparators(s);
            const QString key = QDir::cleanPath(norm).toLower();
            if (!seenMedia.contains(key)) {
                seenMedia.insert(key);
                result->mediaPaths.push_back(norm);
            }
            continue;
        }

        if (s.startsWith(QLatin1String("VEGAS Track "), Qt::CaseInsensitive)) {
            if (!seenTrackFx.contains(s)) {
                seenTrackFx.insert(s);
                result->trackFxNames.push_back(s);
            }
            continue;
        }

        // Mixing Console channel labels (Bus A / Input A / FX N + plugin name)
        {
            static const QRegularExpression busRe(
                QStringLiteral("^Bus\\s+([A-Z]|\\d+)$"), QRegularExpression::CaseInsensitiveOption);
            static const QRegularExpression inputRe(
                QStringLiteral("^Input\\s+([A-Z]|\\d+)$"), QRegularExpression::CaseInsensitiveOption);
            static const QRegularExpression fxRe(QStringLiteral("^FX\\s+(\\d+)$"),
                                                QRegularExpression::CaseInsensitiveOption);
            if (busRe.match(s).hasMatch()) {
                if (!result->mixerBusNames.contains(s, Qt::CaseInsensitive)) {
                    result->mixerBusNames.push_back(s);
                }
                continue;
            }
            if (inputRe.match(s).hasMatch()) {
                if (!result->mixerInputBusNames.contains(s, Qt::CaseInsensitive)) {
                    result->mixerInputBusNames.push_back(s);
                }
                continue;
            }
            if (fxRe.match(s).hasMatch()) {
                // Pair with the next short plug-in label in the UTF-16 stream
                QString plugin;
                const int idx = utf16.indexOf(s);
                for (int j = idx + 1; j < utf16.size(); ++j) {
                    const QString &cand = utf16.at(j);
                    if (cand.contains(QLatin1Char('\\')) || cand.contains(QLatin1Char('/'))) {
                        continue;
                    }
                    if (fxRe.match(cand).hasMatch() || busRe.match(cand).hasMatch()
                        || inputRe.match(cand).hasMatch()) {
                        break;
                    }
                    if (cand.compare(QLatin1String("Master"), Qt::CaseInsensitive) == 0
                        || cand.compare(QLatin1String("Preview"), Qt::CaseInsensitive) == 0
                        || cand.startsWith(QLatin1String("VEGAS "), Qt::CaseInsensitive)
                        || cand.startsWith(QLatin1String("Main Timeline"), Qt::CaseInsensitive)) {
                        continue;
                    }
                    if (cand.size() >= 2 && cand.size() <= 64) {
                        plugin = cand;
                        break;
                    }
                }
                if (!result->mixerAssignableFxLabels.contains(s, Qt::CaseInsensitive)) {
                    result->mixerAssignableFxLabels.push_back(s);
                    result->mixerAssignableFxPlugins.push_back(
                        plugin.isEmpty() ? s : plugin);
                }
                continue;
            }
        }

        if (s.startsWith(QLatin1String("{Svfx:"), Qt::CaseInsensitive)
            || s.startsWith(QLatin1String("OFX:"), Qt::CaseInsensitive)) {
            if (!seenEventFx.contains(s)) {
                seenEventFx.insert(s);
                result->eventFxNames.push_back(s);
            }
            continue;
        }

        // Event / take labels (docs_veg: sample_for_project_* correlating with timeline)
        const bool sampleLabel =
            s.startsWith(QLatin1String("sample_for_project_"), Qt::CaseInsensitive);
        const bool untitledLabel = s.contains(QLatin1String("untitled"), Qt::CaseInsensitive)
                                   && !s.contains(QLatin1Char('.'));
        if ((sampleLabel || untitledLabel) && !s.contains(QLatin1Char('\\'))
            && !s.contains(QLatin1Char('/')) && !seenLabels.contains(s)) {
            seenLabels.insert(s);
            result->eventLabels.push_back(s);
        }
    }

    // Audio Event FX chain (UTF-16 order). Names and "(VSTn, 64 Bit)" are often
    // separate strings because TAB is not kept by the UTF-16 extractor.
    {
        static const QRegularExpression vstTagRe(
            QStringLiteral(R"(^\(VST([123])?,?\s*64\s*Bit\)$)"),
            QRegularExpression::CaseInsensitiveOption);
        static const QStringList builtinEventFx = {
            QStringLiteral("Chorus"),
            QStringLiteral("Volume"),
            QStringLiteral("Noise Gate"),
            QStringLiteral("Reverb"),
            QStringLiteral("Delay"),
            QStringLiteral("Distortion"),
            QStringLiteral("Equalization"),
            QStringLiteral("Pitch Shift"),
            QStringLiteral("Time Stretch"),
            QStringLiteral("Vibrato"),
            QStringLiteral("Flange/Wah-Wah"),
            QStringLiteral("Amplitude Modulation"),
            QStringLiteral("ExpressFX Chorus"),
            QStringLiteral("ExpressFX Delay"),
            QStringLiteral("ExpressFX Distortion"),
            QStringLiteral("ExpressFX EQ"),
            QStringLiteral("ExpressFX Flange"),
            QStringLiteral("ExpressFX Reverb"),
            QStringLiteral("ExpressFX Stutter"),
        };
        QSet<QString> seenAudioFx;
        auto pushAudioFx = [&](const QString &raw, const QString &dedupeKey) {
            const QString key = dedupeKey.toLower();
            if (key.isEmpty() || seenAudioFx.contains(key)) {
                return;
            }
            seenAudioFx.insert(key);
            result->audioEventFxNames.push_back(raw);
        };
        for (int i = 0; i < utf16.size(); ++i) {
            const QString &s = utf16.at(i);
            const auto tag = vstTagRe.match(s.trimmed());
            if (tag.hasMatch() && i > 0) {
                const QString &name = utf16.at(i - 1).trimmed();
                if (name.startsWith(QLatin1String("VEGAS Track "), Qt::CaseInsensitive)
                    || name.contains(QLatin1Char('\\')) || name.contains(QLatin1Char('/'))
                    || name.compare(QLatin1String("Empty Graph"), Qt::CaseInsensitive) == 0
                    || name.compare(QLatin1String("Preview"), Qt::CaseInsensitive) == 0
                    || name.compare(QLatin1String("Master"), Qt::CaseInsensitive) == 0) {
                    continue;
                }
                // Keep tag in the raw string so fxSlotFromVegName sees VST1/VST2/VST3.
                pushAudioFx(QStringLiteral("%1 %2").arg(name, s.trimmed()), name);
                continue;
            }
            for (const QString &b : builtinEventFx) {
                if (s.compare(b, Qt::CaseInsensitive) == 0) {
                    pushAudioFx(s, s);
                    break;
                }
            }
        }
    }

    // Fallback labels from media basenames when none found
    if (result->eventLabels.isEmpty()) {
        QSet<QString> bases;
        for (const QString &mp : result->mediaPaths) {
            const QString base = QFileInfo(mp).completeBaseName();
            if (!base.isEmpty() && !bases.contains(base)) {
                bases.insert(base);
                result->eventLabels.push_back(base);
            }
        }
    }

    if (result->mediaPaths.isEmpty()) {
        result->warnings << QStringLiteral(
            "No media file paths found in .veg (empty pool or unsupported layout).");
    }
}

void VegReader::parseTimelineEvents(const QByteArray &data, VegOpenResult *result)
{
    /*
     * Pattern (docs_veg / COMPARE compressed vs stretched):
     *   [u64 trackKind=2|3] [u64 startTicks] [u64 lengthTicks] [u64 0] [f64 rate] [u64 0]
     * Time unit: 10_000_000 ticks/sec. kind 3 ≈ video, 2 ≈ audio.
     */
    QVector<VegEventInfo> raw;
    const uchar *base = reinterpret_cast<const uchar *>(data.constData());
    const int n = data.size();

    for (int off = 64; off + 40 <= n; off += 8) {
        const quint64 startTicks = qFromLittleEndian<quint64>(base + off);
        const quint64 lengthTicks = qFromLittleEndian<quint64>(base + off + 8);
        const quint64 z1 = qFromLittleEndian<quint64>(base + off + 16);
        const double rate = qFromLittleEndian<double>(base + off + 24);
        const quint64 z2 = qFromLittleEndian<quint64>(base + off + 32);
        if (z1 != 0 || z2 != 0) {
            continue;
        }
        if (!isFinitePositive(rate, 0.05, 20.0)) {
            continue;
        }
        if (lengthTicks < kMinLengthTicks || lengthTicks > kMaxLengthTicks) {
            continue;
        }
        if (startTicks > kMaxStartTicks) {
            continue;
        }

        quint64 kindCode = 0;
        if (off >= 8) {
            kindCode = qFromLittleEndian<quint64>(base + off - 8);
        }
        if (kindCode != 2 && kindCode != 3) {
            continue;
        }

        VegEventInfo ev;
        ev.kind = (kindCode == 3) ? VegEventInfo::Kind::Video : VegEventInfo::Kind::Audio;
        ev.startSec = static_cast<double>(startTicks) / static_cast<double>(kTicksPerSec);
        ev.lengthSec = static_cast<double>(lengthTicks) / static_cast<double>(kTicksPerSec);
        ev.playbackRate = rate;
        raw.push_back(ev);
    }

    // Deduplicate identical (kind, start, length, rate) keeping first occurrence
    QSet<QString> seen;
    for (const VegEventInfo &ev : raw) {
        const QString key =
            QStringLiteral("%1|%2|%3|%4")
                .arg(static_cast<int>(ev.kind))
                .arg(ev.startSec, 0, 'f', 4)
                .arg(ev.lengthSec, 0, 'f', 4)
                .arg(ev.playbackRate, 0, 'f', 6);
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        result->events.push_back(ev);
    }

    std::sort(result->events.begin(), result->events.end(),
              [](const VegEventInfo &a, const VegEventInfo &b) {
                  if (a.kind != b.kind) {
                      return static_cast<int>(a.kind) < static_cast<int>(b.kind);
                  }
                  if (std::abs(a.startSec - b.startSec) > 1e-4) {
                      return a.startSec < b.startSec;
                  }
                  return a.lengthSec < b.lengthSec;
              });

    result->hasTimelineTimings = !result->events.isEmpty();
    if (!result->hasTimelineTimings) {
        result->warnings << QStringLiteral(
            "No binary timeline timing blocks found — using heuristic event lengths.");
    }
}

void VegReader::assignEventNames(VegOpenResult *result)
{
    QStringList videoNames;
    QStringList audioNames;
    for (const QString &label : result->eventLabels) {
        const QString lower = label.toLower();
        if (lower.contains(QLatin1String("audio")) || lower.endsWith(QLatin1String("_audio"))) {
            audioNames.push_back(label);
        } else {
            videoNames.push_back(label);
        }
    }

    QStringList mediaVideo;
    QStringList mediaAudio;
    for (const QString &mp : result->mediaPaths) {
        const QString lower = mp.toLower();
        const QString base = QFileInfo(mp).completeBaseName();
        if (lower.endsWith(QLatin1String(".wav")) || lower.endsWith(QLatin1String(".mp3"))
            || lower.endsWith(QLatin1String(".flac")) || lower.endsWith(QLatin1String(".aif"))
            || lower.endsWith(QLatin1String(".aiff")) || lower.endsWith(QLatin1String(".ogg"))) {
            mediaAudio.push_back(base);
        } else if (!(lower.endsWith(QLatin1String(".png")) || lower.endsWith(QLatin1String(".jpg"))
                     || lower.endsWith(QLatin1String(".jpeg"))
                     || lower.endsWith(QLatin1String(".tga"))
                     || lower.endsWith(QLatin1String(".tif"))
                     || lower.endsWith(QLatin1String(".tiff"))
                     || lower.endsWith(QLatin1String(".bmp"))
                     || lower.endsWith(QLatin1String(".gif")))) {
            mediaVideo.push_back(base);
        }
    }

    auto pickName = [](int idx, const QStringList &primary, const QStringList &fallback,
                       const QString &generic) {
        if (!primary.isEmpty()) {
            if (primary.size() == 1 && idx > 0) {
                return QStringLiteral("%1 %2").arg(primary.first()).arg(idx + 1);
            }
            return primary[idx % primary.size()];
        }
        if (!fallback.isEmpty()) {
            if (fallback.size() == 1 && idx > 0) {
                return QStringLiteral("%1 %2").arg(fallback.first()).arg(idx + 1);
            }
            return fallback[idx % fallback.size()];
        }
        return QStringLiteral("%1 %2").arg(generic).arg(idx + 1);
    };

    int vIdx = 0;
    int aIdx = 0;
    for (VegEventInfo &ev : result->events) {
        if (ev.kind == VegEventInfo::Kind::Video) {
            ev.name = pickName(vIdx, videoNames, mediaVideo, QStringLiteral("Video"));
            ++vIdx;
        } else if (ev.kind == VegEventInfo::Kind::Audio) {
            ev.name = pickName(aIdx, audioNames,
                               !mediaAudio.isEmpty() ? mediaAudio : mediaVideo,
                               QStringLiteral("Audio"));
            ++aIdx;
        }
    }
}

void VegReader::parseMarkers(const QByteArray &data, VegOpenResult *result)
{
    if (!result || data.size() < 64) {
        return;
    }

    // Vegas Pro timeline marker record (seen in project_big--buck-bunny_markers.veg):
    //   GUID 56 62 f7 ab 2d 39 d2 11 86 c7 00 c0 4f 8e db 8a
    //   u64 recordHint, u64 (often 0x28), u64 timeTicks, u64 durationTicks,
    //   u32 number, u32 flags, u64 labelBytes, utf16le label
    static const uchar kMarkerGuid[16] = {0x56, 0x62, 0xf7, 0xab, 0x2d, 0x39, 0xd2, 0x11,
                                          0x86, 0xc7, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a};
    const QByteArray guid(reinterpret_cast<const char *>(kMarkerGuid), 16);
    const uchar *base = reinterpret_cast<const uchar *>(data.constData());
    const int n = data.size();

    int pos = 0;
    while (pos + 56 < n) {
        const int found = data.indexOf(guid, pos);
        if (found < 0) {
            break;
        }
        pos = found + 16;
        if (found + 16 + 40 > n) {
            break;
        }

        const quint64 timeTicks =
            qFromLittleEndian<quint64>(base + found + 16 + 16); // skip two u64 headers
        const quint64 durTicks = qFromLittleEndian<quint64>(base + found + 16 + 24);
        Q_UNUSED(durTicks);
        const quint32 number = qFromLittleEndian<quint32>(base + found + 16 + 32);
        const quint64 labelBytes = qFromLittleEndian<quint64>(base + found + 16 + 40);
        const int labelOff = found + 16 + 48;

        if (timeTicks > kMaxStartTicks) {
            continue;
        }
        if (labelBytes == 0 || labelBytes > 512 || (labelBytes & 1ULL) != 0) {
            continue;
        }
        if (labelOff + static_cast<int>(labelBytes) > n) {
            continue;
        }

        QString label;
        label.resize(static_cast<int>(labelBytes / 2));
        for (int i = 0; i < label.size(); ++i) {
            label[i] = QChar(qFromLittleEndian<quint16>(base + labelOff + i * 2));
        }
        while (label.endsWith(QChar(0))) {
            label.chop(1);
        }
        label = label.trimmed();
        if (label.isEmpty()) {
            continue;
        }

        VegMarkerInfo m;
        m.timeSec = static_cast<double>(timeTicks) / static_cast<double>(kTicksPerSec);
        m.label = label;
        m.number = static_cast<int>(number);
        result->markers.push_back(m);
    }

    std::sort(result->markers.begin(), result->markers.end(),
              [](const VegMarkerInfo &a, const VegMarkerInfo &b) {
                  if (a.timeSec != b.timeSec) {
                      return a.timeSec < b.timeSec;
                  }
                  return a.number < b.number;
              });
}

VegOpenResult VegReader::open(const QString &path, QString *error)
{
    VegOpenResult result;
    result.sourcePath = path;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("Cannot open file: %1").arg(file.errorString());
        }
        return result;
    }
    const QByteArray data = file.readAll();
    file.close();

    if (!looksLikeVeg(data)) {
        if (error) {
            *error = QStringLiteral("Not a VEGAS Pro .veg file (bad magic/header).");
        }
        return result;
    }

    parseHeader(data, &result);
    parseUtf16Metadata(data, &result);
    parseTimelineEvents(data, &result);
    parseMarkers(data, &result);
    parseTrackMotion(data, &result);
    parsePanCrop(data, &result);
    assignEventNames(&result);

    return result;
}

void VegReader::parseTrackMotion(const QByteArray &data, VegOpenResult *result)
{
    if (!result || data.size() < 0x200) {
        return;
    }
    // GUID pair identifying Track Motion list payload (Vegas Pro 22 sample).
    static const uchar kGuid1[16] = {0x87, 0xf9, 0x4d, 0xd2, 0x42, 0x18, 0x39, 0x4a,
                                     0xb0, 0x79, 0x9e, 0x43, 0x79, 0x48, 0x5a, 0x38};
    static const uchar kGuid2[16] = {0xd8, 0x15, 0x69, 0xcd, 0x7a, 0x11, 0xd6, 0x45,
                                     0x8c, 0xcd, 0xbe, 0x37, 0xa6, 0xc3, 0xbc, 0x9d};
    const QByteArray g1(reinterpret_cast<const char *>(kGuid1), 16);
    int pos = 0;
    while (pos + 64 < data.size()) {
        const int found = data.indexOf(g1, pos);
        if (found < 0 || found + 48 >= data.size()) {
            break;
        }
        if (data.mid(found + 16, 16)
            != QByteArray(reinterpret_cast<const char *>(kGuid2), 16)) {
            pos = found + 1;
            continue;
        }

        // After GUID pair: miscellaneous header, then u32 motionCount + u64 recordSize(220)
        int p = found + 32;
        int motionCount = -1;
        int recOff = -1;
        for (int scan = p; scan + 12 < data.size() && scan < p + 80; scan += 4) {
            const quint32 cnt = qFromLittleEndian<quint32>(
                reinterpret_cast<const uchar *>(data.constData() + scan));
            const quint64 sz = qFromLittleEndian<quint64>(
                reinterpret_cast<const uchar *>(data.constData() + scan + 4));
            if (cnt >= 1 && cnt <= 512 && sz == 220) {
                motionCount = int(cnt);
                recOff = scan + 4; // first record starts at size field
                break;
            }
        }
        if (motionCount < 0 || recOff < 0) {
            pos = found + 1;
            continue;
        }

        TrackMotionState motion;
        const double aspect = (result->header.width > 0 && result->header.height > 0)
                                  ? double(result->header.width) / double(result->header.height)
                                  : (16.0 / 9.0);

        int off = recOff;
        for (int i = 0; i < motionCount; ++i) {
            if (off + 220 > data.size()) {
                break;
            }
            const uchar *r = reinterpret_cast<const uchar *>(data.constData() + off);
            const quint64 sz = qFromLittleEndian<quint64>(r);
            if (sz != 220) {
                break;
            }
            TrackMotionKeyframe kf = TrackMotionState::identityKeyframe(aspect);
            kf.timeSec = double(qFromLittleEndian<quint64>(r + 8)) / double(kTicksPerSec);
            kf.positionX = qFromLittleEndian<double>(r + 48);
            kf.positionY = qFromLittleEndian<double>(r + 56);
            kf.width = qFromLittleEndian<double>(r + 80);
            kf.height = qFromLittleEndian<double>(r + 88);
            if (kf.width < 1e-6) {
                kf.width = aspect;
            }
            if (kf.height < 1e-6) {
                kf.height = 1.0;
            }
            kf.smoothness = qFromLittleEndian<double>(r + 96);
            kf.rotationZ = qFromLittleEndian<double>(r + 144);
            kf.orientationZ = qFromLittleEndian<double>(r + 168);
            const double typeCode = qFromLittleEndian<double>(r + 104);
            // Vegas stores type loosely; 1.0 ≈ Linear in our sample
            if (typeCode > 1.5) {
                kf.type = VideoKeyframeType::Smooth;
            } else {
                kf.type = VideoKeyframeType::Linear;
            }
            motion.motionKeyframes.push_back(kf);
            off += 220;
        }

        auto parseFxChannel = [&](QVector<TrackFXKeyframe> *out) -> bool {
            if (off + 4 > data.size()) {
                return false;
            }
            const quint32 count = qFromLittleEndian<quint32>(
                reinterpret_cast<const uchar *>(data.constData() + off));
            if (count == 0 || count > 512) {
                return false;
            }
            // Next should be u64 size 216
            if (off + 12 > data.size()) {
                return false;
            }
            const quint64 sz0 = qFromLittleEndian<quint64>(
                reinterpret_cast<const uchar *>(data.constData() + off + 4));
            if (sz0 != 216) {
                return false;
            }
            off += 4; // point at first record size
            for (quint32 i = 0; i < count; ++i) {
                if (off + 216 > data.size()) {
                    break;
                }
                const uchar *r = reinterpret_cast<const uchar *>(data.constData() + off);
                if (qFromLittleEndian<quint64>(r) != 216) {
                    break;
                }
                TrackFXKeyframe kf;
                kf.timeSec = double(qFromLittleEndian<quint64>(r + 8)) / double(kTicksPerSec);
                kf.positionX = qFromLittleEndian<double>(r + 48);
                kf.positionY = qFromLittleEndian<double>(r + 56);
                kf.width = qFromLittleEndian<double>(r + 80);
                kf.height = qFromLittleEndian<double>(r + 88);
                kf.blur = qFromLittleEndian<double>(r + 192);
                if (!(kf.blur > 0.0) || kf.blur > 10.0) {
                    kf.blur = 0.05;
                }
                // Optional RGB triplet (float) just before blur double
                const float rC = qFromLittleEndian<float>(r + 180);
                const float gC = qFromLittleEndian<float>(r + 184);
                const float bC = qFromLittleEndian<float>(r + 188);
                if (rC >= 0.f && rC <= 1.05f && gC >= 0.f && gC <= 1.05f && bC >= 0.f
                    && bC <= 1.05f) {
                    kf.color = QColor::fromRgbF(std::clamp(double(rC), 0.0, 1.0),
                                                std::clamp(double(gC), 0.0, 1.0),
                                                std::clamp(double(bC), 0.0, 1.0));
                    kf.intensity = (rC + gC + bC) / 3.0 * 100.0;
                }
                out->push_back(kf);
                off += 216;
            }
            return !out->isEmpty();
        };

        if (parseFxChannel(&motion.shadowKeyframes)) {
            motion.shadowEnabled = true;
        }
        if (parseFxChannel(&motion.glowKeyframes)) {
            motion.glowEnabled = true;
        }

        if (!motion.motionKeyframes.isEmpty()) {
            result->trackMotion = motion;
            result->hasTrackMotion = true;
            return;
        }
        pos = found + 1;
    }
}

void VegReader::parsePanCrop(const QByteArray &data, VegOpenResult *result)
{
    if (!result || data.size() < 0x200) {
        return;
    }
    // Event Pan/Crop: LIST/EPCP … KFRS … LIST/POSL then POSK; optional LIST/MSKL … MSKK/PATH/ANCP.
    const QByteArray poslTag("POSL");
    int pos = 0;
    while (pos + 128 < data.size()) {
        const int posl = data.indexOf(poslTag, pos);
        if (posl < 0 || posl + 12 >= data.size()) {
            break;
        }
        const int epcp = data.lastIndexOf("EPCP", posl);
        if (epcp < 0 || posl - epcp > 0x800) {
            pos = posl + 1;
            continue;
        }

        EventPanCropState state;
        const int fw = result->header.width > 0 ? result->header.width : 1920;
        const int fh = result->header.height > 0 ? result->header.height : 1080;

        int off = posl + 4; // after POSL — next is usually first POSK
        while (off + 92 <= data.size()) {
            if (data.mid(off, 4) != QByteArray("POSK")) {
                break;
            }
            const quint32 sz = qFromLittleEndian<quint32>(
                reinterpret_cast<const uchar *>(data.constData() + off + 4));
            if (sz != 84) {
                break;
            }
            PanCropKeyframe kf = EventPanCropState::identityKeyframe(fw, fh);
            kf.timeSec = double(qFromLittleEndian<quint64>(
                             reinterpret_cast<const uchar *>(data.constData() + off + 8)))
                         / double(kTicksPerSec);
            const uchar *g = reinterpret_cast<const uchar *>(data.constData() + off + 0x1C);
            auto rd = [&](int i) {
                double v = 0.0;
                memcpy(&v, g + i * 8, sizeof(double));
                return v;
            };
            kf.width = rd(0);
            kf.height = rd(1);
            kf.xCenter = rd(2);
            kf.yCenter = rd(3);
            kf.angleDeg = rd(4);
            kf.rotationXCenter = rd(5);
            kf.rotationYCenter = rd(6);
            kf.smoothness = rd(7);
            state.positionKeyframes.push_back(kf);
            off += 92; // 4 + 4 + 84
        }

        // Mask: LIST/MSKL inside the same EPCP block.
        const int mskl = data.indexOf("MSKL", epcp);
        if (mskl > epcp && mskl < epcp + 0x8000) {
            // LIST size sits 8 bytes before MSKL type fourcc: LIST + size + MSKL.
            int listOff = mskl - 8;
            if (listOff >= 0 && data.mid(listOff, 4) == QByteArray("LIST")) {
                const quint32 listSize = qFromLittleEndian<quint32>(
                    reinterpret_cast<const uchar *>(data.constData() + listOff + 4));
                const int listEnd = listOff + 8 + int(listSize);
                auto parseAncl = [&](int anclTypeOff, int anclEnd, MaskPath *path) {
                    int a = anclTypeOff + 4; // after ANCL
                    while (a + 8 <= anclEnd) {
                        if (data.mid(a, 4) != QByteArray("ANCP")) {
                            break;
                        }
                        const quint32 asz = qFromLittleEndian<quint32>(
                            reinterpret_cast<const uchar *>(data.constData() + a + 4));
                        if (asz < 56 || a + 8 + int(asz) > anclEnd) {
                            break;
                        }
                        const uchar *p = reinterpret_cast<const uchar *>(data.constData() + a + 8);
                        // idx(u32) flags(u32) + 6 absolute doubles: x,y, in, out
                        auto rdAbs = [&](int i) {
                            double v = 0.0;
                            memcpy(&v, p + 8 + i * 8, sizeof(double));
                            return v;
                        };
                        MaskAnchor an;
                        an.x = rdAbs(0);
                        an.y = rdAbs(1);
                        an.inX = rdAbs(2) - an.x;
                        an.inY = rdAbs(3) - an.y;
                        an.outX = rdAbs(4) - an.x;
                        an.outY = rdAbs(5) - an.y;
                        path->anchors.push_back(an);
                        a += 8 + int(asz);
                    }
                };
                auto parsePath = [&](int pathTypeOff, int pathEnd, MaskKeyframe *mk) {
                    int p = pathTypeOff + 4; // after PATH
                    MaskPath path;
                    path.closed = true;
                    path.mode = MaskPathMode::Positive;
                    while (p + 8 <= pathEnd) {
                        if (data.mid(p, 4) == QByteArray("PTHB")) {
                            const quint32 psz = qFromLittleEndian<quint32>(
                                reinterpret_cast<const uchar *>(data.constData() + p + 4));
                            if (psz >= 44) {
                                const uchar *b =
                                    reinterpret_cast<const uchar *>(data.constData() + p + 8);
                                const quint32 closed =
                                    qFromLittleEndian<quint32>(b + 40);
                                path.closed = (closed != 0);
                                const quint32 modeHint =
                                    qFromLittleEndian<quint32>(b + 24);
                                // Vegas samples use 3 for Positive in this field.
                                if (modeHint == 1) {
                                    path.mode = MaskPathMode::Negative;
                                } else if (modeHint == 2) {
                                    path.mode = MaskPathMode::Disabled;
                                } else {
                                    path.mode = MaskPathMode::Positive;
                                }
                            }
                            p += 8 + int(psz);
                            continue;
                        }
                        if (data.mid(p, 4) == QByteArray("LIST") && p + 12 <= pathEnd
                            && data.mid(p + 8, 4) == QByteArray("ANCL")) {
                            const quint32 lsz = qFromLittleEndian<quint32>(
                                reinterpret_cast<const uchar *>(data.constData() + p + 4));
                            parseAncl(p + 8, p + 8 + int(lsz), &path);
                            p += 8 + int(lsz);
                            continue;
                        }
                        break;
                    }
                    if (!path.anchors.isEmpty()) {
                        mk->paths.push_back(path);
                    }
                };
                auto parsePthl = [&](int pthlTypeOff, int pthlEnd, MaskKeyframe *mk) {
                    int p = pthlTypeOff + 4;
                    while (p + 12 <= pthlEnd) {
                        if (data.mid(p, 4) != QByteArray("LIST")
                            || data.mid(p + 8, 4) != QByteArray("PATH")) {
                            break;
                        }
                        const quint32 lsz = qFromLittleEndian<quint32>(
                            reinterpret_cast<const uchar *>(data.constData() + p + 4));
                        parsePath(p + 8, p + 8 + int(lsz), mk);
                        p += 8 + int(lsz);
                    }
                };
                auto parseMskk = [&](int mskkTypeOff, int mskkEnd) {
                    MaskKeyframe mk;
                    mk.timeSec = 0.0;
                    int p = mskkTypeOff + 4; // after MSKK
                    while (p + 8 <= mskkEnd) {
                        if (data.mid(p, 4) == QByteArray("MSKB")) {
                            const quint32 sz = qFromLittleEndian<quint32>(
                                reinterpret_cast<const uchar *>(data.constData() + p + 4));
                            if (sz >= 8) {
                                mk.timeSec =
                                    double(qFromLittleEndian<quint64>(
                                        reinterpret_cast<const uchar *>(data.constData() + p
                                                                        + 8)))
                                    / double(kTicksPerSec);
                            }
                            p += 8 + int(sz);
                            continue;
                        }
                        if (data.mid(p, 4) == QByteArray("LIST") && p + 12 <= mskkEnd
                            && data.mid(p + 8, 4) == QByteArray("PTHL")) {
                            const quint32 lsz = qFromLittleEndian<quint32>(
                                reinterpret_cast<const uchar *>(data.constData() + p + 4));
                            parsePthl(p + 8, p + 8 + int(lsz), &mk);
                            p += 8 + int(lsz);
                            continue;
                        }
                        break;
                    }
                    state.maskKeyframes.push_back(mk);
                };

                int p = mskl + 4; // after MSKL type inside LIST
                while (p + 12 <= listEnd) {
                    if (data.mid(p, 4) != QByteArray("LIST")
                        || data.mid(p + 8, 4) != QByteArray("MSKK")) {
                        break;
                    }
                    const quint32 lsz = qFromLittleEndian<quint32>(
                        reinterpret_cast<const uchar *>(data.constData() + p + 4));
                    parseMskk(p + 8, p + 8 + int(lsz));
                    p += 8 + int(lsz);
                }
                if (!state.maskKeyframes.isEmpty()) {
                    state.maskEnabled = true;
                }
            }
        }

        if (!state.positionKeyframes.isEmpty() || !state.maskKeyframes.isEmpty()) {
            if (state.positionKeyframes.isEmpty()) {
                state.positionKeyframes.push_back(EventPanCropState::identityKeyframe(fw, fh));
            }
            result->eventPanCrop = state;
            result->hasEventPanCrop = true;
            return;
        }
        pos = posl + 1;
    }
}

} // namespace openvegas
