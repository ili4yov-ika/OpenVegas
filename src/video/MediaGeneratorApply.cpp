#include "video/MediaGeneratorApply.h"
#include "video/GeneratorPluginHook.h"
#include "video/GeneratorPresetData.h"
#include "video/VegasGenerators.h"

#include <QDataStream>
#include <QHash>
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

QString mediaGeneratorVegasId(const MediaGeneratorParams &p)
{
    if (!p.vegasPluginId.isEmpty()) {
        return p.vegasPluginId;
    }
    for (const StockGeneratorGroup &g : stockGeneratorPresets()) {
        if (g.name.compare(p.pluginName, Qt::CaseInsensitive) == 0) {
            return g.pluginId;
        }
    }
    return {};
}

QImage renderMediaGenerator(const MediaGeneratorParams &p, const QSize &size, double timeSec,
                            double lengthSec)
{
    const QString id = mediaGeneratorVegasId(p);
    QVariantMap params = p.vegasParams;
    if (params.isEmpty() && !id.isEmpty()) {
        // A slot from before the VEGAS values were kept: the generator's own default.
        if (const StockGeneratorGroup *g = stockGeneratorGroup(id); g && !g->presets.isEmpty()) {
            const StockGeneratorPreset *named = stockGeneratorPreset(id, p.vegasPreset);
            params = named ? named->params : g->presets.first().params;
        }
    }
    if (id.startsWith(QLatin1String("{Svfx:"), Qt::CaseInsensitive)) {
        const QString key = id.mid(6, id.size() - 7).section(QLatin1Char(':'), 1);
        QImage drawn;
        if (generatorPluginRender(key, params, timeSec, size, &drawn) && !drawn.isNull()) {
            return drawn;
        }
    }
    if (isVegasGeneratorId(id)) {
        const QImage own = renderVegasGenerator(id, params, size, timeSec, lengthSec);
        if (!own.isNull()) {
            return own;
        }
    }
    return renderMediaGeneratorPattern(p, size);
}

MediaGeneratorParams mediaGeneratorFromVegas(const QString &vegasPluginId, const QString &presetName,
                                             const QVariantMap &params, const QVariantMap &curves,
                                             const QString &displayName)
{
    struct Stand {
        const char *id;
        const char *name;
        MediaGeneratorPattern pattern;
        QColor c0, c1;
    };
    static const Stand kStands[] = {
        {"{Svfx:com.vegascreativesoftware:checkerboard}", "Checkerboard",
         MediaGeneratorPattern::Checker, QColor(0x11, 0x11, 0x11), QColor(0xee, 0xee, 0xee)},
        {"{Svfx:com.vegascreativesoftware:noisetexture}", "Noise Texture",
         MediaGeneratorPattern::Bumps, QColor(0x30, 0x30, 0x30), QColor(0x90, 0x90, 0x90)},
        {"{Svfx:com.vegascreativesoftware:solidcolor}", "Solid Color",
         MediaGeneratorPattern::Gradient, QColor(0x00, 0x78, 0xd7), QColor(0x00, 0x78, 0xd7)},
        {"{Svfx:com.vegascreativesoftware:testpattern}", "Test Pattern",
         MediaGeneratorPattern::Grille, QColor(0x11, 0x11, 0x11), QColor(0xee, 0xee, 0xee)},
        {"{8B5DEABC-53BD-49F6-9803-367478307A59}", "Color Gradient",
         MediaGeneratorPattern::Gradient, QColor(0x20, 0x40, 0x80), QColor(0xc0, 0x60, 0x30)},
        {"{02FD3B60-986B-11D4-9F6F-00485481BD31}", "Credit Roll",
         MediaGeneratorPattern::Gradient, QColor(0x10, 0x10, 0x10), QColor(0x40, 0x40, 0x50)},
    };
    MediaGeneratorParams p;
    if (!displayName.isEmpty()) {
        p.pluginName = displayName;
    }
    for (const Stand &s : kStands) {
        if (vegasPluginId.compare(QLatin1String(s.id), Qt::CaseInsensitive) == 0) {
            p.pluginName = QString::fromLatin1(s.name);
            p.pattern = s.pattern;
            p.c0 = s.c0;
            p.c1 = s.c1;
            break;
        }
    }
    p.vegasPluginId = vegasPluginId;
    p.vegasPreset = presetName;
    p.vegasParams = params;
    p.vegasCurves = curves;

    auto colour = [&](const QString &name) -> QColor {
        const QVariantList c = params.value(name).toList();
        if (c.size() < 3) {
            return {};
        }
        auto channel = [](const QVariant &v) { return std::clamp(v.toDouble(), 0.0, 1.0); };
        return QColor::fromRgbF(float(channel(c[0])), float(channel(c[1])), float(channel(c[2])),
                                c.size() > 3 ? float(channel(c[3])) : 1.0f);
    };
    if (const QColor c = colour(QStringLiteral("Color")); c.isValid()) {
        p.c0 = c;
        p.c1 = c;
    }
    if (const QColor c = colour(QStringLiteral("Color1")); c.isValid()) {
        p.c0 = c;
    }
    if (const QColor c = colour(QStringLiteral("Color2")); c.isValid()) {
        p.c1 = c;
    }
    // The stand-in draws square tiles only. Checkerboard's stripe and frame presets are
    // tiles of very different widths and heights (Horizontal Blinds is 1.0 by 0.07), which
    // as squares would be a few giant cells; their own stand-in patterns look like them.
    if (p.pattern == MediaGeneratorPattern::Checker) {
        static const QHash<QString, MediaGeneratorPattern> kLooks = {
            {QStringLiteral("horizontal blinds"), MediaGeneratorPattern::HBlinds},
            {QStringLiteral("vertical blinds"), MediaGeneratorPattern::VBlinds},
            {QStringLiteral("grate"), MediaGeneratorPattern::Grille},
            {QStringLiteral("fence"), MediaGeneratorPattern::Fence},
            {QStringLiteral("ridges"), MediaGeneratorPattern::Ridges},
            {QStringLiteral("bumps"), MediaGeneratorPattern::Bumps},
            {QStringLiteral("plaid"), MediaGeneratorPattern::Plaid},
            {QStringLiteral("letterhead"), MediaGeneratorPattern::Letterbox},
            {QStringLiteral("split screen"), MediaGeneratorPattern::SplitScreen},
            {QStringLiteral("horizon"), MediaGeneratorPattern::Horizon},
        };
        p.pattern = kLooks.value(presetName.toLower(), MediaGeneratorPattern::Checker);
    }
    // Checkerboard's tile is one cell, as a fraction of the frame, with its own width and
    // height. A blind is long one way and thin the other — Horizontal Blinds is 1.0 by
    // 0.07 — so the size the eye reads is the smaller of the two; and the stand-in's
    // blinds draw a stripe as half their tile, so those get it doubled.
    bool okW = false, okH = false;
    const double w = params.value(QStringLiteral("TileDimensionsWidth")).toDouble(&okW);
    const double h = params.value(QStringLiteral("TileDimensionsHeight")).toDouble(&okH);
    const double cell = okW && okH ? std::min(w, h) : okW ? w : okH ? h : 0.0;
    if (cell > 0.0) {
        const bool blinds = p.pattern == MediaGeneratorPattern::HBlinds
                            || p.pattern == MediaGeneratorPattern::VBlinds;
        p.tile = std::clamp(int(std::lround(cell * 100.0 * (blinds ? 2.0 : 1.0))), 2, 64);
    }
    return p;
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
    p.vegasPluginId = m.value(QStringLiteral("vegasPluginId")).toString();
    p.vegasPreset = m.value(QStringLiteral("vegasPreset")).toString();
    p.vegasParams = m.value(QStringLiteral("vegasParams")).toMap();
    p.vegasCurves = m.value(QStringLiteral("vegasCurves")).toMap();
    return p;
}

void mediaGeneratorSaveToSlot(FxSlot *slot, const MediaGeneratorParams &p)
{
    QVariantMap m{
        {QStringLiteral("pluginName"), p.pluginName},
        {QStringLiteral("pattern"), int(p.pattern)},
        {QStringLiteral("c0"), p.c0},
        {QStringLiteral("c1"), p.c1},
        {QStringLiteral("tile"), p.tile},
    };
    // Only when there is something: a generator placed from the browser stays as small as
    // it was.
    if (!p.vegasPluginId.isEmpty()) {
        m.insert(QStringLiteral("vegasPluginId"), p.vegasPluginId);
        m.insert(QStringLiteral("vegasPreset"), p.vegasPreset);
        m.insert(QStringLiteral("vegasParams"), p.vegasParams);
        if (!p.vegasCurves.isEmpty()) {
            m.insert(QStringLiteral("vegasCurves"), p.vegasCurves);
        }
    }
    saveParams(slot, m);
}

QString mediaGeneratorParamsToPayload(const MediaGeneratorParams &p)
{
    // The VEGAS generator and preset ride along by name: the values are VEGAS's stock ones
    // and come back from the stock table, rather than being squeezed into a drag string.
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
        .arg(p.pluginName)
        .arg(int(p.pattern))
        .arg(p.c0.name(QColor::HexArgb))
        .arg(p.c1.name(QColor::HexArgb))
        .arg(p.tile)
        .arg(p.vegasPluginId, p.vegasPreset);
}

MediaGeneratorParams mediaGeneratorParamsFromPayload(const QString &payload)
{
    MediaGeneratorParams p;
    const QStringList parts = payload.split(QLatin1Char('|'));
    if (parts.size() >= 7 && !parts[5].isEmpty()) {
        const StockGeneratorPreset *stock = stockGeneratorPreset(parts[5], parts[6]);
        return mediaGeneratorFromVegas(parts[5], parts[6], stock ? stock->params : QVariantMap(),
                                       {}, parts[0]);
    }
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
