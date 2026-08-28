#pragma once

#include <QRect>
#include <QVector>
#include <QWidget>

class QVBoxLayout;

namespace openvegas {

/**
 * The column of video source cards, and the two kinds of drop it takes.
 *
 * A card dropped on it moves to where it was dropped — the order matters, because the
 * take's resolution follows the first video source unless told otherwise.
 *
 * A window dragged in from the list on the right becomes a source. That direction is the
 * whole gesture: the right-hand panel is everything open on screen, this one is what is
 * going to be recorded, and moving something from one to the other is how you say so.
 *
 * The insertion point is drawn while a drag is over the column, because a drop with no
 * visible target is a guess.
 */
class CaptureSourceList : public QWidget {
    Q_OBJECT
public:
    explicit CaptureSourceList(QWidget *parent = nullptr);

    /** The layout the cards live in; the window fills it. */
    QVBoxLayout *cardLayout() const { return m_layout; }

    /**
     * Which gap between rows a drop at `y` falls in, given where the rows are.
     *
     * Gaps are counted, not rows: with three rows there are four places a card can land,
     * and the one past the last row is how a card is moved to the end. Past the middle of
     * a row means "after it", so dropping on the lower half of the second row lands third.
     *
     * Static and taking plain rectangles because this is the arithmetic that goes wrong,
     * and it can be checked without a desktop, a drag, or a mouse.
     */
    static int gapAt(const QVector<QRect> &rows, int y);

    /**
     * Where the row dragged from `from` ends up when it is dropped in gap `gap`.
     *
     * Gaps are counted with the row still in the list, so a row moved downwards lands one
     * place short once it has been lifted out. Everything above it is unaffected.
     */
    static int landingIndex(int from, int gap);

signals:
    /** A card was dropped: move the source at `fromIndex` so it sits at `toIndex`. */
    void reorderRequested(int fromIndex, int toIndex);
    /** A window was dropped in from the right-hand list. */
    void windowDropped(const QString &windowId);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    /** The card rectangles, in list order. */
    QVector<QRect> cardRects() const;
    /** `gapAt()` over this column's own cards. */
    int dropIndexAt(int y) const;

    QVBoxLayout *m_layout = nullptr;
    /** Where the insertion line is drawn, or -1 when no drag is over the column. */
    int m_dropIndex = -1;
};

} // namespace openvegas
