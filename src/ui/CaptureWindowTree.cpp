#include "ui/CaptureWindowTree.h"

#include <QHeaderView>
#include <QMimeData>

namespace openvegas {

namespace {

constexpr int kWindowIdRole = Qt::UserRole + 1;

/**
 * The monitor a window is mostly on.
 *
 * By its centre rather than its top-left corner. A maximised window's frame starts a few
 * pixels outside the monitor it fills — that is where the drop shadow lives — so filing by
 * the corner puts most of a tidy desktop under "Elsewhere", which is exactly what it did.
 */
int screenForWindow(const QVector<CaptureSource> &screens, const CaptureSource &window)
{
    const QPoint centre = window.origin
                          + QPoint(window.nativeSize.width() / 2, window.nativeSize.height() / 2);
    for (int i = 0; i < screens.size(); ++i) {
        if (QRect(screens[i].origin, screens[i].nativeSize).contains(centre)) {
            return i;
        }
    }
    // Nothing contained it — fall back to the nearest monitor by centre distance, so a
    // window half off the desktop still lands somewhere sensible.
    int best = -1;
    qint64 bestDistance = 0;
    for (int i = 0; i < screens.size(); ++i) {
        const QPoint c = QRect(screens[i].origin, screens[i].nativeSize).center();
        const qint64 dx = c.x() - centre.x();
        const qint64 dy = c.y() - centre.y();
        const qint64 distance = dx * dx + dy * dy;
        if (best < 0 || distance < bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

} // namespace

CaptureWindowTree::CaptureWindowTree(QWidget *parent)
    : QTreeWidget(parent)
{
    setObjectName(QStringLiteral("captureWindowTree"));
    setHeaderHidden(true);
    setRootIsDecorated(true);
    setIndentation(12);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setUniformRowHeights(true);

    connect(this, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *current, QTreeWidgetItem *) {
                if (!current) {
                    return;
                }
                const QString id = current->data(0, kWindowIdRole).toString();
                if (id.isEmpty()) {
                    return; // a monitor heading, not a window
                }
                emit windowSelected(windowById(id));
            });
}

void CaptureWindowTree::setContents(const QVector<CaptureSource> &screens,
                                    const QVector<CaptureSource> &windows)
{
    m_windows = windows;
    clear();

    // Built on demand: a monitor with nothing open on it is a heading with nothing under
    // it, and a list of those is harder to read than a shorter list.
    QVector<QTreeWidgetItem *> groups(screens.size(), nullptr);
    auto groupFor = [&](int index) {
        if (!groups[index]) {
            groups[index] = new QTreeWidgetItem(this, {screens[index].name});
            groups[index]->setFlags(Qt::ItemIsEnabled); // a heading, not something to drag
        }
        return groups[index];
    };
    // Only reached when the scan found no monitors at all. A window still has to be
    // listed then, or it would silently vanish from the picker.
    QTreeWidgetItem *elsewhere = nullptr;

    for (const CaptureSource &window : windows) {
        const int screen = screenForWindow(screens, window);
        QTreeWidgetItem *parent = nullptr;
        if (screen >= 0 && screen < groups.size()) {
            parent = groupFor(screen);
        } else {
            if (!elsewhere) {
                elsewhere = new QTreeWidgetItem(this, {tr("Elsewhere")});
                elsewhere->setFlags(Qt::ItemIsEnabled);
                elsewhere->setExpanded(true);
            }
            parent = elsewhere;
        }
        auto *item = new QTreeWidgetItem(parent, {window.name});
        item->setData(0, kWindowIdRole, window.id);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        item->setToolTip(0, tr("Drag onto the sources on the left to record it"));
    }
    expandAll();
}

CaptureSource CaptureWindowTree::windowById(const QString &id) const
{
    for (const CaptureSource &window : m_windows) {
        if (window.id == id) {
            return window;
        }
    }
    return {};
}

QStringList CaptureWindowTree::mimeTypes() const
{
    return {QLatin1String(kCaptureWindowMime)};
}

Qt::DropActions CaptureWindowTree::supportedDropActions() const
{
    // Copy, not move: dragging a window to the sources records it, it does not take it off
    // the desktop inventory — the same window can be picked again later.
    return Qt::CopyAction;
}

QMimeData *CaptureWindowTree::mimeData(const QList<QTreeWidgetItem *> &items) const
{
    for (QTreeWidgetItem *item : items) {
        const QString id = item->data(0, kWindowIdRole).toString();
        if (id.isEmpty()) {
            continue;
        }
        auto *mime = new QMimeData();
        mime->setData(QLatin1String(kCaptureWindowMime), id.toUtf8());
        mime->setText(item->text(0));
        return mime;
    }
    return nullptr;
}

} // namespace openvegas
