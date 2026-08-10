#pragma once

#include "video/TitlesTextApply.h"

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QSpinBox;
class QToolButton;

namespace openvegas {

/**
 * Vegas Pro's "Properties" window for a piece of media, reached from the Media
 * Properties toolbar button in the Video Media Generator window. Only the "Media" tab
 * exists (that's all Vegas shows for generated, file-less media).
 *
 * Modal, edit-then-OK — unlike the live generator window itself, nothing is applied
 * until OK, matching Vegas. Read back the result with mediaProps()/lengthSec() when
 * exec() returns Accepted.
 *
 * Frame rate is display-only here (it belongs to the project, and Vegas likewise shows
 * it greyed for generated media); Length maps to the TrackEvent's own length.
 */
class MediaPropertiesDialog : public QDialog {
    Q_OBJECT
public:
    explicit MediaPropertiesDialog(QWidget *parent = nullptr);

    /**
     * @param fileName    Generated media name shown in the read-only "File name" row.
     * @param projectW/H  Project frame size — used for the "follow the project" default
     *                    when props.frameWidth/Height are still 0.
     */
    void setMedia(const QString &fileName, const GeneratorMediaProps &props, double lengthSec,
                  double frameRateFps, int projectW, int projectH);

    GeneratorMediaProps mediaProps() const;
    double lengthSec() const;

private:
    void buildUi();
    void syncEnabled();
    void updateBackgroundSwatch();

    double m_frameRateFps = 30.0;
    QColor m_backgroundColor = QColor(0, 0, 0, 255);

    QLabel *m_fileNameLabel = nullptr;
    QLineEdit *m_tapeNameEdit = nullptr;

    QRadioButton *m_timecodeInFileRadio = nullptr;
    QRadioButton *m_customTimecodeRadio = nullptr;
    QLineEdit *m_customTimecodeEdit = nullptr;
    QComboBox *m_timecodeFormatCombo = nullptr;

    QComboBox *m_streamCombo = nullptr;
    QSpinBox *m_frameWidthSpin = nullptr;
    QSpinBox *m_frameHeightSpin = nullptr;
    QLabel *m_frameRateLabel = nullptr;
    QLineEdit *m_lengthEdit = nullptr;
    QComboBox *m_fieldOrderCombo = nullptr;
    QComboBox *m_pixelAspectCombo = nullptr;
    QComboBox *m_alphaChannelCombo = nullptr;
    QToolButton *m_backgroundColorBtn = nullptr;
    QComboBox *m_rotationCombo = nullptr;
};

} // namespace openvegas
