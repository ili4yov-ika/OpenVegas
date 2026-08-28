#include "ui/CaptureSourceList.h"

#include "ui/CaptureSourceCard.h"
#include "ui/CaptureWindowTree.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPainter>
#include <QVBoxLayout>

namespace openvegas {

CaptureSourceList::CaptureSourceList(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("captureSourceList"));
    setAcceptDrops(true);
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(1);
    m_layout->addStretch(1);
}

int CaptureSourceList::gapAt(const QVector<QRect> &rows, int y)
{
    for (int i = 0; i < rows.size(); ++i) {
        // Past the middle of a row means "after it": dropping on the lower half of the
        // second row should land third, not second, or nothing can be moved to the end.
        if (y < rows[i].center().y()) {
            return i;
        }
    }
    return rows.size();
}

int CaptureSourceList::landingIndex(int from, int gap)
{
    return gap > from ? gap - 1 : gap;
}

QVector<QRect> CaptureSourceList::cardRects() const
{
    QVector<QRect> rects;
    for (int i = 0; i < m_layout->count(); ++i) {
        QWidget *w = m_layout->itemAt(i)->widget();
        if (qobject_cast<CaptureSourceCard *>(w)) {
            rects.push_back(w->geometry());
        }
    }
    return rects;
}

int CaptureSourceList::dropIndexAt(int y) const
{
    return gapAt(cardRects(), y);
}

void CaptureSourceList::dragEnterEvent(QDragEnterEvent *event)
{
    const QMimeData *mime = event->mimeData();
    if (mime->hasFormat(QLatin1String(kCaptureSourceMime))
        || mime->hasFormat(QLatin1String(kCaptureWindowMime))) {
        event->acceptProposedAction();
        m_dropIndex = dropIndexAt(int(event->position().y()));
        update();
        return;
    }
    event->ignore();
}

void CaptureSourceList::dragMoveEvent(QDragMoveEvent *event)
{
    const QMimeData *mime = event->mimeData();
    if (!mime->hasFormat(QLatin1String(kCaptureSourceMime))
        && !mime->hasFormat(QLatin1String(kCaptureWindowMime))) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    const int where = dropIndexAt(int(event->position().y()));
    if (where != m_dropIndex) {
        m_dropIndex = where;
        update();
    }
}

void CaptureSourceList::dragLeaveEvent(QDragLeaveEvent *)
{
    m_dropIndex = -1;
    update();
}

void CaptureSourceList::dropEvent(QDropEvent *event)
{
    const int where = dropIndexAt(int(event->position().y()));
    m_dropIndex = -1;
    update();

    const QMimeData *mime = event->mimeData();
    if (mime->hasFormat(QLatin1String(kCaptureWindowMime))) {
        event->acceptProposedAction();
        emit windowDropped(QString::fromUtf8(mime->data(QLatin1String(kCaptureWindowMime))));
        return;
    }
    if (!mime->hasFormat(QLatin1String(kCaptureSourceMime))) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    bool ok = false;
    const int from = mime->data(QLatin1String(kCaptureSourceMime)).toInt(&ok);
    if (!ok) {
        return;
    }
    const int to = landingIndex(from, where);
    if (to != from) {
        emit reorderRequested(from, to);
    }
}

void CaptureSourceList::paintEvent(QPaintEvent *)
{
    if (m_dropIndex < 0) {
        return;
    }
    // The line sits at the top of the card that would be pushed down, or under the last one
    // when the drop is past the end.
    const QVector<QRect> rects = cardRects();
    int y = 0;
    if (m_dropIndex < rects.size()) {
        y = rects[m_dropIndex].top();
    } else if (!rects.isEmpty()) {
        y = rects.last().bottom();
    }

    QPainter p(this);
    p.setPen(QPen(QColor(255, 255, 255), 2));
    p.drawLine(0, y, width(), y);
}

} // namespace openvegas
