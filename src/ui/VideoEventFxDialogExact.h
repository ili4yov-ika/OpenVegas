#pragma once



#include "model/ProjectModel.h"

#include "plugins/AudioPluginTypes.h"



#include <QDialog>

#include <QPixmap>

#include <QVector>



class QCheckBox;

class QComboBox;

class QDoubleSpinBox;

class QHBoxLayout;

class QLabel;

class QScrollArea;

class QStackedWidget;

class QToolButton;

class QWidget;



namespace openvegas {



class PanCropCanvas;

class FxChainNodeWidget;

class PanCropKeyframeRuler;



/** Video Event FX / Pan/Crop — Vegas-style workspace + keyframed Position/Mask. */

class VideoEventFxDialogExact : public QDialog {

    Q_OBJECT

public:

    explicit VideoEventFxDialogExact(QWidget *parent = nullptr);



    void setEvent(TrackEvent *ev, int frameW, int frameH, double playheadSec = 0.0);



private:

    enum class MoveAxis { Free, XOnly, YOnly };

    enum class PanCropChannel { Position, Mask };



    void ensurePanCropFirst();

    void buildUi();

    QWidget *buildPanCropView();

    QWidget *buildToolsColumn();

    QWidget *buildPropsPanel();

    QWidget *buildKeyframePanel();

    QWidget *buildGenericFxPage();

    void rebuildChain();

    void selectPlugin(int index);

    void setBypass(int index, bool bypass);

    void movePlugin(int from, int insertBefore);

    void addPlugins();

    void removeSelected();

    void refreshViewport();

    void syncUiFromSelected();

    void syncSelectedFromUi();

    void syncMaskFromUi();

    void refreshKeyframeLanes();

    void refreshChannelUi();

    void setChannel(PanCropChannel ch);

    void selectPositionIndex(int index);

    void selectMaskIndex(int index);

    void setPlayheadSec(double sec, bool selectNearestKf);

    void applyCanvasOptions();

    void loadMediaPreview();

    void onThumbnailReady(const QString &path);

    void flipHorizontal();

    void flipVertical();

    void syncFlipButtons();

    void navigateKeyframeFirst();

    void navigateKeyframePrev();

    void navigateKeyframeNext();

    void navigateKeyframeLast();

    void addKeyframeAtPlayhead();

    void deleteSelectedKeyframe();

    EventPanCropState &panCrop();

    PanCropKeyframe *selectedKf();

    MaskKeyframe *selectedMaskKf();

    bool isPanCropSlot(int index) const;



    TrackEvent *m_event = nullptr;

    bool m_thumbHooked = false;

    int m_frameW = 1920;

    int m_frameH = 1080;

    double m_durationSec = 10.0;

    double m_playheadSec = 0.0;

    int m_posIndex = 0;

    int m_maskIndex = 0;
    int m_selMaskPath = -1;
    int m_selMaskAnchor = -1;

    int m_selectedFx = 0;

    bool m_block = false;

    bool m_propsVisible = true;

    bool m_lockAspect = true;

    bool m_sizeAboutCenter = true;

    bool m_snapping = false;

    bool m_zoomTool = false;

    MoveAxis m_moveAxis = MoveAxis::Free;

    PanCropChannel m_channel = PanCropChannel::Position;



    QLabel *m_subtitle = nullptr;

    QLabel *m_titleExtra = nullptr;

    QLabel *m_tc = nullptr;

    QLabel *m_genericTitle = nullptr;

    PanCropCanvas *m_canvas = nullptr;

    QWidget *m_posLane = nullptr;

    QWidget *m_maskLane = nullptr;

    QWidget *m_posRow = nullptr;

    QWidget *m_maskRow = nullptr;

    QWidget *m_posTools = nullptr;

    QWidget *m_maskTools = nullptr;

    QWidget *m_posPropsBlock = nullptr;

    QWidget *m_rotPropsBlock = nullptr;

    QWidget *m_interpPropsBlock = nullptr;

    QWidget *m_sourcePropsBlock = nullptr;

    QWidget *m_maskPropsBlock = nullptr;

    QWidget *m_propsScroll = nullptr;

    QScrollArea *m_chainScroll = nullptr;

    QWidget *m_chainHost = nullptr;

    QHBoxLayout *m_chainLay = nullptr;

    QStackedWidget *m_stack = nullptr;

    PanCropKeyframeRuler *m_ruler = nullptr;

    QVector<FxChainNodeWidget *> m_nodes;

    QVector<QToolButton *> m_moveAxisBtns;



    QToolButton *m_btnProps = nullptr;

    QToolButton *m_btnEdit = nullptr;

    QToolButton *m_btnZoom = nullptr;

    QToolButton *m_btnSnap = nullptr;

    QToolButton *m_btnLockAspect = nullptr;

    QToolButton *m_btnSizeCenter = nullptr;

    QToolButton *m_btnFlipH = nullptr;

    QToolButton *m_btnFlipV = nullptr;

    QToolButton *m_btnMaskEdit = nullptr;

    QToolButton *m_btnMaskPen = nullptr;

    QToolButton *m_btnMaskDel = nullptr;

    QToolButton *m_btnMaskRect = nullptr;

    QToolButton *m_btnMaskOval = nullptr;



    QCheckBox *m_maskEnable = nullptr;
    QCheckBox *m_maskRowCheck = nullptr;

    QDoubleSpinBox *m_w = nullptr;

    QDoubleSpinBox *m_h = nullptr;

    QDoubleSpinBox *m_x = nullptr;

    QDoubleSpinBox *m_y = nullptr;

    QDoubleSpinBox *m_angle = nullptr;

    QDoubleSpinBox *m_rotX = nullptr;

    QDoubleSpinBox *m_rotY = nullptr;

    QDoubleSpinBox *m_smooth = nullptr;

    QDoubleSpinBox *m_maskAnchorX = nullptr;

    QDoubleSpinBox *m_maskAnchorY = nullptr;

    QDoubleSpinBox *m_wsZoom = nullptr;

    QDoubleSpinBox *m_wsX = nullptr;

    QDoubleSpinBox *m_wsY = nullptr;

    QDoubleSpinBox *m_grid = nullptr;

    QComboBox *m_aspect = nullptr;

    QComboBox *m_stretch = nullptr;

    QComboBox *m_pathMode = nullptr;

};



} // namespace openvegas

