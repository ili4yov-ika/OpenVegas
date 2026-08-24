#pragma once

#include "model/ProjectModel.h"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QCheckBox;
class QWidget;

namespace openvegas {

class TrackMotionCanvas;

/** Vegas-style Track Motion window for a video track (Position / Shadow / Glow KF). */
class TrackMotionDialog : public QDialog {
    Q_OBJECT
public:
    explicit TrackMotionDialog(QWidget *parent = nullptr);

    void setTrack(Track *track, int frameW, int frameH, double durationSec, double playheadSec = 0.0);

signals:
    /** A value changed: the preview and the timeline should catch up while the window
     *  stays open, which is the whole point of it not being modal. */
    void motionChanged();

private:
    void buildUi();
    QWidget *buildToolbar();
    QWidget *buildPropsPanel();
    QWidget *buildKeyframePanel();
    void syncUiFromSelected();
    void syncSelectedFromUi();
    void refreshTitle();
    void refreshKeyframeLanes();
    void selectMotionIndex(int index);
    void setPlayheadSec(double sec);
    TrackMotionKeyframe *selectedMotion();
    TrackMotionState &motion();

    Track *m_track = nullptr;
    int m_frameW = 1920;
    int m_frameH = 1080;
    double m_durationSec = 10.0;
    double m_playheadSec = 0.0;
    int m_motionIndex = 0;

    QComboBox *m_preset = nullptr;
    QComboBox *m_compMode = nullptr;
    TrackMotionCanvas *m_canvas = nullptr;
    QWidget *m_motionLane = nullptr;
    QWidget *m_shadowLane = nullptr;
    QWidget *m_glowLane = nullptr;

    QDoubleSpinBox *m_x = nullptr;
    QDoubleSpinBox *m_y = nullptr;
    QDoubleSpinBox *m_w = nullptr;
    QDoubleSpinBox *m_h = nullptr;
    QDoubleSpinBox *m_orient = nullptr;
    QDoubleSpinBox *m_rot = nullptr;
    QDoubleSpinBox *m_rotX = nullptr;
    QDoubleSpinBox *m_rotY = nullptr;
    QDoubleSpinBox *m_kfSmooth = nullptr;
    QComboBox *m_kfType = nullptr;
    QDoubleSpinBox *m_wsZoom = nullptr;
    QDoubleSpinBox *m_wsX = nullptr;
    QDoubleSpinBox *m_wsY = nullptr;
    QDoubleSpinBox *m_snapGrid = nullptr;
    QDoubleSpinBox *m_snapRot = nullptr;

    QWidget *m_shadowGroup = nullptr;
    QDoubleSpinBox *m_shadowBlur = nullptr;
    QDoubleSpinBox *m_shadowInt = nullptr;
    QCheckBox *m_shadowEnable = nullptr;
    QCheckBox *m_glowEnable = nullptr;

    QLabel *m_tc = nullptr;
    bool m_block = false;
};

} // namespace openvegas
