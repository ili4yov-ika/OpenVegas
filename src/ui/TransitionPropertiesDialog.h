#pragma once

#include "video/TransitionApply.h"

#include <QDialog>
#include <QHash>
#include <QString>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QWidget;

namespace openvegas {

/**
 * Vegas's "Video Event FX" window in its Transition role: the group's parameter rows
 * (slider + value box, or a combo for a choice parameter) plus the Preset picker, and an
 * "Animate" button that reveals the keyframe strip.
 *
 * Non-modal and live — every edit writes straight through and emits transitionChanged(),
 * so the Video Preview updates while dragging a slider, matching the real window.
 */
class TransitionPropertiesDialog : public QDialog {
    Q_OBJECT
public:
    explicit TransitionPropertiesDialog(QWidget *parent = nullptr);

    /** Binds to a transition on an event's fade; `title` names the clip for the header. */
    void setTransition(const TransitionInstance &t, int eventId, bool fadeIn,
                       const QString &clipName);

    int eventId() const { return m_eventId; }
    bool isFadeIn() const { return m_fadeIn; }
    TransitionInstance transition() const { return m_transition; }

signals:
    void transitionChanged(int eventId, bool fadeIn, const TransitionInstance &t);

private:
    void rebuildParamRows();
    void applyPreset(const QString &presetName);
    void pushValue(const QString &key, double value);
    void syncPresetCombo();

    TransitionInstance m_transition;
    int m_eventId = -1;
    bool m_fadeIn = true;
    bool m_block = false;

    QLabel *m_headerLabel = nullptr;
    QLabel *m_groupLabel = nullptr;
    QComboBox *m_presetCombo = nullptr;
    QWidget *m_paramsHost = nullptr;
    QPushButton *m_animateBtn = nullptr;
    QWidget *m_animatePane = nullptr;

    QHash<QString, QSlider *> m_sliders;
    QHash<QString, QDoubleSpinBox *> m_spins;
    QHash<QString, QComboBox *> m_combos;
};

} // namespace openvegas
