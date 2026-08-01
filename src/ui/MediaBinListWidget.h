#pragma once

#include <QListWidget>

namespace openvegas {

/** Project Media list: drag to timeline + accept files from OS / Explorer. */
class MediaBinListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit MediaBinListWidget(QWidget *parent = nullptr);

    static QString mimeType();

signals:
    /** Local media files dropped onto the bin (from Explorer / Windows). */
    void filesDropped(const QStringList &paths);

protected:
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override;
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
};

} // namespace openvegas

using openvegas::MediaBinListWidget;
