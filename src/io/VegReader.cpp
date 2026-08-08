#include "io/VegReader.h"
#include "video/VideoKeyframeEval.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <climits>
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
        {720, 1280},  {576, 1024},  {1440, 1080}, {720, 480},   {720, 576},
        {640, 480},   {4096, 2160},
    };

    // Step 2: Vegas often stores width/height on a 2-byte boundary after a UTF-16
    // path (e.g. 640×480 @ 0x242 in 4:3 preview projects). A 4-byte stride misses them.
    for (int off = 0x40; off + 8 < data.size(); off += 2) {
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

QByteArray utf16LeBytes(const QString &s)
{
    QByteArray out;
    out.resize(s.size() * 2);
    for (int i = 0; i < s.size(); ++i) {
        const ushort c = s.at(i).unicode();
        out[i * 2] = char(c & 0xff);
        out[i * 2 + 1] = char((c >> 8) & 0xff);
    }
    return out;
}

/** Decode a UTF-16LE C-string starting at `bytePos` (must point at first char). */
QString decodeUtf16zAt(const QByteArray &data, int bytePos, int maxChars = 256)
{
    QString s;
    const uchar *base = reinterpret_cast<const uchar *>(data.constData());
    for (int n = 0; bytePos + 1 < data.size() && n < maxChars; ++n, bytePos += 2) {
        const ushort c = qFromLittleEndian<ushort>(base + bytePos);
        if (c == 0) {
            break;
        }
        if (!isPrintableUtf16Char(c) && c != ushort('{') && c != ushort('}')
            && c != ushort(':') && c != ushort('.')) {
            // Allow typical Svfx id punctuation already covered; stop on junk.
            if (c < 32) {
                break;
            }
        }
        s.append(QChar(c));
        if (c == ushort('}')) {
            break;
        }
    }
    return s;
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

        // Video Event FX (`{Svfx:…}` / OFX XML) — recovered in recoverVideoEventFxNames.

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

        // META:\SubClip\{path} (n)[startTicks][lengthTicks][reverseFlag]
        if (s.startsWith(QLatin1String("META:\\SubClip\\"), Qt::CaseInsensitive)
            || s.startsWith(QLatin1String("META:/SubClip/"), Qt::CaseInsensitive)) {
            static const QRegularExpression subRe(
                QStringLiteral(
                    R"(META:\\SubClip\\\{([^}]+)\}[^\[]*\[(\d+)\]\[(\d+)\]\[(\d+)\])"),
                QRegularExpression::CaseInsensitiveOption);
            const auto m = subRe.match(s);
            if (m.hasMatch() && m.captured(4).toLongLong() != 0) {
                const QString base = QFileInfo(QDir::fromNativeSeparators(m.captured(1)))
                                         .fileName()
                                         .toLower();
                if (!base.isEmpty() && !result->reversedMediaBasenames.contains(base)) {
                    result->reversedMediaBasenames.push_back(base);
                }
                if (!base.isEmpty()) {
                    constexpr double kTicks = 10000000.0;
                    result->reversedSubclipStartSec.insert(
                        base, double(m.captured(2).toLongLong()) / kTicks);
                    result->reversedSubclipLengthSec.insert(
                        base, double(m.captured(3).toLongLong()) / kTicks);
                }
            }
        }
        // "file.mp4 - subclip 1 (reversed)"
        if (s.contains(QLatin1String("(reversed)"), Qt::CaseInsensitive)) {
            const int dash = s.indexOf(QLatin1String(" - "));
            const QString base =
                (dash > 0 ? s.left(dash) : s).trimmed().toLower();
            if (!base.isEmpty() && !result->reversedMediaBasenames.contains(base)) {
                result->reversedMediaBasenames.push_back(base);
            }
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

    recoverVideoEventFxNames(data, result);
    recoverVideoTrackFxNames(data, result);

    if (result->mediaPaths.isEmpty()) {
        result->warnings << QStringLiteral(
            "No media file paths found in .veg (empty pool or unsupported layout).");
    }
}

void VegReader::recoverVideoEventFxNames(const QByteArray &data, VegOpenResult *result)
{
    /*
     * Vegas stores Video Event FX as `{Svfx:pluginId}` plus, for some VelvetMatter
     * plugs, ASCII OFX XML (`<Glint>…`) without a matching Svfx string.
     *
     * Magix AI (e.g. `{Svfx:de.magix:autoframe}`) often appears before `CountEventFXs`
     * as media/project AI reframe — not in the Video Event FX chain (Pan/Crop + OFX).
     *
     * Orphan Soft Contrast state can sit after a mismatched `{Svfx:…:sepia}` (`<Softlight>`
     * XML + «Soft Moderate Contrast»); Vegas Event FX UI does not list that as Sepia.
     */
    result->eventFxNames.clear();

    const QByteArray countMarker = utf16LeBytes(QStringLiteral("CountEventFXs"));
    const int countPos = data.indexOf(countMarker);
    const int regionStart = countPos >= 0 ? countPos : 0;

    struct Item {
        int pos = 0;
        QString raw;
        QString key;
    };
    QVector<Item> items;

    const QByteArray svfxPrefix = utf16LeBytes(QStringLiteral("{Svfx:"));
    const QByteArray ofxPrefix = utf16LeBytes(QStringLiteral("OFX:"));

    auto pluginKey = [](QString raw) {
        raw = raw.trimmed();
        if (raw.startsWith(QLatin1String("{Svfx:"), Qt::CaseInsensitive)) {
            const int colon = raw.indexOf(QLatin1Char(':'));
            const int end = raw.indexOf(QLatin1Char('}'));
            if (colon >= 0) {
                raw = raw.mid(colon + 1, end > colon ? end - colon - 1 : -1);
            }
        } else if (raw.startsWith(QLatin1String("OFX:"), Qt::CaseInsensitive)) {
            raw = raw.mid(4);
        }
        return raw.trimmed().toLower();
    };

    auto pushUnique = [&](int pos, const QString &raw) {
        const QString key = pluginKey(raw);
        if (key.isEmpty()) {
            return;
        }
        for (const Item &it : items) {
            if (it.key == key) {
                return;
            }
        }
        items.push_back(Item{pos, raw, key});
    };

    auto scanPrefixed = [&](const QByteArray &prefix) {
        int pos = 0;
        while (true) {
            pos = data.indexOf(prefix, pos);
            if (pos < 0) {
                break;
            }
            const QString raw = decodeUtf16zAt(data, pos);
            pos += qMax(2, raw.size() * 2);
            if (raw.size() < 5) {
                continue;
            }
            if (raw.contains(QLatin1String("colorgrading"), Qt::CaseInsensitive)) {
                continue;
            }
            // Magix AI Auto Frame / Colorization / … ≠ Video Event FX chain.
            if (raw.contains(QLatin1String("de.magix:"), Qt::CaseInsensitive)) {
                continue;
            }
            if (countPos >= 0 && pos - raw.size() * 2 < countPos) {
                continue;
            }
            // `{Svfx:…:sepia}` followed by Soft Contrast `<Softlight>` XML → not Sepia.
            if (pluginKey(raw).contains(QLatin1String("sepia"))) {
                const int after = pos;
                const int soft = data.indexOf("<Softlight", after);
                const int nextSvfx = data.indexOf(svfxPrefix, after);
                if (soft >= 0 && soft < after + 5000
                    && (nextSvfx < 0 || soft < nextSvfx)) {
                    continue;
                }
            }
            pushUnique(pos - raw.size() * 2, raw);
        }
    };
    scanPrefixed(svfxPrefix);
    scanPrefixed(ofxPrefix);

    // Glint often has no `{Svfx:…glint…}` — only `<Glint xmlns=…>` keyframe XML.
    {
        const int glintPos = data.indexOf("<Glint", regionStart);
        if (glintPos >= 0) {
            pushUnique(glintPos,
                       QStringLiteral("{Svfx:com.vegascreativesoftware:glintvelvetmatter}"));
        }
    }

    std::sort(items.begin(), items.end(),
              [](const Item &a, const Item &b) { return a.pos < b.pos; });
    for (const Item &it : items) {
        result->eventFxNames.push_back(it.raw);
    }
}

void VegReader::recoverVideoTrackFxNames(const QByteArray &data, VegOpenResult *result)
{
    /*
     * The `{Svfx:…:sepia}` + Soft Contrast tail that recoverVideoEventFxNames()
     * deliberately excludes from the Event FX chain isn't noise — Vegas renders
     * it as a **Video Track FX** pair: Sepia + VelvetMatter Soft Contrast
     * (`com.vegascreativesoftware:softcontrastvelvetmatter`, real preset name
     * "Soft Moderate Contrast" — confirmed against the installed OFX catalog).
     * Soft Contrast has no `{Svfx:…}` string of its own in the binary, only its
     * `<Softlight>` XML state, so it's synthesized once the sepia pairing matches.
     */
    result->videoTrackFxNames.clear();

    const QByteArray svfxPrefix = utf16LeBytes(QStringLiteral("{Svfx:"));
    int pos = 0;
    while (true) {
        pos = data.indexOf(svfxPrefix, pos);
        if (pos < 0) {
            break;
        }
        const QString raw = decodeUtf16zAt(data, pos);
        const int rawBytes = qMax(2, raw.size() * 2);
        if (raw.contains(QLatin1String("sepia"), Qt::CaseInsensitive)) {
            const int after = pos + rawBytes;
            const int soft = data.indexOf("<Softlight", after);
            const int nextSvfx = data.indexOf(svfxPrefix, after);
            if (soft >= 0 && soft < after + 5000 && (nextSvfx < 0 || soft < nextSvfx)) {
                result->videoTrackFxNames.push_back(raw.trimmed());
                result->videoTrackFxNames.push_back(QStringLiteral(
                    "{Svfx:com.vegascreativesoftware:softcontrastvelvetmatter}"));
                break; // one Track FX tail per file (MVP — matches known samples)
            }
        }
        pos += rawBytes;
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
        ev.offset = off - 8;
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
    parseColorGrading(data, &result);
    parseVideoTitlesText(data, &result);
    parseFxStateChunks(data, &result);
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
            // Stored on disk as turns (1.0 == one full 360° revolution, indistinguishable from
            // no rotation), not radians — convert to radians here so the rest of the app (which
            // treats TrackMotionKeyframe::rotationZ/orientationZ as radians throughout, see
            // TrackMotionDialog's kRadToDeg/kDegToRad) doesn't need special-casing.
            kf.rotationZ = qFromLittleEndian<double>(r + 144) * 2.0 * M_PI;
            kf.orientationZ = qFromLittleEndian<double>(r + 168) * 2.0 * M_PI;
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
            const uchar *base =
                reinterpret_cast<const uchar *>(data.constData() + off);
            kf.timeSec = double(qFromLittleEndian<quint64>(base + 8)) / double(kTicksPerSec);
            // POSK payload: time(u64) unk(u64) type(u32) + 8×double geometry @ +0x1C.
            const int typeCode = int(qFromLittleEndian<quint32>(base + 0x18));
            kf.type = videoKeyframeTypeFromVegasCode(typeCode);
            const uchar *g = base + 0x1C;
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

void VegReader::parseColorGrading(const QByteArray &data, VegOpenResult *result)
{
    if (!result || data.size() < 64) {
        return;
    }

    auto toUtf16Le = [](const QString &s) -> QByteArray {
        QByteArray n;
        n.resize(s.size() * 2);
        for (int i = 0; i < s.size(); ++i) {
            const ushort c = s.at(i).unicode();
            n[i * 2] = char(c & 0xFF);
            n[i * 2 + 1] = char((c >> 8) & 0xFF);
        }
        return n;
    };

    if (!data.contains(
            toUtf16Le(QStringLiteral("{Svfx:com.vegascreativesoftware:colorgrading}")))) {
        return;
    }
    result->hasColorGrading = true;

    auto findUtf16 = [&](const QString &name) -> int { return data.indexOf(toUtf16Le(name)); };

    /** After UTF-16 name + NUL, Vegas stores the OFX param double at offset 0. */
    auto readParamDouble = [&](const QString &name, bool *ok) -> double {
        const int at = findUtf16(name);
        if (at < 0) {
            if (ok) {
                *ok = false;
            }
            return 0.0;
        }
        int end = at + name.size() * 2;
        if (end + 2 > data.size() || data.at(end) != '\0' || data.at(end + 1) != '\0') {
            // still try reading immediately after the matched name bytes
        } else {
            end += 2;
        }
        if (end + 8 > data.size()) {
            if (ok) {
                *ok = false;
            }
            return 0.0;
        }
        const double v = qFromLittleEndian<double>(
            reinterpret_cast<const uchar *>(data.constData() + end));
        if (!std::isfinite(v)) {
            if (ok) {
                *ok = false;
            }
            return 0.0;
        }
        if (ok) {
            *ok = true;
        }
        return v;
    };

    auto putWheel = [&](const QString &vegasPrefix, const QString &key) {
        static const char *chans[] = {"R", "G", "B", "Y"};
        static const char *outs[] = {"r", "g", "b", "y"};
        for (int i = 0; i < 4; ++i) {
            bool ok = false;
            const double v =
                readParamDouble(vegasPrefix + QLatin1Char('_') + QLatin1String(chans[i]), &ok);
            if (ok) {
                result->colorGradingParams.insert(key + QLatin1Char('.') + QLatin1String(outs[i]),
                                                  v);
            }
        }
    };

    putWheel(QStringLiteral("Lift"), QStringLiteral("lift"));
    putWheel(QStringLiteral("Gamma"), QStringLiteral("gamma"));
    putWheel(QStringLiteral("Gain"), QStringLiteral("gain"));
    putWheel(QStringLiteral("LogWheel_Offset"), QStringLiteral("offset"));

    // Curve blob: "N:x0:y0:inX:inY:outX:outY:x1:y1:…" (6 floats per point after count).
    // Prefer Curve_Y (luma); fall back to Curve_R.
    auto parseCurveBlob = [&](const QString &curveName) -> QVariantList {
        const int at = findUtf16(curveName);
        if (at < 0) {
            return {};
        }
        int end = at + curveName.size() * 2;
        if (end + 2 <= data.size() && data.at(end) == '\0' && data.at(end + 1) == '\0') {
            end += 2;
        }
        // Skip small binary header until ASCII digit of the blob (UTF-16 digit).
        int p = end;
        while (p + 1 < data.size() && p < end + 64) {
            if (data.at(p + 1) == '\0' && data.at(p) >= '0' && data.at(p) <= '9') {
                break;
            }
            ++p;
        }
        QString blob;
        while (p + 1 < data.size() && data.at(p + 1) == '\0') {
            const uchar c = uchar(data.at(p));
            if (!((c >= '0' && c <= '9') || c == '.' || c == '-' || c == ':' || c == '+')) {
                break;
            }
            blob.append(QChar(c));
            p += 2;
        }
        const QStringList parts = blob.split(QLatin1Char(':'));
        if (parts.size() < 7) {
            return {};
        }
        bool okCount = false;
        const int count = parts[0].toInt(&okCount);
        if (!okCount || count < 2) {
            return {};
        }
        QVariantList pts;
        int idx = 1;
        for (int i = 0; i < count && idx + 1 < parts.size(); ++i) {
            bool okX = false;
            bool okY = false;
            const double x = parts[idx].toDouble(&okX);
            const double y = parts[idx + 1].toDouble(&okY);
            idx += 6; // x,y + 4 tangent components
            if (!okX || !okY) {
                continue;
            }
            QVariantMap pt;
            pt.insert(QStringLiteral("x"), x);
            pt.insert(QStringLiteral("y"), y);
            pts.push_back(pt);
        }
        return pts;
    };

    QVariantList curve = parseCurveBlob(QStringLiteral("Curve_Y"));
    if (curve.size() < 2) {
        curve = parseCurveBlob(QStringLiteral("Curve_R"));
    }
    if (curve.size() >= 2) {
        result->colorGradingParams.insert(QStringLiteral("curve.rgb"), curve);
    }
}

void VegReader::parseVideoTitlesText(const QByteArray &data, VegOpenResult *result)
{
    if (!result || data.size() < 64) {
        return;
    }

    auto toUtf16Le = [](const QString &s) -> QByteArray {
        QByteArray n;
        n.resize(s.size() * 2);
        for (int i = 0; i < s.size(); ++i) {
            const ushort c = s.at(i).unicode();
            n[i * 2] = char(c & 0xFF);
            n[i * 2 + 1] = char((c >> 8) & 0xFF);
        }
        return n;
    };

    const QByteArray marker =
        toUtf16Le(QStringLiteral("{Svfx:com.vegascreativesoftware:titlesandtext}"));
    if (!data.contains(marker)) {
        return;
    }

    /*
     * Each instance stores params as a sequence of named-property records:
     *   int32 valueByteLen; int32 nameByteLen; UTF-16LE name[nameByteLen] (incl. NUL);
     *   value[valueByteLen] (ends in a trailing int32, 0 in every static sample seen —
     *   likely a keyframe/curve-point count; animated params aren't handled here).
     * Vegas also echoes the plugin id a second time per instance with no properties of
     * its own (a class/preset-id reference) — findNameValue()'s window bound plus the
     * "has a decodable Text" filter below naturally skips it without an explicit rule.
     */
    constexpr int kWindow = 2000;
    auto findNameValue = [&](const QString &name, int from) -> QByteArray {
        const QByteArray nameBytes = toUtf16Le(name);
        const int searchEnd = int(std::min<qsizetype>(data.size(), from + kWindow));
        const int idx = data.indexOf(nameBytes, from);
        if (idx < 0 || idx >= searchEnd || idx < 8) {
            return {};
        }
        const auto *base = reinterpret_cast<const uchar *>(data.constData());
        const qint32 valueLen = qFromLittleEndian<qint32>(base + idx - 8);
        const qint32 nameLen = qFromLittleEndian<qint32>(base + idx - 4);
        const qint32 expectedNameLen = qint32((name.size() + 1) * 2);
        if (nameLen != expectedNameLen || valueLen < 0) {
            return {};
        }
        const int valueStart = idx + nameLen;
        if (valueStart + valueLen > data.size()) {
            return {};
        }
        return data.mid(valueStart, valueLen);
    };
    auto readDouble = [&](const QString &name, int from, double def) {
        const QByteArray v = findNameValue(name, from);
        if (v.size() < 8) {
            return def;
        }
        const double d = qFromLittleEndian<double>(reinterpret_cast<const uchar *>(v.constData()));
        return std::isfinite(d) ? d : def;
    };
    auto readInt = [&](const QString &name, int from, int def) {
        const QByteArray v = findNameValue(name, from);
        return v.size() < 4
                   ? def
                   : qFromLittleEndian<qint32>(reinterpret_cast<const uchar *>(v.constData()));
    };
    auto readBool = [&](const QString &name, int from, bool def) {
        return readInt(name, from, def ? 1 : 0) != 0;
    };
    auto readColor = [&](const QString &name, int from, QColor def) {
        const QByteArray v = findNameValue(name, from);
        if (v.size() < 32) {
            return def;
        }
        const auto *p = reinterpret_cast<const uchar *>(v.constData());
        auto c01 = [](double x) { return std::clamp(x, 0.0, 1.0); };
        QColor c;
        c.setRgbF(c01(qFromLittleEndian<double>(p)), c01(qFromLittleEndian<double>(p + 8)),
                 c01(qFromLittleEndian<double>(p + 16)), c01(qFromLittleEndian<double>(p + 24)));
        return c;
    };
    auto readString = [&](const QString &name, int from, const QString &def) {
        const QByteArray v = findNameValue(name, from);
        if (v.size() < 4) {
            return def;
        }
        const qint32 strLen =
            qFromLittleEndian<qint32>(reinterpret_cast<const uchar *>(v.constData()));
        if (strLen < 0 || 4 + strLen > v.size()) {
            return def;
        }
        QString s = QString::fromUtf16(reinterpret_cast<const char16_t *>(v.constData() + 4),
                                       strLen / 2);
        while (!s.isEmpty() && s.endsWith(QChar(u'\0'))) {
            s.chop(1);
        }
        return s;
    };

    // RTF-lite: single uniform run assumed for font/size/bold/alignment (matches the
    // dialog's own toolbar — one font, one size, Bold/Italic, one alignment); every
    // \parN-terminated segment's plain text is still joined so multi-line titles (e.g.
    // headline + subtitle, each its own run) keep their full text even though the
    // per-line formatting difference is lost.
    auto parseRtf = [](const QString &rtf, QString *outText, QString *outFont, double *outSize,
                       bool *outBold, int *outAlign) {
        *outFont = QStringLiteral("Verdana");
        *outSize = 48.0;
        *outBold = false;
        *outAlign = 0;
        outText->clear();
        if (rtf.isEmpty()) {
            return;
        }

        static const QRegularExpression fontRe(QStringLiteral("\\\\fcharset\\d+\\s+([^;]+);"));
        const auto fontMatch = fontRe.match(rtf);
        if (fontMatch.hasMatch()) {
            *outFont = fontMatch.captured(1).trimmed();
        }
        static const QRegularExpression sizeRe(QStringLiteral("\\\\fs(\\d+)"));
        const auto sizeMatch = sizeRe.match(rtf);
        if (sizeMatch.hasMatch()) {
            *outSize = sizeMatch.captured(1).toDouble() / 2.0;
        }
        if (rtf.contains(QLatin1String("\\b ")) || rtf.contains(QLatin1String("\\b\\"))) {
            *outBold = true;
        }
        if (rtf.contains(QLatin1String("\\qc"))) {
            *outAlign = 1;
        } else if (rtf.contains(QLatin1String("\\qr"))) {
            *outAlign = 2;
        }

        QString body = rtf;
        const int tblStart = body.indexOf(QLatin1String("{\\fonttbl"));
        if (tblStart >= 0) {
            int depth = 0;
            int i = tblStart;
            for (; i < body.size(); ++i) {
                if (body.at(i) == QLatin1Char('{')) {
                    ++depth;
                } else if (body.at(i) == QLatin1Char('}')) {
                    --depth;
                    if (depth == 0) {
                        ++i;
                        break;
                    }
                }
            }
            body.remove(tblStart, i - tblStart);
        }

        // Word-boundary-anchored: a plain "\par" substring replace would also match
        // (and mangle) "\pard", which starts with the same four characters.
        static const QRegularExpression parBreak(QStringLiteral("\\\\par\\b"));
        body.replace(parBreak, QStringLiteral("\n"));
        static const QRegularExpression ctrlWord(QStringLiteral("\\\\[a-zA-Z]+-?\\d*\\s?"));
        body.remove(ctrlWord);
        body.remove(QLatin1Char('{'));
        body.remove(QLatin1Char('}'));
        body.remove(QLatin1Char('\r'));
        *outText = body.trimmed();
    };

    // Every marker occurrence with a decodable "Text" property nearby is a real
    // instance; the plugin-id echo (see comment above) has none and is skipped.
    QVector<int> instanceOffsets;
    for (int pos = 0;;) {
        const int idx = data.indexOf(marker, pos);
        if (idx < 0) {
            break;
        }
        pos = idx + 1;
        if (!findNameValue(QStringLiteral("Text"), idx).isEmpty()) {
            instanceOffsets.push_back(idx);
        }
    }
    if (instanceOffsets.isEmpty()) {
        return;
    }

    // Video-kind timeline records, ascending by offset, excluding gross length outliers
    // (the full-span background video clip this demo project's titles sit over — not a
    // title itself, and far longer than any of them).
    QVector<VegEventInfo> videoTimings;
    for (const VegEventInfo &ev : result->events) {
        if (ev.kind == VegEventInfo::Kind::Video && ev.offset >= 0) {
            videoTimings.push_back(ev);
        }
    }
    std::sort(videoTimings.begin(), videoTimings.end(),
              [](const VegEventInfo &a, const VegEventInfo &b) { return a.offset < b.offset; });
    if (videoTimings.size() > 2) {
        QVector<double> lens;
        lens.reserve(videoTimings.size());
        for (const VegEventInfo &e : videoTimings) {
            lens.push_back(e.lengthSec);
        }
        std::sort(lens.begin(), lens.end());
        const double median = lens[lens.size() / 2];
        QVector<VegEventInfo> filtered;
        for (const VegEventInfo &e : videoTimings) {
            if (e.lengthSec <= median * 3.0 + 1.0) {
                filtered.push_back(e);
            }
        }
        if (!filtered.isEmpty()) {
            videoTimings = filtered;
        }
    }

    // Pair instances with timing records by ascending order (verified against a real
    // sample project — not a structural guarantee). Any instance beyond the available
    // timing records falls back to sequential default-length placement.
    double fallbackStart = 0.0;
    result->titlesAndText.reserve(instanceOffsets.size());
    for (int i = 0; i < instanceOffsets.size(); ++i) {
        const int off = instanceOffsets[i];
        VegTitleTextInfo t;

        const QString rtf = readString(QStringLiteral("Text"), off, QString());
        QString font, plainText;
        double fontSize = 48.0;
        bool bold = false;
        int align = 0;
        parseRtf(rtf, &plainText, &font, &fontSize, &bold, &align);
        t.text = plainText.isEmpty() ? QStringLiteral("Sample Text") : plainText;
        t.fontFamily = font;
        t.fontSize = fontSize;
        t.bold = bold;
        t.alignment = align;

        t.textColor = readColor(QStringLiteral("TextColor"), off, t.textColor);
        t.animationName = readString(QStringLiteral("AnimationName"), off, t.animationName);
        t.scale = readDouble(QStringLiteral("Scale"), off, t.scale);
        {
            const QByteArray loc = findNameValue(QStringLiteral("Location"), off);
            if (loc.size() >= 16) {
                const auto *p = reinterpret_cast<const uchar *>(loc.constData());
                t.locationX = qFromLittleEndian<double>(p);
                t.locationY = qFromLittleEndian<double>(p + 8);
            }
        }
        t.cropBackgroundToText =
            readBool(QStringLiteral("FitBackgroundColor"), off, t.cropBackgroundToText);
        t.backgroundColor = readColor(QStringLiteral("Background"), off, t.backgroundColor);
        t.tracking = readDouble(QStringLiteral("Tracking"), off, t.tracking);
        t.lineSpacing = readDouble(QStringLiteral("LineSpacing"), off, t.lineSpacing);
        t.outlineWidth = readDouble(QStringLiteral("OutlineWidth"), off, t.outlineWidth);
        t.outlineColor = readColor(QStringLiteral("OutlineColor"), off, t.outlineColor);
        t.shadowEnable = readBool(QStringLiteral("ShadowEnable"), off, t.shadowEnable);
        t.shadowColor = readColor(QStringLiteral("ShadowColor"), off, t.shadowColor);
        t.shadowOffsetX = readDouble(QStringLiteral("ShadowOffsetX"), off, t.shadowOffsetX);
        t.shadowOffsetY = readDouble(QStringLiteral("ShadowOffsetY"), off, t.shadowOffsetY);
        t.shadowBlur = readDouble(QStringLiteral("ShadowBlur"), off, t.shadowBlur);

        if (i < videoTimings.size()) {
            t.startSec = videoTimings[i].startSec;
            t.lengthSec = videoTimings[i].lengthSec;
        } else {
            t.startSec = fallbackStart;
            t.lengthSec = 10.0;
        }
        fallbackStart = t.startSec + t.lengthSec;

        result->titlesAndText.push_back(t);
    }
}

void VegReader::parseFxStateChunks(const QByteArray &data, VegOpenResult *result)
{
    if (!result || data.size() < 64) {
        return;
    }
    const uchar *base = reinterpret_cast<const uchar *>(data.constData());
    const int n = data.size();

    auto readBe32 = [](const uchar *p) -> qint32 {
        return qint32((quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8)
                      | quint32(p[3]));
    };

    struct ChunkHit {
        int offset = 0;
        int length = 0;
    };
    QVector<ChunkHit> hits;
    for (int i = 0; i + 8 <= n; ++i) {
        if (base[i] != 'C' || base[i + 1] != 'c' || base[i + 2] != 'n' || base[i + 3] != 'K') {
            continue;
        }
        const qint32 byteSize = readBe32(base + i + 4);
        int len = 0;
        if (byteSize > 0 && byteSize < n && i + 8 + byteSize <= n) {
            // Standard VST bank/program: byteSize is size after the size field.
            len = 8 + byteSize;
        } else if (i + 60 <= n) {
            const char m0 = char(base[i + 8]);
            const char m1 = char(base[i + 9]);
            const char m2 = char(base[i + 10]);
            const char m3 = char(base[i + 11]);
            const bool isFpCh = (m0 == 'F' && m1 == 'P' && m2 == 'C' && m3 == 'h');
            const bool isFxCk = (m0 == 'F' && m1 == 'x' && m2 == 'C' && m3 == 'k');
            if (isFpCh) {
                const qint32 chunkSize = readBe32(base + i + 56);
                if (chunkSize > 0 && chunkSize < 8 * 1024 * 1024 && i + 60 + chunkSize <= n) {
                    len = 60 + chunkSize;
                }
            } else if (isFxCk) {
                // Param program — extend to next CcnK or cap.
                int end = n;
                for (int j = i + 12; j + 4 <= n; ++j) {
                    if (base[j] == 'C' && base[j + 1] == 'c' && base[j + 2] == 'n'
                        && base[j + 3] == 'K') {
                        end = j;
                        break;
                    }
                }
                len = std::min(end - i, 64 * 1024);
            }
        }
        if (len >= 60) {
            hits.push_back({i, len});
            i += len - 1;
        }
    }
    if (hits.isEmpty()) {
        return;
    }

    // Candidate FX names from UTF-16 recovery (audio event FX / track FX / assignable).
    QStringList names = result->audioEventFxNames;
    names += result->trackFxNames;
    names += result->mixerAssignableFxPlugins;
    names += result->eventFxNames;
    auto cleanName = [](QString s) -> QString {
        // Strip " (VST2, 64 Bit)" style suffixes for matching.
        const int paren = s.indexOf(QLatin1Char('('));
        if (paren > 0) {
            s = s.left(paren).trimmed();
        }
        if (s.startsWith(QLatin1String("VEGAS Track "), Qt::CaseInsensitive)) {
            s = s.mid(12).trimmed();
        }
        return s;
    };

    auto toUtf16Le = [](const QString &s) -> QByteArray {
        QByteArray n;
        n.resize(s.size() * 2);
        for (int i = 0; i < s.size(); ++i) {
            const ushort c = s.at(i).unicode();
            n[i * 2] = char(c & 0xFF);
            n[i * 2 + 1] = char((c >> 8) & 0xFF);
        }
        return n;
    };

    for (const QString &raw : names) {
        const QString name = cleanName(raw);
        if (name.size() < 2) {
            continue;
        }
        const QByteArray needle = toUtf16Le(name);
        int pos = 0;
        int bestDelta = INT_MAX;
        int bestHit = -1;
        while (true) {
            const int at = data.indexOf(needle, pos);
            if (at < 0) {
                break;
            }
            for (int hi = 0; hi < hits.size(); ++hi) {
                const int delta = hits[hi].offset - at;
                if (delta >= 0 && delta < bestDelta && delta < 4096) {
                    bestDelta = delta;
                    bestHit = hi;
                }
            }
            pos = at + needle.size();
        }
        if (bestHit >= 0) {
            const ChunkHit &h = hits[bestHit];
            result->fxStateChunks.insert(name.toLower(), data.mid(h.offset, h.length));
        }
    }

    if (!result->fxStateChunks.isEmpty()) {
        result->warnings << QStringLiteral("Recovered %1 FX state chunk(s) (best-effort CcnK).")
                                .arg(result->fxStateChunks.size());
    }
}

} // namespace openvegas
