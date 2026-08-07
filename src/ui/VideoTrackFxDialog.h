#pragma once

#include "model/ProjectModel.h"
#include "plugins/AudioPluginTypes.h"
#include "plugins/OfxHost.h"

#include <QDialog>
#include <QVector>

class QLabel;
class QComboBox;
class QHBoxLayout;
class QVBoxLayout;
class QPushButton;
class QScrollArea;
class QWidget;

namespace openvegas {

class PluginScanner;
class FxChainNodeWidget;
class PanCropKeyframeRuler;

/**
 * Vegas-style Video Track FX window: horizontal plug-in chain + real OFX
 * params (or approximate fallback) + keyframe lanes — the track-scoped
 * counterpart of VideoEventFxDialogExact's generic FX page, minus Pan/Crop
 * (which is per-clip, never a Track FX chain member).
 */
class VideoTrackFxDialog : public QDialog {
    Q_OBJECT
public:
    explicit VideoTrackFxDialog(QWidget *parent = nullptr);

    /** durationSec/playheadSec drive the keyframe timeline (project-scale, not per-event). */
    void setTrack(Track *track, double durationSec, double playheadSec);
    void setPluginScanner(PluginScanner *scanner) { m_pluginScanner = scanner; }
    void addPlugins();

private:
    void buildUi();
    void rebuildChain();
    void selectPlugin(int index);
    void setBypass(int index, bool bypass);
    void movePlugin(int from, int insertBefore);
    void removeSelected();
    void rebuildParamsUi();

    QWidget *buildKeyframePanel();
    void rebuildKeyframeLanes();
    void refreshKeyframeLanes();
    void setCurvesMode(bool curves);
    void selectKeyframeIndex(int pointIndex);
    void navigateFirst();
    void navigatePrev();
    void navigateNext();
    void navigateLast();
    void addKeyframeAtPlayhead();
    void addKeyframeAtTime(double timeSec);
    void deleteSelectedKeyframe();
    void moveKeyframe(int pointIndex, double timeSec, bool finalize);
    void setPlayheadSec(double sec);

    QString fxMasterAutomationId(const FxSlot &slot) const;
    QString fxParamAutomationId(const FxSlot &slot, const QString &paramKey) const;
    AutomationLane *findAutomationLane(const QString &targetId);
    AutomationLane &ensureAutomationLane(const QString &targetId);
    double currentParamValue(const FxSlot &slot, const QString &paramKey) const;

    Track *m_track = nullptr;
    PluginScanner *m_pluginScanner = nullptr;
    double m_durationSec = 10.0;
    double m_playheadSec = 0.0;

    int m_selectedFx = 0;
    int m_kfIndex = 0;
    int m_kfFocusFx = -1;
    QString m_kfParamKey;
    bool m_kfCurves = true;

    QLabel *m_subtitle = nullptr;
    QScrollArea *m_chainScroll = nullptr;
    QWidget *m_chainHost = nullptr;
    QHBoxLayout *m_chainLay = nullptr;
    QVector<FxChainNodeWidget *> m_nodes;

    QLabel *m_genericTitle = nullptr;
    QLabel *m_genericHint = nullptr;
    QWidget *m_paramsHost = nullptr;
    QVBoxLayout *m_paramsLay = nullptr;

    QWidget *m_kfPanel = nullptr;
    PanCropKeyframeRuler *m_kfRuler = nullptr;
    QWidget *m_kfLanesHost = nullptr;
    QVBoxLayout *m_kfLanesLay = nullptr;
    QLabel *m_kfTc = nullptr;
    QPushButton *m_btnLanes = nullptr;
    QPushButton *m_btnCurves = nullptr;
};

} // namespace openvegas
