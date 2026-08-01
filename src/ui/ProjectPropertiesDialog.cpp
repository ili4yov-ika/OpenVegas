#include "ui/ProjectPropertiesDialog.h"
#include "model/ProjectModel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QStorageInfo>
#include <QStandardPaths>
#include <QSpacerItem>
#include <QSizePolicy>
#include <algorithm>
#include <cmath>

namespace openvegas {

namespace {

QComboBox *makeCombo(QWidget *parent, const QStringList &items, int current = 0)
{
    auto *c = new QComboBox(parent);
    c->addItems(items);
    if (current >= 0 && current < items.size()) {
        c->setCurrentIndex(current);
    }
    c->setMinimumHeight(26);
    c->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return c;
}

void styleField(QWidget *w)
{
    if (!w) {
        return;
    }
    w->setMinimumHeight(26);
    w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QScrollArea *wrapScroll(QWidget *inner)
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(inner);
    inner->setMinimumWidth(480);
    return scroll;
}

QFrame *hLine(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    line->setStyleSheet(QStringLiteral("color:#444;"));
    return line;
}

QString freeSpaceLabel(const QString &path)
{
    QStorageInfo info(path);
    if (!info.isValid()) {
        return QObject::tr("Free storage space in selected folder: —");
    }
    const double gb = double(info.bytesAvailable()) / (1024.0 * 1024.0 * 1024.0);
    return QObject::tr("Free storage space in selected folder: %1 Gigabytes")
        .arg(QString::number(gb, 'f', 1).replace(QLatin1Char('.'), QLatin1Char(',')));
}

} // namespace

ProjectPropertiesDialog::ProjectPropertiesDialog(ProjectModel *model, QWidget *parent)
    : QDialog(parent)
    , m_model(model)
{
    setWindowTitle(tr("Project Properties"));
    setModal(true);
    setMinimumSize(560, 520);
    resize(640, 600);
    setObjectName(QStringLiteral("projectPropertiesDialog"));
    setStyleSheet(QStringLiteral(
        "#projectPropertiesDialog { background:#2a2a2a; color:#e0e0e0; }"
        "QTabWidget::pane { border:1px solid #444; background:#2a2a2a; }"
        "QTabBar::tab { background:#333; color:#ccc; padding:6px 12px; border:1px solid #444; "
        "border-bottom:none; margin-right:2px; }"
        "QTabBar::tab:selected { background:#3a3a3a; color:#fff; }"
        "QLabel { color:#ddd; }"
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit {"
        "  background:#1e1e1e; color:#eee; border:1px solid #555;"
        "  padding:4px 8px; min-height:22px; }"
        "QComboBox { min-height:22px; }"
        "QComboBox::drop-down { width:20px; border:none; }"
        "QComboBox QAbstractItemView {"
        "  background:#1e1e1e; color:#eee; selection-background-color:#0078d7;"
        "  border:1px solid #555; outline:0; min-height:22px; }"
        "QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled {"
        "  color:#888; background:#252525; }"
        "QCheckBox { color:#ddd; spacing:6px; min-height:20px; }"
        "QCheckBox::indicator { width:14px; height:14px; }"
        "QPushButton { background:#3a3a3a; color:#eee; border:1px solid #555; padding:5px 14px;"
        "  min-height:22px; }"
        "QPushButton:hover { background:#4a4a4a; }"
        "QPushButton:disabled { color:#666; background:#2e2e2e; }"
        "QSlider::groove:horizontal { height:4px; background:#444; }"
        "QSlider::handle:horizontal { width:12px; margin:-5px 0; background:#888; }"
        "QScrollArea { background:transparent; border:none; }"));
    buildUi();
    loadFromModel();
}

void ProjectPropertiesDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    auto *tabs = new QTabWidget(this);

    // —— Video ——
    {
        auto *page = new QWidget;
        auto *lay = new QVBoxLayout(page);
        lay->setContentsMargins(8, 8, 8, 8);
        lay->setSpacing(8);
        lay->setSizeConstraint(QLayout::SetMinimumSize);

        auto *form = new QFormLayout();
        form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        form->setVerticalSpacing(8);
        form->setHorizontalSpacing(12);

        auto *tplRow = new QHBoxLayout();
        m_template = makeCombo(page,
                               {tr("Custom (1920x1080; 29,970 fps)"),
                                tr("Custom (1920x1080; 59,940 fps)"),
                                tr("HD 1280x720; 29,970 fps"), tr("UHD 3840x2160; 29,970 fps")});
        tplRow->addWidget(m_template, 1);
        auto *saveTpl = new QPushButton(tr("💾"), page);
        saveTpl->setFixedSize(28, 26);
        saveTpl->setToolTip(tr("Save template"));
        auto *delTpl = new QPushButton(tr("✕"), page);
        delTpl->setFixedSize(28, 26);
        delTpl->setToolTip(tr("Delete template"));
        tplRow->addWidget(saveTpl);
        tplRow->addWidget(delTpl);
        form->addRow(tr("Template:"), tplRow);

        auto *sizeRow = new QHBoxLayout();
        m_width = new QSpinBox(page);
        m_width->setRange(16, 8192);
        m_width->setValue(1920);
        styleField(m_width);
        m_height = new QSpinBox(page);
        m_height->setRange(16, 8192);
        m_height->setValue(1080);
        styleField(m_height);
        sizeRow->addWidget(m_width);
        sizeRow->addWidget(new QLabel(QStringLiteral("×"), page));
        sizeRow->addWidget(m_height);
        sizeRow->addStretch(1);
        form->addRow(tr("Width / Height:"), sizeRow);

        m_hdr = makeCombo(page, {tr("Off"), tr("HDR10"), tr("HLG")});
        form->addRow(tr("HDR Mode:"), m_hdr);
        m_fieldOrder =
            makeCombo(page, {tr("None (progressive scan)"), tr("Upper field first"),
                             tr("Lower field first")});
        form->addRow(tr("Field order:"), m_fieldOrder);
        m_pixelAspect = makeCombo(page, {tr("1,0000 (Square)"), tr("0,9091 (NTSC DV)"),
                                         tr("1,0926 (PAL DV)")});
        form->addRow(tr("Pixel aspect ratio:"), m_pixelAspect);
        m_rotation = makeCombo(page, {tr("0° (original)"), tr("90° clockwise"),
                                      tr("90° counter-clockwise"), tr("180°")});
        form->addRow(tr("Output rotation:"), m_rotation);
        m_frameRate = makeCombo(page,
                                {tr("23,976 (Film)"), tr("24,000 (Film)"), tr("25,000 (PAL)"),
                                 tr("29,970 (NTSC)"), tr("30,000"), tr("50,000"),
                                 tr("59,940 (Double NTSC)"), tr("60,000")});
        m_frameRate->setCurrentIndex(3);
        form->addRow(tr("Frame rate:"), m_frameRate);

        lay->addLayout(form);
        lay->addWidget(hLine(page));

        auto *form2 = new QFormLayout();
        form2->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        form2->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        form2->setVerticalSpacing(8);
        form2->setHorizontalSpacing(12);
        m_pixelFormat =
            makeCombo(page, {tr("8-bit (full range)"), tr("8-bit (studio RGB)"),
                             tr("32-bit floating point (video levels)"), tr("32-bit floating point")});
        form2->addRow(tr("Pixel format:"), m_pixelFormat);

        auto addDisabled = [&](const QString &label, const QString &value) {
            auto *c = makeCombo(page, {value});
            c->setEnabled(false);
            form2->addRow(label, c);
        };
        addDisabled(tr("Compositing gamma:"), tr("2,222 (Video)"));
        addDisabled(tr("ACES version:"), QStringLiteral("0.7"));
        addDisabled(tr("ACES color space:"), tr("Default (ACES)"));
        addDisabled(tr("View transform:"), tr("Off"));
        addDisabled(tr("Look modification transform:"), tr("None"));

        m_renderQuality = makeCombo(page, {tr("Draft"), tr("Preview"), tr("Good"), tr("Best")}, 2);
        form2->addRow(tr("Full-resolution rendering quality:"), m_renderQuality);
        m_motionBlur = makeCombo(page, {tr("Gaussian"), tr("Pyramid"), tr("Box")});
        form2->addRow(tr("Motion blur type:"), m_motionBlur);
        m_deinterlace =
            makeCombo(page, {tr("None"), tr("Blend fields"), tr("Interpolate fields")});
        form2->addRow(tr("Deinterlace method:"), m_deinterlace);
        m_resample = makeCombo(
            page, {tr("Smart resample"), tr("Force resample"), tr("Disable resample")}, 2);
        form2->addRow(tr("Resample mode:"), m_resample);
        lay->addLayout(form2);
        lay->addWidget(hLine(page));

        m_adjustSource = new QCheckBox(
            tr("Adjust source media to better match project or render settings"), page);
        m_adjustSource->setChecked(true);
        lay->addWidget(m_adjustSource);
        m_overridePrerender =
            new QCheckBox(tr("Override project settings when prerendering video"), page);
        lay->addWidget(m_overridePrerender);
        auto *fmtHint = new QLabel(tr("MAGIX AVC/AAC MP4\nTemplate: Internet 1920x1080 progressive"),
                                   page);
        fmtHint->setStyleSheet(QStringLiteral("color:#999; margin-left:22px;"));
        lay->addWidget(fmtHint);

        auto *folderRow = new QHBoxLayout();
        m_prerenderFolder = new QLineEdit(page);
        styleField(m_prerenderFolder);
        m_prerenderFolder->setText(
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
        auto *browse = new QPushButton(tr("Browse…"), page);
        browse->setMinimumHeight(26);
        connect(browse, &QPushButton::clicked, this, [this]() {
            const QString dir = QFileDialog::getExistingDirectory(this, tr("Prerendered files folder"),
                                                                  m_prerenderFolder->text());
            if (!dir.isEmpty()) {
                m_prerenderFolder->setText(dir);
                markDirty();
            }
        });
        folderRow->addWidget(m_prerenderFolder, 1);
        folderRow->addWidget(browse);
        auto *folderForm = new QFormLayout();
        folderForm->setVerticalSpacing(8);
        folderForm->addRow(tr("Prerendered files folder:"), folderRow);
        lay->addLayout(folderForm);
        auto *freeLbl = new QLabel(freeSpaceLabel(m_prerenderFolder->text()), page);
        freeLbl->setStyleSheet(QStringLiteral("color:#999;"));
        lay->addWidget(freeLbl);
        m_startAllVideo = new QCheckBox(tr("Start all new projects with these settings"), page);
        lay->addWidget(m_startAllVideo);
        lay->addStretch(1);
        tabs->addTab(wrapScroll(page), tr("Video"));
    }

    // —— Audio ——
    {
        auto *page = new QWidget;
        auto *lay = new QVBoxLayout(page);
        lay->setContentsMargins(8, 8, 8, 8);
        lay->setSpacing(8);
        lay->setSizeConstraint(QLayout::SetMinimumSize);
        auto *form = new QFormLayout();
        form->setVerticalSpacing(8);
        form->setHorizontalSpacing(12);
        m_masterBus = makeCombo(page, {tr("Stereo"), tr("Surround 5.1"), tr("Surround 7.1")});
        form->addRow(tr("Master bus mode:"), m_masterBus);
        m_stereoBusses = new QSpinBox(page);
        m_stereoBusses->setRange(0, 32);
        styleField(m_stereoBusses);
        form->addRow(tr("Number of stereo busses:"), m_stereoBusses);
        m_sampleRate = makeCombo(page, {QStringLiteral("44 100"), QStringLiteral("48 000"),
                                        QStringLiteral("96 000")},
                                 1);
        form->addRow(tr("Sample rate (Hz):"), m_sampleRate);
        m_bitDepth = makeCombo(page, {QStringLiteral("16"), QStringLiteral("24"), QStringLiteral("32")});
        form->addRow(tr("Bit depth:"), m_bitDepth);
        m_audioQuality = makeCombo(page, {tr("Draft"), tr("Good"), tr("Best")}, 1);
        form->addRow(tr("Resample and stretch quality:"), m_audioQuality);
        lay->addLayout(form);

        m_lfeFilter =
            new QCheckBox(tr("Enable low-pass filter on LFE (surround projects only)"), page);
        lay->addWidget(m_lfeFilter);
        auto *lfeForm = new QFormLayout();
        lfeForm->setVerticalSpacing(8);
        m_lfeCutoff = makeCombo(page, {tr("80"), tr("100"), tr("120 (pro/film)"), tr("150")}, 2);
        m_lfeCutoff->setEnabled(false);
        lfeForm->addRow(tr("Cutoff frequency for low-pass filter (Hz):"), m_lfeCutoff);
        m_lfeQuality = makeCombo(page, {tr("Good"), tr("Best")});
        m_lfeQuality->setEnabled(false);
        lfeForm->addRow(tr("Low-pass filter quality:"), m_lfeQuality);
        lay->addLayout(lfeForm);
        connect(m_lfeFilter, &QCheckBox::toggled, this, [this](bool on) {
            m_lfeCutoff->setEnabled(on);
            m_lfeQuality->setEnabled(on);
            markDirty();
        });

        auto *recRow = new QHBoxLayout();
        m_recordFolder = new QLineEdit(page);
        styleField(m_recordFolder);
        m_recordFolder->setText(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
        auto *browse = new QPushButton(tr("Browse…"), page);
        browse->setMinimumHeight(26);
        connect(browse, &QPushButton::clicked, this, [this]() {
            const QString dir = QFileDialog::getExistingDirectory(this, tr("Recorded files folder"),
                                                                  m_recordFolder->text());
            if (!dir.isEmpty()) {
                m_recordFolder->setText(dir);
                markDirty();
            }
        });
        recRow->addWidget(m_recordFolder, 1);
        recRow->addWidget(browse);
        auto *recForm = new QFormLayout();
        recForm->setVerticalSpacing(8);
        recForm->addRow(tr("Recorded files folder:"), recRow);
        lay->addLayout(recForm);
        auto *freeLbl = new QLabel(freeSpaceLabel(m_recordFolder->text()), page);
        freeLbl->setStyleSheet(QStringLiteral("color:#999;"));
        lay->addWidget(freeLbl);
        m_startAllAudio = new QCheckBox(tr("Start all new projects with these settings"), page);
        lay->addWidget(m_startAllAudio);
        lay->addStretch(1);
        tabs->addTab(wrapScroll(page), tr("Audio"));
    }

    // —— Ruler ——
    {
        auto *page = new QWidget(tabs);
        auto *lay = new QVBoxLayout(page);
        auto *form = new QFormLayout();
        m_rulerFormat = makeCombo(
            page,
            {tr("Time"), tr("Seconds"), tr("Time & Frames"), tr("Absolute Frames"),
             tr("Measures & Beats"), tr("SMPTE Non-Drop (29.97 fps, Video)")},
            4);
        form->addRow(tr("Ruler time format:"), m_rulerFormat);
        m_rulerStart = new QLineEdit(QStringLiteral("1.1.000"), page);
        form->addRow(tr("Ruler start time:"), m_rulerStart);
        lay->addLayout(form);
        lay->addWidget(hLine(page));
        auto *secLbl = new QLabel(tr("Measures & Beats"), page);
        secLbl->setStyleSheet(QStringLiteral("font-weight:bold; color:#bbb;"));
        lay->addWidget(secLbl);
        auto *form2 = new QFormLayout();
        m_tempo = new QDoubleSpinBox(page);
        m_tempo->setDecimals(3);
        m_tempo->setRange(20.0, 400.0);
        m_tempo->setValue(120.0);
        form2->addRow(tr("Beats per minute (tempo):"), m_tempo);
        m_beatsPerMeasure = new QSpinBox(page);
        m_beatsPerMeasure->setRange(1, 16);
        m_beatsPerMeasure->setValue(4);
        form2->addRow(tr("Beats per measure:"), m_beatsPerMeasure);
        m_noteBeat = makeCombo(page, {tr("Whole"), tr("Half"), tr("Quarter"), tr("Eighth"),
                                      tr("Sixteenth")},
                               2);
        form2->addRow(tr("Note that gets one beat:"), m_noteBeat);
        lay->addLayout(form2);
        m_startAllRuler = new QCheckBox(tr("Start all new projects with these settings"), page);
        lay->addWidget(m_startAllRuler);
        lay->addStretch(1);
        tabs->addTab(page, tr("Ruler"));
    }

    // —— Summary ——
    {
        auto *page = new QWidget(tabs);
        auto *form = new QFormLayout(page);
        m_title = new QLineEdit(page);
        m_artist = new QLineEdit(page);
        m_engineer = new QLineEdit(page);
        m_copyright = new QLineEdit(page);
        m_comments = new QPlainTextEdit(page);
        m_comments->setMinimumHeight(120);
        form->addRow(tr("Title:"), m_title);
        form->addRow(tr("Artist:"), m_artist);
        form->addRow(tr("Engineer:"), m_engineer);
        form->addRow(tr("Copyright:"), m_copyright);
        form->addRow(tr("Comments:"), m_comments);
        m_startAllSummary = new QCheckBox(tr("Start all new projects with these settings"), page);
        form->addRow(QString(), m_startAllSummary);
        tabs->addTab(page, tr("Summary"));
    }

    // —— Audio CD ——
    {
        auto *page = new QWidget(tabs);
        auto *form = new QFormLayout(page);
        m_upc = new QLineEdit(page);
        form->addRow(tr("Universal Product Code / Media Catalog Number:"), m_upc);
        m_firstTrack = new QSpinBox(page);
        m_firstTrack->setRange(1, 99);
        m_firstTrack->setValue(1);
        form->addRow(tr("First track number on disc:"), m_firstTrack);
        form->addItem(new QSpacerItem(1, 1, QSizePolicy::Minimum, QSizePolicy::Expanding));
        tabs->addTab(page, tr("Audio CD"));
    }

    // —— Advanced ——
    {
        auto *page = new QWidget(tabs);
        auto *lay = new QVBoxLayout(page);
        auto *mdRow = new QHBoxLayout();
        m_masterDisplay = makeCombo(
            page, {tr("Rec.2020, 1000 Nits, D65, ST.2084, Full")});
        m_masterDisplay->setEnabled(false);
        auto *customize = new QPushButton(tr("Customize…"), page);
        customize->setEnabled(false);
        mdRow->addWidget(m_masterDisplay, 1);
        mdRow->addWidget(customize);
        auto *form = new QFormLayout();
        form->addRow(tr("Master Display:"), mdRow);
        lay->addLayout(form);
        lay->addWidget(hLine(page));
        m_360 = new QCheckBox(tr("360 Output"), page);
        lay->addWidget(m_360);
        auto *s3dRow = new QHBoxLayout();
        m_stereo3d = makeCombo(page, {tr("Off"), tr("Side by side"), tr("Top/Bottom"),
                                      tr("Anaglyph")});
        m_swapLR = new QCheckBox(tr("Swap Left/Right"), page);
        m_swapLR->setEnabled(false);
        s3dRow->addWidget(m_stereo3d, 1);
        s3dRow->addWidget(m_swapLR);
        auto *form2 = new QFormLayout();
        form2->addRow(tr("Stereoscopic 3D mode:"), s3dRow);
        auto *ctRow = new QHBoxLayout();
        m_crosstalk = new QSlider(Qt::Horizontal, page);
        m_crosstalk->setRange(0, 1000);
        m_crosstalk->setEnabled(false);
        m_crosstalkVal = new QLabel(QStringLiteral("0,000"), page);
        m_crosstalkVal->setEnabled(false);
        ctRow->addWidget(m_crosstalk, 1);
        ctRow->addWidget(m_crosstalkVal);
        form2->addRow(tr("Crosstalk cancellation:"), ctRow);
        lay->addLayout(form2);
        m_includeCancel =
            new QCheckBox(tr("Include cancellation in renders and print to tape"), page);
        m_includeCancel->setEnabled(false);
        lay->addWidget(m_includeCancel);
        connect(m_stereo3d, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](int idx) {
                    const bool on = idx > 0;
                    m_swapLR->setEnabled(on);
                    m_crosstalk->setEnabled(on);
                    m_crosstalkVal->setEnabled(on);
                    m_includeCancel->setEnabled(on);
                    markDirty();
                });
        connect(m_crosstalk, &QSlider::valueChanged, this, [this](int v) {
            m_crosstalkVal->setText(
                QString::number(v / 1000.0, 'f', 3).replace(QLatin1Char('.'), QLatin1Char(',')));
            markDirty();
        });
        lay->addWidget(hLine(page));
        m_startAllAdvanced = new QCheckBox(tr("Start all new projects with these settings"), page);
        lay->addWidget(m_startAllAdvanced);
        lay->addStretch(1);
        tabs->addTab(page, tr("Advanced"));
    }

    root->addWidget(tabs, 1);

    auto *buttons = new QDialogButtonBox(this);
    auto *ok = buttons->addButton(QDialogButtonBox::Ok);
    auto *cancel = buttons->addButton(QDialogButtonBox::Cancel);
    m_applyBtn = buttons->addButton(QDialogButtonBox::Apply);
    m_applyBtn->setEnabled(false);
    connect(ok, &QPushButton::clicked, this, [this]() {
        applyToModel();
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_applyBtn, &QPushButton::clicked, this, [this]() {
        applyToModel();
        m_dirty = false;
        updateApplyEnabled();
    });
    root->addWidget(buttons);

    // Dirty tracking
    const auto dirty = [this]() { markDirty(); };
    for (QComboBox *c :
         {m_template, m_hdr, m_fieldOrder, m_pixelAspect, m_rotation, m_frameRate, m_pixelFormat,
          m_renderQuality, m_motionBlur, m_deinterlace, m_resample, m_masterBus, m_sampleRate,
          m_bitDepth, m_audioQuality, m_rulerFormat, m_noteBeat, m_stereo3d}) {
        if (c) {
            connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this, dirty);
        }
    }
    for (QSpinBox *s : {m_width, m_height, m_stereoBusses, m_beatsPerMeasure, m_firstTrack}) {
        if (s) {
            connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, dirty);
        }
    }
    if (m_tempo) {
        connect(m_tempo, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, dirty);
    }
    for (QLineEdit *e : {m_prerenderFolder, m_recordFolder, m_rulerStart, m_title, m_artist,
                         m_engineer, m_copyright, m_upc}) {
        if (e) {
            connect(e, &QLineEdit::textChanged, this, dirty);
        }
    }
    if (m_comments) {
        connect(m_comments, &QPlainTextEdit::textChanged, this, dirty);
    }
    for (QCheckBox *c : {m_adjustSource, m_overridePrerender, m_startAllVideo, m_lfeFilter,
                         m_startAllAudio, m_startAllRuler, m_startAllSummary, m_360, m_swapLR,
                         m_includeCancel, m_startAllAdvanced}) {
        if (c) {
            connect(c, &QCheckBox::toggled, this, dirty);
        }
    }
}

void ProjectPropertiesDialog::markDirty()
{
    m_dirty = true;
    updateApplyEnabled();
}

void ProjectPropertiesDialog::updateApplyEnabled()
{
    if (m_applyBtn) {
        m_applyBtn->setEnabled(m_dirty);
    }
}

void ProjectPropertiesDialog::loadFromModel()
{
    if (!m_model) {
        return;
    }
    m_width->setValue(m_model->frameWidth());
    m_height->setValue(m_model->frameHeight());

    const double fps = m_model->frameRate();
    int fpsIdx = 3; // 29.97
    const auto near = [](double a, double b) { return std::abs(a - b) < 0.01; };
    if (near(fps, 23.976)) {
        fpsIdx = 0;
    } else if (near(fps, 24.0)) {
        fpsIdx = 1;
    } else if (near(fps, 25.0)) {
        fpsIdx = 2;
    } else if (near(fps, 29.97)) {
        fpsIdx = 3;
    } else if (near(fps, 30.0)) {
        fpsIdx = 4;
    } else if (near(fps, 50.0)) {
        fpsIdx = 5;
    } else if (near(fps, 59.94)) {
        fpsIdx = 6;
    } else if (near(fps, 60.0)) {
        fpsIdx = 7;
    }
    m_frameRate->setCurrentIndex(fpsIdx);

    const quint32 sr = m_model->sampleRate();
    m_sampleRate->setCurrentIndex(sr >= 96000 ? 2 : (sr >= 48000 ? 1 : 0));
    m_tempo->setValue(m_model->tempoBpm());
    m_title->setText(m_model->projectTitle());

    m_dirty = false;
    updateApplyEnabled();
}

void ProjectPropertiesDialog::applyToModel()
{
    if (!m_model) {
        return;
    }
    m_model->setFrameSize(m_width->value(), m_height->value());

    static const double kFps[] = {23.976, 24.0, 25.0, 29.97, 30.0, 50.0, 59.94, 60.0};
    const int fi = std::clamp(m_frameRate->currentIndex(), 0, 7);
    m_model->setFrameRate(kFps[fi]);

    static const quint32 kSr[] = {44100u, 48000u, 96000u};
    const int si = std::clamp(m_sampleRate->currentIndex(), 0, 2);
    m_model->setSampleRate(kSr[si]);
    m_model->setTempoBpm(m_tempo->value());
}

} // namespace openvegas
