#include "ui/FadeCurvePopup.h"

#include "audio/FadeCurves.h"

#include <QApplication>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QGuiApplication>

#include <algorithm>
#include <cmath>

namespace openvegas {

QPainterPath fadeCurveLinePath(const QRectF &r, FadeCurveType type, bool fadeIn)
{
    QPainterPath path;
    const int steps = std::max(12, static_cast<int>(r.width()));
    for (int i = 0; i <= steps; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(steps);
        const double amp = fadeIn ? fadeCurveAmplitude(type, t) : (1.0 - fadeCurveAmplitude(type, t));
        const double x = r.left() + t * r.width();
        const double y = r.bottom() - amp * r.height();
        if (i == 0) {
            path.moveTo(x, y);
        } else {
            path.lineTo(x, y);
        }
    }
    return path;
}

QPainterPath fadeCurveFillPath(const QRectF &r, FadeCurveType type, bool fadeIn)
{
    const int steps = std::max(12, static_cast<int>(r.width()));
    if (fadeIn) {
        QPainterPath path;
        path.moveTo(r.left(), r.top());
        path.lineTo(r.left(), r.bottom());
        for (int i = 0; i <= steps; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(steps);
            const double amp = fadeCurveAmplitude(type, t);
            path.lineTo(r.left() + t * r.width(), r.bottom() - amp * r.height());
        }
        path.closeSubpath();
        return path;
    }
    QPainterPath path;
    path.moveTo(r.left(), r.top());
    for (int i = 0; i <= steps; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(steps);
        const double amp = 1.0 - fadeCurveAmplitude(type, t);
        path.lineTo(r.left() + t * r.width(), r.bottom() - amp * r.height());
    }
    path.lineTo(r.right(), r.top());
    path.closeSubpath();
    return path;
}

namespace {

constexpr int kPad = 4;
constexpr int kRadioCol = 12;
constexpr int kIconW = 52;
constexpr int kIconH = 28;
constexpr int kGap = 3;
constexpr int kItemH = kIconH + 4;
constexpr int kBorder = 1;

} // namespace

FadeCurvePopup::FadeCurvePopup(QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground, false);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    const int w = kPad * 2 + kRadioCol + kIconW + 2;
    const int h = kPad * 2 + fadeCurveCount() * kItemH + (fadeCurveCount() - 1) * kGap;
    setFixedSize(w + kBorder * 2, h + kBorder * 2);
    qApp->installEventFilter(this);
}

FadeCurvePopup::~FadeCurvePopup()
{
    qApp->removeEventFilter(this);
}

void FadeCurvePopup::setFadeIn(bool fadeIn)
{
    m_fadeIn = fadeIn;
    update();
}

void FadeCurvePopup::setCurrent(FadeCurveType type)
{
    m_current = type;
    update();
}

void FadeCurvePopup::popupAt(const QPoint &globalPos)
{
    QPoint pos = globalPos;
    if (QScreen *screen = QGuiApplication::screenAt(globalPos)) {
        const QRect ag = screen->availableGeometry();
        if (pos.x() + width() > ag.right()) {
            pos.setX(ag.right() - width());
        }
        if (pos.y() + height() > ag.bottom()) {
            pos.setY(ag.bottom() - height());
        }
        if (pos.x() < ag.left()) {
            pos.setX(ag.left());
        }
        if (pos.y() < ag.top()) {
            pos.setY(ag.top());
        }
    }
    move(pos);
    show();
    raise();
    activateWindow();
    setFocus(Qt::PopupFocusReason);
}

QRect FadeCurvePopup::itemRect(int index) const
{
    const int y = kBorder + kPad + index * (kItemH + kGap);
    return QRect(kBorder + kPad, y, width() - (kBorder + kPad) * 2, kItemH);
}

QRect FadeCurvePopup::iconRect(int index) const
{
    const QRect item = itemRect(index);
    return QRect(item.left() + kRadioCol, item.top() + (item.height() - kIconH) / 2, kIconW, kIconH);
}

int FadeCurvePopup::indexAt(const QPoint &pos) const
{
    for (int i = 0; i < fadeCurveCount(); ++i) {
        if (itemRect(i).adjusted(-2, -1, 2, 1).contains(pos)) {
            return i;
        }
    }
    return -1;
}

void FadeCurvePopup::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF outer = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(QColor(220, 220, 220), 1.0));
    p.setBrush(QColor(28, 28, 30));
    p.drawRect(outer);

    for (int i = 0; i < fadeCurveCount(); ++i) {
        const auto type = static_cast<FadeCurveType>(i);
        const QRect item = itemRect(i);
        const QRect icon = iconRect(i);

        if (i == m_hover) {
            p.fillRect(item.adjusted(-1, 0, 1, 0), QColor(255, 255, 255, 18));
        }

        if (type == m_current) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(240, 240, 245));
            p.drawEllipse(QPointF(item.left() + 5, item.center().y()), 3.2, 3.2);
        }

        p.setPen(QPen(QColor(200, 200, 205), 1.0));
        p.setBrush(QColor(18, 18, 20));
        p.drawRect(QRectF(icon).adjusted(0.5, 0.5, -0.5, -0.5));

        const QRectF curveR = QRectF(icon).adjusted(4, 4, -4, -4);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(245, 245, 250), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(fadeCurveLinePath(curveR, type, m_fadeIn));
    }
}

void FadeCurvePopup::mouseMoveEvent(QMouseEvent *event)
{
    const int idx = indexAt(event->position().toPoint());
    if (idx != m_hover) {
        m_hover = idx;
        update();
    }
}

void FadeCurvePopup::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        hide();
        return;
    }
    const int idx = indexAt(event->position().toPoint());
    if (idx < 0) {
        hide();
        return;
    }
    m_current = static_cast<FadeCurveType>(idx);
    emit curveChosen(m_current);
    hide();
}

void FadeCurvePopup::leaveEvent(QEvent *event)
{
    m_hover = -1;
    update();
    QWidget::leaveEvent(event);
}

void FadeCurvePopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QWidget::keyPressEvent(event);
}

void FadeCurvePopup::hideEvent(QHideEvent *event)
{
    m_hover = -1;
    QWidget::hideEvent(event);
}

bool FadeCurvePopup::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);
    if (!isVisible()) {
        return false;
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (!rect().contains(mapFromGlobal(me->globalPosition().toPoint()))) {
            hide();
        }
    }
    return false;
}

} // namespace openvegas
