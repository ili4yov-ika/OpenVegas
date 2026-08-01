#include "ui/RateSlider.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QSizePolicy>
#include <algorithm>
#include <cmath>

namespace openvegas {

RateSlider::RateSlider(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("rateSlider"));
    setToolTip(tr("Playback rate (−20…+20). Release to return to 0."));
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(20);
    setMouseTracking(true);
}

void RateSlider::setRate(double rate)
{
    const double clamped = std::clamp(rate, kMinRate, kMaxRate);
    if (std::abs(clamped - m_rate) < 1e-6) {
        return;
    }
    m_rate = clamped;
    update();
    emit rateChanged(m_rate);
}

QSize RateSlider::sizeHint() const
{
    return {96, 20};
}

QSize RateSlider::minimumSizeHint() const
{
    return {48, 18};
}

QRect RateSlider::trackRect() const
{
    // Leave room for the center triangle under the groove
    return QRect(3, 4, std::max(8, width() - 6), 6);
}

double RateSlider::rateFromX(int x) const
{
    const QRect tr = trackRect();
    if (tr.width() <= 1) {
        return 0.0;
    }
    // Use tr.width()-1 so that left edge maps to t=0 and right edge maps to t=1
    // (otherwise center zero can drift by 1px).
    const double denom = double(tr.width() - 1);
    const double t = std::clamp((x - tr.left()) / denom, 0.0, 1.0);
    return kMinRate + t * (kMaxRate - kMinRate);
}

int RateSlider::xFromRate(double rate) const
{
    const QRect tr = trackRect();
    const double t = (std::clamp(rate, kMinRate, kMaxRate) - kMinRate) / (kMaxRate - kMinRate);
    const int span = std::max(1, tr.width() - 1);
    return tr.left() + static_cast<int>(std::lround(t * span));
}

void RateSlider::applyRateFromPos(const QPoint &pos)
{
    setRate(rateFromX(pos.x()));
}

void RateSlider::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const QRect tr = trackRect();

    // Groove
    p.fillRect(tr, QColor(0x12, 0x12, 0x12));
    p.setPen(QColor(0x2e, 0x2e, 0x2e));
    p.drawRect(tr.adjusted(0, 0, -1, -1));

    // Zero marker — small white ▲ under track center
    const int zx = xFromRate(0.0);
    QPainterPath zero;
    zero.moveTo(zx, tr.bottom() + 1);
    zero.lineTo(zx - 4, tr.bottom() + 6);
    zero.lineTo(zx + 4, tr.bottom() + 6);
    zero.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xe8, 0xe8, 0xe8));
    p.drawPath(zero);

    // Thumb (Vegas: light rect with two grip lines)
    const int tx = xFromRate(m_rate);
    const QRect thumb(tx - 4, tr.top() - 2, 8, tr.height() + 4);
    p.fillRect(thumb, m_dragging || m_hover ? QColor(0xf0, 0xf0, 0xf0) : QColor(0xd0, 0xd0, 0xd0));
    p.setPen(QColor(0x66, 0x66, 0x66));
    p.drawRect(thumb.adjusted(0, 0, -1, -1));
    p.setPen(QColor(0x33, 0x33, 0x33));
    p.drawLine(tx - 1, thumb.top() + 2, tx - 1, thumb.bottom() - 2);
    p.drawLine(tx + 1, thumb.top() + 2, tx + 1, thumb.bottom() - 2);
}

void RateSlider::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        applyRateFromPos(event->pos());
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void RateSlider::mouseMoveEvent(QMouseEvent *event)
{
    const bool wasHover = m_hover;
    m_hover = trackRect().adjusted(-4, -4, 4, 8).contains(event->pos())
              || QRect(xFromRate(m_rate) - 5, 0, 10, height()).contains(event->pos());
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        applyRateFromPos(event->pos());
        event->accept();
        return;
    }
    if (wasHover != m_hover) {
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void RateSlider::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        // Spring back to 0 (Vegas shuttle)
        setRate(0.0);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void RateSlider::leaveEvent(QEvent *event)
{
    if (m_hover) {
        m_hover = false;
        update();
    }
    QWidget::leaveEvent(event);
}

} // namespace openvegas
