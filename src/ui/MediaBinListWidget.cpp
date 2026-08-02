#include "ui/MediaBinListWidget.h"
#include "ui/MediaThumbHoverScrub.h"
#include "io/MediaFilmstripCache.h"
#include "io/MediaMime.h"
#include "io/MediaThumbCache.h"

#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QStyledItemDelegate>

namespace openvegas {
namespace {

QRect mediaBinThumbRect(const QRect &itemRect)
{
    const QRect r = itemRect.adjusted(2, 2, -2, -2);
    return QRect(r.left() + (r.width() - 120) / 2, r.top() + 2, 120, 68);
}

class MediaBinGridDelegate : public QStyledItemDelegate {
public:
    explicit MediaBinGridDelegate(MediaBinListWidget *view)
        : QStyledItemDelegate(view)
        , m_view(view)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(132, 100);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        painter->save();
        const QRect r = option.rect.adjusted(2, 2, -2, -2);
        if (option.state & QStyle::State_Selected) {
            painter->fillRect(option.rect.adjusted(1, 1, -1, -1), QColor(0x00, 0x78, 0xd7, 60));
            painter->setPen(QColor(0x00, 0x78, 0xd7));
            painter->drawRect(option.rect.adjusted(1, 1, -1, -1));
        } else if (option.state & QStyle::State_MouseOver) {
            painter->fillRect(option.rect.adjusted(1, 1, -1, -1), QColor(0x25, 0x25, 0x25));
        }

        const QString path = index.data(Qt::UserRole + 2).toString();
        const QString kind = index.data(Qt::UserRole).toString();
        const QRect thumb = mediaBinThumbRect(option.rect);

        if (!path.isEmpty()) {
            const bool scrub = m_view && m_view->isScrubbing(path);
            const int mx = scrub ? m_view->scrubMouseX() : -1;
            const double hint = index.data(Qt::UserRole + 3).toDouble();
            MediaThumbHoverScrub::paintThumb(painter, thumb, path, kind, scrub, mx, hint);
        } else {
            const QIcon ico = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
            ico.paint(painter, thumb);
        }

        QRect textR(r.left(), thumb.bottom() + 4, r.width(), r.height() - thumb.height() - 6);
        painter->setPen(QColor(0xc0, 0xc0, 0xc0));
        QFont f = option.font;
        f.setPixelSize(10);
        painter->setFont(f);
        painter->drawText(textR, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                          index.data(Qt::DisplayRole).toString());
        painter->restore();
    }

private:
    MediaBinListWidget *m_view = nullptr;
};

} // namespace

MediaBinListWidget::MediaBinListWidget(QWidget *parent)
    : QListWidget(parent)
{
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    setItemDelegate(new MediaBinGridDelegate(this));
    connect(&MediaFilmstripCache::instance(), &MediaFilmstripCache::frameReady, this,
            [this](const QString &) { viewport()->update(); });
    connect(&MediaThumbCache::instance(), &MediaThumbCache::thumbnailReady, this,
            [this](const QString &) { viewport()->update(); });
}

void MediaBinListWidget::clearScrub()
{
    if (m_scrubPath.isEmpty() && m_scrubMouseX < 0) {
        return;
    }
    m_scrubPath.clear();
    m_scrubMouseX = -1;
    viewport()->update();
}

void MediaBinListWidget::updateScrubAt(const QPoint &pos)
{
    QListWidgetItem *item = itemAt(pos);
    if (!item) {
        clearScrub();
        return;
    }
    const QString path = item->data(Qt::UserRole + 2).toString();
    if (!MediaThumbHoverScrub::isVideoPath(path)) {
        clearScrub();
        return;
    }
    const QRect thumb = mediaBinThumbRect(visualItemRect(item));
    if (!thumb.contains(pos)) {
        clearScrub();
        return;
    }
    if (m_scrubPath != path || m_scrubMouseX != pos.x()) {
        m_scrubPath = path;
        m_scrubMouseX = pos.x();
        viewport()->update(visualItemRect(item));
    }
}

bool MediaBinListWidget::viewportEvent(QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseMove: {
        const auto *me = static_cast<QMouseEvent *>(event);
        updateScrubAt(me->pos());
        break;
    }
    case QEvent::Leave:
    case QEvent::HoverLeave:
        clearScrub();
        break;
    default:
        break;
    }
    return QListWidget::viewportEvent(event);
}

void MediaBinListWidget::leaveEvent(QEvent *event)
{
    clearScrub();
    QListWidget::leaveEvent(event);
}

QString MediaBinListWidget::mimeType()
{
    return MediaMime::mimeType();
}

QStringList MediaBinListWidget::mimeTypes() const
{
    return {MediaMime::mimeType(), QStringLiteral("text/plain"), QStringLiteral("text/uri-list")};
}

QMimeData *MediaBinListWidget::mimeData(const QList<QListWidgetItem *> &items) const
{
    if (items.isEmpty()) {
        return nullptr;
    }

    QStringList paths;
    QStringList blocks;
    QStringList names;
    for (QListWidgetItem *item : items) {
        if (!item) {
            continue;
        }
        const QString kind = item->data(Qt::UserRole).toString();
        const QString name = item->text();
        const QString path = item->data(Qt::UserRole + 2).toString();
        const double lengthSec = item->data(Qt::UserRole + 3).toDouble();
        if (!path.isEmpty()) {
            paths << path;
        }
        blocks << QStringLiteral("%1\n%2\n%3\n%4")
                      .arg(kind, name, path)
                      .arg(lengthSec > 0.05 ? lengthSec : 0.0);
        names << name;
    }

    if (!paths.isEmpty()) {
        // Prefer path-based MIME (includes urls for OS interoperability)
        if (QMimeData *fromPaths = MediaMime::fromLocalPaths(paths)) {
            // Preserve length hints from cards when present
            if (!blocks.isEmpty()) {
                fromPaths->setData(MediaMime::mimeType(),
                                   blocks.join(QStringLiteral("\n---\n")).toUtf8());
            }
            return fromPaths;
        }
    }

    auto *md = new QMimeData;
    md->setData(MediaMime::mimeType(), blocks.join(QStringLiteral("\n---\n")).toUtf8());
    md->setText(names.join(QLatin1Char('\n')));
    return md;
}

void MediaBinListWidget::startDrag(Qt::DropActions supportedActions)
{
    const QList<QListWidgetItem *> items = selectedItems();
    if (items.isEmpty()) {
        return;
    }
    QMimeData *md = mimeData(items);
    if (!md) {
        return;
    }

    auto *drag = new QDrag(this);
    drag->setMimeData(md);

    QListWidgetItem *first = items.first();
    if (first && !first->icon().isNull()) {
        const QPixmap pm = first->icon().pixmap(QSize(120, 68));
        drag->setPixmap(pm);
        drag->setHotSpot(QPoint(pm.width() / 2, pm.height() / 2));
    }

    drag->exec(supportedActions, Qt::CopyAction);
}

void MediaBinListWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (MediaMime::hasMediaPayload(event->mimeData())) {
        event->acceptProposedAction();
        return;
    }
    QListWidget::dragEnterEvent(event);
}

void MediaBinListWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (MediaMime::hasMediaPayload(event->mimeData())) {
        event->acceptProposedAction();
        return;
    }
    QListWidget::dragMoveEvent(event);
}

void MediaBinListWidget::dropEvent(QDropEvent *event)
{
    QStringList names;
    QStringList paths;
    MediaMime::parse(event->mimeData(), &names, nullptr, &paths, nullptr);
    if (!paths.isEmpty()) {
        emit filesDropped(paths);
        event->acceptProposedAction();
        return;
    }
    // Ignore internal rearrange
    event->ignore();
}

} // namespace openvegas
