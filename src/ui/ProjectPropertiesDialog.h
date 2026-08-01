#pragma once

#include <QDialog>

class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QSlider;
class QLabel;
class QPushButton;

namespace openvegas {

class ProjectModel;

class ProjectPropertiesDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProjectPropertiesDialog(ProjectModel *model, QWidget *parent = nullptr);

private:
    void buildUi();
    void loadFromModel();
    void applyToModel();
    void markDirty();
    void updateApplyEnabled();

    ProjectModel *m_model = nullptr;
    bool m_dirty = false;

    // Video
    QComboBox *m_template = nullptr;
    QSpinBox *m_width = nullptr;
    QSpinBox *m_height = nullptr;
    QComboBox *m_hdr = nullptr;
    QComboBox *m_fieldOrder = nullptr;
    QComboBox *m_pixelAspect = nullptr;
    QComboBox *m_rotation = nullptr;
    QComboBox *m_frameRate = nullptr;
    QComboBox *m_pixelFormat = nullptr;
    QComboBox *m_renderQuality = nullptr;
    QComboBox *m_motionBlur = nullptr;
    QComboBox *m_deinterlace = nullptr;
    QComboBox *m_resample = nullptr;
    QCheckBox *m_adjustSource = nullptr;
    QCheckBox *m_overridePrerender = nullptr;
    QLineEdit *m_prerenderFolder = nullptr;
    QCheckBox *m_startAllVideo = nullptr;

    // Audio
    QComboBox *m_masterBus = nullptr;
    QSpinBox *m_stereoBusses = nullptr;
    QComboBox *m_sampleRate = nullptr;
    QComboBox *m_bitDepth = nullptr;
    QComboBox *m_audioQuality = nullptr;
    QCheckBox *m_lfeFilter = nullptr;
    QComboBox *m_lfeCutoff = nullptr;
    QComboBox *m_lfeQuality = nullptr;
    QLineEdit *m_recordFolder = nullptr;
    QCheckBox *m_startAllAudio = nullptr;

    // Ruler
    QComboBox *m_rulerFormat = nullptr;
    QLineEdit *m_rulerStart = nullptr;
    QDoubleSpinBox *m_tempo = nullptr;
    QSpinBox *m_beatsPerMeasure = nullptr;
    QComboBox *m_noteBeat = nullptr;
    QCheckBox *m_startAllRuler = nullptr;

    // Summary
    QLineEdit *m_title = nullptr;
    QLineEdit *m_artist = nullptr;
    QLineEdit *m_engineer = nullptr;
    QLineEdit *m_copyright = nullptr;
    QPlainTextEdit *m_comments = nullptr;
    QCheckBox *m_startAllSummary = nullptr;

    // Audio CD
    QLineEdit *m_upc = nullptr;
    QSpinBox *m_firstTrack = nullptr;

    // Advanced
    QComboBox *m_masterDisplay = nullptr;
    QCheckBox *m_360 = nullptr;
    QComboBox *m_stereo3d = nullptr;
    QCheckBox *m_swapLR = nullptr;
    QSlider *m_crosstalk = nullptr;
    QLabel *m_crosstalkVal = nullptr;
    QCheckBox *m_includeCancel = nullptr;
    QCheckBox *m_startAllAdvanced = nullptr;

    QPushButton *m_applyBtn = nullptr;
};

} // namespace openvegas
