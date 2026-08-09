#include "video/MediaGeneratorApply.h"

#include <QDataStream>
#include <QIODevice>
#include <QLinearGradient>
#include <QPainter>
#include <QRadialGradient>
#include <QStringList>
#include <QVariantMap>

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

constexpr int kPatternMax = int(MediaGeneratorPattern::Horizon);

} // namespace

QImage renderMediaGeneratorPattern(const MediaGeneratorParams &p, const QSize &size)
{
    const int w = std::max(1, size.width());
    const int h = std::max(1, size.height());
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, false);

    // Tile / stripe widths are authored against a 100px-wide reference (the preset
    // thumbnail) — scale so the pattern reads the same at any output resolution.
    const double scale = w / 100.0;
    const int tile = std::max(2, int(std::lround(std::max(4, p.tile) * scale)));

    auto fillGrad = [&]() {
        QLinearGradient g(0, 0, w, h);
        g.setColorAt(0, p.c0);
        g.setColorAt(1, p.c1);
        painter.fillRect(img.rect(), g);
    };

    switch (p.pattern) {
    case MediaGeneratorPattern::Checker:
        for (int y = 0; y < h; y += tile) {
            for (int x = 0; x < w; x += tile) {
                const bool dark = ((x / tile) + (y / tile)) % 2 == 0;
                painter.fillRect(x, y, tile, tile, dark ? p.c0 : p.c1);
            }
        }
        break;
    case MediaGeneratorPattern::HBlinds: {
        const int band = std::max(1, tile / 2);
        for (int y = 0; y < h; y += band * 2) {
            painter.fillRect(0, y, w, band, p.c0);
            painter.fillRect(0, y + band, w, band, p.c1);
        }
        break;
    }
    case MediaGeneratorPattern::VBlinds: {
        const int band = std::max(1, tile / 2);
        for (int x = 0; x < w; x += band * 2) {
            painter.fillRect(x, 0, band, h, p.c0);
            painter.fillRect(x + band, 0, band, h, p.c1);
        }
        break;
    }
    case MediaGeneratorPattern::Grille: {
        painter.fillRect(img.rect(), p.c1);
        const int stripe = std::max(1, tile * 3 / 4);
        for (int x = 0; x < w; x += stripe) {
            painter.fillRect(x, 0, std::max(1, stripe / 3), h, p.c0);
        }
        break;
    }
    case MediaGeneratorPattern::Fence: {
        painter.fillRect(img.rect(), QColor(0xaa, 0xaa, 0xaa));
        const int band = std::max(1, tile);
        for (int y = 0; y < h; y += band) {
            painter.fillRect(0, y, w, std::max(1, band * 3 / 8), QColor(0x33, 0x33, 0x33));
        }
        break;
    }
    case MediaGeneratorPattern::Ridges: {
        QLinearGradient g(0, 0, w, 0);
        g.setColorAt(0, QColor(0x1a, 0x1a, 0x1a));
        g.setColorAt(0.5, QColor(0x88, 0x88, 0x88));
        g.setColorAt(1, QColor(0x1a, 0x1a, 0x1a));
        painter.fillRect(img.rect(), g);
        break;
    }
    case MediaGeneratorPattern::Bumps: {
        painter.fillRect(img.rect(), QColor(0x22, 0x22, 0x22));
        const int step = std::max(4, tile * 2);
        const int r = std::max(2, step / 2 - 2);
        for (int y = step / 2; y < h; y += step) {
            for (int x = step / 2; x < w; x += step) {
                QRadialGradient g(x, y, r);
                g.setColorAt(0, QColor(0xcc, 0xcc, 0xcc));
                g.setColorAt(1, QColor(0x22, 0x22, 0x22));
                painter.setPen(Qt::NoPen);
                painter.setBrush(g);
                painter.drawEllipse(QPointF(x, y), r, r);
            }
        }
        break;
    }
    case MediaGeneratorPattern::Plaid: {
        const int band = std::max(2, tile * 2);
        for (int x = 0; x < w; x += band) {
            painter.fillRect(x, 0, band / 2, h, QColor(0x20, 0x40, 0x60));
            painter.fillRect(x + band / 2, 0, band / 2, h, QColor(0x80, 0x20, 0x40));
        }
        break;
    }
    case MediaGeneratorPattern::Letterbox: {
        painter.fillRect(img.rect(), QColor(0xcc, 0xcc, 0xcc));
        const int bar = std::max(1, int(std::lround(h * 11.0 / 62.0)));
        painter.fillRect(0, 0, w, bar, Qt::black);
        painter.fillRect(0, h - bar, w, bar, Qt::black);
        break;
    }
    case MediaGeneratorPattern::SplitScreen:
        painter.fillRect(0, 0, w / 2, h, QColor(0x20, 0x60, 0xa0));
        painter.fillRect(w / 2, 0, w - w / 2, h, QColor(0xa0, 0x40, 0x20));
        break;
    case MediaGeneratorPattern::Horizon: {
        QLinearGradient g(0, 0, 0, h);
        g.setColorAt(0, QColor(0x40, 0x60, 0xa0));
        g.setColorAt(0.45, QColor(0x40, 0x60, 0xa0));
        g.setColorAt(0.55, QColor(0xc0, 0x80, 0x40));
        g.setColorAt(1, QColor(0xc0, 0x80, 0x40));
        painter.fillRect(img.rect(), g);
        break;
    }
    case MediaGeneratorPattern::Gradient:
    default:
        fillGrad();
        break;
    }

    painter.end();
    return img;
}

FxSlot mediaGeneratorSlotFor(const MediaGeneratorParams &p)
{
    const QString name = p.pluginName.isEmpty() ? QStringLiteral("Media Generator") : p.pluginName;
    FxSlot slot =
        makeFxSlot(name, PluginFormat::Builtin, QStringLiteral("builtin:MediaGenerator:") + name);
    mediaGeneratorSaveToSlot(&slot, p);
    return slot;
}

MediaGeneratorParams mediaGeneratorFromSlot(const FxSlot &slot)
{
    const QVariantMap m = loadParams(slot);
    MediaGeneratorParams p;
    p.pluginName = m.value(QStringLiteral("pluginName"), slot.displayName).toString();
    p.pattern = static_cast<MediaGeneratorPattern>(
        std::clamp(m.value(QStringLiteral("pattern"), int(p.pattern)).toInt(), 0, kPatternMax));
    const QVariant c0 = m.value(QStringLiteral("c0"));
    if (c0.canConvert<QColor>()) {
        p.c0 = c0.value<QColor>();
    }
    const QVariant c1 = m.value(QStringLiteral("c1"));
    if (c1.canConvert<QColor>()) {
        p.c1 = c1.value<QColor>();
    }
    p.tile = std::clamp(m.value(QStringLiteral("tile"), p.tile).toInt(), 2, 64);
    return p;
}

void mediaGeneratorSaveToSlot(FxSlot *slot, const MediaGeneratorParams &p)
{
    saveParams(slot,
              QVariantMap{
                  {QStringLiteral("pluginName"), p.pluginName},
                  {QStringLiteral("pattern"), int(p.pattern)},
                  {QStringLiteral("c0"), p.c0},
                  {QStringLiteral("c1"), p.c1},
                  {QStringLiteral("tile"), p.tile},
              });
}

QString mediaGeneratorParamsToPayload(const MediaGeneratorParams &p)
{
    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(p.pluginName)
        .arg(int(p.pattern))
        .arg(p.c0.name(QColor::HexArgb))
        .arg(p.c1.name(QColor::HexArgb))
        .arg(p.tile);
}

MediaGeneratorParams mediaGeneratorParamsFromPayload(const QString &payload)
{
    MediaGeneratorParams p;
    const QStringList parts = payload.split(QLatin1Char('|'));
    if (parts.size() >= 1 && !parts[0].isEmpty()) {
        p.pluginName = parts[0];
    }
    if (parts.size() >= 2) {
        bool ok = false;
        const int v = parts[1].toInt(&ok);
        if (ok) {
            p.pattern = static_cast<MediaGeneratorPattern>(std::clamp(v, 0, kPatternMax));
        }
    }
    if (parts.size() >= 3 && QColor::isValidColorName(parts[2])) {
        p.c0 = QColor(parts[2]);
    }
    if (parts.size() >= 4 && QColor::isValidColorName(parts[3])) {
        p.c1 = QColor(parts[3]);
    }
    if (parts.size() >= 5) {
        bool ok = false;
        const int v = parts[4].toInt(&ok);
        if (ok) {
            p.tile = std::clamp(v, 2, 64);
        }
    }
    return p;
}

} // namespace openvegas
