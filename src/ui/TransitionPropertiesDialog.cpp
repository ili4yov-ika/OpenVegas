#include "ui/TransitionPropertiesDialog.h"

#include "ui/IconFactory.h"
#include "ui/KeyframeLaneWidgets.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace openvegas {
namespace {

/** Sliders work in integer steps; 1000 is enough for the 4-decimal parameters. */
constexpr int kSliderSteps = 1000;

int toSliderPos(double value, const TransitionParamInfo &info)
{
    const double span = info.maxValue > info.minValue ? info.maxValue - info.minValue : 1.0;
    return std::clamp(int(std::lround((value - info.minValue) / span * kSliderSteps)), 0,
                      kSliderSteps);
}

double fromSliderPos(int pos, const TransitionParamInfo &info)
{
    const double span = info.maxValue > info.minValue ? info.maxValue - info.minValue : 1.0;
    const double v = info.minValue + span * (double(pos) / kSliderSteps);
    // Integer rows (Divisions, Extra spins) must land exactly on whole numbers.
    return info.decimals == 0 ? std::lround(v) : v;
}

} // namespace

TransitionPropertiesDialog::TransitionPropertiesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    setModal(false);
    setWindowTitle(tr("Video Event FX"));
    resize(460, 340);

    auto *root = new QVBoxLayout(this);

    m_headerLabel = new QLabel(this);
    m_headerLabel->setObjectName(QStringLiteral("transitionHeader"));
    root->addWidget(m_headerLabel);

    auto *presetRow = new QHBoxLayout();
    presetRow->addWidget(new QLabel(tr("Preset:"), this));
    m_presetCombo = new QComboBox(this);
    m_presetCombo->setEditable(false);
    presetRow->addWidget(m_presetCombo, 1);
    root->addLayout(presetRow);

    m_groupLabel = new QLabel(this);
    QFont gf = m_groupLabel->font();
    gf.setBold(true);
    m_groupLabel->setFont(gf);
    root->addWidget(m_groupLabel);

    m_paramsHost = new QWidget(this);
    new QFormLayout(m_paramsHost);
    root->addWidget(m_paramsHost);

    m_animateBtn = new QPushButton(tr("Animate"), this);
    m_animateBtn->setCheckable(true);
    m_animateBtn->setFixedWidth(90);
    // Where a transition is taken off: the settings are the one place it is certainly
    // being looked at, and until now the only way to undo dropping one was Ctrl+Z.
    m_removeBtn = new QPushButton(tr("Remove Transition"), this);
    m_removeBtn->setIcon(IconFactory::iconFromSvgBody(IconFactory::svgRemove()));
    m_removeBtn->setToolTip(tr("Take the transition off this fade, leaving a plain crossfade"));

    auto *animRow = new QHBoxLayout();
    animRow->addWidget(m_animateBtn);
    animRow->addStretch(1);
    animRow->addWidget(m_removeBtn);
    root->addLayout(animRow);

    // Keyframe strip: shown by the Animate toggle, exactly like the real window. Only the
    // transition's own row exists so far — per-parameter keyframes are not stored yet
    // (see ISSUES_AND_PLANS.md backlog), so the lane is presented read-only.
    m_animatePane = new QWidget(this);
    {
        auto *lay = new QVBoxLayout(m_animatePane);
        lay->setContentsMargins(0, 0, 0, 0);
        auto *ruler = new PanCropKeyframeRuler(m_animatePane);
        ruler->setRange(10.0, 0.0);
        lay->addWidget(ruler);
        auto *lane = new KeyframeLane(m_animatePane);
        lane->setTimes({0.0}, {0}, 10.0, 0, 0.0);
        lane->setEnabled(false);
        lay->addWidget(lane);
    }
    m_animatePane->setVisible(false);
    root->addWidget(m_animatePane);
    root->addStretch(1);

    connect(m_removeBtn, &QPushButton::clicked, this, [this]() {
        if (m_eventId < 0) {
            return;
        }
        emit transitionRemoved(m_eventId, m_fadeIn);
        // Nothing left to configure, so the window goes with it rather than sitting there
        // showing the settings of something that is no longer on the timeline.
        close();
    });
    connect(m_animateBtn, &QPushButton::toggled, this, [this](bool on) {
        m_animatePane->setVisible(on);
        adjustSize();
    });
    connect(m_presetCombo, &QComboBox::activated, this, [this](int) {
        if (!m_block) {
            applyPreset(m_presetCombo->currentText());
        }
    });
}

void TransitionPropertiesDialog::setTransition(const TransitionInstance &t, int eventId,
                                               bool fadeIn, const QString &clipName)
{
    m_transition = t;
    m_eventId = eventId;
    m_fadeIn = fadeIn;

    const TransitionPluginInfo *info = transitionPluginById(t.pluginId);
    const QString groupName = info ? info->name : t.pluginId;
    m_headerLabel->setText(tr("<b>Transition:</b> %1").arg(groupName));
    m_groupLabel->setText(groupName);
    setWindowTitle(clipName.isEmpty() ? tr("Video Event FX")
                                      : tr("Video Event FX — %1").arg(clipName));

    m_block = true;
    m_presetCombo->clear();
    if (info) {
        for (const TransitionPresetInfo &preset : info->presets) {
            m_presetCombo->addItem(preset.name);
        }
    }
    m_block = false;

    rebuildParamRows();
    syncPresetCombo();
}

void TransitionPropertiesDialog::rebuildParamRows()
{
    auto *form = qobject_cast<QFormLayout *>(m_paramsHost->layout());
    if (!form) {
        return;
    }
    while (form->rowCount() > 0) {
        form->removeRow(0);
    }
    m_sliders.clear();
    m_spins.clear();
    m_combos.clear();

    const TransitionPluginInfo *info = transitionPluginById(m_transition.pluginId);
    if (!info) {
        return;
    }
    for (const TransitionParamInfo &param : info->params) {
        const double value = transitionParamValue(m_transition, param.key);
        if (!param.choices.isEmpty()) {
            auto *combo = new QComboBox(m_paramsHost);
            combo->addItems(param.choices);
            combo->setCurrentIndex(
                std::clamp(int(std::lround(value)), 0, int(param.choices.size()) - 1));
            connect(combo, &QComboBox::activated, this, [this, key = param.key](int index) {
                if (!m_block) {
                    pushValue(key, index);
                }
            });
            m_combos.insert(param.key, combo);
            form->addRow(param.label + QLatin1Char(':'), combo);
            continue;
        }

        auto *row = new QWidget(m_paramsHost);
        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(0, 0, 0, 0);
        auto *slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(0, kSliderSteps);
        slider->setValue(toSliderPos(value, param));
        auto *spin = new QDoubleSpinBox(row);
        spin->setRange(param.minValue, param.maxValue);
        spin->setDecimals(param.decimals);
        spin->setSingleStep(param.decimals == 0 ? 1.0 : 0.05);
        spin->setValue(value);
        spin->setFixedWidth(90);
        lay->addWidget(slider, 1);
        lay->addWidget(spin);

        // The spin box is the single source of truth: the slider feeds it, and only its
        // valueChanged pushes into the transition, so one gesture applies exactly once.
        connect(slider, &QSlider::valueChanged, spin, [spin, param](int pos) {
            spin->setValue(fromSliderPos(pos, param));
        });
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this, slider, param, key = param.key](double v) {
                    if (m_block) {
                        return;
                    }
                    QSignalBlocker block(slider);
                    slider->setValue(toSliderPos(v, param));
                    pushValue(key, v);
                });
        m_sliders.insert(param.key, slider);
        m_spins.insert(param.key, spin);
        form->addRow(param.label + QLatin1Char(':'), row);
    }
}

void TransitionPropertiesDialog::pushValue(const QString &key, double value)
{
    transitionSetParamValue(&m_transition, key, value);
    syncPresetCombo();
    emit transitionChanged(m_eventId, m_fadeIn, m_transition);
}

void TransitionPropertiesDialog::applyPreset(const QString &presetName)
{
    const TransitionInstance fresh = makeTransitionInstance(m_transition.pluginId, presetName);
    if (!fresh.isValid()) {
        return;
    }
    m_transition = fresh;
    m_block = true;
    const TransitionPluginInfo *info = transitionPluginById(m_transition.pluginId);
    if (info) {
        for (const TransitionParamInfo &param : info->params) {
            const double value = transitionParamValue(m_transition, param.key);
            if (QComboBox *combo = m_combos.value(param.key)) {
                combo->setCurrentIndex(
                    std::clamp(int(std::lround(value)), 0, int(param.choices.size()) - 1));
            }
            if (QSlider *slider = m_sliders.value(param.key)) {
                slider->setValue(toSliderPos(value, param));
            }
            if (QDoubleSpinBox *spin = m_spins.value(param.key)) {
                spin->setValue(value);
            }
        }
    }
    m_block = false;
    emit transitionChanged(m_eventId, m_fadeIn, m_transition);
}

void TransitionPropertiesDialog::syncPresetCombo()
{
    QSignalBlocker block(m_presetCombo);
    const int idx = m_presetCombo->findText(m_transition.presetName);
    // A hand-edited instance drops its preset name; show no selection rather than
    // implying it still matches a stock preset.
    m_presetCombo->setCurrentIndex(idx);
}

} // namespace openvegas
