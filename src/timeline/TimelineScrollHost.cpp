#include "timeline/TimelineScrollHost.h"
#include "timeline/TimelineView.h"
#include "model/ProjectModel.h"

#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace openvegas {

namespace {

constexpr int kBar = 15;

QToolButton *makeZoomBtn(QWidget *parent, const QString &text, const QString &tip)
{
    auto *b = new QToolButton(parent);
    b->setObjectName(QStringLiteral("tlScrollZoomBtn"));
    b->setText(text);
    b->setToolTip(tip);
    b->setFixedSize(kBar, kBar);
    b->setAutoRaise(false);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

} // namespace

TimelineScrollHost::TimelineScrollHost(ProjectModel *model, QWidget *parent)
    : QWidget(parent)
    , m_model(model)
{
    setObjectName(QStringLiteral("timelineScrollHost"));
    m_timeline = new TimelineView(model, this);
    buildUi();
    connect(m_timeline, &TimelineView::scrollMetricsChanged, this, &TimelineScrollHost::syncFromTimeline);
    connect(m_timeline, &TimelineView::scrollOffsetChanged, this, &TimelineScrollHost::updateThumbs);
    connect(m_timeline, &TimelineView::headerWidthChanged, this, &TimelineScrollHost::setCornerWidth);
    setCornerWidth(m_timeline->headerWidth());
    syncFromTimeline();
}

void TimelineScrollHost::buildUi()
{
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);

    grid->addWidget(m_timeline, 0, 0);
    grid->setRowStretch(0, 1);
    grid->setColumnStretch(0, 1);

    // Vertical scrollbar column
    m_vCol = new QWidget(this);
    m_vCol->setObjectName(QStringLiteral("tlScrollVCol"));
    m_vCol->setFixedWidth(kBar);
    auto *vLay = new QVBoxLayout(m_vCol);
    vLay->setContentsMargins(0, 0, 0, 0);
    vLay->setSpacing(0);

    m_vTrack = new QWidget(m_vCol);
    m_vTrack->setObjectName(QStringLiteral("tlScrollVTrack"));
    m_vTrack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_vThumb = new QFrame(m_vTrack);
    m_vThumb->setObjectName(QStringLiteral("tlScrollVThumb"));
    m_vThumb->setCursor(Qt::SizeVerCursor);
    m_vThumb->installEventFilter(this);
    m_vTrack->installEventFilter(this);
    vLay->addWidget(m_vTrack, 1);

    auto *zInV = makeZoomBtn(m_vCol, QStringLiteral("+"), tr("Zoom Track Height In"));
    auto *zOutV = makeZoomBtn(m_vCol, QStringLiteral("−"), tr("Zoom Track Height Out"));
    connect(zInV, &QToolButton::clicked, this, [this]() { zoomTracks(1.12); });
    connect(zOutV, &QToolButton::clicked, this, [this]() { zoomTracks(1.0 / 1.12); });
    vLay->addWidget(zInV);
    vLay->addWidget(zOutV);
    grid->addWidget(m_vCol, 0, 1);

    // Horizontal scrollbar row
    m_hRow = new QWidget(this);
    m_hRow->setObjectName(QStringLiteral("tlScrollHRow"));
    m_hRow->setFixedHeight(kBar);
    auto *hLay = new QHBoxLayout(m_hRow);
    hLay->setContentsMargins(0, 0, 0, 0);
    hLay->setSpacing(0);

    m_hCorner = new QWidget(m_hRow);
    m_hCorner->setObjectName(QStringLiteral("tlScrollCorner"));
    m_hCorner->setFixedHeight(kBar);
    hLay->addWidget(m_hCorner);

    m_hTrack = new QWidget(m_hRow);
    m_hTrack->setObjectName(QStringLiteral("tlScrollHTrack"));
    m_hTrack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_hThumb = new QFrame(m_hTrack);
    m_hThumb->setObjectName(QStringLiteral("tlScrollHThumb"));
    m_hThumb->setCursor(Qt::SizeHorCursor);
    m_hThumb->setToolTip(tr("Drag to scroll · edges to zoom"));
    m_hThumb->installEventFilter(this);
    m_hTrack->installEventFilter(this);

    // Zoom grips on thumb edges
    auto *gripL = new QFrame(m_hThumb);
    gripL->setObjectName(QStringLiteral("tlScrollHGripL"));
    gripL->setFixedWidth(11);
    gripL->setCursor(Qt::SizeHorCursor);
    gripL->setToolTip(tr("Zoom"));
    gripL->installEventFilter(this);
    auto *gripR = new QFrame(m_hThumb);
    gripR->setObjectName(QStringLiteral("tlScrollHGripR"));
    gripR->setFixedWidth(11);
    gripR->setCursor(Qt::SizeHorCursor);
    gripR->setToolTip(tr("Zoom"));
    gripR->installEventFilter(this);

    hLay->addWidget(m_hTrack, 1);

    auto *zOutH = makeZoomBtn(m_hRow, QStringLiteral("−"), tr("Zoom Out Time (Mouse Wheel)"));
    auto *zInH = makeZoomBtn(m_hRow, QStringLiteral("+"), tr("Zoom In Time (Mouse Wheel)"));
    connect(zOutH, &QToolButton::clicked, this, [this]() { zoomTime(1.0 / 1.18); });
    connect(zInH, &QToolButton::clicked, this, [this]() { zoomTime(1.18); });
    hLay->addWidget(zOutH);
    hLay->addWidget(zInH);
    grid->addWidget(m_hRow, 1, 0);

    m_brCorner = new QWidget(this);
    m_brCorner->setObjectName(QStringLiteral("tlScrollBrCorner"));
    m_brCorner->setFixedSize(kBar, kBar);
    grid->addWidget(m_brCorner, 1, 1);
}

void TimelineScrollHost::setCornerWidth(int headerWidth)
{
    if (m_hCorner) {
        m_hCorner->setFixedWidth(std::max(0, headerWidth));
    }
}

void TimelineScrollHost::syncFromTimeline()
{
    updateThumbs();
}

void TimelineScrollHost::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateThumbs();
}

void TimelineScrollHost::updateThumbs()
{
    if (!m_timeline || !m_hTrack || !m_vTrack || !m_hThumb || !m_vThumb) {
        return;
    }

    const int maxX = m_timeline->maxScrollX();
    const int maxY = m_timeline->maxScrollY();
    const int viewW = std::max(1, m_timeline->width());
    const int viewH = std::max(1, m_timeline->height());
    const int contentW = std::max(viewW, m_timeline->contentWidthPx());
    const int tracksH = std::max(1, m_timeline->tracksHeightPx());
    const int viewTracksH = std::max(1, viewH - m_timeline->rulerHeight());

    // Horizontal thumb
    const int hTrackW = std::max(1, m_hTrack->width());
    const double hRatio = double(viewW - m_timeline->headerWidth())
                          / std::max(1.0, double(contentW - m_timeline->headerWidth()));
    int hThumbW = std::clamp(int(std::lround(hTrackW * hRatio)), 28, hTrackW);
    if (maxX <= 0) {
        hThumbW = hTrackW;
    }
    const int hTravel = std::max(0, hTrackW - hThumbW);
    const int hX = (maxX > 0) ? int(std::lround(double(m_timeline->scrollX()) / maxX * hTravel)) : 0;
    m_hThumb->setGeometry(hX, 2, hThumbW, std::max(4, m_hTrack->height() - 4));

    if (QWidget *gripL = m_hThumb->findChild<QWidget *>(QStringLiteral("tlScrollHGripL"))) {
        gripL->setGeometry(0, 0, 11, m_hThumb->height());
        gripL->raise();
    }
    if (QWidget *gripR = m_hThumb->findChild<QWidget *>(QStringLiteral("tlScrollHGripR"))) {
        gripR->setGeometry(m_hThumb->width() - 11, 0, 11, m_hThumb->height());
        gripR->raise();
    }

    // Vertical thumb
    const int vTrackH = std::max(1, m_vTrack->height());
    const double vRatio = double(viewTracksH) / std::max(1.0, double(tracksH));
    int vThumbH = std::clamp(int(std::lround(vTrackH * vRatio)), 20, vTrackH);
    if (maxY <= 0) {
        vThumbH = vTrackH;
    }
    const int vTravel = std::max(0, vTrackH - vThumbH);
    const int vY = (maxY > 0) ? int(std::lround(double(m_timeline->scrollY()) / maxY * vTravel)) : 0;
    m_vThumb->setGeometry(2, vY, std::max(4, m_vTrack->width() - 4), vThumbH);
}

void TimelineScrollHost::applyHScrollFromThumb(int thumbX)
{
    const int hTrackW = std::max(1, m_hTrack->width());
    const int hThumbW = m_hThumb->width();
    const int hTravel = std::max(1, hTrackW - hThumbW);
    const int maxX = m_timeline->maxScrollX();
    const int x = std::clamp(thumbX, 0, hTravel);
    m_timeline->setScrollX(int(std::lround(double(x) / hTravel * maxX)));
}

void TimelineScrollHost::applyVScrollFromThumb(int thumbY)
{
    const int vTrackH = std::max(1, m_vTrack->height());
    const int vThumbH = m_vThumb->height();
    const int vTravel = std::max(1, vTrackH - vThumbH);
    const int maxY = m_timeline->maxScrollY();
    const int y = std::clamp(thumbY, 0, vTravel);
    m_timeline->setScrollY(int(std::lround(double(y) / vTravel * maxY)));
}

void TimelineScrollHost::zoomTime(double factor)
{
    if (!m_model || !m_timeline) {
        return;
    }
    const double pps = std::clamp(m_model->pixelsPerSecond() * factor, 0.5, 400.0);
    m_model->setPixelsPerSecond(pps);
    m_timeline->refreshLayout();
    updateThumbs();
}

void TimelineScrollHost::zoomTracks(double factor)
{
    if (!m_model || !m_timeline) {
        return;
    }
    for (Track &t : m_model->tracks()) {
        const int base = (t.kind == TrackKind::Audio) ? 100 : 96;
        const int next = std::clamp(int(std::lround(t.height * factor)), 36, 420);
        // Keep relative to base when near default
        Q_UNUSED(base);
        t.height = next;
    }
    m_timeline->refreshLayout();
    updateThumbs();
}

bool TimelineScrollHost::eventFilter(QObject *watched, QEvent *event)
{
    auto *w = qobject_cast<QWidget *>(watched);
    if (!w || !m_timeline) {
        return QWidget::eventFilter(watched, event);
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() != Qt::LeftButton) {
            return false;
        }
        if (watched == m_hTrack && !m_hThumb->geometry().contains(me->pos())) {
            // Jump page
            const int target = me->pos().x() - m_hThumb->width() / 2;
            applyHScrollFromThumb(target);
            updateThumbs();
            return true;
        }
        if (watched == m_vTrack && !m_vThumb->geometry().contains(me->pos())) {
            const int target = me->pos().y() - m_vThumb->height() / 2;
            applyVScrollFromThumb(target);
            updateThumbs();
            return true;
        }
        if (w->objectName() == QLatin1String("tlScrollHGripL")
            || w->objectName() == QLatin1String("tlScrollHGripR")) {
            m_drag = (w->objectName() == QLatin1String("tlScrollHGripL")) ? DragKind::HGripL
                                                                           : DragKind::HGripR;
            m_dragOrigin = me->globalPosition().toPoint().x();
            m_dragPpsOrigin = m_model ? m_model->pixelsPerSecond() : 40.0;
            m_dragScrollOrigin = m_timeline->scrollX();
            m_dragThumbWidthOrigin = m_hThumb->width();
            m_dragTrackW = std::max(1, m_hTrack->width());
            m_dragViewBodyW = std::max(1, m_timeline->width() - m_timeline->headerWidth());
            // contentBody = maxEnd*pps + 80  →  span used to invert thumb ratio → pps
            const int contentBody =
                std::max(1, m_timeline->contentWidthPx() - m_timeline->headerWidth());
            m_dragContentSpanSec =
                std::max(1.0, (double(contentBody) - 80.0) / std::max(0.5, m_dragPpsOrigin));
            w->grabMouse();
            return true;
        }
        if (watched == m_hThumb) {
            m_drag = DragKind::HThumb;
            m_dragOrigin = me->globalPosition().toPoint().x();
            m_dragThumbOrigin = m_hThumb->x();
            m_dragScrollOrigin = m_timeline->scrollX();
            m_hThumb->grabMouse();
            return true;
        }
        if (watched == m_vThumb) {
            m_drag = DragKind::VThumb;
            m_dragOrigin = me->globalPosition().toPoint().y();
            m_dragThumbOrigin = m_vThumb->y();
            m_dragScrollOrigin = m_timeline->scrollY();
            m_vThumb->grabMouse();
            return true;
        }
    } else if (event->type() == QEvent::MouseMove && m_drag != DragKind::None) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (m_drag == DragKind::HThumb) {
            const int dx = me->globalPosition().toPoint().x() - m_dragOrigin;
            applyHScrollFromThumb(m_dragThumbOrigin + dx);
            updateThumbs();
            return true;
        }
        if (m_drag == DragKind::VThumb) {
            const int dy = me->globalPosition().toPoint().y() - m_dragOrigin;
            applyVScrollFromThumb(m_dragThumbOrigin + dy);
            updateThumbs();
            return true;
        }
        if (m_drag == DragKind::HGripL || m_drag == DragKind::HGripR) {
            // Vegas: resize carriage thumb → zoom. Inward = zoom in, outward = zoom out.
            // Right grip keeps left view time fixed; left grip keeps right view time fixed.
            const int dx = me->globalPosition().toPoint().x() - m_dragOrigin;
            const int minThumb = 28;
            int newThumbW = (m_drag == DragKind::HGripR)
                                ? (m_dragThumbWidthOrigin + dx)
                                : (m_dragThumbWidthOrigin - dx);
            newThumbW = std::clamp(newThumbW, minThumb, m_dragTrackW);
            const double ratio =
                std::clamp(double(newThumbW) / double(m_dragTrackW), 0.02, 1.0);
            const double contentBody = double(m_dragViewBodyW) / ratio;
            const double pps = std::clamp(
                (contentBody - 80.0) / m_dragContentSpanSec, 0.5, 400.0);
            if (m_model) {
                m_model->setPixelsPerSecond(pps);
                if (m_drag == DragKind::HGripR) {
                    const double tLeft = double(m_dragScrollOrigin) / m_dragPpsOrigin;
                    m_timeline->setScrollX(int(std::lround(tLeft * pps)));
                } else {
                    const double tRight =
                        (double(m_dragScrollOrigin) + double(m_dragViewBodyW)) / m_dragPpsOrigin;
                    m_timeline->setScrollX(
                        int(std::lround(tRight * pps - double(m_dragViewBodyW))));
                }
                m_timeline->refreshLayout();
                updateThumbs();
            }
            return true;
        }
    } else if (event->type() == QEvent::MouseButtonRelease && m_drag != DragKind::None) {
        m_drag = DragKind::None;
        if (QWidget *grabber = QWidget::mouseGrabber()) {
            grabber->releaseMouse();
        }
        return true;
    } else if (event->type() == QEvent::Resize && (watched == m_hTrack || watched == m_vTrack)) {
        updateThumbs();
    }

    return QWidget::eventFilter(watched, event);
}

} // namespace openvegas
