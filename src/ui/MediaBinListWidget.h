#pragma once

#include <QListWidget>
#include <QPoint>
#include <QString>

class QEvent;

namespace openvegas {

/** Project Media list: drag to timeline + accept files from OS / Explorer + hover scrub. */
class MediaBinListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit MediaBinListWidget(QWidget *parent = nullptr);

    static QString mimeType();

    QString scrubPath() const { return m_scrubPath; }
    int scrubMouseX() const { return m_scrubMouseX; }
    bool isScrubbing(const QString &path) const
    {
        return !m_scrubPath.isEmpty() && m_scrubPath == path && m_scrubMouseX >= 0;
    }

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
    bool viewportEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void clearScrub();
    void updateScrubAt(const QPoint &pos);

    QString m_scrubPath;
    int m_scrubMouseX = -1;
};

} // namespace openvegas

using openvegas::MediaBinListWidget;
