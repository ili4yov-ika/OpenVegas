#pragma once

#include <QWidget>

class QFrame;

namespace openvegas {

class TimelineView;
class ProjectModel;

/**
 * Vegas-style timeline scroll chrome:
 *   [ TimelineView | V-track + zoom+/- ]
 *   [ corner | H-track + grips | zoom+/- ] [br]
 */
class TimelineScrollHost : public QWidget {
    Q_OBJECT
public:
    explicit TimelineScrollHost(ProjectModel *model, QWidget *parent = nullptr);

    TimelineView *timeline() const { return m_timeline; }

public slots:
    void syncFromTimeline();
    void setCornerWidth(int headerWidth);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    enum class DragKind { None, HThumb, VThumb, HGripL, HGripR };

    void buildUi();
    void updateThumbs();
    void applyHScrollFromThumb(int thumbX);
    void applyVScrollFromThumb(int thumbY);
    void zoomTime(double factor);
    void zoomTracks(double factor);

    TimelineView *m_timeline = nullptr;
    ProjectModel *m_model = nullptr;

    QWidget *m_vCol = nullptr;
    QWidget *m_vTrack = nullptr;
    QWidget *m_vThumb = nullptr;
    QWidget *m_hRow = nullptr;
    QWidget *m_hCorner = nullptr;
    QWidget *m_hTrack = nullptr;
    QWidget *m_hThumb = nullptr;
    QWidget *m_brCorner = nullptr;

    DragKind m_drag = DragKind::None;
    int m_dragOrigin = 0;
    int m_dragScrollOrigin = 0;
    double m_dragPpsOrigin = 40.0;
    int m_dragThumbOrigin = 0;
};

} // namespace openvegas
