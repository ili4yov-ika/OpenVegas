#pragma once

#include "model/ProjectModel.h"
#include "plugins/AudioPluginTypes.h"

#include <QDialog>
#include <QPoint>
#include <QVector>

class QLabel;
class QComboBox;
class QScrollArea;
class QStackedWidget;
class QToolButton;
class QHBoxLayout;
class QCheckBox;
class QSlider;
class QDoubleSpinBox;
class QWidget;
class QMouseEvent;
class QPaintEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QDragLeaveEvent;

namespace openvegas {

/** One node in the Vegas-style horizontal FX chain (draggable to reorder). */
class FxChainNodeWidget : public QWidget {
    Q_OBJECT
public:
    explicit FxChainNodeWidget(int index, const FxSlot &slot, QWidget *parent = nullptr);

    int index() const { return m_index; }
    void setIndex(int i) { m_index = i; }
    void setSelected(bool on);
    bool isBypassed() const;

signals:
    void selected(int index);
    void bypassToggled(int index, bool bypass);
    /** Drop: move plug-in `from` so it lands at `insertBefore` (pre-remove index). */
    void moveRequested(int from, int insertBefore);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    static constexpr const char *kMime = "application/x-openvegas-fxchain-index";
    int insertBeforeFromPos(const QPoint &pos) const;
    void clearDropIndicator();

    int m_index = 0;
    bool m_selected = false;
    bool m_pressActive = false;
    QPoint m_dragStart;
    /** -1 none, 0 draw bar on left, 1 on right */
    int m_dropSide = -1;
    QCheckBox *m_enabled = nullptr;
    QLabel *m_name = nullptr;
};

/**
 * Vegas-style Audio Event FX / Track FX / Assignable FX window:
 * header → horizontal plug-in chain → preset bar → plug-in UI viewport.
 */
class AudioEventFxDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mode { Event, Track, Chain };

    explicit AudioEventFxDialog(QWidget *parent = nullptr);

    void setEvent(TrackEvent *ev);
    void setTrack(Track *track);
    void setChain(QVector<FxSlot> *chain, const QString &title);
    /** Select chain node by display name (case-insensitive); no-op if missing. */
    void selectByName(const QString &displayName);
    Mode mode() const { return m_mode; }

private:
    void buildUi();
    void rebuildChain();
    void selectPlugin(int index);
    void refreshViewport();
    void addPlugins();
    void removeSelected();
    void setBypass(int index, bool bypass);
    void movePlugin(int from, int insertBefore);
    void scrollChain(int dx);
    QVector<FxSlot> *chain();
    QString eventTitle() const;

public:
    static QString formatSlotLabel(const FxSlot &s);

private:
    QWidget *buildBuiltinEditor(FxSlot &slot);
    QWidget *buildColorGradingEditor(FxSlot &slot);
    QWidget *buildChorusEditor(FxSlot &slot);
    QWidget *buildNoiseGateEditor(FxSlot &slot);
    QWidget *buildTrackEqEditor(FxSlot &slot);
    QWidget *buildTrackCompressorEditor(FxSlot &slot);
    QWidget *buildGenericBuiltinEditor(FxSlot &slot);
    QWidget *buildVstPlaceholder(const FxSlot &slot);

    Mode m_mode = Mode::Event;
    TrackEvent *m_event = nullptr;
    Track *m_track = nullptr;
    QVector<FxSlot> *m_chain = nullptr;

    QLabel *m_eventIcon = nullptr;
    QLabel *m_eventName = nullptr;
    QScrollArea *m_chainScroll = nullptr;
    QWidget *m_chainHost = nullptr;
    QHBoxLayout *m_chainLay = nullptr;
    QComboBox *m_presetCombo = nullptr;
    QStackedWidget *m_viewport = nullptr;
    QLabel *m_emptyHint = nullptr;

    int m_selected = -1;
    QVector<FxChainNodeWidget *> m_nodes;
};

} // namespace openvegas
