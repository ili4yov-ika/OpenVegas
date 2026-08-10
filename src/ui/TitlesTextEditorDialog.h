#pragma once

#include <QDialog>

class QLabel;
class QPlainTextEdit;
class QFontComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QToolButton;
class QComboBox;
class QSlider;
class QCheckBox;

namespace openvegas {

struct TrackEvent;
class CollapsibleSection;
class ColorPickerWidget;
class LocationPad;

/**
 * Floating, non-modal property window for a VEGAS Titles & Text generator event —
 * the OpenVegas equivalent of Vegas Pro's "Video Media Generator: VEGAS Titles & Text"
 * window. Binds directly to a live TrackEvent (same raw-pointer-into-ProjectModel
 * pattern as VideoEventFxDialogExact).
 *
 * Scope (see MARKDOWN plan): static text only — Animation lists every real preset
 * name so imports round-trip, but only "None" actually animates anything; the color
 * pickers are a functional approximation, not a pixel match of Vegas's own chrome.
 */
class TitlesTextEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit TitlesTextEditorDialog(QWidget *parent = nullptr);

    void setEvent(TrackEvent *ev, int frameWidth, int frameHeight);
    TrackEvent *event() const { return m_event; }
    /** Re-reads the current event's params into every field — for external edits (the
     *  on-canvas Video Preview move/resize overlay) that write straight to the event
     *  and bypass this dialog's own widgets. */
    void refreshFromEvent();

signals:
    /** Emitted after any edit that should repaint the Video Preview. */
    void previewInvalidated();
    /** Emitted after the duration spin box changes the event's length. */
    void durationChanged();

private:
    void buildUi();
    void loadFromEvent();
    void saveToEvent();
    void syncUiEnabled();

    TrackEvent *m_event = nullptr;
    int m_frameWidth = 1920;
    int m_frameHeight = 1080;
    bool m_block = false;

    QLabel *m_frameSizeLabel = nullptr;
    QDoubleSpinBox *m_durationSpin = nullptr;

    QPlainTextEdit *m_textEdit = nullptr;
    QFontComboBox *m_fontCombo = nullptr;
    QSpinBox *m_fontSizeSpin = nullptr;
    QToolButton *m_boldBtn = nullptr;
    QToolButton *m_italicBtn = nullptr;
    QToolButton *m_alignLeftBtn = nullptr;
    QToolButton *m_alignCenterBtn = nullptr;
    QToolButton *m_alignRightBtn = nullptr;

    CollapsibleSection *m_textColorSection = nullptr;
    ColorPickerWidget *m_textColorPicker = nullptr;

    QComboBox *m_animationCombo = nullptr;

    QDoubleSpinBox *m_scaleSpin = nullptr;
    CollapsibleSection *m_locationSection = nullptr;
    LocationPad *m_locationPad = nullptr;
    QDoubleSpinBox *m_locationXSpin = nullptr;
    QDoubleSpinBox *m_locationYSpin = nullptr;
    QComboBox *m_anchorCombo = nullptr;

    CollapsibleSection *m_advancedSection = nullptr;
    QCheckBox *m_cropCheckbox = nullptr;
    ColorPickerWidget *m_backgroundPicker = nullptr;
    QDoubleSpinBox *m_trackingSpin = nullptr;
    QDoubleSpinBox *m_lineSpacingSpin = nullptr;

    CollapsibleSection *m_outlineSection = nullptr;
    QDoubleSpinBox *m_outlineWidthSpin = nullptr;
    ColorPickerWidget *m_outlineColorPicker = nullptr;

    CollapsibleSection *m_shadowSection = nullptr;
    QCheckBox *m_shadowEnableCheckbox = nullptr;
    ColorPickerWidget *m_shadowColorPicker = nullptr;
    QDoubleSpinBox *m_shadowOffsetXSpin = nullptr;
    QDoubleSpinBox *m_shadowOffsetYSpin = nullptr;
    QDoubleSpinBox *m_shadowBlurSpin = nullptr;
};

} // namespace openvegas
