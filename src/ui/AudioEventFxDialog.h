#pragma once

#include "model/ProjectModel.h"
#include "plugins/AudioPluginTypes.h"

#include <QDialog>
#include <QPoint>
#include <QVector>

/**
 * OpenVegas's hand-drawn copies of the VEGAS / Sound Forge effect dialogs — **off,
 * deliberately.**
 *
 * Chorus, Reverb, Delay, Noise Gate, Track EQ and Track Compressor each had a Qt form
 * built to look like the plug-in's own window, down to the control layout and the
 * "(0.001 to 20.0 Hz)" label text. They were never the plug-in's dialog, only a drawing
 * of one: the controls drive OpenVegas's builtin DSP, not the VEGAS effect, so the same
 * knob in the same place produced a different sound — and there was no way to tell from
 * looking.
 *
 * The real dialogs are available now. Every one of these effects is a registered
 * DirectShow filter that SoundForgeDsHost hosts, `ISpecifyPropertyPages` hands over the
 * plug-in's own property pages, and they are embedded straight into this window — the
 * genuine article, with the genuine DSP behind it.
 *
 * With this off, a builtin slot falls back to the neutral generic editor (gain, dry/wet),
 * which does not pretend to be anyone's dialog. **Do not draw more of these copies** —
 * a slot that should look like the VEGAS plug-in should *be* the VEGAS plug-in, i.e.
 * PluginFormat::DirectShow. See MARKDOWN/VEGAS_SHARED_PLUGINS_REVERSE_FULL.md §6б.
 *
 * The code is compiled out rather than deleted: it is a usable starting point if
 * OpenVegas ever grows its own effects with their own honest UI, and it is worth keeping
 * for A/B comparison against the real plug-in.
 */
#ifndef OPENVEGAS_EMULATED_AUDIO_FX_UI
#define OPENVEGAS_EMULATED_AUDIO_FX_UI 0
#endif

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

class PluginScanner;

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
    /** Optional OFX/VST path scanner for Plug-In Chooser (fx+). */
    void setPluginScanner(PluginScanner *scanner) { m_pluginScanner = scanner; }
    /** Open Plug-In Chooser and append selected plugs (same as fx+). */
    void addPlugins();

private:
    void buildUi();
    void rebuildChain();
    void selectPlugin(int index);
    void refreshViewport();
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
#if OPENVEGAS_EMULATED_AUDIO_FX_UI
    // Copies of plug-in dialogs for effects outside the default chain — see the note above.
    QWidget *buildChorusEditor(FxSlot &slot);
    QWidget *buildDelayEditor(FxSlot &slot);
    QWidget *buildReverbEditor(FxSlot &slot);
#endif
    // The standard audio-track chain: OpenVegas's own track effects, always compiled in.
    QWidget *buildNoiseGateEditor(FxSlot &slot);
    QWidget *buildTrackEqEditor(FxSlot &slot);
    QWidget *buildTrackCompressorEditor(FxSlot &slot);
    QWidget *buildGenericBuiltinEditor(FxSlot &slot);
    QWidget *buildVstEditorPage(FxSlot &slot);

    Mode m_mode = Mode::Event;
    TrackEvent *m_event = nullptr;
    Track *m_track = nullptr;
    QVector<FxSlot> *m_chain = nullptr;
    PluginScanner *m_pluginScanner = nullptr;

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
