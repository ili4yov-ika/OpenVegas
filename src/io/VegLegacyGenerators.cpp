#include "io/VegLegacyGenerators.h"

#include <QtEndian>
#include <QVariantList>

#include <cstring>

namespace openvegas {
namespace {

class Reader {
public:
    explicit Reader(const QByteArray &b)
        : m_b(b)
    {
    }
    bool has(int off, int n) const { return off >= 0 && n >= 0 && off + n <= m_b.size(); }
    qint32 i32(int off) const
    {
        return has(off, 4) ? qFromLittleEndian<qint32>(m_b.constData() + off) : 0;
    }
    float f32(int off) const
    {
        float v = 0.0f;
        if (has(off, 4)) {
            std::memcpy(&v, m_b.constData() + off, 4);
        }
        return v;
    }
    double f64(int off) const
    {
        double v = 0.0;
        if (has(off, 8)) {
            std::memcpy(&v, m_b.constData() + off, 8);
        }
        return v;
    }
    /** A colour as VEGAS's DirectX plug-ins store it — four floats, blue first — as [r,g,b,a]. */
    QVariantList bgra(int off) const
    {
        return {double(f32(off + 8)), double(f32(off + 4)), double(f32(off)), double(f32(off + 12))};
    }
    /** A fixed WCHAR field, up to its terminator. */
    QString wstr(int off, int chars) const
    {
        QString s;
        for (int i = 0; i < chars && has(off + 2 * i, 2); ++i) {
            const ushort c = qFromLittleEndian<quint16>(m_b.constData() + off + 2 * i);
            if (c == 0) {
                break;
            }
            s.append(QChar(c));
        }
        return s;
    }

private:
    const QByteArray &m_b;
};

// Color Gradient. One parameter block, 0x150 bytes; arrays are indexed from 1, slot 0 unused.
constexpr int kGradCount = 0x00;  // int: points + 1
constexpr int kGradType = 0x04;   // int
constexpr int kGradAngle = 0x08;  // double, radians
constexpr int kGradColor = 0x10;  // float[4] BGRA per point, 16 bytes each
constexpr int kGradX = 0xb0;      // double per point
constexpr int kGradY = 0x100;     // double per point
constexpr int kGradMaxPoints = 9;
constexpr int kGradBlock = 0x150;

QVariantMap colorGradient(const QByteArray &body)
{
    const Reader r(body);
    if (!r.has(0, kGradBlock)) {
        return {};
    }
    const int n = r.i32(kGradCount) - 1;
    if (n < 1 || n > kGradMaxPoints) {
        return {};
    }
    QVariantList points;
    for (int i = 1; i <= n; ++i) {
        points << QVariantMap{{QStringLiteral("X"), r.f64(kGradX + 8 * i)},
                              {QStringLiteral("Y"), r.f64(kGradY + 8 * i)},
                              {QStringLiteral("Color"), r.bgra(kGradColor + 16 * i)}};
    }
    return {{QStringLiteral("GradientType"), r.i32(kGradType)},
            {QStringLiteral("AspectAngle"), r.f64(kGradAngle)},
            {QStringLiteral("Points"), points}};
}

// Credit Roll. The fixed part is 712 bytes (its own +4 says so), then the text.
constexpr int kCrFixed = 0x04;     // int: size of the fixed part, 712
constexpr int kCrEffect = 0x08;    // int
constexpr int kCrLeft = 0x18;      // double
constexpr int kCrRight = 0x20;     // double
constexpr int kCrTop = 0x28;       // double
constexpr int kCrIn = 0x30;        // int
constexpr int kCrOut = 0x34;       // int
constexpr int kCrDisplay = 0x38;   // int
constexpr int kCrConnector = 0x3c; // WCHAR[2]: what joins a two-column line's sides
constexpr int kCrBack = 0x54;      // float[4] BGRA
constexpr int kCrDirection = 0x64; // int
constexpr int kCrStyles = 0x70;    // five records of 120 bytes
constexpr int kCrStyleSize = 120;
constexpr int kCrStyleCount = 5;

QVariantMap creditRoll(const QByteArray &body)
{
    const Reader r(body);
    const int fixed = r.i32(kCrFixed);
    if (fixed != kCrStyles + kCrStyleCount * kCrStyleSize || !r.has(0, fixed + 8)) {
        return {};
    }
    QVariantList styles;
    for (int k = 0; k < kCrStyleCount; ++k) {
        const int s = kCrStyles + k * kCrStyleSize;
        styles << QVariantMap{
            {QStringLiteral("Font"), r.wstr(s, 32)},
            {QStringLiteral("Size"), r.i32(s + 64)},
            {QStringLiteral("Bold"), r.i32(s + 68)},
            {QStringLiteral("Color"), r.bgra(s + 72)},
            {QStringLiteral("Align"), r.i32(s + 88)},
            {QStringLiteral("Tracking"), r.f64(s + 96)},
            {QStringLiteral("SpaceBelow"), r.f64(s + 104)},
            {QStringLiteral("Scale"), r.f64(s + 112)},
        };
    }
    const int textBytes = r.i32(fixed);
    QString text;
    if (textBytes > 0 && r.has(fixed + 8, textBytes)) {
        text = QString::fromUtf16(reinterpret_cast<const char16_t *>(body.constData() + fixed + 8),
                                  textBytes / 2);
        while (text.endsWith(QChar(0))) {
            text.chop(1);
        }
    }
    return {{QStringLiteral("Effect"), r.i32(kCrEffect)},
            {QStringLiteral("Left"), r.f64(kCrLeft)},
            {QStringLiteral("Right"), r.f64(kCrRight)},
            {QStringLiteral("Top"), r.f64(kCrTop)},
            {QStringLiteral("InEffect"), r.i32(kCrIn)},
            {QStringLiteral("OutEffect"), r.i32(kCrOut)},
            {QStringLiteral("Display"), r.i32(kCrDisplay)},
            {QStringLiteral("Connector"), r.wstr(kCrConnector, 2)},
            {QStringLiteral("Background"), r.bgra(kCrBack)},
            {QStringLiteral("ScrollDirection"), r.i32(kCrDirection)},
            {QStringLiteral("Styles"), styles},
            {QStringLiteral("Text"), text}};
}

} // namespace

QVariantMap vegLegacyGeneratorParams(const QString &clsid, const QByteArray &body)
{
    if (clsid.compare(vegColorGradientClsid(), Qt::CaseInsensitive) == 0) {
        return colorGradient(body);
    }
    if (clsid.compare(vegCreditRollClsid(), Qt::CaseInsensitive) == 0) {
        return creditRoll(body);
    }
    return {};
}

} // namespace openvegas
