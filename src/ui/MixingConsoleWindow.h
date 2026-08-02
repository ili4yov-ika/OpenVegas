#pragma once

#include <QWidget>
#include <QVector>
#include <QPoint>

class QListWidget;
class QHBoxLayout;
class QSlider;
class QButtonGroup;
class QWidget;
class QMenu;
class QAction;
class QActionGroup;
class QToolButton;
class QEvent;
class QMouseEvent;
class QFrame;

namespace openvegas {

class ProjectModel;
class MixerChannelStrip;

/** Vegas-style Mixing Console: sidebar filters + channel strips. */
class MixingConsoleWindow : public QWidget {
    Q_OBJECT
public:
    explicit MixingConsoleWindow(QWidget *parent = nullptr);

    void setProject(ProjectModel *project);
    void refreshFromProject();
    /** Update VU meters from realtime peaks (0…1 linear). */
    void setMasterMeter(float peakL, float peakR);
    void setTrackMeter(int trackId, float peakL, float peakR);

    /** Mirror Master Bus Downmix / Dim buttons (mode: 0=Surround, 1=Stereo, 2=Mono). */
    void syncMonitorButtons(int downmixMode, bool dimOutput);

signals:
    /** Emitted after project track list changes from this window. */
    void tracksChanged();
    void documentEditBegan();
    void documentEditCommitted(const QString &text);
    void downmixOutputCycled();
    void dimOutputChanged(bool on);

protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class ChannelWidth { Narrow, Default, Wide };

    void buildUi();
    void rebuildStrips();
    void applyFilter();
    void applyChannelWidth();
    void applyChromeVisibility();
    void showAllChannels();
    void insertAudioTrack();
    void insertAssignableFx();
    void insertBus();
    void insertInputBus();
    void openAssignableFx(int busId);
    MixerChannelStrip *addStrip(const QString &title, const QString &subtitle, const QString &route,
                                const QColor &swatch, int kind, int trackNumber = 0);
    int stripIndexAtX(int x) const;
    int insertIndexAtX(int x) const;
    void updateReorderGhost(int insertIndex);
    void clearReorderUi();

    ProjectModel *m_project = nullptr;
    QWidget *m_sidebar = nullptr;
    QListWidget *m_channelList = nullptr;
    QWidget *m_viewControls = nullptr;
    QHBoxLayout *m_stripsLay = nullptr;
    QWidget *m_stripsHost = nullptr;
    QFrame *m_insertMarker = nullptr;
    QButtonGroup *m_filterGroup = nullptr;
    QSlider *m_zoomSlider = nullptr;
    QVector<MixerChannelStrip *> m_strips;
    ChannelWidth m_channelWidth = ChannelWidth::Default;
    bool m_showChannelList = true;
    bool m_showViewControls = true;
    bool m_faderTicks = true;
    QAction *m_actChannelList = nullptr;
    QAction *m_actViewControls = nullptr;
    QAction *m_actFaderTicks = nullptr;
    QAction *m_actDownmix = nullptr;
    QAction *m_actDim = nullptr;
    QActionGroup *m_widthGroup = nullptr;
    QToolButton *m_downmixBtn = nullptr;
    QToolButton *m_dimBtn = nullptr;

    bool m_reordering = false;
    int m_reorderFrom = -1;
    QPoint m_pressPos;
    MixerChannelStrip *m_pressStrip = nullptr;
};

} // namespace openvegas
