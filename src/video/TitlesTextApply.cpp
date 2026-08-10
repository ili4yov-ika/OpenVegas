#include "video/TitlesTextApply.h"

#include "video/VideoKeyframeEval.h"

#include <QDataStream>
#include <QFont>
#include <QFontMetricsF>
#include <QHash>
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
    /** Real Vegas AnimationName value (see SAMPLES/veg_project/project_titles-and-text.veg) —
     *  NOT derivable from label (Vegas's own scheme mixes Title_Case and lower_case words:
     *  "_Drop_split", "_Fly_in_from_Right", …), so every key is spelled out explicitly. */
    QString key;
    TitlesTextMotionSpec spec;
    /** Real recovered TextColor; alpha 0 means "not reliably recovered" → falls back to white. */
    QColor textColor = QColor(255, 255, 255, 255);
    /** Real recovered Background; transparent (the default) for 48 of the 51 presets —
     *  only Drop Split/Menace/Rough Day have a real opaque fill. */
    QColor backgroundColor = QColor(0, 0, 0, 0);
    double scale = 1.0;
};

/**
 * Real Vegas preset names AND AnimationName keys (recovered from
 * SAMPLES/veg_project/project_titles-and-text.veg — every one of its 51 distinct
 * AnimationName values, cross-checked against the real Animation dropdown / Media
 * Generator catalog screenshots). TextColor/Scale are real recovered defaults; the
 * motion CURVE itself is still a hand-crafted functional approximation (same status as
 * the color picker) since Vegas's exact per-preset easing isn't reverse-engineerable
 * without the plugin binary. The 25 Title-N presets round-robin over a small motion set
 * since their individual real motions are unknown (their text/scale are real).
 */
const QVector<PresetDef> &presetTable()
{
    static const QVector<PresetDef> table = [] {
        QVector<PresetDef> t;
        auto add = [&](const char *label, const char *key, TitlesTextMotion kind,
                       TitlesTextDirection dir = TitlesTextDirection::None,
                       QColor textColor = QColor(255, 255, 255, 255), double scale = 1.0,
                       QColor backgroundColor = QColor(0, 0, 0, 0)) {
            t.push_back({QString::fromLatin1(label), QString::fromLatin1(key),
                        TitlesTextMotionSpec{kind, dir}, textColor, backgroundColor, scale});
        };
        add("None", "_None", TitlesTextMotion::None);
        add("Action Flip", "_Action_Flip", TitlesTextMotion::PopIn, TitlesTextDirection::None,
            QColor(255, 0, 0));
        add("Bounce", "_Bounce", TitlesTextMotion::PopIn, TitlesTextDirection::None,
            QColor(0, 128, 0));
        add("Coming at You", "_Coming_at_You", TitlesTextMotion::PopIn, TitlesTextDirection::None,
            QColor(255, 255, 0));
        add("Double Flash Glow", "_Double_Flash_Glow", TitlesTextMotion::Flicker,
            TitlesTextDirection::None, QColor(255, 0, 255));
        // Real recovered Background — one of only 3 presets (with Menace/Rough Day) that
        // isn't transparent; fixed alongside the VegReader "Background"/"FitBackgroundColor"
        // substring-collision bug that was hiding it on VEG import (see findNameValue()).
        add("Drop Split", "_Drop_split", TitlesTextMotion::DropIn, TitlesTextDirection::None,
            QColor(0, 0, 0), 1.0, QColor(0, 255, 255));
        add("Dropping Words", "_Dropping_words", TitlesTextMotion::DropIn,
            TitlesTextDirection::None, QColor(255, 255, 255));
        add("Earthquake", "_Earthquake", TitlesTextMotion::Shake, TitlesTextDirection::None,
            QColor(39, 16, 231));
        add("Fall Down", "_Fall_Down", TitlesTextMotion::SlideIn, TitlesTextDirection::Up,
            QColor(0, 255, 255));
        add("Float and Pop", "_Float_and_pop", TitlesTextMotion::PopIn, TitlesTextDirection::None,
            QColor(255, 0, 128));
        add("Fly In", "_Fly_in", TitlesTextMotion::SlideIn, TitlesTextDirection::Down,
            QColor(255, 255, 255), 0.75);
        add("Fly in from Right", "_Fly_in_from_Right", TitlesTextMotion::SlideIn,
            TitlesTextDirection::Right, QColor(128, 0, 128));
        add("Jump", "_Jump", TitlesTextMotion::PopIn, TitlesTextDirection::None,
            QColor(255, 0, 255));
        // Real recovered Background (see Drop Split's note above).
        add("Menace", "_Menace", TitlesTextMotion::Shake, TitlesTextDirection::None,
            QColor(0, 0, 0), 1.0, QColor(255, 255, 255));
        add("Popup", "_Popup", TitlesTextMotion::PopIn, TitlesTextDirection::None,
            QColor(255, 255, 255));
        add("Rolling Glow and Enlarge", "_Rolling_Glow_and_Enlarge", TitlesTextMotion::TwistIn,
            TitlesTextDirection::None, QColor(0, 128, 0));
        // Real recovered TextColor is fully transparent (alpha 0, confirmed — not a
        // read bug like the Background one above: no other property name collides with
        // "TextColor") — unusable as-is (invisible text), approximated as black like its
        // Menace/Drop Split shake/drop siblings. Background is real: opaque yellow.
        add("Rough Day", "_Rough_Day", TitlesTextMotion::Shake, TitlesTextDirection::None,
            QColor(0, 0, 0), 1.0, QColor(255, 255, 0));
        add("Scroll", "_Scroll", TitlesTextMotion::Scroll, TitlesTextDirection::Up,
            QColor(255, 255, 255));
        add("Scroll Left", "_Scroll_Left", TitlesTextMotion::Scroll, TitlesTextDirection::Left,
            QColor(255, 64, 64));
        add("Slide", "_Slide", TitlesTextMotion::SlideIn, TitlesTextDirection::Left,
            QColor(0, 255, 255), 0.5);
        add("Slide Down", "_Slide_Down", TitlesTextMotion::SlideIn, TitlesTextDirection::Up,
            QColor(0, 128, 255), 0.75);
        add("Slide Left", "_Slide_Left", TitlesTextMotion::SlideIn, TitlesTextDirection::Right,
            QColor(0, 128, 128));
        add("Slide Right", "_Slide_Right", TitlesTextMotion::SlideIn, TitlesTextDirection::Left,
            QColor(0, 0, 255));
        add("Slide Up", "_Slide_Up", TitlesTextMotion::SlideIn, TitlesTextDirection::Down,
            QColor(0, 0, 128));
        add("Speedy", "_Speedy", TitlesTextMotion::SlideIn, TitlesTextDirection::Right,
            QColor(255, 255, 0));
        add("Twist In", "_Twist_In", TitlesTextMotion::TwistIn, TitlesTextDirection::None,
            QColor(255, 255, 0));

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
        // Real recovered Scale per Title-N preset; every other Title-N is 1.0.
        static const QHash<int, double> kTitleScale = {{3, 1.3}, {8, 1.125}};
        for (int i = 1; i <= 25; ++i) {
            const QString label = QStringLiteral("Title%1").arg(i, 2, 10, QLatin1Char('0'));
            t.push_back({label, QLatin1Char('_') + label, kTitleCycle[(i - 1) % 10],
                        QColor(255, 255, 255), QColor(0, 0, 0, 0), kTitleScale.value(i, 1.0)});
        }
        return t;
    }();
    return table;
}

const PresetDef *presetByKey(const QString &animationKey)
{
    for (const PresetDef &def : presetTable()) {
        if (def.key == animationKey) {
            return &def;
        }
    }
    return nullptr;
}

} // namespace

TitlesTextMotionSpec titlesTextMotionForPreset(const QString &animationKey)
{
    if (const PresetDef *def = presetByKey(animationKey)) {
        return def->spec;
    }
    return {};
}

QVector<TitlesTextPresetEntry> titlesTextAnimationPresets()
{
    QVector<TitlesTextPresetEntry> out;
    out.reserve(presetTable().size());
    for (const PresetDef &def : presetTable()) {
        out.push_back({def.label, def.key});
    }
    return out;
}

TitlesTextPresetVisuals titlesTextPresetVisuals(const QString &animationKey)
{
    if (const PresetDef *def = presetByKey(animationKey);
        def && def->textColor.alpha() > 0) {
        return {def->textColor, def->backgroundColor, def->scale};
    }
    return {};
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
    // Media Properties — "media." prefixed so they stay visibly separate from the
    // generator's own params inside the same flat state map.
    auto mediaEnum = [&m](const QString &key, int def, int maxValue) {
        return std::clamp(int(mapGet(m, key, double(def))), 0, maxValue);
    };
    p.media.tapeName = mapGetString(m, QStringLiteral("media.tapeName"), p.media.tapeName);
    p.media.useCustomTimecode =
        mapGetBool(m, QStringLiteral("media.useCustomTimecode"), p.media.useCustomTimecode);
    p.media.customTimecodeSec = std::max(
        0.0, mapGet(m, QStringLiteral("media.customTimecodeSec"), p.media.customTimecodeSec));
    p.media.frameWidth =
        std::max(0, int(mapGet(m, QStringLiteral("media.frameWidth"), p.media.frameWidth)));
    p.media.frameHeight =
        std::max(0, int(mapGet(m, QStringLiteral("media.frameHeight"), p.media.frameHeight)));
    p.media.fieldOrder = static_cast<MediaFieldOrder>(
        mediaEnum(QStringLiteral("media.fieldOrder"), int(p.media.fieldOrder), 2));
    p.media.pixelAspect =
        std::clamp(mapGet(m, QStringLiteral("media.pixelAspect"), p.media.pixelAspect), 0.01, 10.0);
    p.media.alphaChannel = static_cast<MediaAlphaChannel>(
        mediaEnum(QStringLiteral("media.alphaChannel"), int(p.media.alphaChannel), 4));
    p.media.backgroundColor =
        mapGetColor(m, QStringLiteral("media.backgroundColor"), p.media.backgroundColor);
    p.media.rotation = static_cast<MediaRotation>(
        mediaEnum(QStringLiteral("media.rotation"), int(p.media.rotation), 3));

    // Keyframe lanes: one list per animated parameter, each keyframe a [time, value,
    // type] triple. Absent key = that parameter is static, which is the common case.
    p.lanes.clear();
    for (const TitlesTextAnimatableParam &ap : titlesTextAnimatableParams()) {
        const auto it = m.constFind(QStringLiteral("kf.") + ap.key);
        if (it == m.cend()) {
            continue;
        }
        TitlesTextParamLane lane;
        lane.paramKey = ap.key;
        for (const QVariant &entry : it->toList()) {
            const QVariantList triple = entry.toList();
            if (triple.size() < 2) {
                continue;
            }
            TitlesTextKeyframe kf;
            kf.timeSec = std::max(0.0, triple[0].toDouble());
            kf.value = triple[1].toDouble();
            kf.type = static_cast<VideoKeyframeType>(
                std::clamp(triple.size() >= 3 ? triple[2].toInt() : 0, 0, 5));
            lane.keys.push_back(kf);
        }
        if (lane.keys.isEmpty()) {
            continue;
        }
        std::sort(lane.keys.begin(), lane.keys.end(),
                  [](const TitlesTextKeyframe &a, const TitlesTextKeyframe &b) {
                      return a.timeSec < b.timeSec;
                  });
        p.lanes.push_back(lane);
    }

    p.textColorExpanded = mapGetBool(m, QStringLiteral("textColorExpanded"), p.textColorExpanded);
    p.locationExpanded = mapGetBool(m, QStringLiteral("locationExpanded"), p.locationExpanded);
    p.advancedExpanded = mapGetBool(m, QStringLiteral("advancedExpanded"), p.advancedExpanded);
    p.outlineExpanded = mapGetBool(m, QStringLiteral("outlineExpanded"), p.outlineExpanded);
    p.shadowExpanded = mapGetBool(m, QStringLiteral("shadowExpanded"), p.shadowExpanded);
    return p;
}

QVariantMap titlesTextToMap(const TitlesTextParams &p)
{
    QVariantMap m{
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
        {QStringLiteral("media.tapeName"), p.media.tapeName},
        {QStringLiteral("media.useCustomTimecode"), p.media.useCustomTimecode},
        {QStringLiteral("media.customTimecodeSec"), p.media.customTimecodeSec},
        {QStringLiteral("media.frameWidth"), p.media.frameWidth},
        {QStringLiteral("media.frameHeight"), p.media.frameHeight},
        {QStringLiteral("media.fieldOrder"), int(p.media.fieldOrder)},
        {QStringLiteral("media.pixelAspect"), p.media.pixelAspect},
        {QStringLiteral("media.alphaChannel"), int(p.media.alphaChannel)},
        {QStringLiteral("media.backgroundColor"), p.media.backgroundColor},
        {QStringLiteral("media.rotation"), int(p.media.rotation)},
        {QStringLiteral("textColorExpanded"), p.textColorExpanded},
        {QStringLiteral("locationExpanded"), p.locationExpanded},
        {QStringLiteral("advancedExpanded"), p.advancedExpanded},
        {QStringLiteral("outlineExpanded"), p.outlineExpanded},
        {QStringLiteral("shadowExpanded"), p.shadowExpanded},
    };
    // Only animated parameters get a "kf.*" entry — a static generator's map stays
    // exactly as small as it was before keyframes existed.
    for (const TitlesTextParamLane &lane : p.lanes) {
        if (lane.keys.isEmpty()) {
            continue;
        }
        QVariantList entries;
        entries.reserve(lane.keys.size());
        for (const TitlesTextKeyframe &kf : lane.keys) {
            entries.push_back(QVariantList{kf.timeSec, kf.value, int(kf.type)});
        }
        m.insert(QStringLiteral("kf.") + lane.paramKey, entries);
    }
    return m;
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

const QVector<TitlesTextAnimatableParam> &titlesTextAnimatableParams()
{
    // Ranges mirror the property dialog's spin boxes so the keyframe pane's value axis
    // and the spin boxes can never disagree about what "full scale" means.
    static const QVector<TitlesTextAnimatableParam> params = {
        {QString(), QStringLiteral("Scale"), QStringLiteral("scale"), 0.01, 20.0},
        {QStringLiteral("Location"), QStringLiteral("X"), QStringLiteral("locationX"), -2.0, 3.0},
        {QStringLiteral("Location"), QStringLiteral("Y"), QStringLiteral("locationY"), -2.0, 3.0},
        {QStringLiteral("Advanced"), QStringLiteral("Tracking"), QStringLiteral("tracking"), -50.0,
         50.0},
        {QStringLiteral("Advanced"), QStringLiteral("Line spacing"), QStringLiteral("lineSpacing"),
         0.1, 5.0},
        {QStringLiteral("Outline"), QStringLiteral("Outline width"), QStringLiteral("outlineWidth"),
         0.0, 100.0},
        {QStringLiteral("Shadow"), QStringLiteral("Shadow offset X"),
         QStringLiteral("shadowOffsetX"), -20.0, 20.0},
        {QStringLiteral("Shadow"), QStringLiteral("Shadow offset Y"),
         QStringLiteral("shadowOffsetY"), -20.0, 20.0},
        {QStringLiteral("Shadow"), QStringLiteral("Shadow blur"), QStringLiteral("shadowBlur"), 0.0,
         20.0},
    };
    return params;
}

double titlesTextParamValue(const TitlesTextParams &p, const QString &key)
{
    if (key == QLatin1String("scale")) {
        return p.scale;
    }
    if (key == QLatin1String("locationX")) {
        return p.locationX;
    }
    if (key == QLatin1String("locationY")) {
        return p.locationY;
    }
    if (key == QLatin1String("tracking")) {
        return p.tracking;
    }
    if (key == QLatin1String("lineSpacing")) {
        return p.lineSpacing;
    }
    if (key == QLatin1String("outlineWidth")) {
        return p.outlineWidth;
    }
    if (key == QLatin1String("shadowOffsetX")) {
        return p.shadowOffsetX;
    }
    if (key == QLatin1String("shadowOffsetY")) {
        return p.shadowOffsetY;
    }
    if (key == QLatin1String("shadowBlur")) {
        return p.shadowBlur;
    }
    return 0.0;
}

void titlesTextSetParamValue(TitlesTextParams *p, const QString &key, double value)
{
    if (!p) {
        return;
    }
    if (key == QLatin1String("scale")) {
        p->scale = value;
    } else if (key == QLatin1String("locationX")) {
        p->locationX = value;
    } else if (key == QLatin1String("locationY")) {
        p->locationY = value;
    } else if (key == QLatin1String("tracking")) {
        p->tracking = value;
    } else if (key == QLatin1String("lineSpacing")) {
        p->lineSpacing = value;
    } else if (key == QLatin1String("outlineWidth")) {
        p->outlineWidth = value;
    } else if (key == QLatin1String("shadowOffsetX")) {
        p->shadowOffsetX = value;
    } else if (key == QLatin1String("shadowOffsetY")) {
        p->shadowOffsetY = value;
    } else if (key == QLatin1String("shadowBlur")) {
        p->shadowBlur = value;
    }
}

const TitlesTextParamLane *titlesTextFindLane(const TitlesTextParams &p, const QString &key)
{
    for (const TitlesTextParamLane &lane : p.lanes) {
        if (lane.paramKey == key) {
            return &lane;
        }
    }
    return nullptr;
}

TitlesTextParamLane &titlesTextEnsureLane(TitlesTextParams *p, const QString &key)
{
    for (TitlesTextParamLane &lane : p->lanes) {
        if (lane.paramKey == key) {
            return lane;
        }
    }
    TitlesTextParamLane lane;
    lane.paramKey = key;
    p->lanes.push_back(lane);
    return p->lanes.last();
}

void titlesTextSetKeyframe(TitlesTextParams *p, const QString &key, double timeSec, double value,
                           VideoKeyframeType type)
{
    if (!p) {
        return;
    }
    TitlesTextParamLane &lane = titlesTextEnsureLane(p, key);
    const double t = std::max(0.0, timeSec);
    for (TitlesTextKeyframe &kf : lane.keys) {
        if (std::abs(kf.timeSec - t) <= 0.001) {
            kf.value = value;
            kf.type = type;
            return;
        }
    }
    lane.keys.push_back({t, value, type});
    std::sort(lane.keys.begin(), lane.keys.end(),
              [](const TitlesTextKeyframe &a, const TitlesTextKeyframe &b) {
                  return a.timeSec < b.timeSec;
              });
}

bool titlesTextRemoveKeyframe(TitlesTextParams *p, const QString &key, double timeSec, double epsSec)
{
    if (!p) {
        return false;
    }
    for (int li = 0; li < p->lanes.size(); ++li) {
        if (p->lanes[li].paramKey != key) {
            continue;
        }
        QVector<TitlesTextKeyframe> &keys = p->lanes[li].keys;
        for (int i = 0; i < keys.size(); ++i) {
            if (std::abs(keys[i].timeSec - timeSec) <= epsSec) {
                keys.removeAt(i);
                if (keys.isEmpty()) {
                    // An empty lane would keep the parameter marked "animated" in the UI
                    // while behaving statically — drop it so the two always agree.
                    p->lanes.removeAt(li);
                }
                return true;
            }
        }
        return false;
    }
    return false;
}

TitlesTextParams titlesTextAtTime(const TitlesTextParams &p, double localSec)
{
    if (p.lanes.isEmpty()) {
        return p;
    }
    TitlesTextParams out = p;
    for (const TitlesTextParamLane &lane : p.lanes) {
        if (lane.keys.isEmpty()) {
            continue;
        }
        if (localSec <= lane.keys.first().timeSec) {
            titlesTextSetParamValue(&out, lane.paramKey, lane.keys.first().value);
            continue;
        }
        if (localSec >= lane.keys.last().timeSec) {
            titlesTextSetParamValue(&out, lane.paramKey, lane.keys.last().value);
            continue;
        }
        for (int i = 0; i + 1 < lane.keys.size(); ++i) {
            const TitlesTextKeyframe &a = lane.keys[i];
            const TitlesTextKeyframe &b = lane.keys[i + 1];
            if (localSec < a.timeSec || localSec > b.timeSec) {
                continue;
            }
            const double span = b.timeSec - a.timeSec;
            const double t = span > 1e-9 ? (localSec - a.timeSec) / span : 0.0;
            // Same easing table Pan/Crop and Track Motion keyframes use, so "Smooth"
            // means the same shape everywhere.
            const double w = videoKeyframeEase(a.type, t);
            titlesTextSetParamValue(&out, lane.paramKey, a.value + (b.value - a.value) * w);
            break;
        }
    }
    return out;
}

QRectF titlesTextBoundingBox(const TitlesTextParams &p, const QSize &size)
{
    if (size.width() < 1 || size.height() < 1 || p.text.trimmed().isEmpty()) {
        return QRectF();
    }

    const double refHeight = 1080.0;
    const double px = (size.height() / refHeight) * std::max(0.01, p.scale);

    QFont font(p.fontFamily);
    font.setPointSizeF(std::max(1.0, p.fontSize * px));
    font.setBold(p.bold);
    font.setItalic(p.italic);
    if (std::abs(p.tracking) > 1e-6) {
        font.setLetterSpacing(QFont::AbsoluteSpacing, p.tracking * px);
    }

    const QStringList lines = p.text.split(QLatin1Char('\n'));
    const QFontMetricsF fm(font);
    const double lineHeight = fm.height() * std::max(0.1, p.lineSpacing);
    double blockW = 0.0;
    for (const QString &line : lines) {
        blockW = std::max(blockW, double(fm.horizontalAdvance(line)));
    }
    const double blockH = lineHeight * lines.size();

    const double originX = p.locationX * size.width() - anchorFracX(p.anchor) * blockW;
    const double originY = p.locationY * size.height() - anchorFracY(p.anchor) * blockH;
    return QRectF(originX, originY, blockW, blockH);
}

QImage checkerboardBackground(const QSize &size)
{
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&img);
    painter.setPen(Qt::NoPen);
    constexpr int kTile = 8;
    for (int y = 0; y < size.height(); y += kTile) {
        for (int x = 0; x < size.width(); x += kTile) {
            const bool light = ((x / kTile) + (y / kTile)) % 2 == 0;
            painter.setBrush(light ? QColor(120, 120, 120) : QColor(85, 85, 85));
            painter.drawRect(x, y, kTile, kTile);
        }
    }
    return img;
}

} // namespace openvegas
