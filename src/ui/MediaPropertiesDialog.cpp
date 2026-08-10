#include "ui/MediaPropertiesDialog.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace openvegas {
namespace {

/** Vegas timecode display: HH:MM:SS,FF (comma before frames, as in the real dialog). */
QString formatTimecode(double sec, double fps)
{
    const int fpsInt = std::max(1, int(std::lround(fps)));
    const qint64 totalFrames = qint64(std::llround(std::max(0.0, sec) * fps));
    const qint64 totalSeconds = totalFrames / fpsInt;
    return QStringLiteral("%1:%2:%3,%4")
        .arg(totalSeconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((totalSeconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'))
        .arg(totalFrames % fpsInt, 2, 10, QLatin1Char('0'));
}

/** Inverse of formatTimecode; returns `def` when the text isn't a usable timecode. */
double parseTimecode(const QString &text, double fps, double def)
{
    const QString normalized = QString(text).replace(QLatin1Char(','), QLatin1Char(':')).trimmed();
    const QStringList parts = normalized.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (parts.size() < 3) {
        return def;
    }
    bool okH = false;
    bool okM = false;
    bool okS = false;
    const int h = parts[0].toInt(&okH);
    const int m = parts[1].toInt(&okM);
    const int s = parts[2].toInt(&okS);
    if (!okH || !okM || !okS) {
        return def;
    }
    double frames = 0.0;
    if (parts.size() >= 4) {
        bool okF = false;
        const int f = parts[3].toInt(&okF);
        if (okF) {
            frames = double(f) / std::max(1.0, fps);
        }
    }
    return double(h) * 3600.0 + double(m) * 60.0 + double(s) + frames;
}

/** Standard Vegas pixel aspect ratios; label matches the dialog's comma decimal style. */
struct PixelAspectEntry {
    double value;
    const char *label;
};

const QVector<PixelAspectEntry> &pixelAspectTable()
{
    static const QVector<PixelAspectEntry> table = {
        {1.0, "1,0000 (Square)"},
        {0.9091, "0,9091 (NTSC DV)"},
        {1.2121, "1,2121 (NTSC DV Widescreen)"},
        {1.0926, "1,0926 (PAL DV)"},
        {1.4568, "1,4568 (PAL DV Widescreen)"},
    };
    return table;
}

} // namespace

MediaPropertiesDialog::MediaPropertiesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Properties"));
    setModal(true);
    buildUi();
}

void MediaPropertiesDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    auto *tabs = new QTabWidget(this);
    auto *mediaTab = new QWidget(tabs);
    auto *tabLay = new QVBoxLayout(mediaTab);

    auto *topForm = new QFormLayout();
    m_fileNameLabel = new QLabel(mediaTab);
    m_fileNameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    topForm->addRow(tr("File name:"), m_fileNameLabel);
    m_tapeNameEdit = new QLineEdit(mediaTab);
    topForm->addRow(tr("Tape name:"), m_tapeNameEdit);
    tabLay->addLayout(topForm);

    auto *timecodeBox = new QGroupBox(tr("Timecode"), mediaTab);
    auto *tcLay = new QVBoxLayout(timecodeBox);
    m_timecodeInFileRadio = new QRadioButton(tr("Use timecode in file"), timecodeBox);
    m_timecodeInFileRadio->setChecked(true);
    tcLay->addWidget(m_timecodeInFileRadio);
    auto *customRow = new QHBoxLayout();
    m_customTimecodeRadio = new QRadioButton(tr("Use custom timecode"), timecodeBox);
    customRow->addWidget(m_customTimecodeRadio, 1);
    m_customTimecodeEdit = new QLineEdit(timecodeBox);
    m_customTimecodeEdit->setFixedWidth(110);
    customRow->addWidget(m_customTimecodeEdit);
    tcLay->addLayout(customRow);
    m_timecodeFormatCombo = new QComboBox(timecodeBox);
    m_timecodeFormatCombo->addItem(tr("Time & Frames"));
    tcLay->addWidget(m_timecodeFormatCombo);
    tabLay->addWidget(timecodeBox);

    auto *streamBox = new QGroupBox(tr("Stream properties"), mediaTab);
    auto *streamForm = new QFormLayout(streamBox);

    auto *streamRow = new QHBoxLayout();
    m_streamCombo = new QComboBox(streamBox);
    m_streamCombo->addItem(tr("Video 1"));
    streamRow->addWidget(m_streamCombo, 1);
    auto *snapshotBtn = new QToolButton(streamBox);
    snapshotBtn->setText(tr("⛶"));
    snapshotBtn->setToolTip(tr("Save snapshot to file"));
    snapshotBtn->setEnabled(false); // no capture pipeline yet — see ISSUES_AND_PLANS.md
    streamRow->addWidget(snapshotBtn);
    streamForm->addRow(tr("Stream:"), streamRow);

    auto *sizeRow = new QHBoxLayout();
    m_frameWidthSpin = new QSpinBox(streamBox);
    m_frameWidthSpin->setRange(16, 16384);
    m_frameHeightSpin = new QSpinBox(streamBox);
    m_frameHeightSpin->setRange(16, 16384);
    sizeRow->addWidget(m_frameWidthSpin);
    sizeRow->addWidget(new QLabel(QStringLiteral("x"), streamBox));
    sizeRow->addWidget(m_frameHeightSpin);
    sizeRow->addStretch(1);
    streamForm->addRow(tr("Frame size:"), sizeRow);

    m_frameRateLabel = new QLabel(streamBox);
    streamForm->addRow(tr("Frame rate:"), m_frameRateLabel);

    m_lengthEdit = new QLineEdit(streamBox);
    m_lengthEdit->setFixedWidth(110);
    streamForm->addRow(tr("Length:"), m_lengthEdit);

    m_fieldOrderCombo = new QComboBox(streamBox);
    m_fieldOrderCombo->addItem(tr("None (progressive scan)"));
    m_fieldOrderCombo->addItem(tr("Upper field first"));
    m_fieldOrderCombo->addItem(tr("Lower field first"));
    streamForm->addRow(tr("Field order:"), m_fieldOrderCombo);

    m_pixelAspectCombo = new QComboBox(streamBox);
    for (const PixelAspectEntry &e : pixelAspectTable()) {
        m_pixelAspectCombo->addItem(QString::fromUtf8(e.label), e.value);
    }
    streamForm->addRow(tr("Pixel aspect:"), m_pixelAspectCombo);

    m_alphaChannelCombo = new QComboBox(streamBox);
    m_alphaChannelCombo->addItem(tr("Undefined"));
    m_alphaChannelCombo->addItem(tr("None"));
    m_alphaChannelCombo->addItem(tr("Straight (unmatted)"));
    m_alphaChannelCombo->addItem(tr("Premultiplied"));
    m_alphaChannelCombo->addItem(tr("Premultiplied (dirty)"));
    streamForm->addRow(tr("Alpha channel:"), m_alphaChannelCombo);

    m_backgroundColorBtn = new QToolButton(streamBox);
    m_backgroundColorBtn->setFixedSize(56, 18);
    streamForm->addRow(tr("Background color:"), m_backgroundColorBtn);

    m_rotationCombo = new QComboBox(streamBox);
    m_rotationCombo->addItem(tr("0° (original)"));
    m_rotationCombo->addItem(tr("90° clockwise"));
    m_rotationCombo->addItem(tr("180° (inverted)"));
    m_rotationCombo->addItem(tr("90° counterclockwise"));
    streamForm->addRow(tr("Rotation:"), m_rotationCombo);

    tabLay->addWidget(streamBox);
    tabLay->addStretch(1);
    tabs->addTab(mediaTab, tr("Media"));
    root->addWidget(tabs, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    connect(m_timecodeInFileRadio, &QRadioButton::toggled, this, [this](bool) { syncEnabled(); });
    connect(m_customTimecodeRadio, &QRadioButton::toggled, this, [this](bool) { syncEnabled(); });
    connect(m_backgroundColorBtn, &QToolButton::clicked, this, [this]() {
        const QColor picked = QColorDialog::getColor(m_backgroundColor, this,
                                                     tr("Background color"),
                                                     QColorDialog::ShowAlphaChannel);
        if (picked.isValid()) {
            m_backgroundColor = picked;
            updateBackgroundSwatch();
        }
    });
    syncEnabled();
}

void MediaPropertiesDialog::syncEnabled()
{
    const bool custom = m_customTimecodeRadio->isChecked();
    m_customTimecodeEdit->setEnabled(custom);
    // Vegas greys the format combo out unless a custom timecode is being entered.
    m_timecodeFormatCombo->setEnabled(custom);
}

void MediaPropertiesDialog::updateBackgroundSwatch()
{
    m_backgroundColorBtn->setStyleSheet(
        QStringLiteral("background:%1; border:1px solid #555;").arg(m_backgroundColor.name()));
}

void MediaPropertiesDialog::setMedia(const QString &fileName, const GeneratorMediaProps &props,
                                     double lengthSec, double frameRateFps, int projectW,
                                     int projectH)
{
    m_frameRateFps = frameRateFps > 0.001 ? frameRateFps : 30.0;

    m_fileNameLabel->setText(fileName);
    m_tapeNameEdit->setText(props.tapeName);

    m_customTimecodeRadio->setChecked(props.useCustomTimecode);
    m_timecodeInFileRadio->setChecked(!props.useCustomTimecode);
    m_customTimecodeEdit->setText(formatTimecode(props.customTimecodeSec, m_frameRateFps));

    // frameWidth/Height of 0 means "not overridden yet" — show the project's size, which
    // is what the generator actually renders at today.
    m_frameWidthSpin->setValue(props.frameWidth > 0 ? props.frameWidth : std::max(16, projectW));
    m_frameHeightSpin->setValue(props.frameHeight > 0 ? props.frameHeight : std::max(16, projectH));

    m_frameRateLabel->setText(tr("%1 fps").arg(QString::number(m_frameRateFps, 'f', 3)
                                                   .replace(QLatin1Char('.'), QLatin1Char(','))));
    m_lengthEdit->setText(formatTimecode(lengthSec, m_frameRateFps));

    m_fieldOrderCombo->setCurrentIndex(int(props.fieldOrder));

    int aspectIdx = -1;
    for (int i = 0; i < m_pixelAspectCombo->count(); ++i) {
        if (std::abs(m_pixelAspectCombo->itemData(i).toDouble() - props.pixelAspect) < 1e-4) {
            aspectIdx = i;
            break;
        }
    }
    if (aspectIdx < 0) {
        // Value came from somewhere outside the standard table (import, hand-edit) —
        // surface it rather than silently snapping to Square.
        m_pixelAspectCombo->insertItem(0, QString::number(props.pixelAspect, 'f', 4)
                                              .replace(QLatin1Char('.'), QLatin1Char(',')),
                                       props.pixelAspect);
        aspectIdx = 0;
    }
    m_pixelAspectCombo->setCurrentIndex(aspectIdx);

    m_alphaChannelCombo->setCurrentIndex(int(props.alphaChannel));
    m_backgroundColor = props.backgroundColor;
    updateBackgroundSwatch();
    m_rotationCombo->setCurrentIndex(int(props.rotation));

    syncEnabled();
}

GeneratorMediaProps MediaPropertiesDialog::mediaProps() const
{
    GeneratorMediaProps p;
    p.tapeName = m_tapeNameEdit->text();
    p.useCustomTimecode = m_customTimecodeRadio->isChecked();
    p.customTimecodeSec = parseTimecode(m_customTimecodeEdit->text(), m_frameRateFps, 0.0);
    p.frameWidth = m_frameWidthSpin->value();
    p.frameHeight = m_frameHeightSpin->value();
    p.fieldOrder = static_cast<MediaFieldOrder>(std::clamp(m_fieldOrderCombo->currentIndex(), 0, 2));
    p.pixelAspect = m_pixelAspectCombo->currentData().toDouble();
    if (p.pixelAspect < 0.01) {
        p.pixelAspect = 1.0;
    }
    p.alphaChannel =
        static_cast<MediaAlphaChannel>(std::clamp(m_alphaChannelCombo->currentIndex(), 0, 4));
    p.backgroundColor = m_backgroundColor;
    p.rotation = static_cast<MediaRotation>(std::clamp(m_rotationCombo->currentIndex(), 0, 3));
    return p;
}

double MediaPropertiesDialog::lengthSec() const
{
    // Keep the caller's length when the field was cleared/garbled rather than collapsing
    // the event to zero: 0.0 here means "unparseable", and callers clamp to a minimum.
    return parseTimecode(m_lengthEdit->text(), m_frameRateFps, 0.0);
}

} // namespace openvegas
