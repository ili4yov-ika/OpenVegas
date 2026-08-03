#pragma once

#include <QColor>
#include <QVector>

#include <algorithm>
#include <limits>

namespace openvegas {

/**
 * Vegas Pro Track Display Color palette (Preferences → Display → Track Colors).
 * 8 swatches sampled from Magix Vegas Pro timeline (audio tracks 9–16 rainbow).
 */
namespace TrackColors {

/** Default 8-swatch palette (cycles for new tracks). */
inline const QVector<QColor> &palette()
{
    static const QVector<QColor> k = {
        QColor(0x70, 0x65, 0x8a), // purple / indigo
        QColor(0xb5, 0x7f, 0x8e), // dusty rose
        QColor(0xf1, 0x81, 0x77), // coral / soft red
        QColor(0xf1, 0xa3, 0x77), // orange / amber
        QColor(0xeb, 0xc2, 0x74), // yellow / ocher
        QColor(0x6d, 0xb7, 0x84), // sage / seafoam green
        QColor(0x81, 0xaa, 0xbc), // sky / light blue
        QColor(0x4b, 0x7f, 0x96), // steel blue
    };
    return k;
}

inline int paletteSize()
{
    return palette().size();
}

inline QColor at(int index)
{
    const auto &p = palette();
    if (p.isEmpty()) {
        return QColor(0x70, 0x65, 0x8a);
    }
    const int i = ((index % p.size()) + p.size()) % p.size();
    return p[i];
}

/** Nearest palette index for menu checkmark (Euclidean RGB). */
inline int nearestIndex(const QColor &c)
{
    if (!c.isValid()) {
        return 0;
    }
    const auto &p = palette();
    int best = 0;
    qint64 bestD = std::numeric_limits<qint64>::max();
    for (int i = 0; i < p.size(); ++i) {
        const qint64 dr = qint64(c.red()) - p[i].red();
        const qint64 dg = qint64(c.green()) - p[i].green();
        const qint64 db = qint64(c.blue()) - p[i].blue();
        const qint64 d = dr * dr + dg * dg + db * db;
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

/** Effective display color: explicit track color, else palette by timeline index. */
inline QColor effective(const QColor &stored, int trackIndex)
{
    return stored.isValid() ? stored : at(trackIndex);
}

/** Event fill (audio: track color wash; video: dark filmstrip bed — color only on header/fx). */
inline QColor eventFill(const QColor &trackColor, bool video)
{
    if (video) {
        // Vegas video events stay charcoal; track color is on header rail + event "fx" chip.
        return QColor(0x28, 0x28, 0x2c);
    }
    if (!trackColor.isValid()) {
        return QColor(0xb5, 0x7f, 0x8e);
    }
    return trackColor;
}

inline QColor eventTitle(const QColor &trackColor)
{
    const QColor base = eventFill(trackColor, false);
    return base.darker(115);
}

inline QColor rail(const QColor &trackColor)
{
    return trackColor.isValid() ? trackColor : QColor(0x70, 0x65, 0x8a);
}

/** Background for the event/track "fx" chip (Vegas: filled with track display color). */
inline QColor fxChip(const QColor &trackColor, bool hot = false)
{
    const QColor c = trackColor.isValid() ? trackColor : at(0);
    return hot ? c.lighter(115) : c;
}

} // namespace TrackColors

} // namespace openvegas
