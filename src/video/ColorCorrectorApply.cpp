#include "video/ColorCorrectorApply.h"

#include <QDataStream>
#include <QIODevice>

#include <algorithm>
#include <cmath>

namespace openvegas {
namespace {

QVariantMap loadParams(const FxSlot &slot)
{
    QVariantMap m;
    if (slot.state.isEmpty()) {
        return m;
    }
    QDataStream in(slot.state);
    in.setVersion(QDataStream::Qt_6_0);
    in >> m;
    return m;
}

void saveParams(FxSlot *slot, const QVariantMap &m)
{
    if (!slot) {
        return;
    }
    QByteArray ba;
    QDataStream out(&ba, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out << m;
    slot->state = ba;
}

inline double clamp01(double v)
{
    return std::clamp(v, 0.0, 1.0);
}

inline bool nearIdentity(const ColorCorrectorParams &p)
{
    return std::abs(p.brightness) < 1e-6 && std::abs(p.contrast - 1.0) < 1e-6
           && std::abs(p.saturation - 1.0) < 1e-6 && std::abs(p.gamma - 1.0) < 1e-6;
}

double mapGet(const QVariantMap &m, const QString &key, double def)
{
    const auto it = m.constFind(key);
    if (it == m.cend()) {
        return def;
    }
    return it->toDouble();
}

} // namespace

ColorCorrectorParams colorCorrectorFromMap(const QVariantMap &m)
{
    ColorCorrectorParams p;
    p.brightness = std::clamp(mapGet(m, QStringLiteral("brightness"), 0.0), -1.0, 1.0);
    p.contrast = std::clamp(mapGet(m, QStringLiteral("contrast"), 1.0), 0.0, 2.0);
    p.saturation = std::clamp(mapGet(m, QStringLiteral("saturation"), 1.0), 0.0, 2.0);
    p.gamma = std::clamp(mapGet(m, QStringLiteral("gamma"), 1.0), 0.1, 3.0);
    return p;
}

QVariantMap colorCorrectorToMap(const ColorCorrectorParams &p)
{
    return QVariantMap{
        {QStringLiteral("brightness"), p.brightness},
        {QStringLiteral("contrast"), p.contrast},
        {QStringLiteral("saturation"), p.saturation},
        {QStringLiteral("gamma"), p.gamma},
    };
}

ColorCorrectorParams colorCorrectorFromSlot(const FxSlot &slot)
{
    return colorCorrectorFromMap(loadParams(slot));
}

void colorCorrectorSaveToSlot(FxSlot *slot, const ColorCorrectorParams &p)
{
    saveParams(slot, colorCorrectorToMap(p));
}

bool isColorCorrectorName(const QString &displayName)
{
    const QString n = displayName.trimmed();
    return n.compare(QLatin1String("Color Corrector"), Qt::CaseInsensitive) == 0
           || n.compare(QLatin1String("Brightness and Contrast"), Qt::CaseInsensitive) == 0
           || n.contains(QLatin1String("colorcorrector"), Qt::CaseInsensitive);
}

bool isColorGradingName(const QString &displayName)
{
    const QString n = displayName.trimmed();
    return n.compare(QLatin1String("Color Grading"), Qt::CaseInsensitive) == 0
           || n.contains(QLatin1String("colorgrading"), Qt::CaseInsensitive);
}

void applyColorCorrector(QImage *img, const ColorCorrectorParams &p)
{
    if (!img || img->isNull() || nearIdentity(p)) {
        return;
    }
    *img = img->convertToFormat(QImage::Format_ARGB32);
    const double bright = p.brightness * 0.5;
    const double contrast = p.contrast;
    const double sat = p.saturation;
    const double invGamma = 1.0 / std::max(0.1, p.gamma);

    for (int y = 0; y < img->height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img->scanLine(y));
        for (int x = 0; x < img->width(); ++x) {
            const QRgb px = line[x];
            const int a = qAlpha(px);
            if (a == 0) {
                continue;
            }
            double r = qRed(px) / 255.0;
            double g = qGreen(px) / 255.0;
            double b = qBlue(px) / 255.0;

            r = clamp01((r - 0.5) * contrast + 0.5 + bright);
            g = clamp01((g - 0.5) * contrast + 0.5 + bright);
            b = clamp01((b - 0.5) * contrast + 0.5 + bright);

            if (std::abs(sat - 1.0) > 1e-6) {
                const double yv = 0.2126 * r + 0.7152 * g + 0.0722 * b;
                r = clamp01(yv + (r - yv) * sat);
                g = clamp01(yv + (g - yv) * sat);
                b = clamp01(yv + (b - yv) * sat);
            }

            if (std::abs(invGamma - 1.0) > 1e-6) {
                r = clamp01(std::pow(r, invGamma));
                g = clamp01(std::pow(g, invGamma));
                b = clamp01(std::pow(b, invGamma));
            }

            line[x] = qRgba(int(std::lround(r * 255.0)), int(std::lround(g * 255.0)),
                            int(std::lround(b * 255.0)), a);
        }
    }
}

void applyColorGrading(QImage *img, const QVariantMap &params)
{
    if (!img || img->isNull() || params.isEmpty()) {
        return;
    }
    const double liftY = mapGet(params, QStringLiteral("lift.y"), 0.0);
    const double liftR = mapGet(params, QStringLiteral("lift.r"), 0.0);
    const double liftG = mapGet(params, QStringLiteral("lift.g"), 0.0);
    const double liftB = mapGet(params, QStringLiteral("lift.b"), 0.0);
    const double gammaY = mapGet(params, QStringLiteral("gamma.y"), 1.0);
    const double gammaR = mapGet(params, QStringLiteral("gamma.r"), 1.0);
    const double gammaG = mapGet(params, QStringLiteral("gamma.g"), 1.0);
    const double gammaB = mapGet(params, QStringLiteral("gamma.b"), 1.0);
    const double gainY = mapGet(params, QStringLiteral("gain.y"), 1.0);
    const double gainR = mapGet(params, QStringLiteral("gain.r"), 1.0);
    const double gainG = mapGet(params, QStringLiteral("gain.g"), 1.0);
    const double gainB = mapGet(params, QStringLiteral("gain.b"), 1.0);
    const double offsetY = mapGet(params, QStringLiteral("offset.y"), 0.0);
    const double offsetR = mapGet(params, QStringLiteral("offset.r"), 0.0);
    const double offsetG = mapGet(params, QStringLiteral("offset.g"), 0.0);
    const double offsetB = mapGet(params, QStringLiteral("offset.b"), 0.0);

    const bool identity =
        std::abs(liftY) < 1e-6 && std::abs(liftR) < 1e-6 && std::abs(liftG) < 1e-6
        && std::abs(liftB) < 1e-6 && std::abs(gammaY - 1.0) < 1e-6 && std::abs(gammaR - 1.0) < 1e-6
        && std::abs(gammaG - 1.0) < 1e-6 && std::abs(gammaB - 1.0) < 1e-6
        && std::abs(gainY - 1.0) < 1e-6 && std::abs(gainR - 1.0) < 1e-6
        && std::abs(gainG - 1.0) < 1e-6 && std::abs(gainB - 1.0) < 1e-6
        && std::abs(offsetY) < 1e-6 && std::abs(offsetR) < 1e-6 && std::abs(offsetG) < 1e-6
        && std::abs(offsetB) < 1e-6;
    if (identity) {
        return;
    }

    *img = img->convertToFormat(QImage::Format_ARGB32);
    const double invGy = 1.0 / std::max(0.05, gammaY * gammaR);
    const double invGg = 1.0 / std::max(0.05, gammaY * gammaG);
    const double invGb = 1.0 / std::max(0.05, gammaY * gammaB);
    // Note: use per-channel gamma = gammaY * gamma{R,G,B} for simplicity.

    for (int y = 0; y < img->height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img->scanLine(y));
        for (int x = 0; x < img->width(); ++x) {
            const QRgb px = line[x];
            const int a = qAlpha(px);
            if (a == 0) {
                continue;
            }
            double r = qRed(px) / 255.0;
            double g = qGreen(px) / 255.0;
            double b = qBlue(px) / 255.0;

            r = clamp01(r + liftY + liftR);
            g = clamp01(g + liftY + liftG);
            b = clamp01(b + liftY + liftB);

            r = clamp01(std::pow(r, invGy));
            g = clamp01(std::pow(g, invGg));
            b = clamp01(std::pow(b, invGb));

            r = clamp01(r * gainY * gainR + offsetY + offsetR);
            g = clamp01(g * gainY * gainG + offsetY + offsetG);
            b = clamp01(b * gainY * gainB + offsetY + offsetB);

            line[x] = qRgba(int(std::lround(r * 255.0)), int(std::lround(g * 255.0)),
                            int(std::lround(b * 255.0)), a);
        }
    }
}

void applyVideoColorFxChain(QImage *img, const QVector<FxSlot> &chain)
{
    if (!img || img->isNull()) {
        return;
    }
    for (const FxSlot &slot : chain) {
        if (slot.bypass) {
            continue;
        }
        if (isColorCorrectorName(slot.displayName)) {
            applyColorCorrector(img, colorCorrectorFromSlot(slot));
        } else if (isColorGradingName(slot.displayName)) {
            applyColorGrading(img, loadParams(slot));
        }
    }
}

} // namespace openvegas
