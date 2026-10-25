#include "video/VegasGenerators.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QStringList>
#include <QVariantList>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace openvegas {
namespace {

constexpr double kPi = 3.14159265358979323846;

const QString kCheckerboard = QStringLiteral("{Svfx:com.vegascreativesoftware:checkerboard}");
const QString kNoiseTexture = QStringLiteral("{Svfx:com.vegascreativesoftware:noisetexture}");
const QString kSolidColor = QStringLiteral("{Svfx:com.vegascreativesoftware:solidcolor}");
const QString kTestPattern = QStringLiteral("{Svfx:com.vegascreativesoftware:testpattern}");
const QString kColorGradient = QStringLiteral("{8B5DEABC-53BD-49F6-9803-367478307A59}");
const QString kCreditRoll = QStringLiteral("{02FD3B60-986B-11D4-9F6F-00485481BD31}");

struct Rgba {
    double r = 0, g = 0, b = 0, a = 1;
};

double num(const QVariantMap &p, const char *key, double fallback)
{
    const QVariant v = p.value(QLatin1String(key));
    if (!v.isValid()) {
        return fallback;
    }
    if (v.typeId() == QMetaType::QVariantList) {
        const QVariantList l = v.toList();
        return l.isEmpty() ? fallback : l.first().toDouble();
    }
    return v.toDouble();
}

Rgba colour(const QVariant &v, Rgba fallback)
{
    const QVariantList l = v.toList();
    if (l.size() < 3) {
        return fallback;
    }
    auto c = [](const QVariant &x) { return std::clamp(x.toDouble(), 0.0, 1.0); };
    return {c(l[0]), c(l[1]), c(l[2]), l.size() > 3 ? c(l[3]) : 1.0};
}

Rgba colour(const QVariantMap &p, const char *key, Rgba fallback)
{
    return colour(p.value(QLatin1String(key)), fallback);
}

Rgba lerp(const Rgba &a, const Rgba &b, double t)
{
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t,
            a.a + (b.a - a.a) * t};
}

/** Straight colour to a premultiplied pixel. */
QRgb premul(const Rgba &c)
{
    const double a = std::clamp(c.a, 0.0, 1.0);
    auto ch = [a](double v) { return int(std::lround(std::clamp(v, 0.0, 1.0) * a * 255.0)); };
    return qRgba(ch(c.r), ch(c.g), ch(c.b), int(std::lround(a * 255.0)));
}

/** Premultiplied colour (already multiplied by its alpha) to a pixel. */
QRgb packPremul(double r, double g, double b, double a)
{
    auto ch = [](double v) { return int(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
    return qRgba(ch(r), ch(g), ch(b), ch(a));
}

QImage blank(const QSize &size)
{
    QImage img(size.expandedTo(QSize(1, 1)), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    return img;
}

// ---- Checkerboard ------------------------------------------------------------------------

/**
 * Which colour a position along one axis belongs to, 0 for Color 1's tiles and 1 for
 * Color 2's, with a linear blend `blend` tiles wide centred on every tile edge.
 */
double checkerAxis(double s, double blend)
{
    const double k = std::floor(s);
    const double f = s - k;
    const double base = std::fmod(std::fabs(k), 2.0) < 0.5 ? 0.0 : 1.0;
    const double half = blend * 0.5;
    if (half <= 0.0) {
        return base;
    }
    if (f < half) {
        return base + (1.0 - 2.0 * base) * (0.5 - 0.5 * f / half); // towards the tile before
    }
    if (1.0 - f < half) {
        return base + (1.0 - 2.0 * base) * (0.5 - 0.5 * (1.0 - f) / half); // the tile after
    }
    return base;
}

/**
 * checkerAxis() averaged over one pixel, `from` to `from + width` in tile units: the plug-in
 * covers a pixel that a tile edge crosses by area, so the edge pixel takes the share of each
 * side rather than whichever its centre falls in.
 */
double checkerAxisPixel(double from, double width, double blend)
{
    constexpr int kSamples = 16;
    double sum = 0.0;
    for (int i = 0; i < kSamples; ++i) {
        sum += checkerAxis(from + width * (i + 0.5) / kSamples, blend);
    }
    return sum / kSamples;
}

/**
 * VEGAS Checkerboard, as its plug-in draws it: tiles of Width × frame width and Height ×
 * frame height (a square tile is as tall in pixels as it is wide), the grid starting at the
 * top-left corner moved on by Grid offset × the frame, Color 1 in the first tile. Edge
 * blending ramps linearly across that share of a tile, centred on its edge; the two axes
 * combine as a soft exclusive-or, and the colours mix premultiplied — which is why a blind
 * of transparent black darkens as it fades rather than holding its colour.
 */
QImage checkerboard(const QVariantMap &p, const QSize &size)
{
    QImage img = blank(size);
    const int w = img.width(), h = img.height();
    const bool square = num(p, "TileDimensionsSquare", 1.0) >= 0.5;
    const double tw = std::max(1e-3, num(p, "TileDimensionsWidth", 0.1)) * w;
    const double th = square ? tw : std::max(1e-3, num(p, "TileDimensionsHeight", 0.1)) * h;
    const bool proportional = num(p, "EdgeBlendingProportional", 1.0) >= 0.5;
    const double bh = std::clamp(num(p, "EdgeBlendingHorizontal", 0.0), 0.0, 1.0);
    const double bv = proportional ? bh : std::clamp(num(p, "EdgeBlendingVertical", 0.0), 0.0, 1.0);
    const double ox = num(p, "GridOffsetX", 0.0) * w;
    const double oy = num(p, "GridOffsetY", 0.0) * h;
    const Rgba c1 = colour(p, "Color1", {0, 0, 0, 1});
    const Rgba c2 = colour(p, "Color2", {1, 1, 1, 1});
    const double p1[4] = {c1.r * c1.a, c1.g * c1.a, c1.b * c1.a, c1.a};
    const double p2[4] = {c2.r * c2.a, c2.g * c2.a, c2.b * c2.a, c2.a};

    std::vector<double> col(static_cast<size_t>(w));
    for (int x = 0; x < w; ++x) {
        col[size_t(x)] = checkerAxisPixel((x - ox) / tw, 1.0 / tw, bh);
    }
    for (int y = 0; y < h; ++y) {
        const double py = checkerAxisPixel((y - oy) / th, 1.0 / th, bv);
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const double px = col[size_t(x)];
            const double t = px * (1.0 - py) + py * (1.0 - px);
            line[x] = packPremul(p1[0] + (p2[0] - p1[0]) * t, p1[1] + (p2[1] - p1[1]) * t,
                                 p1[2] + (p2[2] - p1[2]) * t, p1[3] + (p2[3] - p1[3]) * t);
        }
    }
    return img;
}

// ---- Test Pattern ------------------------------------------------------------------------

QRgb level(int r, int g, int b)
{
    return qRgb(r, g, b);
}

/**
 * VEGAS Test Pattern. Every level is what its plug-in writes, in studio-range 8-bit values
 * (16 black, 235 white, 7 below black for the porches' surround and the pluge). The
 * layouts, measured at 1280×720 and kept as shares of the frame:
 *
 *  0/1 Color bars NTSC/PAL — seven 75% bars, each floor(width/7) wide; PAL's first is 100%
 *  2   Gradient — red falls and green rises left to right, blue rises top to bottom
 *  3   Pluge and porches — a pluge (16 | 25 | 16) from 0.2 to 0.5 of the width and porches
 *      (235, 180, 125, 70) stacked from the top between 0.5 and 0.9, all from 0.1 to 0.9 of
 *      the height, on 7
 *  4   Ramp — 25 to 230 across 0.1–0.9 of the width, 0.1–0.9 of the height, on 7
 *  5/6 SMPTE bars NTSC/PAL — bars over the top two thirds, reverse-blue castellations over
 *      the next twelfth, and a bottom quarter of −I, white, +Q (NTSC only), black and a
 *      pluge (7, 16, 25) at 45/64–54/64 of the width
 *  7/8 White/Yellow porches — four boxes (235, 180, 125, 70) side by side across 0.1–0.9,
 *      0.1–0.9 of the height, on 7; yellow keeps blue at 7
 *
 * A box's edges are inclusive on both sides in the plug-in's bottom-up pixel coordinates,
 * which puts its top one row higher than its share of the frame.
 */
QImage testPattern(const QVariantMap &p, const QSize &size)
{
    QImage img = blank(size);
    const int w = img.width(), h = img.height();
    const int type = int(std::lround(num(p, "TestPatternType", 0.0)));
    const int bar = std::max(1, w / 7);
    auto barIndex = [bar](int x) { return std::min(6, x / bar); };
    static const std::array<QRgb, 7> bars75 = {
        level(180, 180, 180), level(180, 180, 16), level(16, 180, 180), level(16, 180, 16),
        level(180, 16, 180),  level(180, 16, 16),  level(16, 16, 180)};
    static const std::array<QRgb, 7> castellations = {
        level(7, 7, 235),   level(16, 16, 16),  level(180, 7, 180), level(16, 16, 16),
        level(7, 180, 180), level(16, 16, 16), level(180, 180, 180)};
    const QRgb surround = level(7, 7, 7);
    const QRgb black = level(16, 16, 16);
    // Inclusive box test in bottom-up coordinates, as the plug-in draws.
    auto inside = [&](int x, int y, double x0, double x1, double y0, double y1) {
        const int yu = h - 1 - y;
        return x >= x0 * w && x <= x1 * w && yu >= y0 * h && yu <= y1 * h;
    };

    for (int y = 0; y < h; ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb c = surround;
            switch (type) {
            case 1:
            case 0:
                c = (type == 1 && barIndex(x) == 0) ? level(235, 235, 235) : bars75[size_t(barIndex(x))];
                break;
            case 2: {
                const double u = double(x) / w, v = double(y) / h;
                c = qRgb(int((1.0 - u) * 255.0), int(u * 255.0), int(v * 255.0));
                break;
            }
            case 3:
                if (inside(x, y, 0.2, 0.5, 0.1, 0.9)) {
                    c = x >= 0.3 * w && x < 0.4 * w ? level(25, 25, 25) : black;
                } else if (inside(x, y, 0.5, 0.9, 0.1, 0.9)) {
                    static const int porches[4] = {235, 180, 125, 70};
                    const int k = std::clamp(int((y - (0.1 * h - 1)) / (0.2 * h)), 0, 3);
                    c = level(porches[k], porches[k], porches[k]);
                }
                break;
            case 4:
                if (inside(x, y, 0.1, 0.9, 0.1, 0.9)) {
                    const int v = int(25.0 + 205.0 * (x - 0.1 * w) / (0.8 * w));
                    c = level(v, v, v);
                }
                break;
            case 5:
            case 6: {
                if (y < h * 2 / 3) {
                    c = (type == 6 && barIndex(x) == 0) ? level(235, 235, 235)
                                                         : bars75[size_t(barIndex(x))];
                } else if (y < h * 3 / 4) {
                    c = castellations[size_t(barIndex(x))];
                } else {
                    const double u = double(x) / w;
                    c = black;
                    if (type == 5 && u < 1.0 / 6.0) {
                        c = level(7, 40, 113); // −I
                    } else if (type == 5 && u < 2.0 / 6.0) {
                        c = level(235, 235, 235);
                    } else if (type == 5 && u < 0.5) {
                        c = level(70, 7, 165); // +Q
                    } else if (u >= 45.0 / 64.0 && u < 48.0 / 64.0) {
                        c = surround;
                    } else if (u >= 51.0 / 64.0 && u < 54.0 / 64.0) {
                        c = level(25, 25, 25);
                    }
                }
                break;
            }
            case 7:
            case 8:
                if (inside(x, y, 0.1, 0.9, 0.1, 0.9)) {
                    static const int porches[4] = {235, 180, 125, 70};
                    const int k = std::clamp(int((x - 0.1 * w) / (0.2 * w)), 0, 3);
                    const int v = porches[k];
                    c = level(v, v, type == 8 ? 7 : v);
                }
                break;
            default:
                c = bars75[size_t(barIndex(x))];
                break;
            }
            line[x] = c;
        }
    }
    return img;
}

// ---- Noise Texture -----------------------------------------------------------------------

/** Gradient noise in −1..1 (classic Perlin, fixed permutation). */
class Perlin {
public:
    Perlin()
    {
        unsigned s = 0x9e3779b9u;
        for (int i = 0; i < 256; ++i) {
            m_p[size_t(i)] = i;
        }
        for (int i = 255; i > 0; --i) {
            s = s * 1664525u + 1013904223u;
            std::swap(m_p[size_t(i)], m_p[size_t(s % unsigned(i + 1))]);
        }
        for (int i = 0; i < 256; ++i) {
            m_p[size_t(256 + i)] = m_p[size_t(i)];
        }
    }
    double operator()(double x, double y) const
    {
        const int xi = int(std::floor(x)) & 255, yi = int(std::floor(y)) & 255;
        const double xf = x - std::floor(x), yf = y - std::floor(y);
        const double u = fade(xf), v = fade(yf);
        const int aa = m_p[size_t(m_p[size_t(xi)] + yi)], ab = m_p[size_t(m_p[size_t(xi)] + yi + 1)];
        const int ba = m_p[size_t(m_p[size_t(xi + 1)] + yi)], bb = m_p[size_t(m_p[size_t(xi + 1)] + yi + 1)];
        const double x1 = mix(grad(aa, xf, yf), grad(ba, xf - 1, yf), u);
        const double x2 = mix(grad(ab, xf, yf - 1), grad(bb, xf - 1, yf - 1), u);
        return mix(x1, x2, v) * 1.414;
    }

private:
    static double fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
    static double mix(double a, double b, double t) { return a + (b - a) * t; }
    static double grad(int h, double x, double y)
    {
        switch (h & 7) {
        case 0: return x + y;
        case 1: return -x + y;
        case 2: return x - y;
        case 3: return -x - y;
        case 4: return x;
        case 5: return -x;
        case 6: return y;
        default: return -y;
        }
    }
    std::array<int, 512> m_p{};
};

/**
 * VEGAS Noise Texture in kind: fractal gradient noise with the plug-in's seven styles,
 * frequency, offset, layers, amplitude falloff, bias, amplitude and the min/max window
 * that maps it onto the two colours. The noise itself is ours, so a preset looks like the
 * same material rather than the same picture; VEGAS's own plug-in draws it when installed.
 */
QImage noiseTexture(const QVariantMap &p, const QSize &size)
{
    static const Perlin noise;
    QImage img = blank(size);
    const int w = img.width(), h = img.height();
    const int style = int(std::lround(num(p, "NoiseStyle", 0.0)));
    const double layers = std::clamp(num(p, "NumberOfLayers", 1.0), 1.0, 5.0);
    const bool swap = num(p, "Swapcolors", 0.0) >= 0.5;
    Rgba c1 = colour(p, "Color1", {0, 0, 0, 1});
    Rgba c2 = colour(p, "Color2", {1, 1, 1, 1});
    if (swap) {
        std::swap(c1, c2);
    }
    const double fx = std::max(1e-3, num(p, "FrequencyX", 10.0));
    const double fy = std::max(1e-3, num(p, "FrequencyY", 10.0));
    const double ox = num(p, "OffsetX", 0.0), oy = num(p, "OffsetY", 0.0);
    const double progress = num(p, "OffsetprogressinDegrees", 0.0);
    const double lo = num(p, "NoiseParametersMin", 0.0), hi = num(p, "NoiseParametersMax", 1.0);
    const double bias = num(p, "NoiseParametersBias", 0.0);
    const double amp = num(p, "NoiseParametersAmplitude", 1.0);
    const double falloff = std::clamp(num(p, "NoiseParametersGrainAmplitudeFalloff", 0.5), 0.0, 1.0);
    const int whole = int(std::floor(layers));
    const double partial = layers - whole;

    auto fractal = [&](double x, double y, bool turbulent) {
        double sum = 0.0, a = 1.0, f = 1.0, norm = 0.0;
        for (int i = 0; i < whole + (partial > 0 ? 1 : 0); ++i) {
            const double weight = i < whole ? 1.0 : partial;
            const double n = noise(x * f + 17.3 * i + progress, y * f - 9.1 * i);
            sum += weight * a * (turbulent ? std::fabs(n) * 2.0 - 1.0 : n);
            norm += weight * a;
            a *= falloff;
            f *= 2.0;
        }
        return norm > 0 ? sum / norm : 0.0;
    };

    for (int y = 0; y < h; ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        const double v = (double(y) / h) * fy + oy;
        for (int x = 0; x < w; ++x) {
            const double u = (double(x) / w) * fx + ox;
            double n = 0.0; // −1..1
            switch (style) {
            case 1: n = fractal(u, v, true); break;
            case 2: n = std::sin((u + v) * kPi + 3.0 * fractal(u, v, false)); break;
            case 3: n = std::sin((u + v) * kPi + 6.0 * fractal(u, v, true)); break;
            case 4: {
                // Thin lines of Color 1 along the noise's zero crossings, Color 2 elsewhere;
                // Amplitude thins them.
                const double d = std::fabs(fractal(u, v, false)) * std::max(1.0, amp);
                n = 2.0 * std::min(1.0, d * 2.0) - 1.0; // the line is Color 1
                break;
            }
            case 5: {
                const double c = fractal(u, v, false) * 4.0;
                n = 2.0 * std::fabs(c - std::round(c)) * 2.0 - 1.0;
                break;
            }
            case 6: {
                // Bands at levels of the noise, like growth rings: Wood's low X frequency
                // stretches them into grain.
                const double r = (fractal(u, v, false) + 1.0) * 4.0;
                n = 2.0 * (r - std::floor(r)) - 1.0;
                break;
            }
            default: n = fractal(u, v, false); break;
            }
            // A negative bias leans towards Color 2, as VEGAS's does: Cloud Cover (bias −1)
            // is mostly its transparent second colour with clouds of the first.
            double t = style == 4 ? (n + 1.0) * 0.5 : (n * amp + 1.0) * 0.5 - bias * 0.5;
            t = hi > lo ? (t - lo) / (hi - lo) : (t >= lo ? 1.0 : 0.0);
            line[x] = premul(lerp(c1, c2, std::clamp(t, 0.0, 1.0)));
        }
    }
    return img;
}

// ---- Color Gradient ----------------------------------------------------------------------

/**
 * VEGAS Color Gradient (legacy sftrans1.dll). Control points sit at X/Y in shares of the
 * frame and colour the frame by one distance from its centre: along the Aspect Ratio Angle
 * for Linear, and for Elliptical and Rectangular across an ellipse or box whose proportions
 * that angle sets (45° is the frame's own shape). Colours interpolate linearly between the
 * points in order of that distance and hold beyond the ends.
 */
QImage colorGradient(const QVariantMap &p, const QSize &size)
{
    QImage img = blank(size);
    const int w = img.width(), h = img.height();
    const int type = p.value(QStringLiteral("GradientType")).toInt();
    const double angle = p.value(QStringLiteral("AspectAngle")).toDouble();
    const double ca = std::cos(angle), sa = std::sin(angle);
    const double rx = std::max(1e-3, std::fabs(ca)), ry = std::max(1e-3, std::fabs(sa));
    auto metric = [&](double u, double v) {
        const double du = u - 0.5, dv = v - 0.5;
        switch (type) {
        case 1: return std::hypot(du / rx, dv / ry);
        case 2: return std::max(std::fabs(du) / rx, std::fabs(dv) / ry);
        default: return du * ca + dv * sa;
        }
    };
    struct Stop {
        double at;
        Rgba c;
    };
    std::vector<Stop> stops;
    for (const QVariant &pv : p.value(QStringLiteral("Points")).toList()) {
        const QVariantMap pt = pv.toMap();
        stops.push_back({metric(pt.value(QStringLiteral("X")).toDouble(),
                                pt.value(QStringLiteral("Y")).toDouble()),
                         colour(pt.value(QStringLiteral("Color")), {0, 0, 0, 1})});
    }
    if (stops.empty()) {
        return img;
    }
    std::stable_sort(stops.begin(), stops.end(), [](const Stop &a, const Stop &b) { return a.at < b.at; });
    for (int y = 0; y < h; ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        const double v = (y + 0.5) / h;
        for (int x = 0; x < w; ++x) {
            const double d = metric((x + 0.5) / w, v);
            Rgba c = stops.front().c;
            if (d >= stops.back().at) {
                c = stops.back().c;
            } else {
                for (size_t i = 1; i < stops.size(); ++i) {
                    if (d < stops[i].at) {
                        const double span = stops[i].at - stops[i - 1].at;
                        const double t = span > 1e-9 ? (d - stops[i - 1].at) / span : 1.0;
                        c = d <= stops[i - 1].at ? stops[i - 1].c : lerp(stops[i - 1].c, stops[i].c, t);
                        break;
                    }
                }
            }
            line[x] = premul(c);
        }
    }
    return img;
}

// ---- Credit Roll -------------------------------------------------------------------------

struct CreditLine {
    int kind = 1; // 0 header, 1 single item, 2 two columns
    QString left, right;
};

QString unquoted(QString s)
{
    if (s.size() >= 2 && s.startsWith(QLatin1Char('"')) && s.endsWith(QLatin1Char('"'))) {
        s = s.mid(1, s.size() - 2);
    }
    return s;
}

/**
 * The credits as lines, the way sftrans1's own instructions put it (strings 6048–6057): a
 * leading space makes a header, a tab splits a two-column item, two slashes begin a comment,
 * and quotation marks keep leading spaces.
 */
QVector<CreditLine> creditLines(const QString &text)
{
    QVector<CreditLine> out;
    QString t = text;
    t.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    for (const QString &raw : t.split(QLatin1Char('\n'))) {
        if (raw.isEmpty() || raw.startsWith(QLatin1String("//"))) {
            continue;
        }
        CreditLine l;
        if (raw.startsWith(QLatin1Char(' '))) {
            l.kind = 0;
            l.left = unquoted(raw.trimmed());
        } else if (raw.contains(QLatin1Char('\t'))) {
            l.kind = 2;
            l.left = unquoted(raw.section(QLatin1Char('\t'), 0, 0));
            l.right = unquoted(raw.section(QLatin1Char('\t'), 1));
        } else {
            l.left = unquoted(raw);
        }
        out.push_back(l);
    }
    return out;
}

struct CreditStyle {
    QFont font;
    Rgba colour;
    int align = 2;
    double spaceBelow = 0.0;
};

CreditStyle creditStyle(const QVariantList &styles, int index, int frameHeight)
{
    const QVariantMap s = index < styles.size() ? styles[index].toMap() : QVariantMap();
    CreditStyle cs;
    cs.font = QFont(s.value(QStringLiteral("Font"), QStringLiteral("Arial")).toString());
    // Sizes are points on the plug-in's own reference frame; scaled with the frame height.
    const double size = std::max(4.0, s.value(QStringLiteral("Size"), 20).toDouble());
    cs.font.setPixelSize(std::max(1, int(std::lround(size * frameHeight / 400.0))));
    cs.font.setBold(s.value(QStringLiteral("Bold")).toInt() != 0);
    cs.colour = colour(s.value(QStringLiteral("Color")), {1, 1, 1, 1});
    cs.align = s.value(QStringLiteral("Align"), 2).toInt();
    cs.spaceBelow = s.value(QStringLiteral("SpaceBelow"), 0.05).toDouble();
    return cs;
}

QColor toQColor(const Rgba &c)
{
    return QColor::fromRgbF(float(c.r), float(c.g), float(c.b), float(c.a));
}

/** Height of one line in its style, with the gap below it. */
double creditLineHeight(const CreditLine &l, const QVariantList &styles, int h)
{
    const CreditStyle s = creditStyle(styles, l.kind == 0 ? 0 : l.kind == 2 ? 2 : 1, h);
    return QFontMetricsF(s.font).lineSpacing() + s.spaceBelow * h * 0.3;
}

void drawCreditLine(QPainter &p, const CreditLine &l, const QVariantList &styles, double top,
                    double left, double right, int h, double opacity, const QString &connector)
{
    const double centre = (left + right) * 0.5;
    auto draw = [&](const CreditStyle &s, const QString &text, double x0, double x1, int align) {
        p.setFont(s.font);
        QColor c = toQColor(s.colour);
        c.setAlphaF(float(c.alphaF() * opacity));
        p.setPen(c);
        const double lh = QFontMetricsF(s.font).lineSpacing();
        const Qt::Alignment a = align == 0 ? Qt::AlignLeft : align == 1 ? Qt::AlignRight : Qt::AlignHCenter;
        p.drawText(QRectF(x0, top, std::max(1.0, x1 - x0), lh), int(a | Qt::AlignTop), text);
    };
    if (l.kind == 2) {
        // Left side against Left, right side against Right, as VEGAS sets a credit, and
        // the gap between filled with the connector ("Item Left ........ Item Right").
        const CreditStyle ls = creditStyle(styles, 2, h), rs = creditStyle(styles, 3, h);
        draw(ls, l.left, left, centre, 0);
        draw(rs, l.right, centre, right, 1);
        if (!connector.trimmed().isEmpty()) {
            const QFontMetricsF lm(ls.font), rm(rs.font);
            const double from = left + lm.horizontalAdvance(l.left) + lm.averageCharWidth();
            const double to = right - rm.horizontalAdvance(l.right) - rm.averageCharWidth();
            const double unit = std::max(1.0, lm.horizontalAdvance(connector));
            if (to > from + unit) {
                draw(ls, connector.repeated(int((to - from) / unit)), from, to, 2);
            }
        }
    } else {
        const CreditStyle s = creditStyle(styles, l.kind == 0 ? 0 : 1, h);
        draw(s, l.left, left, right, s.align);
    }
}

/**
 * VEGAS Credit Roll (legacy sftrans1.dll): the text laid out line by line between Left
 * and Right, headers, single items and two-column items each in their own style, over the
 * background colour. Scrolling Credits move the whole block up through the frame over the
 * event (down when the direction is Backward); a Timed Sequence shows one group at a time
 * at Top, a header with what follows it, fading in and out when its effects are fades.
 *
 * `still` lays the block out at Top without motion — what a thumbnail shows.
 */
QImage creditRoll(const QVariantMap &p, const QSize &size, double timeSec, double lengthSec, bool still)
{
    QImage img = blank(size);
    const int w = img.width(), h = img.height();
    img.fill(premul(colour(p, "Background", {0, 0, 0, 1})));
    const QVector<CreditLine> lines = creditLines(p.value(QStringLiteral("Text")).toString());
    const QVariantList styles = p.value(QStringLiteral("Styles")).toList();
    const double left = std::clamp(num(p, "Left", 0.1), 0.0, 1.0) * w;
    const double right = std::clamp(num(p, "Right", 0.9), 0.0, 1.0) * w;
    const double topY = std::clamp(num(p, "Top", 0.3), 0.0, 1.0) * h;
    const bool timed = num(p, "Effect", 0.0) >= 0.5;
    const QString connector = p.value(QStringLiteral("Connector")).toString();
    const double progress = lengthSec > 0 ? std::clamp(timeSec / lengthSec, 0.0, 1.0) : 0.0;

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    if (!timed || still) {
        double block = 0.0;
        for (const CreditLine &l : lines) {
            block += creditLineHeight(l, styles, h);
        }
        double y = topY;
        if (!still && !timed) {
            // Up through the frame: from just below it to just above it.
            const bool backward = num(p, "ScrollDirection", 0.0) >= 0.5;
            const double travel = progress * (h + block);
            y = backward ? -block + travel : h - travel;
        }
        for (const CreditLine &l : lines) {
            drawCreditLine(painter, l, styles, y, left, right, h, 1.0, connector);
            y += creditLineHeight(l, styles, h);
        }
        return img;
    }

    // Timed sequence: groups of a header and what follows it, an equal share of the event each.
    QVector<QVector<CreditLine>> groups;
    for (const CreditLine &l : lines) {
        if (groups.isEmpty() || l.kind == 0) {
            groups.push_back({});
        }
        groups.back().push_back(l);
    }
    if (groups.isEmpty()) {
        return img;
    }
    const int n = int(groups.size());
    const int g = std::min(n - 1, int(progress * n));
    const double local = progress * n - g; // 0..1 within the group's slot
    double opacity = 1.0;
    const int in = int(num(p, "InEffect", 0.0)), out = int(num(p, "OutEffect", 0.0));
    if ((in == 1 || in == 2) && local < 0.2) {
        opacity = local / 0.2;
    } else if ((out == 1 || out == 2) && local > 0.8) {
        opacity = (1.0 - local) / 0.2;
    }
    double y = topY;
    for (const CreditLine &l : groups[g]) {
        drawCreditLine(painter, l, styles, y, left, right, h, std::clamp(opacity, 0.0, 1.0),
                       connector);
        y += creditLineHeight(l, styles, h);
    }
    return img;
}

} // namespace

bool isVegasGeneratorId(const QString &pluginId)
{
    for (const QString &id : {kCheckerboard, kNoiseTexture, kSolidColor, kTestPattern, kColorGradient,
                              kCreditRoll}) {
        if (pluginId.compare(id, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QImage renderCreditRollStill(const QVariantMap &params, const QSize &size)
{
    return creditRoll(params, size, 0.0, 1.0, true);
}

QImage renderVegasGenerator(const QString &pluginId, const QVariantMap &params, const QSize &size,
                            double timeSec, double lengthSec)
{
    const auto is = [&](const QString &id) { return pluginId.compare(id, Qt::CaseInsensitive) == 0; };
    if (is(kCheckerboard)) {
        return checkerboard(params, size);
    }
    if (is(kSolidColor)) {
        QImage img = blank(size);
        img.fill(premul(colour(params, "Color", {1, 1, 1, 1})));
        return img;
    }
    if (is(kTestPattern)) {
        return testPattern(params, size);
    }
    if (is(kNoiseTexture)) {
        return noiseTexture(params, size);
    }
    if (is(kColorGradient)) {
        return colorGradient(params, size);
    }
    if (is(kCreditRoll)) {
        return creditRoll(params, size, timeSec, lengthSec, false);
    }
    return {};
}

} // namespace openvegas
