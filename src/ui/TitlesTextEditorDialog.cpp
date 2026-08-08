#include "ui/TitlesTextEditorDialog.h"

#include "model/ProjectModel.h"
#include "ui/CollapsibleSection.h"
#include "ui/ColorPickerWidget.h"
#include "video/TitlesTextApply.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace openvegas {

/** Draggable position pad bound to normalized (0..1, 0..1) frame coordinates. */
class LocationPad : public QWidget {
public:
    explicit LocationPad(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(200, 112);
    }

    void setOnPick(std::function<void(double, double)> cb) { m_onPick = std::move(cb); }

    void setPosition(double x, double y)
    {
        m_x = x;
        m_y = y;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x14, 0x14, 0x14));
        p.setPen(QColor(0x50, 0x50, 0x50));
        p.drawRect(rect().adjusted(0, 0, -1, -1));

        const int px = int(m_x * width());
        const int py = int(m_y * height());
        p.setPen(QColor(0xd0, 0xd0, 0xd0));
        p.drawLine(px, 0, px, height());
        p.drawLine(0, py, width(), py);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPoint(px, py), 4, 4);
    }

    void mousePressEvent(QMouseEvent *e) override { pick(e->pos()); }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (e->buttons() & Qt::LeftButton) {
            pick(e->pos());
        }
    }

private:
    void pick(const QPoint &pos)
    {
        m_x = std::clamp(double(pos.x()) / std::max(1, width()), 0.0, 1.0);
        m_y = std::clamp(double(pos.y()) / std::max(1, height()), 0.0, 1.0);
        update();
        if (m_onPick) {
            m_onPick(m_x, m_y);
        }
    }

    double m_x = 0.5;
    double m_y = 0.5;
    std::function<void(double, double)> m_onPick;
};

TitlesTextEditorDialog::TitlesTextEditorDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint
                   | Qt::WindowMinMaxButtonsHint);
    setWindowModality(Qt::NonModal);
    setModal(false);
    setWindowTitle(tr("Video Media Generator"));
    resize(420, 720);
    buildUi();
}

void TitlesTextEditorDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    auto *header = new QFormLayout();
    m_frameSizeLabel = new QLabel(this);
    header->addRow(tr("Frame Size:"), m_frameSizeLabel);
    m_durationSpin = new QDoubleSpinBox(this);
    m_durationSpin->setRange(0.1, 3600.0);
    m_durationSpin->setDecimals(2);
    m_durationSpin->setSuffix(tr(" s"));
    header->addRow(tr("Duration:"), m_durationSpin);
    root->addLayout(header);
    root->addWidget(new QLabel(QStringLiteral("<b>VEGAS Titles &amp; Text</b>"), this));

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *body = new QWidget(scroll);
    auto *bodyLay = new QVBoxLayout(body);

    bodyLay->addWidget(new QLabel(tr("Text:"), body));
    auto *toolbar = new QHBoxLayout();
    m_fontCombo = new QFontComboBox(body);
    toolbar->addWidget(m_fontCombo, 1);
    m_fontSizeSpin = new QSpinBox(body);
    m_fontSizeSpin->setRange(4, 500);
    toolbar->addWidget(m_fontSizeSpin);
    m_boldBtn = new QToolButton(body);
    m_boldBtn->setText(tr("B"));
    m_boldBtn->setCheckable(true);
    toolbar->addWidget(m_boldBtn);
    m_italicBtn = new QToolButton(body);
    m_italicBtn->setText(tr("I"));
    m_italicBtn->setCheckable(true);
    toolbar->addWidget(m_italicBtn);
    m_alignLeftBtn = new QToolButton(body);
    m_alignLeftBtn->setText(tr("L"));
    m_alignLeftBtn->setCheckable(true);
    m_alignCenterBtn = new QToolButton(body);
    m_alignCenterBtn->setText(tr("C"));
    m_alignCenterBtn->setCheckable(true);
    m_alignRightBtn = new QToolButton(body);
    m_alignRightBtn->setText(tr("R"));
    m_alignRightBtn->setCheckable(true);
    for (QToolButton *b : {m_alignLeftBtn, m_alignCenterBtn, m_alignRightBtn}) {
        toolbar->addWidget(b);
    }
    bodyLay->addLayout(toolbar);
    m_textEdit = new QPlainTextEdit(body);
    m_textEdit->setFixedHeight(140);
    bodyLay->addWidget(m_textEdit);

    m_textColorSection = new CollapsibleSection(tr("Text color"), body);
    m_textColorPicker = new ColorPickerWidget(body);
    m_textColorSection->setContentWidget(m_textColorPicker);
    bodyLay->addWidget(m_textColorSection);

    auto *animRow = new QFormLayout();
    m_animationCombo = new QComboBox(body);
    for (const TitlesTextPresetEntry &e : titlesTextAnimationPresets()) {
        m_animationCombo->addItem(e.label, e.key);
    }
    animRow->addRow(tr("Animation:"), m_animationCombo);
    bodyLay->addLayout(animRow);

    auto *scaleRow = new QFormLayout();
    m_scaleSpin = new QDoubleSpinBox(body);
    m_scaleSpin->setRange(0.01, 20.0);
    m_scaleSpin->setSingleStep(0.05);
    m_scaleSpin->setDecimals(3);
    scaleRow->addRow(tr("Scale:"), m_scaleSpin);
    bodyLay->addLayout(scaleRow);

    auto *locRow = new QHBoxLayout();
    m_locationPad = new LocationPad(body);
    m_locationPad->setOnPick([this](double x, double y) {
        if (m_block) {
            return;
        }
        m_locationXSpin->setValue(x);
        m_locationYSpin->setValue(y);
        saveToEvent();
    });
    locRow->addWidget(m_locationPad);
    auto *locSpins = new QFormLayout();
    m_locationXSpin = new QDoubleSpinBox(body);
    m_locationXSpin->setRange(-2.0, 3.0);
    m_locationXSpin->setDecimals(3);
    m_locationXSpin->setSingleStep(0.01);
    m_locationYSpin = new QDoubleSpinBox(body);
    m_locationYSpin->setRange(-2.0, 3.0);
    m_locationYSpin->setDecimals(3);
    m_locationYSpin->setSingleStep(0.01);
    locSpins->addRow(tr("Location X:"), m_locationXSpin);
    locSpins->addRow(tr("Location Y:"), m_locationYSpin);
    locRow->addLayout(locSpins);
    bodyLay->addLayout(locRow);

    auto *anchorRow = new QFormLayout();
    m_anchorCombo = new QComboBox(body);
    for (const char *n : {"Top Left", "Top Center", "Top Right", "Middle Left", "Center",
                          "Middle Right", "Bottom Left", "Bottom Center", "Bottom Right"}) {
        m_anchorCombo->addItem(QString::fromLatin1(n));
    }
    anchorRow->addRow(tr("Anchor Point:"), m_anchorCombo);
    bodyLay->addLayout(anchorRow);

    m_advancedSection = new CollapsibleSection(tr("Advanced"), body);
    auto *advBody = new QWidget(body);
    auto *advLay = new QFormLayout(advBody);
    m_cropCheckbox = new QCheckBox(tr("Crop background to text"), advBody);
    advLay->addRow(m_cropCheckbox);
    m_backgroundPicker = new ColorPickerWidget(advBody);
    advLay->addRow(tr("Background:"), m_backgroundPicker);
    m_trackingSpin = new QDoubleSpinBox(advBody);
    m_trackingSpin->setRange(-50.0, 50.0);
    m_trackingSpin->setDecimals(2);
    advLay->addRow(tr("Tracking:"), m_trackingSpin);
    m_lineSpacingSpin = new QDoubleSpinBox(advBody);
    m_lineSpacingSpin->setRange(0.1, 5.0);
    m_lineSpacingSpin->setDecimals(2);
    advLay->addRow(tr("Line spacing:"), m_lineSpacingSpin);
    m_advancedSection->setContentWidget(advBody);
    bodyLay->addWidget(m_advancedSection);

    m_outlineSection = new CollapsibleSection(tr("Outline"), body);
    auto *outBody = new QWidget(body);
    auto *outLay = new QFormLayout(outBody);
    m_outlineWidthSpin = new QDoubleSpinBox(outBody);
    m_outlineWidthSpin->setRange(0.0, 100.0);
    m_outlineWidthSpin->setDecimals(2);
    outLay->addRow(tr("Outline width:"), m_outlineWidthSpin);
    m_outlineColorPicker = new ColorPickerWidget(outBody);
    outLay->addRow(tr("Outline color:"), m_outlineColorPicker);
    m_outlineSection->setContentWidget(outBody);
    bodyLay->addWidget(m_outlineSection);

    m_shadowSection = new CollapsibleSection(tr("Shadow"), body);
    auto *shBody = new QWidget(body);
    auto *shLay = new QFormLayout(shBody);
    m_shadowEnableCheckbox = new QCheckBox(tr("Shadow enable"), shBody);
    shLay->addRow(m_shadowEnableCheckbox);
    m_shadowColorPicker = new ColorPickerWidget(shBody);
    shLay->addRow(tr("Shadow color:"), m_shadowColorPicker);
    m_shadowOffsetXSpin = new QDoubleSpinBox(shBody);
    m_shadowOffsetXSpin->setRange(-20.0, 20.0);
    m_shadowOffsetXSpin->setDecimals(2);
    shLay->addRow(tr("Shadow offset X:"), m_shadowOffsetXSpin);
    m_shadowOffsetYSpin = new QDoubleSpinBox(shBody);
    m_shadowOffsetYSpin->setRange(-20.0, 20.0);
    m_shadowOffsetYSpin->setDecimals(2);
    shLay->addRow(tr("Shadow offset Y:"), m_shadowOffsetYSpin);
    m_shadowBlurSpin = new QDoubleSpinBox(shBody);
    m_shadowBlurSpin->setRange(0.0, 20.0);
    m_shadowBlurSpin->setDecimals(2);
    shLay->addRow(tr("Shadow blur:"), m_shadowBlurSpin);
    m_shadowSection->setContentWidget(shBody);
    bodyLay->addWidget(m_shadowSection);

    bodyLay->addStretch(1);
    scroll->setWidget(body);
    root->addWidget(scroll, 1);

    auto *closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);

    // Every meaningful edit re-saves to the event and asks the host to repaint the
    // preview (see class doc — no existing dialog does this while paused).
    connect(m_durationSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_block || !m_event) {
            return;
        }
        m_event->lengthSec = std::max(0.05, v);
        emit durationChanged();
        emit previewInvalidated();
    });
    connect(m_textEdit, &QPlainTextEdit::textChanged, this, [this] { saveToEvent(); });
    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this, [this](const QFont &) {
        saveToEvent();
    });
    connect(m_fontSizeSpin, &QSpinBox::valueChanged, this, [this](int) { saveToEvent(); });
    connect(m_boldBtn, &QToolButton::toggled, this, [this](bool) { saveToEvent(); });
    connect(m_italicBtn, &QToolButton::toggled, this, [this](bool) { saveToEvent(); });
    auto alignClicked = [this](QToolButton *chosen) {
        if (m_block) {
            return;
        }
        for (QToolButton *b : {m_alignLeftBtn, m_alignCenterBtn, m_alignRightBtn}) {
            b->setChecked(b == chosen);
        }
        saveToEvent();
    };
    connect(m_alignLeftBtn, &QToolButton::clicked, this,
            [this, alignClicked] { alignClicked(m_alignLeftBtn); });
    connect(m_alignCenterBtn, &QToolButton::clicked, this,
            [this, alignClicked] { alignClicked(m_alignCenterBtn); });
    connect(m_alignRightBtn, &QToolButton::clicked, this,
            [this, alignClicked] { alignClicked(m_alignRightBtn); });
    connect(m_textColorPicker, &ColorPickerWidget::colorChanged, this,
            [this](const QColor &) { saveToEvent(); });
    connect(m_textColorSection, &CollapsibleSection::expandedChanged, this,
            [this](bool) { saveToEvent(); });
    connect(m_animationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { saveToEvent(); });
    connect(m_scaleSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { saveToEvent(); });
    connect(m_locationXSpin, &QDoubleSpinBox::valueChanged, this, [this](double) {
        if (!m_block) {
            saveToEvent();
        }
    });
    connect(m_locationYSpin, &QDoubleSpinBox::valueChanged, this, [this](double) {
        if (!m_block) {
            saveToEvent();
        }
    });
    connect(m_anchorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { saveToEvent(); });
    connect(m_advancedSection, &CollapsibleSection::expandedChanged, this,
            [this](bool) { saveToEvent(); });
    connect(m_cropCheckbox, &QCheckBox::toggled, this, [this](bool) { saveToEvent(); });
    connect(m_backgroundPicker, &ColorPickerWidget::colorChanged, this,
            [this](const QColor &) { saveToEvent(); });
    connect(m_trackingSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { saveToEvent(); });
    connect(m_lineSpacingSpin, &QDoubleSpinBox::valueChanged, this,
            [this](double) { saveToEvent(); });
    connect(m_outlineSection, &CollapsibleSection::expandedChanged, this,
            [this](bool) { saveToEvent(); });
    connect(m_outlineWidthSpin, &QDoubleSpinBox::valueChanged, this,
            [this](double) { saveToEvent(); });
    connect(m_outlineColorPicker, &ColorPickerWidget::colorChanged, this,
            [this](const QColor &) { saveToEvent(); });
    connect(m_shadowSection, &CollapsibleSection::expandedChanged, this,
            [this](bool) { saveToEvent(); });
    connect(m_shadowEnableCheckbox, &QCheckBox::toggled, this, [this](bool) { syncUiEnabled(); saveToEvent(); });
    connect(m_shadowColorPicker, &ColorPickerWidget::colorChanged, this,
            [this](const QColor &) { saveToEvent(); });
    connect(m_shadowOffsetXSpin, &QDoubleSpinBox::valueChanged, this,
            [this](double) { saveToEvent(); });
    connect(m_shadowOffsetYSpin, &QDoubleSpinBox::valueChanged, this,
            [this](double) { saveToEvent(); });
    connect(m_shadowBlurSpin, &QDoubleSpinBox::valueChanged, this,
            [this](double) { saveToEvent(); });
}

void TitlesTextEditorDialog::setEvent(TrackEvent *ev, int frameWidth, int frameHeight)
{
    m_event = ev;
    m_frameWidth = frameWidth;
    m_frameHeight = frameHeight;
    m_frameSizeLabel->setText(QStringLiteral("%1 x %2").arg(frameWidth).arg(frameHeight));
    loadFromEvent();
}

void TitlesTextEditorDialog::loadFromEvent()
{
    if (!m_event || m_event->fxChain.isEmpty()) {
        return;
    }
    m_block = true;
    const TitlesTextParams p = titlesTextFromSlot(m_event->fxChain[0]);

    m_durationSpin->setValue(m_event->lengthSec);
    m_textEdit->setPlainText(p.text);
    m_fontCombo->setCurrentFont(QFont(p.fontFamily));
    m_fontSizeSpin->setValue(int(std::round(p.fontSize)));
    m_boldBtn->setChecked(p.bold);
    m_italicBtn->setChecked(p.italic);
    m_alignLeftBtn->setChecked(p.alignment == TitlesTextAlignment::Left);
    m_alignCenterBtn->setChecked(p.alignment == TitlesTextAlignment::Center);
    m_alignRightBtn->setChecked(p.alignment == TitlesTextAlignment::Right);

    m_textColorPicker->setColor(p.textColor);
    m_textColorSection->setExpanded(p.textColorExpanded);

    int animIdx = m_animationCombo->findData(p.animationName);
    if (animIdx < 0) {
        m_animationCombo->insertItem(0, p.animationName.isEmpty() ? tr("(Unknown)")
                                                                  : p.animationName,
                                     p.animationName);
        animIdx = 0;
    }
    m_animationCombo->setCurrentIndex(animIdx);

    m_scaleSpin->setValue(p.scale);
    m_locationXSpin->setValue(p.locationX);
    m_locationYSpin->setValue(p.locationY);
    m_locationPad->setPosition(std::clamp(p.locationX, 0.0, 1.0),
                               std::clamp(p.locationY, 0.0, 1.0));
    m_anchorCombo->setCurrentIndex(int(p.anchor));

    m_advancedSection->setExpanded(p.advancedExpanded);
    m_cropCheckbox->setChecked(p.cropBackgroundToText);
    m_backgroundPicker->setColor(p.backgroundColor);
    m_trackingSpin->setValue(p.tracking);
    m_lineSpacingSpin->setValue(p.lineSpacing);

    m_outlineSection->setExpanded(p.outlineExpanded);
    m_outlineWidthSpin->setValue(p.outlineWidth);
    m_outlineColorPicker->setColor(p.outlineColor);

    m_shadowSection->setExpanded(p.shadowExpanded);
    m_shadowEnableCheckbox->setChecked(p.shadowEnable);
    m_shadowColorPicker->setColor(p.shadowColor);
    m_shadowOffsetXSpin->setValue(p.shadowOffsetX);
    m_shadowOffsetYSpin->setValue(p.shadowOffsetY);
    m_shadowBlurSpin->setValue(p.shadowBlur);

    m_block = false;
    syncUiEnabled();
}

void TitlesTextEditorDialog::saveToEvent()
{
    if (m_block || !m_event || m_event->fxChain.isEmpty()) {
        return;
    }
    TitlesTextParams p;
    p.text = m_textEdit->toPlainText();
    p.fontFamily = m_fontCombo->currentFont().family();
    p.fontSize = m_fontSizeSpin->value();
    p.bold = m_boldBtn->isChecked();
    p.italic = m_italicBtn->isChecked();
    p.alignment = m_alignCenterBtn->isChecked()
                     ? TitlesTextAlignment::Center
                     : (m_alignRightBtn->isChecked() ? TitlesTextAlignment::Right
                                                     : TitlesTextAlignment::Left);

    p.textColor = m_textColorPicker->color();
    p.textColorExpanded = m_textColorSection->isExpanded();

    p.animationName = m_animationCombo->currentData().toString();

    p.scale = m_scaleSpin->value();
    p.locationX = m_locationXSpin->value();
    p.locationY = m_locationYSpin->value();
    p.anchor = static_cast<TitlesTextAnchor>(
        std::clamp(m_anchorCombo->currentIndex(), 0, 8));

    p.advancedExpanded = m_advancedSection->isExpanded();
    p.cropBackgroundToText = m_cropCheckbox->isChecked();
    p.backgroundColor = m_backgroundPicker->color();
    p.tracking = m_trackingSpin->value();
    p.lineSpacing = m_lineSpacingSpin->value();

    p.outlineExpanded = m_outlineSection->isExpanded();
    p.outlineWidth = m_outlineWidthSpin->value();
    p.outlineColor = m_outlineColorPicker->color();

    p.shadowExpanded = m_shadowSection->isExpanded();
    p.shadowEnable = m_shadowEnableCheckbox->isChecked();
    p.shadowColor = m_shadowColorPicker->color();
    p.shadowOffsetX = m_shadowOffsetXSpin->value();
    p.shadowOffsetY = m_shadowOffsetYSpin->value();
    p.shadowBlur = m_shadowBlurSpin->value();

    titlesTextSaveToSlot(&m_event->fxChain[0], p);

    m_block = true;
    m_locationPad->setPosition(std::clamp(p.locationX, 0.0, 1.0),
                               std::clamp(p.locationY, 0.0, 1.0));
    m_block = false;

    emit previewInvalidated();
}

void TitlesTextEditorDialog::syncUiEnabled()
{
    const bool shadowOn = m_shadowEnableCheckbox->isChecked();
    m_shadowColorPicker->setEnabled(shadowOn);
    m_shadowOffsetXSpin->setEnabled(shadowOn);
    m_shadowOffsetYSpin->setEnabled(shadowOn);
    m_shadowBlurSpin->setEnabled(shadowOn);
}

} // namespace openvegas
