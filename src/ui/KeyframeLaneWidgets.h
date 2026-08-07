#pragma once

#include "video/VideoKeyframeEval.h"

#include <QColor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>

namespace openvegas {

inline QString formatTc(double sec)
{
    sec = std::max(0.0, sec);
    const int totalCs = int(std::llround(sec * 100.0));
    const int cs = totalCs % 100;
    const int totalSec = totalCs / 100;
    const int s = totalSec % 60;
    const int totalMin = totalSec / 60;
    const int m = totalMin % 60;
    const int h = totalMin / 60;
    return QStringLiteral("%1:%2:%3,%4")
        .arg(h, 2, 10, QChar(u'0'))
        .arg(m, 2, 10, QChar(u'0'))
        .arg(s, 2, 10, QChar(u'0'))
        .arg(cs, 2, 10, QChar(u'0'));
}

/**
 * Generic Vegas-style keyframe lane: draws diamonds (or a value curve + dots
 * when showCurve is set), supports drag/scrub/create/delete via callbacks.
 * Shared by Pan/Crop position/mask lanes and generic plug-in parameter lanes.
 */
class KeyframeLane : public QWidget {
public:
    explicit KeyframeLane(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("pcKfLane"));
        setMinimumHeight(28);
        setMouseTracking(true);
        setFocusPolicy(Qt::ClickFocus);
    }

    void setTimes(const QVector<double> &times, const QVector<int> &types, double duration,
                  int selected, double playhead)
    {
        if (!m_dragging) {
            m_times = times;
            m_types = types;
            m_selected = selected;
            m_values.clear();
            m_showCurve = false;
        }
        m_duration = std::max(0.001, duration);
        m_playhead = playhead;
        setMinimumHeight(28);
        update();
    }

    /** Optional value curve (same length as times); drawn when showCurve is true. */
    void setCurve(const QVector<double> &times, const QVector<double> &values,
                  const QVector<int> &types, double duration, int selected, double playhead,
                  bool showCurve)
    {
        if (!m_dragging) {
            m_times = times;
            m_values = values;
            m_types = types;
            m_selected = selected;
            m_showCurve = showCurve;
        }
        m_duration = std::max(0.001, duration);
        m_playhead = playhead;
        setMinimumHeight(showCurve ? 48 : 28);
        update();
    }

    void setOnSelect(const std::function<void(int)> &fn) { m_onSelect = fn; }
    void setOnScrub(const std::function<void(double)> &fn) { m_onScrub = fn; }
    /** Drag keyframe: (index, timeSec, finalize). Live moves pass finalize=false; release=true. */
    void setOnMove(const std::function<void(int, double, bool)> &fn) { m_onMove = fn; }
    /** Double-click empty lane → create keyframe at time. */
    void setOnCreateAt(const std::function<void(double)> &fn) { m_onCreateAt = fn; }
    void setOnDeleteSelected(const std::function<void()> &fn) { m_onDelete = fn; }
    /** Right-click context menu on a keyframe (index, globalPos). */
    void setOnContextMenu(const std::function<void(int, const QPoint &)> &fn)
    {
        m_onContextMenu = fn;
    }

protected:
    static QColor colorForType(int vegasOrInternalType)
    {
        // Match Vegas-ish diamond tint by interpolation kind.
        switch (static_cast<VideoKeyframeType>(vegasOrInternalType)) {
        case VideoKeyframeType::Fast:
            return QColor(0x60, 0xd0, 0x60);
        case VideoKeyframeType::Slow:
            return QColor(0x50, 0xb0, 0xff);
        case VideoKeyframeType::Smooth:
            return QColor(0xc0, 0x80, 0xff);
        case VideoKeyframeType::Sharp:
            return QColor(0xff, 0x90, 0x40);
        case VideoKeyframeType::Hold:
            return QColor(0xff, 0x55, 0x55);
        case VideoKeyframeType::Linear:
        default:
            return QColor(0xf0, 0xf0, 0xf0);
        }
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x2a, 0x2a, 0x2e));
        const double phX = (m_playhead / m_duration) * width();
        p.setPen(QPen(QColor(0xf0, 0x90, 0x20), 1));
        p.drawLine(QPointF(phX, 0), QPointF(phX, height()));

        const bool curve = m_showCurve && m_values.size() == m_times.size() && !m_times.isEmpty();
        double vmin = 0.0;
        double vmax = 1.0;
        if (curve) {
            vmin = m_values.first();
            vmax = m_values.first();
            for (double v : m_values) {
                vmin = std::min(vmin, v);
                vmax = std::max(vmax, v);
            }
            if (std::abs(vmax - vmin) < 1e-9) {
                vmin -= 0.5;
                vmax += 0.5;
            }
            const double pad = (vmax - vmin) * 0.12;
            vmin -= pad;
            vmax += pad;
            QPainterPath path;
            for (int i = 0; i < m_times.size(); ++i) {
                const double t = (m_dragging && i == m_dragIndex) ? m_dragTime : m_times[i];
                const double x = (t / m_duration) * width();
                const double yNorm = (m_values[i] - vmin) / (vmax - vmin);
                const double y = height() - 6.0 - yNorm * (height() - 12.0);
                if (i == 0) {
                    path.moveTo(x, y);
                } else {
                    path.lineTo(x, y);
                }
            }
            p.setPen(QPen(QColor(0xb0, 0xb0, 0xb8), 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        }

        const int midY = height() / 2;
        for (int i = 0; i < m_times.size(); ++i) {
            const double t = (m_dragging && i == m_dragIndex) ? m_dragTime : m_times[i];
            const double x = (t / m_duration) * width();
            double y = midY;
            if (curve) {
                const double yNorm = (m_values[i] - vmin) / (vmax - vmin);
                y = height() - 6.0 - yNorm * (height() - 12.0);
            }
            const bool sel = (i == m_selected) || (m_dragging && i == m_dragIndex);
            if (curve) {
                const double r = sel ? 4.5 : 3.5;
                p.setBrush(sel ? QColor(0x60, 0xc0, 0xff) : QColor(0xe8, 0xe8, 0xec));
                p.setPen(QPen(Qt::black, 1));
                p.drawEllipse(QPointF(x, y), r, r);
            } else {
                const double s = sel ? 6.5 : 5.0;
                QPolygonF dia;
                dia << QPointF(x, y - s) << QPointF(x + s, y) << QPointF(x, y + s)
                    << QPointF(x - s, y);
                const int typeCode = (i < m_types.size()) ? m_types[i] : 0;
                QColor fill = colorForType(typeCode);
                if (sel) {
                    fill = fill.lighter(130);
                }
                p.setBrush(fill);
                p.setPen(QPen(sel ? QColor(0x40, 0xa0, 0xff) : Qt::black, sel ? 2 : 1));
                p.drawPolygon(dia);
            }
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::RightButton) {
            setFocus(Qt::MouseFocusReason);
            const int hit = hitTest(event->position().x());
            if (hit >= 0) {
                if (m_onSelect) {
                    m_onSelect(hit);
                }
                if (m_onContextMenu) {
                    m_onContextMenu(hit, event->globalPosition().toPoint());
                }
            }
            return;
        }
        if (event->button() != Qt::LeftButton) {
            return;
        }
        setFocus(Qt::MouseFocusReason);
        const double mx = event->position().x();
        const int hit = hitTest(mx);
        if (hit >= 0) {
            m_dragging = true;
            m_dragIndex = hit;
            m_dragTime = m_times[hit];
            m_pressX = mx;
            m_moved = false;
            if (m_onSelect) {
                m_onSelect(hit);
            }
            setCursor(Qt::SizeHorCursor);
            return;
        }
        if (m_onScrub) {
            m_scrubbing = true;
            m_onScrub(xToTime(mx));
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const double mx = event->position().x();
        if (m_dragging && m_dragIndex >= 0) {
            if (!m_moved && std::abs(mx - m_pressX) < 3.0) {
                return;
            }
            m_moved = true;
            m_dragTime = xToTime(mx);
            // Live preview of diamond position.
            if (m_dragIndex >= 0 && m_dragIndex < m_times.size()) {
                m_times[m_dragIndex] = m_dragTime;
            }
            if (m_onMove) {
                m_onMove(m_dragIndex, m_dragTime, false);
            }
            update();
            return;
        }
        if (m_scrubbing && m_onScrub) {
            m_onScrub(xToTime(mx));
            return;
        }
        setCursor(hitTest(mx) >= 0 ? Qt::SizeHorCursor : Qt::ArrowCursor);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            return;
        }
        const int idx = m_dragIndex;
        const double t = m_dragTime;
        const bool didMove = m_dragging && m_moved && idx >= 0;
        m_dragging = false;
        m_scrubbing = false;
        m_dragIndex = -1;
        m_moved = false;
        setCursor(Qt::ArrowCursor);
        if (didMove && m_onMove) {
            m_onMove(idx, t, true);
        }
        update();
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            return;
        }
        const double t = xToTime(event->position().x());
        if (hitTest(event->position().x()) >= 0) {
            return;
        }
        if (m_onCreateAt) {
            m_onCreateAt(t);
        }
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
            if (m_onDelete) {
                m_onDelete();
            }
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

private:
    double xToTime(double x) const
    {
        return std::clamp(x / std::max(1.0, double(width())), 0.0, 1.0) * m_duration;
    }

    int hitTest(double mx) const
    {
        int best = -1;
        double bestD = 1e9;
        for (int i = 0; i < m_times.size(); ++i) {
            const double x = (m_times[i] / m_duration) * width();
            const double d = std::abs(x - mx);
            if (d < bestD) {
                bestD = d;
                best = i;
            }
        }
        return (best >= 0 && bestD < 12.0) ? best : -1;
    }

    QVector<double> m_times;
    QVector<double> m_values;
    QVector<int> m_types;
    double m_duration = 10.0;
    int m_selected = 0;
    double m_playhead = 0.0;
    bool m_showCurve = false;
    bool m_dragging = false;
    bool m_scrubbing = false;
    bool m_moved = false;
    int m_dragIndex = -1;
    double m_dragTime = 0.0;
    double m_pressX = 0.0;
    std::function<void(int)> m_onSelect;
    std::function<void(double)> m_onScrub;
    std::function<void(int, double, bool)> m_onMove;
    std::function<void(double)> m_onCreateAt;
    std::function<void()> m_onDelete;
    std::function<void(int, const QPoint &)> m_onContextMenu;
};

/** Forwards the first mouse press on a widget to a callback (row-header click-to-select). */
class RowClickFilter : public QObject {
public:
    explicit RowClickFilter(std::function<void()> fn, QObject *parent = nullptr)
        : QObject(parent)
        , m_fn(std::move(fn))
    {
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::MouseButtonPress && m_fn) {
            m_fn();
        }
        return false;
    }

private:
    std::function<void()> m_fn;
};

/** Timecode ruler above keyframe lanes. */
class PanCropKeyframeRuler : public QWidget {
public:
    explicit PanCropKeyframeRuler(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("pcKfRuler"));
        setFixedHeight(20);
        setMouseTracking(true);
    }

    void setRange(double duration, double playhead)
    {
        m_duration = std::max(0.001, duration);
        m_playhead = playhead;
        update();
    }

    void setOnScrub(const std::function<void(double)> &fn) { m_onScrub = fn; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x24, 0x24, 0x28));
        p.setPen(QColor(0xaa, 0xaa, 0xaa));
        QFont f = font();
        f.setPointSize(8);
        p.setFont(f);
        const double step = m_duration > 30.0 ? 2.0 : 1.0;
        for (double t = 0.0; t <= m_duration + 0.01; t += step) {
            const double x = (t / m_duration) * width();
            p.drawLine(QPointF(x, height() - 4), QPointF(x, height()));
            p.drawText(QRectF(x + 2, 0, 64, height() - 2), Qt::AlignLeft | Qt::AlignVCenter,
                       formatTc(t).left(8));
        }
        const double phX = (m_playhead / m_duration) * width();
        p.setPen(QPen(QColor(0xf0, 0x90, 0x20), 1));
        p.drawLine(QPointF(phX, 0), QPointF(phX, height()));
        QPolygonF tip;
        tip << QPointF(phX - 5, 0) << QPointF(phX + 5, 0) << QPointF(phX, 7);
        p.setBrush(QColor(0xf0, 0x90, 0x20));
        p.setPen(Qt::NoPen);
        p.drawPolygon(tip);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_onScrub) {
            m_dragging = true;
            m_onScrub(std::clamp(event->position().x() / std::max(1.0, double(width())), 0.0, 1.0)
                      * m_duration);
        }
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_dragging && m_onScrub) {
            m_onScrub(std::clamp(event->position().x() / std::max(1.0, double(width())), 0.0, 1.0)
                      * m_duration);
        }
    }
    void mouseReleaseEvent(QMouseEvent *) override { m_dragging = false; }

private:
    double m_duration = 10.0;
    double m_playhead = 0.0;
    bool m_dragging = false;
    std::function<void(double)> m_onScrub;
};

} // namespace openvegas
