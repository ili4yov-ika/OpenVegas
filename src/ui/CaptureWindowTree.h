#pragma once

#include "capture/CaptureSource.h"

#include <QTreeWidget>
#include <QVector>

namespace openvegas {

/** MIME type a window entry carries while being dragged onto the source column. */
const char *const kCaptureWindowMime = "application/x-openvegas-capture-window";

/**
 * Everything open on screen, grouped by the monitor it is on.
 *
 * A flat list of window titles is hard to read on a multi-monitor desktop — half of them
 * are the same application — so they are grouped under the display they sit on, which is
 * usually how someone remembers where a window is.
 *
 * Entries are dragged out of here and onto the source column to be recorded. Nothing is
 * recorded from this panel directly: it is the inventory, not the take.
 */
class CaptureWindowTree : public QTreeWidget {
    Q_OBJECT
public:
    explicit CaptureWindowTree(QWidget *parent = nullptr);

    /**
     * Fill the tree from the sources found by a scan.
     *
     * @param screens the monitors, which become the groups.
     * @param windows the windows, each filed under the monitor it is mostly on.
     */
    void setContents(const QVector<CaptureSource> &screens,
                     const QVector<CaptureSource> &windows);

    /** The window an id names, or an empty source when it is no longer listed. */
    CaptureSource windowById(const QString &id) const;

signals:
    /** A window row was selected; the panel below shows it. */
    void windowSelected(const CaptureSource &window);

protected:
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override;
    QStringList mimeTypes() const override;
    Qt::DropActions supportedDropActions() const override;

private:
    QVector<CaptureSource> m_windows;
};

} // namespace openvegas
