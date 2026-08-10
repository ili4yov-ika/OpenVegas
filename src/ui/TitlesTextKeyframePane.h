#pragma once

#include "video/TitlesTextApply.h"

#include <QHash>
#include <QVector>
#include <QWidget>

class QLineEdit;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace openvegas {

class TitlesTextKeyframeGraph;
class PanCropKeyframeRuler;

/**
 * Vegas's keyframe pane at the bottom of the Video Media Generator window: a parameter
 * tree on the left, a time ruler + keyframe area on the right, and a toolbar with
 * keyframe navigation and the Lanes / Curves view toggle.
 *
 * "Lanes" draws one row of keyframe markers per tree row (aggregated diamonds on the
 * group rows); "Curves" overlays every animated parameter's value curve in one graph
 * with the selected parameter highlighted — exactly the two modes Vegas offers.
 *
 * Holds a copy of the params and emits paramsEdited() with the updated copy; the owning
 * dialog stays the single writer to the TrackEvent.
 */
class TitlesTextKeyframePane : public QWidget {
    Q_OBJECT
public:
    explicit TitlesTextKeyframePane(QWidget *parent = nullptr);

    void setParams(const TitlesTextParams &params, double lengthSec, double frameRateFps);
    void setPlayheadSec(double localSec);
    double playheadSec() const { return m_playheadSec; }

    /** Adds a keyframe for `key` at the playhead holding the parameter's current value. */
    void addKeyframeForParam(const QString &key);
    /** True when `key` has at least one keyframe (drives the clock-button icon state). */
    bool isAnimated(const QString &key) const;

signals:
    void paramsEdited(const TitlesTextParams &params);
    /** Playhead scrubbed inside the pane (event-local seconds). */
    void playheadMoved(double localSec);

private:
    friend class TitlesTextKeyframeGraph;

    void buildUi();
    void rebuildTree();
    void syncToolbarState();
    void selectParam(const QString &key);
    QString selectedParamKey() const { return m_selectedKey; }
    /** Keyframe times of the selected parameter, or the union across all lanes for a
     *  group / the root row (which is what Vegas's navigation buttons step through). */
    QVector<double> navigationTimes() const;
    void goToKeyframe(int direction, bool toEnd);
    void addKeyframeAtPlayhead();
    void deleteKeyframeAtPlayhead();
    void commit();

    TitlesTextParams m_params;
    double m_lengthSec = 10.0;
    double m_frameRateFps = 30.0;
    double m_playheadSec = 0.0;
    QString m_selectedKey;
    bool m_curvesMode = true;
    bool m_syncCursor = true;

    QTreeWidget *m_tree = nullptr;
    TitlesTextKeyframeGraph *m_graph = nullptr;
    PanCropKeyframeRuler *m_ruler = nullptr;
    QToolButton *m_syncBtn = nullptr;
    QToolButton *m_firstBtn = nullptr;
    QToolButton *m_prevBtn = nullptr;
    QToolButton *m_nextBtn = nullptr;
    QToolButton *m_lastBtn = nullptr;
    QToolButton *m_addBtn = nullptr;
    QToolButton *m_deleteBtn = nullptr;
    QLineEdit *m_timecodeEdit = nullptr;
    QToolButton *m_lanesBtn = nullptr;
    QToolButton *m_curvesBtn = nullptr;
    /** Tree row per animatable param key, for row-aligned lane painting. */
    QHash<QString, QTreeWidgetItem *> m_paramItems;
};

} // namespace openvegas
