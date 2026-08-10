#include "video/TransitionApply.h"

#include <QFont>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace openvegas {
namespace {

/** Direction choice order — index is what gets stored in the params map. */
enum BlindsDirection {
    DirLeftToRight = 0,
    DirRightToLeft,
    DirTopToBottom,
    DirBottomToTop,
};

QStringList blindsDirections()
{
    // Left to Right and Top to Bottom are the two confirmed by the reference
    // screenshots (Simple/Left to Right/Spin vs Slot Machine); the two reverses are the
    // natural completion of the set.
    return {QStringLiteral("Left to Right"), QStringLiteral("Right to Left"),
            QStringLiteral("Top to Bottom"), QStringLiteral("Bottom to Top")};
}

TransitionPluginInfo makeBlinds()
{
    TransitionPluginInfo info;
    info.id = transition3dBlindsId();
    info.name = QStringLiteral("3D Blinds");
    info.format = QStringLiteral("DXT, 32-bit floating point");
    info.description = QStringLiteral("VEGAS 3D Blinds");

    // Ranges read straight off the reference screenshots' extreme-value captures:
    // Divisions 1…16, Extra spins 0…10, Stagger 0…1, Specular light 0…1.
    info.params = {
        {QStringLiteral("divisions"), QStringLiteral("Divisions"), 1.0, 16.0, 0, {}},
        {QStringLiteral("extraSpins"), QStringLiteral("Extra spins"), 0.0, 10.0, 0, {}},
        {QStringLiteral("stagger"), QStringLiteral("Stagger"), 0.0, 1.0, 4, {}},
        {QStringLiteral("specularLight"), QStringLiteral("Specular light"), 0.0, 1.0, 4, {}},
        {QStringLiteral("direction"), QStringLiteral("Direction"), 0.0, 3.0, 0,
         blindsDirections()},
    };

    auto preset = [](const QString &name, double divisions, double extraSpins, double stagger,
                     double specular, int direction) {
        TransitionPresetInfo p;
        p.name = name;
        p.params = QVariantMap{
            {QStringLiteral("divisions"), divisions},
            {QStringLiteral("extraSpins"), extraSpins},
            {QStringLiteral("stagger"), stagger},
            {QStringLiteral("specularLight"), specular},
            {QStringLiteral("direction"), direction},
        };
        return p;
    };
    // Defaults transcribed from SAMPLES/screenshots/Transitions/3D_Blinds/*default_set.png.
    info.presets = {
        preset(QStringLiteral("Simple"), 8, 0, 0.0, 1.0, DirLeftToRight),
        preset(QStringLiteral("Left to Right"), 4, 0, 0.2, 0.7, DirLeftToRight),
        preset(QStringLiteral("Slot Machine"), 4, 4, 0.3, 1.0, DirTopToBottom),
        preset(QStringLiteral("Spin"), 1, 0, 0.0, 1.0, DirLeftToRight),
    };
    return info;
}

/** Per-strip local progress, honouring stagger and the sweep direction. */
double stripProgress(double progress, int index, int count, double stagger, bool reverse)
{
    const double frac = count > 1 ? double(reverse ? (count - 1 - index) : index) / (count - 1) : 0.0;
    // Spread the strip start times over at most 60% of the transition so even a full
    // stagger still lands every strip on "fully B" exactly at progress 1.
    const double spread = std::clamp(stagger, 0.0, 1.0) * 0.6;
    const double start = spread * frac;
    const double span = std::max(1e-6, 1.0 - spread);
    return std::clamp((progress - start) / span, 0.0, 1.0);
}

QImage toArgb(const QImage &img, const QSize &size)
{
    if (img.isNull()) {
        return QImage();
    }
    QImage out = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (out.size() != size) {
        out = out.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    return out;
}

/** Draws one rotating blind. `scale` collapses it about its own centre line. */
void drawStrip(QPainter &p, const QImage &face, const QRectF &stripRect, double scale,
               bool horizontalSplit, double specular)
{
    if (face.isNull() || scale <= 0.001) {
        return;
    }
    QRectF dest = stripRect;
    if (horizontalSplit) {
        const double w = stripRect.width() * scale;
        dest.setX(stripRect.center().x() - w / 2.0);
        dest.setWidth(w);
    } else {
        const double h = stripRect.height() * scale;
        dest.setY(stripRect.center().y() - h / 2.0);
        dest.setHeight(h);
    }
    p.drawImage(dest, face, stripRect);
    if (specular > 0.001) {
        // Edge-on blinds catch the light: brightest as the panel turns away from the
        // viewer, gone when it faces front.
        p.save();
        p.setCompositionMode(QPainter::CompositionMode_Plus);
        const int a = int(std::clamp(specular, 0.0, 1.0) * 255.0);
        p.fillRect(dest, QColor(255, 255, 255, a));
        p.restore();
    }
}

QImage renderBlinds(const QImage &from, const QImage &to, double progress,
                    const TransitionInstance &t, const QSize &size)
{
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);

    const QImage a = toArgb(from, size);
    const QImage b = toArgb(to, size);
    const int divisions =
        std::clamp(int(std::lround(transitionParamValue(t, QStringLiteral("divisions")))), 1, 16);
    const int extraSpins =
        std::clamp(int(std::lround(transitionParamValue(t, QStringLiteral("extraSpins")))), 0, 10);
    const double stagger = transitionParamValue(t, QStringLiteral("stagger"));
    const double specular = transitionParamValue(t, QStringLiteral("specularLight"));
    const int direction =
        std::clamp(int(std::lround(transitionParamValue(t, QStringLiteral("direction")))), 0, 3);

    const bool horizontalSplit = direction == DirLeftToRight || direction == DirRightToLeft;
    const bool reverse = direction == DirRightToLeft || direction == DirBottomToTop;

    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const double total = horizontalSplit ? size.width() : size.height();
    for (int i = 0; i < divisions; ++i) {
        const double lo = total * i / divisions;
        const double hi = total * (i + 1) / divisions;
        const QRectF strip = horizontalSplit ? QRectF(lo, 0, hi - lo, size.height())
                                             : QRectF(0, lo, size.width(), hi - lo);

        const double lp = stripProgress(progress, i, divisions, stagger, reverse);
        const double angle = lp * (M_PI + 2.0 * M_PI * extraSpins);
        const double cosA = std::cos(angle);
        // Front face (cos >= 0) still shows the outgoing clip; once the panel passes
        // edge-on it is the incoming clip's back face that faces the viewer.
        const QImage &face = cosA >= 0.0 ? a : b;
        const double sinA = std::sin(angle);
        drawStrip(p, face, strip, std::abs(cosA), horizontalSplit,
                  specular * sinA * sinA * 0.55);
    }
    p.end();
    return out;
}

QImage crossDissolve(const QImage &from, const QImage &to, double progress, const QSize &size)
{
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    const QImage a = toArgb(from, size);
    const QImage b = toArgb(to, size);
    if (!a.isNull()) {
        p.setOpacity(1.0 - progress);
        p.drawImage(0, 0, a);
    }
    if (!b.isNull()) {
        p.setOpacity(progress);
        p.drawImage(0, 0, b);
    }
    p.end();
    return out;
}

/** "A" / "B" test cards Vegas uses for its own preset thumbnails. */
QImage testCard(const QSize &size, const QString &letter, const QColor &top, const QColor &bottom)
{
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&img);
    QLinearGradient g(0, 0, 0, size.height());
    g.setColorAt(0.0, top);
    g.setColorAt(1.0, bottom);
    p.fillRect(img.rect(), g);
    QFont f = p.font();
    f.setPointSizeF(std::max(8.0, size.height() * 0.55));
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(255, 255, 255, 235));
    p.drawText(img.rect(), Qt::AlignCenter, letter);
    p.end();
    return img;
}

} // namespace

const QVector<TransitionPluginInfo> &transitionCatalog()
{
    static const QVector<TransitionPluginInfo> catalog = {makeBlinds()};
    return catalog;
}

const TransitionPluginInfo *transitionPluginById(const QString &pluginId)
{
    for (const TransitionPluginInfo &info : transitionCatalog()) {
        if (info.id == pluginId) {
            return &info;
        }
    }
    return nullptr;
}

const TransitionPresetInfo *transitionPreset(const QString &pluginId, const QString &presetName)
{
    const TransitionPluginInfo *info = transitionPluginById(pluginId);
    if (!info) {
        return nullptr;
    }
    for (const TransitionPresetInfo &preset : info->presets) {
        if (preset.name.compare(presetName, Qt::CaseInsensitive) == 0) {
            return &preset;
        }
    }
    return nullptr;
}

TransitionInstance makeTransitionInstance(const QString &pluginId, const QString &presetName)
{
    TransitionInstance t;
    const TransitionPluginInfo *info = transitionPluginById(pluginId);
    if (!info) {
        return t;
    }
    t.pluginId = pluginId;
    if (const TransitionPresetInfo *preset = transitionPreset(pluginId, presetName)) {
        t.presetName = preset->name;
        t.params = preset->params;
    } else if (!info->presets.isEmpty()) {
        t.presetName = info->presets.first().name;
        t.params = info->presets.first().params;
    }
    return t;
}

double transitionParamValue(const TransitionInstance &t, const QString &key)
{
    const auto it = t.params.constFind(key);
    if (it != t.params.cend()) {
        return it->toDouble();
    }
    // Missing key: fall back to the group's first preset so a project saved before a
    // parameter existed still renders with a sane value instead of 0.
    if (const TransitionPluginInfo *info = transitionPluginById(t.pluginId)) {
        if (!info->presets.isEmpty()) {
            const auto pit = info->presets.first().params.constFind(key);
            if (pit != info->presets.first().params.cend()) {
                return pit->toDouble();
            }
        }
    }
    return 0.0;
}

void transitionSetParamValue(TransitionInstance *t, const QString &key, double value)
{
    if (!t) {
        return;
    }
    t->params.insert(key, value);
    // Any hand edit takes the instance off its preset, exactly like Vegas showing
    // "(Untitled)" once a preset's slider is touched.
    if (const TransitionPresetInfo *preset = transitionPreset(t->pluginId, t->presetName)) {
        if (preset->params != t->params) {
            t->presetName.clear();
        }
    }
}

QVariantMap transitionToMap(const TransitionInstance &t)
{
    QVariantMap m;
    if (!t.isValid()) {
        return m;
    }
    m.insert(QStringLiteral("pluginId"), t.pluginId);
    m.insert(QStringLiteral("presetName"), t.presetName);
    m.insert(QStringLiteral("params"), t.params);
    return m;
}

TransitionInstance transitionFromMap(const QVariantMap &m)
{
    TransitionInstance t;
    t.pluginId = m.value(QStringLiteral("pluginId")).toString();
    t.presetName = m.value(QStringLiteral("presetName")).toString();
    t.params = m.value(QStringLiteral("params")).toMap();
    return t;
}

QImage renderTransition(const QImage &from, const QImage &to, double progress,
                        const TransitionInstance &t)
{
    const QSize size = !from.isNull() ? from.size() : to.size();
    if (size.isEmpty()) {
        return QImage();
    }
    const double p = std::clamp(progress, 0.0, 1.0);
    if (t.pluginId == transition3dBlindsId()) {
        return renderBlinds(from, to, p, t, size);
    }
    return crossDissolve(from, to, p, size);
}

QImage renderTransitionPreview(const TransitionInstance &t, const QSize &size, double progress)
{
    if (size.isEmpty()) {
        return QImage();
    }
    const QImage a = testCard(size, QStringLiteral("A"), QColor(0x4a, 0x9a, 0xd0),
                              QColor(0x1c, 0x5a, 0x8a));
    const QImage b = testCard(size, QStringLiteral("B"), QColor(0x9a, 0xd8, 0xf0),
                              QColor(0x4a, 0x9a, 0xd0));
    const QImage blended = renderTransition(a, b, progress, t);

    // Transparent gaps between the blinds are the whole point of the thumbnail — show
    // them over the same checkerboard the Media Generator previews use.
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&out);
    constexpr int kTile = 8;
    p.setPen(Qt::NoPen);
    for (int y = 0; y < size.height(); y += kTile) {
        for (int x = 0; x < size.width(); x += kTile) {
            const bool light = ((x / kTile) + (y / kTile)) % 2 == 0;
            p.setBrush(light ? QColor(120, 120, 120) : QColor(85, 85, 85));
            p.drawRect(x, y, kTile, kTile);
        }
    }
    if (!blended.isNull()) {
        p.drawImage(0, 0, blended);
    }
    p.end();
    return out;
}

} // namespace openvegas
