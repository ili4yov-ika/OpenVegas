#include "io/VegOfxParams.h"

#include <QVariantList>
#include <QtEndian>

namespace openvegas {

namespace {

/**
 * What the record looks like, taken from projects VEGAS wrote.
 *
 * Before the identifier text sit the two string lengths; then the two strings; then the
 * parameter block. All lengths are in bytes and all text is UTF-16LE with a terminator
 * counted in the length.
 *
 *     u32   idBytes                 88 for "{Svfx:com.vegascreativesoftware:chromablur}\0"
 *     u32   presetBytes             20 for "(Default)\0"
 *     char  id[idBytes]
 *     char  preset[presetBytes]
 *     u32   paramsBytes
 *     u32   paramCount
 *     per parameter:
 *         u32     valueBytes        everything after the name, which is what checks the read
 *         u32     nameBytes
 *         char    name[nameBytes]
 *         <value>                   4 bytes for an integer, else 8 per component
 *         u32     keyCount
 *         per key (52 bytes):
 *             double time           milliseconds
 *             u32    flags
 *             double value
 *             double inTime, inValue, outTime, outValue
 *
 * The handles are absolute times, not offsets: a key at t=0 has them at -0.1 and +0.1, and
 * one at t=1037.001 has them at 1036.901 and 1037.101.
 *
 * How wide the value is cannot be read anywhere — it follows from what the parameter is.
 * It does not have to be guessed, though: the count that follows the value is written
 * independently of `valueBytes`, so trying each plausible width and keeping the one where
 * the two agree identifies it. In practice only one ever fits.
 */
constexpr int kKeyframeBytes = 52;

/** Times are stored in milliseconds — 6354.293 for a key six and a third seconds in. */
constexpr double kMsToSec = 1.0 / 1000.0;

bool readU32(const QByteArray &d, int at, quint32 *out)
{
    if (at < 0 || at + 4 > d.size()) {
        return false;
    }
    *out = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(d.constData()) + at);
    return true;
}

bool readDouble(const QByteArray &d, int at, double *out)
{
    if (at < 0 || at + 8 > d.size()) {
        return false;
    }
    *out = qFromLittleEndian<double>(reinterpret_cast<const uchar *>(d.constData()) + at);
    return true;
}

/** UTF-16LE text of `bytes` length at `at`, without its terminator. */
bool readText(const QByteArray &d, int at, int bytes, QString *out)
{
    if (at < 0 || bytes < 2 || bytes % 2 != 0 || at + bytes > d.size()) {
        return false;
    }
    QString s = QString::fromUtf16(reinterpret_cast<const char16_t *>(d.constData() + at),
                                   bytes / 2);
    while (s.endsWith(QChar(u'\0'))) {
        s.chop(1);
    }
    *out = s;
    return true;
}

/**
 * Split `valueBytes` into the width of the value and the number of keyframes after it.
 *
 * Accepts the split only when the count stored in the file matches the one the size
 * implies, which is what makes this identification rather than guesswork.
 */
bool splitValueBlock(const QByteArray &d, int valueAt, quint32 valueBytes, int *widthOut,
                     quint32 *keyCountOut)
{
    // Four bytes is an integer — a choice or a flag; the rest are whole doubles, up to a
    // colour with alpha.
    for (int width : {4, 8, 16, 24, 32}) {
        const int after = int(valueBytes) - width - 4;
        if (after < 0 || after % kKeyframeBytes != 0) {
            continue;
        }
        quint32 stored = 0;
        if (!readU32(d, valueAt + width, &stored)) {
            continue;
        }
        if (stored != quint32(after / kKeyframeBytes)) {
            continue;
        }
        *widthOut = width;
        *keyCountOut = stored;
        return true;
    }
    return false;
}

QVariant readValue(const QByteArray &d, int at, int width)
{
    if (width == 4) {
        quint32 v = 0;
        if (!readU32(d, at, &v)) {
            return {};
        }
        return QVariant(int(qint32(v)));
    }
    const int parts = width / 8;
    if (parts == 1) {
        double v = 0.0;
        if (!readDouble(d, at, &v)) {
            return {};
        }
        return QVariant(v);
    }
    QVariantList list;
    for (int i = 0; i < parts; ++i) {
        double v = 0.0;
        if (!readDouble(d, at + i * 8, &v)) {
            return {};
        }
        list.push_back(v);
    }
    return list;
}

} // namespace

double VegOfxParam::scalar() const
{
    if (value.typeId() == QMetaType::QVariantList) {
        const QVariantList parts = value.toList();
        return parts.isEmpty() ? 0.0 : parts.first().toDouble();
    }
    return value.toDouble();
}

bool vegOfxDecodeEffect(const QByteArray &data, int idPos, VegOfxEffect *out)
{
    if (!out || idPos < 8) {
        return false;
    }
    quint32 idBytes = 0;
    quint32 presetBytes = 0;
    if (!readU32(data, idPos - 8, &idBytes) || !readU32(data, idPos - 4, &presetBytes)) {
        return false;
    }
    // The length in front has to match the text that follows, or this is some other
    // structure that merely happens to sit before a "{Svfx:" marker.
    if (idBytes < 4 || idBytes > 4096 || presetBytes > 4096 || idBytes % 2 || presetBytes % 2) {
        return false;
    }

    VegOfxEffect effect;
    if (!readText(data, idPos, int(idBytes), &effect.pluginId)) {
        return false;
    }
    if (!effect.pluginId.startsWith(QLatin1String("{Svfx:"), Qt::CaseInsensitive)) {
        return false;
    }
    int p = idPos + int(idBytes);
    if (presetBytes > 0) {
        if (!readText(data, p, int(presetBytes), &effect.presetName)) {
            return false;
        }
        p += int(presetBytes);
    }

    quint32 paramsBytes = 0;
    quint32 paramCount = 0;
    if (!readU32(data, p, &paramsBytes) || !readU32(data, p + 4, &paramCount)) {
        return false;
    }
    // An effect can legitimately store nothing; a thousand parameters is a misread.
    if (paramCount > 1024 || paramsBytes > quint32(data.size())) {
        return false;
    }

    int q = p + 8;
    for (quint32 i = 0; i < paramCount; ++i) {
        quint32 valueBytes = 0;
        quint32 nameBytes = 0;
        if (!readU32(data, q, &valueBytes) || !readU32(data, q + 4, &nameBytes)) {
            return false;
        }
        if (valueBytes < 8 || valueBytes > 1 << 20) {
            return false;
        }
        VegOfxParam param;
        if (!readText(data, q + 8, int(nameBytes), &param.name) || param.name.isEmpty()) {
            return false;
        }
        const int valueAt = q + 8 + int(nameBytes);
        int width = 0;
        quint32 keyCount = 0;
        if (!splitValueBlock(data, valueAt, valueBytes, &width, &keyCount)) {
            return false;
        }
        param.value = readValue(data, valueAt, width);
        if (!param.value.isValid()) {
            return false;
        }

        int r = valueAt + width + 4;
        param.keys.reserve(int(keyCount));
        for (quint32 k = 0; k < keyCount; ++k) {
            VegOfxKeyframe key;
            double t = 0.0;
            double inT = 0.0;
            double outT = 0.0;
            if (!readDouble(data, r, &t) || !readU32(data, r + 8, &key.flags)
                || !readDouble(data, r + 12, &key.value)
                || !readDouble(data, r + 20, &inT) || !readDouble(data, r + 28, &key.inValue)
                || !readDouble(data, r + 36, &outT)
                || !readDouble(data, r + 44, &key.outValue)) {
                return false;
            }
            key.timeSec = t * kMsToSec;
            key.inTimeSec = inT * kMsToSec;
            key.outTimeSec = outT * kMsToSec;
            param.keys.push_back(key);
            r += kKeyframeBytes;
        }
        effect.params.push_back(param);
        q = valueAt + int(valueBytes);
    }

    *out = effect;
    return true;
}

QVariantMap vegOfxParamMap(const VegOfxEffect &effect)
{
    QVariantMap m;
    for (const VegOfxParam &p : effect.params) {
        m.insert(p.name, p.value);
    }
    return m;
}

} // namespace openvegas
