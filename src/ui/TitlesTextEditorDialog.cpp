#include "ui/TitlesTextEditorDialog.h"

#include "model/ProjectModel.h"
#include "ui/CollapsibleSection.h"
#include "ui/ColorPickerWidget.h"
#include "ui/IconFactory.h"
#include "ui/MediaPropertiesDialog.h"
#include "ui/TitlesTextKeyframePane.h"
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
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
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

    // Vegas puts Frame Size / Duration on one strip with the generator's toolbar buttons
    // right-aligned on it. Only the Media Properties button is real here — the rest of
    // Vegas's strip (help / fx+ / settings) is deliberately not stubbed out.
    auto *header = new QHBoxLayout();
    header->addWidget(new QLabel(tr("Frame Size:"), this));
    m_frameSizeLabel = new QLabel(this);
    header->addWidget(m_frameSizeLabel);
    header->addSpacing(16);
    header->addWidget(new QLabel(tr("Duration:"), this));
    m_durationSpin = new QDoubleSpinBox(this);
    m_durationSpin->setRange(0.1, 3600.0);
    m_durationSpin->setDecimals(2);
    m_durationSpin->setSuffix(tr(" s"));
    header->addWidget(m_durationSpin);
    header->addStretch(1);
    m_mediaPropsBtn = IconFactory::toolButton(this, tr("Media Properties…"),
                                              IconFactory::svgMediaProps());
    connect(m_mediaPropsBtn, &QToolButton::clicked, this,
            &TitlesTextEditorDialog::openMediaProperties);
    header->addWidget(m_mediaPropsBtn);
    root->addLayout(header);
    root->addWidget(new QLabel(QStringLiteral("<b>VEGAS Titles &amp; Text</b>"), this));

    // Built before the parameter rows so their clock buttons can bind to it.
    m_keyframePane = new TitlesTextKeyframePane(this);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *body = new QWidget(scroll);
    auto *bodyLay = new QVBoxLayout(body);

    // Vegas pairs every numeric parameter row with a slider next to its spinbox — this
    // dialog only ever had the spinboxes. Wrap an existing QDoubleSpinBox with a
    // QSlider that mirrors it bidirectionally; the spinbox's own valueChanged (already
    // wired to saveToEvent() below) stays the single source of truth, so no matter which
    // control the user drags, the same "apply" path runs exactly once.
    auto sliderSpinRow = [this](QDoubleSpinBox *spin,
                                const QString &paramKey = QString()) -> QWidget * {
        auto *row = new QWidget(spin->parentWidget());
        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(0, 0, 0, 0);
        auto *slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(0, 1000);
        const double minV = spin->minimum();
        const double maxV = spin->maximum();
        const double span = maxV > minV ? (maxV - minV) : 1.0;
        auto toSliderPos = [minV, span](double v) {
            return std::clamp(int(std::lround((v - minV) / span * 1000.0)), 0, 1000);
        };
        slider->setValue(toSliderPos(spin->value()));
        connect(slider, &QSlider::valueChanged, spin, [spin, minV, span](int v) {
            spin->setValue(minV + span * (v / 1000.0));
        });
        connect(spin, &QDoubleSpinBox::valueChanged, slider,
               [slider, toSliderPos](double v) { slider->setValue(toSliderPos(v)); });
        lay->addWidget(slider, 1);
        lay->addWidget(spin);
        if (!paramKey.isEmpty()) {
            lay->addWidget(makeKeyframeButton(row, paramKey));
        }
        return row;
    };

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
    scaleRow->addRow(tr("Scale:"), sliderSpinRow(m_scaleSpin, QStringLiteral("scale")));
    bodyLay->addLayout(scaleRow);

    m_locationSection = new CollapsibleSection(tr("Location"), body);
    auto *locBody = new QWidget(body);
    auto *locRow = new QHBoxLayout(locBody);
    m_locationPad = new LocationPad(locBody);
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
    m_locationXSpin = new QDoubleSpinBox(locBody);
    m_locationXSpin->setRange(-2.0, 3.0);
    m_locationXSpin->setDecimals(3);
    m_locationXSpin->setSingleStep(0.01);
    m_locationYSpin = new QDoubleSpinBox(locBody);
    m_locationYSpin->setRange(-2.0, 3.0);
    m_locationYSpin->setDecimals(3);
    m_locationYSpin->setSingleStep(0.01);
    auto spinWithKeyframe = [this](QDoubleSpinBox *spin, const QString &paramKey) -> QWidget * {
        auto *row = new QWidget(spin->parentWidget());
        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->addWidget(spin, 1);
        lay->addWidget(makeKeyframeButton(row, paramKey));
        return row;
    };
    locSpins->addRow(tr("Location X:"),
                     spinWithKeyframe(m_locationXSpin, QStringLiteral("locationX")));
    locSpins->addRow(tr("Location Y:"),
                     spinWithKeyframe(m_locationYSpin, QStringLiteral("locationY")));
    locRow->addLayout(locSpins);
    m_locationSection->setContentWidget(locBody);
    bodyLay->addWidget(m_locationSection);

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
    advLay->addRow(tr("Tracking:"), sliderSpinRow(m_trackingSpin, QStringLiteral("tracking")));
    m_lineSpacingSpin = new QDoubleSpinBox(advBody);
    m_lineSpacingSpin->setRange(0.1, 5.0);
    m_lineSpacingSpin->setDecimals(2);
    advLay->addRow(tr("Line spacing:"),
                   sliderSpinRow(m_lineSpacingSpin, QStringLiteral("lineSpacing")));
    m_advancedSection->setContentWidget(advBody);
    bodyLay->addWidget(m_advancedSection);

    m_outlineSection = new CollapsibleSection(tr("Outline"), body);
    auto *outBody = new QWidget(body);
    auto *outLay = new QFormLayout(outBody);
    m_outlineWidthSpin = new QDoubleSpinBox(outBody);
    m_outlineWidthSpin->setRange(0.0, 100.0);
    m_outlineWidthSpin->setDecimals(2);
    outLay->addRow(tr("Outline width:"),
                   sliderSpinRow(m_outlineWidthSpin, QStringLiteral("outlineWidth")));
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
    shLay->addRow(tr("Shadow offset X:"),
                  sliderSpinRow(m_shadowOffsetXSpin, QStringLiteral("shadowOffsetX")));
    m_shadowOffsetYSpin = new QDoubleSpinBox(shBody);
    m_shadowOffsetYSpin->setRange(-20.0, 20.0);
    m_shadowOffsetYSpin->setDecimals(2);
    shLay->addRow(tr("Shadow offset Y:"),
                  sliderSpinRow(m_shadowOffsetYSpin, QStringLiteral("shadowOffsetY")));
    m_shadowBlurSpin = new QDoubleSpinBox(shBody);
    m_shadowBlurSpin->setRange(0.0, 20.0);
    m_shadowBlurSpin->setDecimals(2);
    shLay->addRow(tr("Shadow blur:"), sliderSpinRow(m_shadowBlurSpin, QStringLiteral("shadowBlur")));
    m_shadowSection->setContentWidget(shBody);
    bodyLay->addWidget(m_shadowSection);

    bodyLay->addStretch(1);
    scroll->setWidget(body);

    // Vegas splits the window: parameters on top, keyframe pane below, user-resizable.
    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->addWidget(scroll);
    m_splitter->addWidget(m_keyframePane);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({520, 200});
    root->addWidget(m_splitter, 1);

    connect(m_keyframePane, &TitlesTextKeyframePane::paramsEdited, this,
            [this](const TitlesTextParams &edited) {
                if (!m_event || m_event->fxChain.isEmpty()) {
                    return;
                }
                titlesTextSaveToSlot(&m_event->fxChain[0], edited);
                // Reflect the keyframed value at the pane's playhead in the spin boxes,
                // so the parameter rows and the curve never disagree.
                m_block = true;
                const TitlesTextParams shown =
                    titlesTextAtTime(edited, m_keyframePane->playheadSec());
                m_scaleSpin->setValue(shown.scale);
                m_locationXSpin->setValue(shown.locationX);
                m_locationYSpin->setValue(shown.locationY);
                m_trackingSpin->setValue(shown.tracking);
                m_lineSpacingSpin->setValue(shown.lineSpacing);
                m_outlineWidthSpin->setValue(shown.outlineWidth);
                m_shadowOffsetXSpin->setValue(shown.shadowOffsetX);
                m_shadowOffsetYSpin->setValue(shown.shadowOffsetY);
                m_shadowBlurSpin->setValue(shown.shadowBlur);
                m_locationPad->setPosition(std::clamp(shown.locationX, 0.0, 1.0),
                                           std::clamp(shown.locationY, 0.0, 1.0));
                m_block = false;
                syncKeyframeButtons();
                emit previewInvalidated();
            });
    connect(m_keyframePane, &TitlesTextKeyframePane::playheadMoved, this, [this](double) {
        refreshKeyframePane();
        emit previewInvalidated();
    });

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
    connect(m_locationSection, &CollapsibleSection::expandedChanged, this,
            [this](bool) { saveToEvent(); });
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

void TitlesTextEditorDialog::setEvent(TrackEvent *ev, int frameWidth, int frameHeight,
                                      double frameRateFps)
{
    m_event = ev;
    m_frameWidth = frameWidth;
    m_frameHeight = frameHeight;
    m_frameRateFps = frameRateFps > 0.001 ? frameRateFps : 30.0;
    m_frameSizeLabel->setText(QStringLiteral("%1 x %2").arg(frameWidth).arg(frameHeight));
    loadFromEvent();
}

void TitlesTextEditorDialog::refreshFromEvent()
{
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
    m_locationSection->setExpanded(p.locationExpanded);
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
    refreshKeyframePane();
}

void TitlesTextEditorDialog::saveToEvent()
{
    if (m_block || !m_event || m_event->fxChain.isEmpty()) {
        return;
    }
    // Start from what's stored, not from a default-constructed struct: Media Properties
    // and keyframe lanes have no widgets of their own here, so building `p` from scratch
    // would silently wipe them on the next spin-box nudge.
    TitlesTextParams p = titlesTextFromSlot(m_event->fxChain[0]);
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
    p.locationExpanded = m_locationSection->isExpanded();
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

    // Vegas: editing a parameter that is already animated writes a keyframe at the
    // cursor instead of a static value (which the lane would override anyway, making
    // the edit look like it did nothing).
    if (m_keyframePane) {
        const double t = m_keyframePane->playheadSec();
        for (const TitlesTextAnimatableParam &ap : titlesTextAnimatableParams()) {
            if (titlesTextFindLane(p, ap.key)) {
                titlesTextSetKeyframe(&p, ap.key, t, titlesTextParamValue(p, ap.key));
            }
        }
    }

    titlesTextSaveToSlot(&m_event->fxChain[0], p);

    m_block = true;
    m_locationPad->setPosition(std::clamp(p.locationX, 0.0, 1.0),
                               std::clamp(p.locationY, 0.0, 1.0));
    m_block = false;

    refreshKeyframePane();
    emit previewInvalidated();
}

QToolButton *TitlesTextEditorDialog::makeKeyframeButton(QWidget *parent, const QString &paramKey)
{
    auto *btn = IconFactory::toolButton(parent, tr("Add/Remove Keyframe"), IconFactory::svgMarker());
    btn->setFixedSize(20, 20);
    connect(btn, &QToolButton::clicked, this, [this, paramKey]() {
        if (!m_keyframePane) {
            return;
        }
        const double t = m_keyframePane->playheadSec();
        if (m_keyframePane->isAnimated(paramKey) && m_event && !m_event->fxChain.isEmpty()) {
            // Second click on an existing keyframe removes it (Vegas's red-clock state).
            TitlesTextParams p = titlesTextFromSlot(m_event->fxChain[0]);
            if (titlesTextRemoveKeyframe(&p, paramKey, t)) {
                titlesTextSaveToSlot(&m_event->fxChain[0], p);
                refreshKeyframePane();
                emit previewInvalidated();
                return;
            }
        }
        m_keyframePane->addKeyframeForParam(paramKey);
    });
    m_keyframeButtons.insert(paramKey, btn);
    return btn;
}

void TitlesTextEditorDialog::syncKeyframeButtons()
{
    if (!m_keyframePane) {
        return;
    }
    for (auto it = m_keyframeButtons.constBegin(); it != m_keyframeButtons.constEnd(); ++it) {
        const bool animated = m_keyframePane->isAnimated(it.key());
        it.value()->setIcon(IconFactory::iconFromSvgBody(
            IconFactory::svgMarker(), 16,
            animated ? QColor(0xe0, 0x50, 0x50) : QColor(0xa0, 0xa0, 0xa0)));
        it.value()->setToolTip(animated ? tr("Remove Keyframe") : tr("Add Keyframe"));
    }
}

void TitlesTextEditorDialog::refreshKeyframePane()
{
    if (!m_keyframePane || !m_event || m_event->fxChain.isEmpty()) {
        return;
    }
    m_keyframePane->setParams(titlesTextFromSlot(m_event->fxChain[0]), m_event->lengthSec,
                              m_frameRateFps);
    syncKeyframeButtons();
}

void TitlesTextEditorDialog::openMediaProperties()
{
    if (!m_event || m_event->fxChain.isEmpty()) {
        return;
    }
    const TitlesTextParams current = titlesTextFromSlot(m_event->fxChain[0]);

    MediaPropertiesDialog dlg(this);
    dlg.setMedia(m_event->name, current.media, m_event->lengthSec, m_frameRateFps, m_frameWidth,
                 m_frameHeight);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    // Re-read rather than reusing `current`: the dialog is modal, but saveToEvent() may
    // still have run in between (e.g. the on-canvas preview overlay), so the rest of the
    // params must come from the event as it is now, not from the pre-dialog snapshot.
    TitlesTextParams p = titlesTextFromSlot(m_event->fxChain[0]);
    p.media = dlg.mediaProps();
    titlesTextSaveToSlot(&m_event->fxChain[0], p);

    const double newLength = dlg.lengthSec();
    if (newLength > 0.05 && std::abs(newLength - m_event->lengthSec) > 1e-6) {
        m_event->lengthSec = newLength;
        m_block = true;
        m_durationSpin->setValue(m_event->lengthSec);
        m_block = false;
        emit durationChanged();
    }
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
