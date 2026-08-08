#include "video/TitlesTextApply.h"

#include "video/VideoKeyframeEval.h"

#include <QDataStream>
#include <QFont>
#include <QFontMetricsF>
#include <QIODevice>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <vector>

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

double mapGet(const QVariantMap &m, const QString &key, double def)
{
    const auto it = m.constFind(key);
    return it == m.cend() ? def : it->toDouble();
}

bool mapGetBool(const QVariantMap &m, const QString &key, bool def)
{
    const auto it = m.constFind(key);
    return it == m.cend() ? def : it->toBool();
}

QString mapGetString(const QVariantMap &m, const QString &key, const QString &def)
{
    const auto it = m.constFind(key);
    return it == m.cend() ? def : it->toString();
}

QColor mapGetColor(const QVariantMap &m, const QString &key, const QColor &def)
{
    const auto it = m.constFind(key);
    if (it == m.cend() || !it->canConvert<QColor>()) {
        return def;
    }
    return it->value<QColor>();
}

/** Single-pass box blur on premultiplied ARGB (correct: averaging premultiplied channels
 *  avoids the color fringing a naive un-premultiplied average would produce at edges). */
void boxBlurLine(std::vector<QRgb> &line, int radius)
{
    const int n = int(line.size());
    if (radius <= 0 || n == 0) {
        return;
    }
    std::vector<QRgb> out(n);
    long sumA = 0, sumR = 0, sumG = 0, sumB = 0;
    auto add = [&](int i) {
        if (i < 0 || i >= n) {
            return;
        }
        const QRgb c = line[size_t(i)];
        sumA += qAlpha(c);
        sumR += qRed(c);
        sumG += qGreen(c);
        sumB += qBlue(c);
    };
    auto sub = [&](int i) {
        if (i < 0 || i >= n) {
            return;
        }
        const QRgb c = line[size_t(i)];
        sumA -= qAlpha(c);
        sumR -= qRed(c);
        sumG -= qGreen(c);
        sumB -= qBlue(c);
    };
    for (int i = -radius; i <= radius; ++i) {
        add(i);
    }
    for (int x = 0; x < n; ++x) {
        const int lo = std::max(0, x - radius);
        const int hi = std::min(n - 1, x + radius);
        const int cnt = hi - lo + 1;
        out[size_t(x)] =
            qRgba(int(sumR / cnt), int(sumG / cnt), int(sumB / cnt), int(sumA / cnt));
        sub(x - radius);
        add(x + radius + 1);
    }
    line.swap(out);
}

void boxBlurImage(QImage *img, int radius)
{
    if (!img || radius <= 0) {
        return;
    }
    const int w = img->width();
    const int h = img->height();
    for (int y = 0; y < h; ++y) {
        auto *scan = reinterpret_cast<QRgb *>(img->scanLine(y));
        std::vector<QRgb> line(scan, scan + w);
        boxBlurLine(line, radius);
        std::copy(line.begin(), line.end(), scan);
    }
    for (int x = 0; x < w; ++x) {
        std::vector<QRgb> col(h);
        for (int y = 0; y < h; ++y) {
            col[size_t(y)] = reinterpret_cast<const QRgb *>(img->constScanLine(y))[x];
        }
        boxBlurLine(col, radius);
        for (int y = 0; y < h; ++y) {
            reinterpret_cast<QRgb *>(img->scanLine(y))[x] = col[size_t(y)];
        }
    }
}

double anchorFracX(TitlesTextAnchor a)
{
    switch (a) {
    case TitlesTextAnchor::TopLeft:
    case TitlesTextAnchor::MiddleLeft:
    case TitlesTextAnchor::BottomLeft:
        return 0.0;
    case TitlesTextAnchor::TopRight:
    case TitlesTextAnchor::MiddleRight:
    case TitlesTextAnchor::BottomRight:
        return 1.0;
    default:
        return 0.5;
    }
}

double anchorFracY(TitlesTextAnchor a)
{
    switch (a) {
    case TitlesTextAnchor::TopLeft:
    case TitlesTextAnchor::TopCenter:
    case TitlesTextAnchor::TopRight:
        return 0.0;
    case TitlesTextAnchor::BottomLeft:
    case TitlesTextAnchor::BottomCenter:
    case TitlesTextAnchor::BottomRight:
        return 1.0;
    default:
        return 0.5;
    }
}

double easeShape(double t)
{
    return videoKeyframeEase(VideoKeyframeType::Smooth, std::clamp(t, 0.0, 1.0));
}

struct PresetDef {
    QString label;
    TitlesTextMotionSpec spec;
};

QString presetKeyFromLabel(const QString &label)
{
    QString key = label;
    key.remove(QLatin1Char(' '));
    return QLatin1Char('_') + key;
}

/**
 * Real Vegas preset names (confirmed against the real app's Animation dropdown), mapped
 * to a small set of hand-crafted motion families — Vegas's exact per-preset curves
 * aren't reverse-engineerable without the real plugin binary, so this is a functional
 * approximation (same status as the color picker), not a byte-exact replica. The 25
 * Title-N presets round-robin over the same kind set since their individual real
 * motions are unknown.
 */
const QVector<PresetDef> &presetTable()
{
    static const QVector<PresetDef> table = [] {
        QVector<PresetDef> t;
        auto add = [&](const char *label, TitlesTextMotion kind,
                       TitlesTextDirection dir = TitlesTextDirection::None) {
            t.push_back({QString::fromLatin1(label), TitlesTextMotionSpec{kind, dir}});
        };
        add("None", TitlesTextMotion::None);
        add("Action Flip", TitlesTextMotion::PopIn);
        add("Bounce", TitlesTextMotion::PopIn);
        add("Coming at You", TitlesTextMotion::PopIn);
        add("Double Flash Glow", TitlesTextMotion::Flicker);
        add("Drop Split", TitlesTextMotion::DropIn);
        add("Dropping Words", TitlesTextMotion::DropIn);
        add("Earthquake", TitlesTextMotion::Shake);
        add("Fall Down", TitlesTextMotion::SlideIn, TitlesTextDirection::Up);
        add("Float and Pop", TitlesTextMotion::PopIn);
        add("Fly In", TitlesTextMotion::SlideIn, TitlesTextDirection::Down);
        add("Fly in from Right", TitlesTextMotion::SlideIn, TitlesTextDirection::Right);
        add("Jump", TitlesTextMotion::PopIn);
        add("Menace", TitlesTextMotion::Shake);
        add("Popup", TitlesTextMotion::PopIn);
        add("Rolling Glow and Enlarge", TitlesTextMotion::TwistIn);
        add("Rough Day", TitlesTextMotion::Shake);
        add("Scroll", TitlesTextMotion::Scroll, TitlesTextDirection::Up);
        add("Scroll Left", TitlesTextMotion::Scroll, TitlesTextDirection::Left);
        add("Slide", TitlesTextMotion::SlideIn, TitlesTextDirection::Left);
        add("Slide Down", TitlesTextMotion::SlideIn, TitlesTextDirection::Up);
        add("Slide Left", TitlesTextMotion::SlideIn, TitlesTextDirection::Right);
        add("Slide Right", TitlesTextMotion::SlideIn, TitlesTextDirection::Left);
        add("Slide Up", TitlesTextMotion::SlideIn, TitlesTextDirection::Down);
        add("Speedy", TitlesTextMotion::SlideIn, TitlesTextDirection::Right);
        add("Twist In", TitlesTextMotion::TwistIn);

        static const TitlesTextMotionSpec kTitleCycle[] = {
            {TitlesTextMotion::FadeIn, TitlesTextDirection::None},
            {TitlesTextMotion::SlideIn, TitlesTextDirection::Left},
            {TitlesTextMotion::SlideIn, TitlesTextDirection::Right},
            {TitlesTextMotion::SlideIn, TitlesTextDirection::Up},
            {TitlesTextMotion::SlideIn, TitlesTextDirection::Down},
            {TitlesTextMotion::PopIn, TitlesTextDirection::None},
            {TitlesTextMotion::DropIn, TitlesTextDirection::None},
            {TitlesTextMotion::TwistIn, TitlesTextDirection::None},
            {TitlesTextMotion::Shake, TitlesTextDirection::None},
            {TitlesTextMotion::Flicker, TitlesTextDirection::None},
        };
        for (int i = 1; i <= 25; ++i) {
            const QString label = QStringLiteral("Title%1").arg(i, 2, 10, QLatin1Char('0'));
            t.push_back({label, kTitleCycle[(i - 1) % 10]});
        }
        return t;
    }();
    return table;
}

} // namespace

TitlesTextMotionSpec titlesTextMotionForPreset(const QString &animationKey)
{
    for (const PresetDef &def : presetTable()) {
        if (presetKeyFromLabel(def.label) == animationKey) {
            return def.spec;
        }
    }
    return {};
}

QVector<TitlesTextPresetEntry> titlesTextAnimationPresets()
{
    QVector<TitlesTextPresetEntry> out;
    out.reserve(presetTable().size());
    for (const PresetDef &def : presetTable()) {
        out.push_back({def.label, presetKeyFromLabel(def.label)});
    }
    return out;
}

TitlesTextAnimKeyframe evaluateTitlesTextAnimation(const TitlesTextMotionSpec &spec, double progress)
{
    TitlesTextAnimKeyframe kf;
    progress = std::clamp(progress, 0.0, 1.0);

    auto applyDirection = [](TitlesTextDirection d, double amount, double *ox, double *oy) {
        switch (d) {
        case TitlesTextDirection::Left:
            *ox = -amount;
            break;
        case TitlesTextDirection::Right:
            *ox = amount;
            break;
        case TitlesTextDirection::Up:
            *oy = -amount;
            break;
        case TitlesTextDirection::Down:
            *oy = amount;
            break;
        default:
            break;
        }
    };

    // Entrance-style kinds play out over the first quarter of the event, then hold.
    constexpr double kEntranceWindow = 0.25;
    const double enter = std::clamp(progress / kEntranceWindow, 0.0, 1.0);
    const double eased = easeShape(enter);

    switch (spec.kind) {
    case TitlesTextMotion::None:
        break;
    case TitlesTextMotion::FadeIn:
        kf.opacity = eased;
        break;
    case TitlesTextMotion::SlideIn: {
        double ox = 0.0, oy = 0.0;
        applyDirection(spec.direction, 0.6 * (1.0 - eased), &ox, &oy);
        kf.offsetX = ox;
        kf.offsetY = oy;
        kf.opacity = std::clamp(enter * 2.0, 0.0, 1.0);
        break;
    }
    case TitlesTextMotion::PopIn: {
        // Scale overshoots past 1.0 then settles, for a springy "pop" feel.
        kf.scaleMul = enter < 0.7 ? 0.2 + (1.15 - 0.2) * easeShape(enter / 0.7)
                                 : 1.15 + (1.0 - 1.15) * easeShape((enter - 0.7) / 0.3);
        kf.opacity = std::clamp(enter * 3.0, 0.0, 1.0);
        break;
    }
    case TitlesTextMotion::DropIn: {
        // Falls from above, overshoots slightly past rest, then settles (a bounce).
        kf.offsetY = enter < 0.7 ? -0.5 + 0.55 * easeShape(enter / 0.7)
                                : 0.05 - 0.05 * easeShape((enter - 0.7) / 0.3);
        kf.opacity = std::clamp(enter * 3.0, 0.0, 1.0);
        break;
    }
    case TitlesTextMotion::TwistIn:
        kf.rotationDeg = -200.0 * (1.0 - eased);
        kf.scaleMul = 0.3 + (1.0 - 0.3) * eased;
        kf.opacity = std::clamp(enter * 2.0, 0.0, 1.0);
        break;
    case TitlesTextMotion::Shake: {
        // Continuous jitter, decaying away over the first ~60% of the event.
        const double decay = std::clamp(1.0 - progress / 0.6, 0.0, 1.0);
        kf.offsetX = 0.015 * decay * std::sin(progress * 18.0 * 2.0 * M_PI);
        kf.offsetY = 0.010 * decay * std::sin(progress * 23.0 * 2.0 * M_PI + 1.3);
        break;
    }
    case TitlesTextMotion::Scroll:
        // Continuous scroll across the whole event (credit-roll style).
        if (spec.direction == TitlesTextDirection::Left) {
            kf.offsetX = 0.6 - 1.2 * progress;
        } else {
            kf.offsetY = 0.6 - 1.2 * progress;
        }
        break;
    case TitlesTextMotion::Flicker:
        if (progress >= kEntranceWindow) {
            kf.opacity = 1.0;
        } else {
            const double onOff = std::fmod(enter * 4.0, 1.0) < 0.5 ? 0.15 : 1.0;
            kf.opacity = onOff + (1.0 - onOff) * eased;
        }
        break;
    }
    return kf;
}

TitlesTextParams titlesTextFromMap(const QVariantMap &m)
{
    TitlesTextParams p;
    p.text = mapGetString(m, QStringLiteral("text"), p.text);
    p.fontFamily = mapGetString(m, QStringLiteral("fontFamily"), p.fontFamily);
    p.fontSize = std::clamp(mapGet(m, QStringLiteral("fontSize"), p.fontSize), 1.0, 500.0);
    p.bold = mapGetBool(m, QStringLiteral("bold"), p.bold);
    p.italic = mapGetBool(m, QStringLiteral("italic"), p.italic);
    p.alignment =
        static_cast<TitlesTextAlignment>(std::clamp(int(mapGet(m, QStringLiteral("alignment"),
                                                                double(int(p.alignment)))),
                                                     0, 2));
    p.textColor = mapGetColor(m, QStringLiteral("textColor"), p.textColor);
    p.animationName = mapGetString(m, QStringLiteral("animationName"), p.animationName);
    p.scale = std::clamp(mapGet(m, QStringLiteral("scale"), p.scale), 0.01, 20.0);
    p.locationX = mapGet(m, QStringLiteral("locationX"), p.locationX);
    p.locationY = mapGet(m, QStringLiteral("locationY"), p.locationY);
    p.anchor = static_cast<TitlesTextAnchor>(
        std::clamp(int(mapGet(m, QStringLiteral("anchor"), double(int(p.anchor)))), 0, 8));
    p.cropBackgroundToText =
        mapGetBool(m, QStringLiteral("cropBackgroundToText"), p.cropBackgroundToText);
    p.backgroundColor = mapGetColor(m, QStringLiteral("backgroundColor"), p.backgroundColor);
    p.tracking = mapGet(m, QStringLiteral("tracking"), p.tracking);
    p.lineSpacing = std::clamp(mapGet(m, QStringLiteral("lineSpacing"), p.lineSpacing), 0.1, 5.0);
    p.outlineWidth = std::clamp(mapGet(m, QStringLiteral("outlineWidth"), p.outlineWidth), 0.0, 100.0);
    p.outlineColor = mapGetColor(m, QStringLiteral("outlineColor"), p.outlineColor);
    p.shadowEnable = mapGetBool(m, QStringLiteral("shadowEnable"), p.shadowEnable);
    p.shadowColor = mapGetColor(m, QStringLiteral("shadowColor"), p.shadowColor);
    p.shadowOffsetX = mapGet(m, QStringLiteral("shadowOffsetX"), p.shadowOffsetX);
    p.shadowOffsetY = mapGet(m, QStringLiteral("shadowOffsetY"), p.shadowOffsetY);
    p.shadowBlur = std::clamp(mapGet(m, QStringLiteral("shadowBlur"), p.shadowBlur), 0.0, 20.0);
    p.textColorExpanded = mapGetBool(m, QStringLiteral("textColorExpanded"), p.textColorExpanded);
    p.advancedExpanded = mapGetBool(m, QStringLiteral("advancedExpanded"), p.advancedExpanded);
    p.outlineExpanded = mapGetBool(m, QStringLiteral("outlineExpanded"), p.outlineExpanded);
    p.shadowExpanded = mapGetBool(m, QStringLiteral("shadowExpanded"), p.shadowExpanded);
    return p;
}

QVariantMap titlesTextToMap(const TitlesTextParams &p)
{
    return QVariantMap{
        {QStringLiteral("text"), p.text},
        {QStringLiteral("fontFamily"), p.fontFamily},
        {QStringLiteral("fontSize"), p.fontSize},
        {QStringLiteral("bold"), p.bold},
        {QStringLiteral("italic"), p.italic},
        {QStringLiteral("alignment"), int(p.alignment)},
        {QStringLiteral("textColor"), p.textColor},
        {QStringLiteral("animationName"), p.animationName},
        {QStringLiteral("scale"), p.scale},
        {QStringLiteral("locationX"), p.locationX},
        {QStringLiteral("locationY"), p.locationY},
        {QStringLiteral("anchor"), int(p.anchor)},
        {QStringLiteral("cropBackgroundToText"), p.cropBackgroundToText},
        {QStringLiteral("backgroundColor"), p.backgroundColor},
        {QStringLiteral("tracking"), p.tracking},
        {QStringLiteral("lineSpacing"), p.lineSpacing},
        {QStringLiteral("outlineWidth"), p.outlineWidth},
        {QStringLiteral("outlineColor"), p.outlineColor},
        {QStringLiteral("shadowEnable"), p.shadowEnable},
        {QStringLiteral("shadowColor"), p.shadowColor},
        {QStringLiteral("shadowOffsetX"), p.shadowOffsetX},
        {QStringLiteral("shadowOffsetY"), p.shadowOffsetY},
        {QStringLiteral("shadowBlur"), p.shadowBlur},
        {QStringLiteral("textColorExpanded"), p.textColorExpanded},
        {QStringLiteral("advancedExpanded"), p.advancedExpanded},
        {QStringLiteral("outlineExpanded"), p.outlineExpanded},
        {QStringLiteral("shadowExpanded"), p.shadowExpanded},
    };
}

TitlesTextParams titlesTextFromSlot(const FxSlot &slot)
{
    return titlesTextFromMap(loadParams(slot));
}

void titlesTextSaveToSlot(FxSlot *slot, const TitlesTextParams &p)
{
    saveParams(slot, titlesTextToMap(p));
}

QImage renderTitlesText(const TitlesTextParams &p, const QSize &size, double progress)
{
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    if (size.width() < 1 || size.height() < 1) {
        return img;
    }

    const TitlesTextMotionSpec motionSpec = titlesTextMotionForPreset(p.animationName);
    const TitlesTextAnimKeyframe anim = evaluateTitlesTextAnimation(motionSpec, progress);
    if (anim.opacity <= 1e-4) {
        return img; // fully invisible at this instant (e.g. mid-flicker) — nothing to draw
    }

    // Params are authored against a 1080-tall reference frame; scale to the actual size.
    const double refHeight = 1080.0;
    const double px = (size.height() / refHeight) * std::max(0.01, p.scale * anim.scaleMul);
    // Approximation constants for units not reverse-engineered from a live sample
    // (shadow offset/blur, outline width) — chosen to look reasonable at 1080p.
    const double offsetUnit = px * 20.0;
    const double outlineUnit = px * 4.0;
    const double blurUnit = px * 10.0;

    QFont font(p.fontFamily);
    font.setPointSizeF(std::max(1.0, p.fontSize * px));
    font.setBold(p.bold);
    font.setItalic(p.italic);
    if (std::abs(p.tracking) > 1e-6) {
        font.setLetterSpacing(QFont::AbsoluteSpacing, p.tracking * px);
    }

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setOpacity(std::clamp(anim.opacity, 0.0, 1.0));

    const QStringList lines = p.text.split(QLatin1Char('\n'));
    QPainterPath path;
    if (!p.text.trimmed().isEmpty()) {
        const QFontMetricsF fm(font);
        const double lineHeight = fm.height() * std::max(0.1, p.lineSpacing);
        double blockW = 0.0;
        for (const QString &line : lines) {
            blockW = std::max(blockW, double(fm.horizontalAdvance(line)));
        }
        const double blockH = lineHeight * lines.size();

        const double originX =
            p.locationX * size.width() - anchorFracX(p.anchor) * blockW + anim.offsetX * size.width();
        const double originY = p.locationY * size.height() - anchorFracY(p.anchor) * blockH
                               + anim.offsetY * size.height();

        for (int i = 0; i < lines.size(); ++i) {
            const QString &line = lines[i];
            if (line.isEmpty()) {
                continue;
            }
            double lx = originX;
            const double lineW = fm.horizontalAdvance(line);
            if (p.alignment == TitlesTextAlignment::Center) {
                lx += (blockW - lineW) / 2.0;
            } else if (p.alignment == TitlesTextAlignment::Right) {
                lx += (blockW - lineW);
            }
            const double ly = originY + fm.ascent() + i * lineHeight;
            path.addText(lx, ly, font, line);
        }

        if (!path.isEmpty() && std::abs(anim.rotationDeg) > 1e-6) {
            QTransform t;
            const QPointF center(originX + blockW / 2.0, originY + blockH / 2.0);
            t.translate(center.x(), center.y());
            t.rotate(anim.rotationDeg);
            t.translate(-center.x(), -center.y());
            path = t.map(path);
        }
    }

    if (p.backgroundColor.alpha() > 0) {
        if (p.cropBackgroundToText && !path.isEmpty()) {
            painter.save();
            painter.setClipPath(path);
            painter.fillRect(img.rect(), p.backgroundColor);
            painter.restore();
        } else if (!p.cropBackgroundToText) {
            painter.fillRect(img.rect(), p.backgroundColor);
        }
    }

    if (!path.isEmpty()) {
        if (p.shadowEnable) {
            QImage shadowLayer(size, QImage::Format_ARGB32_Premultiplied);
            shadowLayer.fill(Qt::transparent);
            {
                QPainter sp(&shadowLayer);
                sp.setRenderHint(QPainter::Antialiasing, true);
                sp.translate(p.shadowOffsetX * offsetUnit, p.shadowOffsetY * offsetUnit);
                sp.fillPath(path, QBrush(p.shadowColor));
            }
            const int blurRadius = std::clamp(int(p.shadowBlur * blurUnit), 0, 24);
            boxBlurImage(&shadowLayer, blurRadius);
            painter.drawImage(0, 0, shadowLayer);
        }

        if (p.outlineWidth > 1e-6) {
            QPen pen(p.outlineColor);
            pen.setWidthF(p.outlineWidth * outlineUnit);
            pen.setJoinStyle(Qt::RoundJoin);
            painter.strokePath(path, pen);
        }

        painter.fillPath(path, QBrush(p.textColor));
    }

    return img;
}

} // namespace openvegas
