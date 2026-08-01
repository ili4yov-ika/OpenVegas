#include "ui/MediaBinListWidget.h"
#include "io/MediaMime.h"

#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPixmap>

namespace openvegas {

MediaBinListWidget::MediaBinListWidget(QWidget *parent)
    : QListWidget(parent)
{
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);
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
