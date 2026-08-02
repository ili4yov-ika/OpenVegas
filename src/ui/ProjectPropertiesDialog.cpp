#include "ui/ProjectPropertiesDialog.h"
#include "model/ProjectModel.h"
#include "ui/IconFactory.h"
#include "ui/MatchMediaVideoSettingsDialog.h"

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
#include <QToolButton>
#include <QVBoxLayout>
#include <QStorageInfo>
#include <QStandardPaths>
#include <QSpacerItem>
#include <QSizePolicy>
#include <algorithm>
#include <cmath>

namespace openvegas {

namespace {

constexpr int kFieldH = 22;

QComboBox *makeCombo(QWidget *parent, const QStringList &items, int current = 0)
{
    auto *c = new QComboBox(parent);
    c->addItems(items);
    if (current >= 0 && current < items.size()) {
        c->setCurrentIndex(current);
    }
    c->setFixedHeight(kFieldH);
    c->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return c;
}

void styleField(QWidget *w, int minW = 0)
{
    if (!w) {
        return;
    }
    w->setFixedHeight(kFieldH);
    w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (minW > 0) {
        w->setMinimumWidth(minW);
        w->setMaximumWidth(minW > 120 ? minW : 120);
        w->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
}

QFormLayout *makeForm()
{
    auto *form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    form->setVerticalSpacing(4);
    form->setHorizontalSpacing(8);
    return form;
}

void tightenPage(QVBoxLayout *lay)
{
    lay->setContentsMargins(8, 6, 8, 6);
    lay->setSpacing(4);
    lay->setSizeConstraint(QLayout::SetMinimumSize);
}

QScrollArea *wrapScroll(QWidget *inner)
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(inner);
    inner->setMinimumWidth(520);
    return scroll;
}

QFrame *hLine(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background:#444; border:none; max-height:1px;"));
    line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return line;
}

QToolButton *icoBtn(QWidget *parent, const QString &tip, const QString &svg)
{
    auto *b = new QToolButton(parent);
    b->setObjectName(QStringLiteral("ppIco"));
    b->setToolTip(tip);
    b->setIcon(IconFactory::iconFromSvgBody(svg, 13));
    b->setIconSize(QSize(13, 13));
    b->setFixedSize(22, 22);
    b->setAutoRaise(false);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
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

QString svgMatchMedia()
{
    return QStringLiteral(
        "<path d=\"M2 4.5h4.2l1.2 1.5H14v6.5H2V4.5z\" fill=\"none\" stroke=\"currentColor\" "
        "stroke-width=\"1.2\"/>"
        "<path d=\"M10 7.5l2.2 2.2L10 11.9M8.5 9.7h3.8\" fill=\"none\" stroke=\"#0078d7\" "
        "stroke-width=\"1.2\"/>");
}

} // namespace

ProjectPropertiesDialog::ProjectPropertiesDialog(ProjectModel *model, QWidget *parent)
    : QDialog(parent)
    , m_model(model)
{
    setWindowTitle(tr("Project Properties"));
    setModal(true);
    setMinimumSize(540, 480);
    resize(580, 560);
    setObjectName(QStringLiteral("projectPropertiesDialog"));
    setStyleSheet(QStringLiteral(
        "#projectPropertiesDialog { background:#2a2a2a; color:#e0e0e0; }"
        "QTabWidget::pane { border:1px solid #444; background:#2a2a2a; top:-1px; }"
        "QTabBar::tab { background:#333; color:#ccc; padding:4px 10px; border:1px solid #444; "
        "border-bottom:none; margin-right:1px; min-height:18px; }"
        "QTabBar::tab:selected { background:#3a3a3a; color:#fff; }"
        "QLabel { color:#ddd; }"
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit {"
        "  background:#1a1a1a; color:#eee; border:1px solid #555;"
        "  padding:1px 6px; min-height:18px; selection-background-color:#0078d7; }"
        "QComboBox { padding-right:18px; }"
        "QComboBox::drop-down { width:16px; border:none; }"
        "QComboBox QAbstractItemView {"
        "  background:#1a1a1a; color:#eee; selection-background-color:#0078d7;"
        "  border:1px solid #555; outline:0; }"
        "QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled {"
        "  color:#777; background:#242424; }"
        "QCheckBox { color:#ddd; spacing:6px; min-height:18px; }"
        "QCheckBox::indicator { width:13px; height:13px; }"
        "QPushButton { background:#3a3a3a; color:#eee; border:1px solid #555; padding:3px 12px;"
        "  min-height:20px; }"
        "QPushButton:hover { background:#4a4a4a; }"
        "QPushButton:disabled { color:#666; background:#2e2e2e; }"
        "QToolButton#ppIco {"
        "  background:#3a3a3a; border:1px solid #555; border-radius:2px; padding:0; }"
        "QToolButton#ppIco:hover { background:#4a4a4a; }"
        "QSlider::groove:horizontal { height:4px; background:#444; }"
        "QSlider::handle:horizontal { width:11px; margin:-4px 0; background:#888; }"
        "QScrollArea { background:transparent; border:none; }"));
    buildUi();
    loadFromModel();
}

void ProjectPropertiesDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto *tabs = new QTabWidget(this);

    // —— Video ——
    {
        auto *page = new QWidget;
        auto *lay = new QVBoxLayout(page);
        tightenPage(lay);

        // Template + tools
        auto *tplRow = new QHBoxLayout();
        tplRow->setContentsMargins(0, 0, 0, 0);
        tplRow->setSpacing(4);
        auto *tplLbl = new QLabel(tr("Template:"), page);
        m_template = makeCombo(page,
                               {tr("Custom (1920x1080; 29,970 fps)"),
                                tr("Custom (1920x1080; 59,940 fps)"),
                                tr("HD 1280x720; 29,970 fps"), tr("UHD 3840x2160; 29,970 fps")});
        tplRow->addWidget(tplLbl);
        tplRow->addWidget(m_template, 1);
        tplRow->addWidget(icoBtn(page, tr("Save Template"), IconFactory::svgSave()));
        tplRow->addWidget(icoBtn(page, tr("Delete Template"), IconFactory::svgRemove()));
        {
            QToolButton *match = icoBtn(page, tr("Match Media Video Settings"), svgMatchMedia());
            connect(match, &QToolButton::clicked, this,
                    &ProjectPropertiesDialog::onMatchMediaVideoSettings);
            tplRow->addWidget(match);
        }
        lay->addLayout(tplRow);

        // Vegas two-column top: size/HDR | field/PAR/rotation/fps
        auto *topGrid = new QHBoxLayout();
        topGrid->setContentsMargins(0, 2, 0, 2);
        topGrid->setSpacing(18);

        auto *leftForm = makeForm();
        m_width = new QSpinBox(page);
        m_width->setRange(16, 8192);
        m_width->setValue(1920);
        m_width->setFixedHeight(kFieldH);
        m_width->setMinimumWidth(88);
        m_width->setMaximumWidth(110);
        m_height = new QSpinBox(page);
        m_height->setRange(16, 8192);
        m_height->setValue(1080);
        m_height->setFixedHeight(kFieldH);
        m_height->setMinimumWidth(88);
        m_height->setMaximumWidth(110);
        m_hdr = makeCombo(page, {tr("Off"), tr("HDR10"), tr("HLG")});
        leftForm->addRow(tr("Width:"), m_width);
        leftForm->addRow(tr("Height:"), m_height);
        leftForm->addRow(tr("HDR Mode:"), m_hdr);

        auto *rightForm = makeForm();
        m_fieldOrder =
            makeCombo(page, {tr("None (progressive scan)"), tr("Upper field first"),
                             tr("Lower field first")});
        m_pixelAspect = makeCombo(page, {tr("1,0000 (Square)"), tr("0,9091 (NTSC DV)"),
                                         tr("1,0926 (PAL DV)")});
        m_rotation = makeCombo(page, {tr("0° (original)"), tr("90° clockwise"),
                                      tr("90° counter-clockwise"), tr("180°")});
        m_frameRate = makeCombo(page,
                                {tr("23,976 (Film)"), tr("24,000 (Film)"), tr("25,000 (PAL)"),
                                 tr("29,970 (NTSC)"), tr("30,000"), tr("50,000"),
                                 tr("59,940 (Double NTSC)"), tr("60,000")});
        m_frameRate->setCurrentIndex(3);
        rightForm->addRow(tr("Field order:"), m_fieldOrder);
        rightForm->addRow(tr("Pixel aspect ratio:"), m_pixelAspect);
        rightForm->addRow(tr("Output rotation:"), m_rotation);
        rightForm->addRow(tr("Frame rate:"), m_frameRate);

        topGrid->addLayout(leftForm, 2);
        topGrid->addLayout(rightForm, 3);
        lay->addLayout(topGrid);
        lay->addWidget(hLine(page));

        auto *form2 = makeForm();
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

        auto *hintRow = new QHBoxLayout();
        hintRow->setContentsMargins(22, 0, 0, 0);
        hintRow->setSpacing(8);
        auto *fmtHint = new QLabel(tr("Format: AVC/AAC MP4\nTemplate: Internet 1920x1080 progressive"),
                                   page);
        fmtHint->setStyleSheet(QStringLiteral("color:#999;"));
        auto *selectBtn = new QPushButton(tr("Select…"), page);
        selectBtn->setEnabled(false);
        selectBtn->setFixedHeight(kFieldH);
        hintRow->addWidget(fmtHint, 1);
        hintRow->addWidget(selectBtn, 0, Qt::AlignTop);
        lay->addLayout(hintRow);
        connect(m_overridePrerender, &QCheckBox::toggled, selectBtn, &QPushButton::setEnabled);

        auto *folderLbl = new QLabel(tr("Prerendered files folder:"), page);
        lay->addWidget(folderLbl);
        auto *folderRow = new QHBoxLayout();
        folderRow->setContentsMargins(0, 0, 0, 0);
        folderRow->setSpacing(4);
        m_prerenderFolder = new QLineEdit(page);
        styleField(m_prerenderFolder);
        m_prerenderFolder->setText(
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
        auto *browse = new QPushButton(tr("Browse…"), page);
        browse->setFixedHeight(kFieldH);
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
        lay->addLayout(folderRow);
        auto *freeLbl = new QLabel(freeSpaceLabel(m_prerenderFolder->text()), page);
        freeLbl->setStyleSheet(QStringLiteral("color:#999; margin-top:2px;"));
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
        tightenPage(lay);
        auto *form = makeForm();
        m_masterBus = makeCombo(page, {tr("Stereo"), tr("Surround 5.1"), tr("Surround 7.1")});
        form->addRow(tr("Master bus mode:"), m_masterBus);
        m_stereoBusses = new QSpinBox(page);
        m_stereoBusses->setRange(0, 32);
        m_stereoBusses->setFixedHeight(kFieldH);
        m_stereoBusses->setMaximumWidth(80);
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
        auto *lfeForm = makeForm();
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

        auto *recLbl = new QLabel(tr("Recorded files folder:"), page);
        lay->addWidget(recLbl);
        auto *recRow = new QHBoxLayout();
        recRow->setSpacing(4);
        m_recordFolder = new QLineEdit(page);
        styleField(m_recordFolder);
        m_recordFolder->setText(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
        auto *browse = new QPushButton(tr("Browse…"), page);
        browse->setFixedHeight(kFieldH);
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
        lay->addLayout(recRow);
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
        tightenPage(lay);
        auto *form = makeForm();
        m_rulerFormat = makeCombo(
            page,
            {tr("Time"), tr("Seconds"), tr("Time & Frames"), tr("Absolute Frames"),
             tr("Measures & Beats"), tr("SMPTE Non-Drop (29.97 fps, Video)")},
            4);
        form->addRow(tr("Ruler time format:"), m_rulerFormat);
        m_rulerStart = new QLineEdit(QStringLiteral("1.1.000"), page);
        styleField(m_rulerStart);
        form->addRow(tr("Ruler start time:"), m_rulerStart);
        lay->addLayout(form);
        lay->addWidget(hLine(page));
        auto *secLbl = new QLabel(tr("Measures & Beats"), page);
        secLbl->setStyleSheet(QStringLiteral("font-weight:600; color:#bbb; margin-top:2px;"));
        lay->addWidget(secLbl);
        auto *form2 = makeForm();
        m_tempo = new QDoubleSpinBox(page);
        m_tempo->setDecimals(3);
        m_tempo->setRange(20.0, 400.0);
        m_tempo->setValue(120.0);
        m_tempo->setFixedHeight(kFieldH);
        form2->addRow(tr("Beats per minute (tempo):"), m_tempo);
        m_beatsPerMeasure = new QSpinBox(page);
        m_beatsPerMeasure->setRange(1, 16);
        m_beatsPerMeasure->setValue(4);
        m_beatsPerMeasure->setFixedHeight(kFieldH);
        m_beatsPerMeasure->setMaximumWidth(72);
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
        auto *lay = new QVBoxLayout(page);
        tightenPage(lay);
        auto *form = makeForm();
        m_title = new QLineEdit(page);
        m_artist = new QLineEdit(page);
        m_engineer = new QLineEdit(page);
        m_copyright = new QLineEdit(page);
        m_comments = new QPlainTextEdit(page);
        m_comments->setMinimumHeight(100);
        styleField(m_title);
        styleField(m_artist);
        styleField(m_engineer);
        styleField(m_copyright);
        form->addRow(tr("Title:"), m_title);
        form->addRow(tr("Artist:"), m_artist);
        form->addRow(tr("Engineer:"), m_engineer);
        form->addRow(tr("Copyright:"), m_copyright);
        form->addRow(tr("Comments:"), m_comments);
        lay->addLayout(form);
        m_startAllSummary = new QCheckBox(tr("Start all new projects with these settings"), page);
        lay->addWidget(m_startAllSummary);
        lay->addStretch(1);
        tabs->addTab(page, tr("Summary"));
    }

    // —— Audio CD ——
    {
        auto *page = new QWidget(tabs);
        auto *lay = new QVBoxLayout(page);
        tightenPage(lay);
        auto *form = makeForm();
        m_upc = new QLineEdit(page);
        styleField(m_upc);
        form->addRow(tr("Universal Product Code / Media Catalog Number:"), m_upc);
        m_firstTrack = new QSpinBox(page);
        m_firstTrack->setRange(1, 99);
        m_firstTrack->setValue(1);
        m_firstTrack->setFixedHeight(kFieldH);
        m_firstTrack->setMaximumWidth(72);
        form->addRow(tr("First track number on disc:"), m_firstTrack);
        lay->addLayout(form);
        lay->addStretch(1);
        tabs->addTab(page, tr("Audio CD"));
    }

    // —— Advanced ——
    {
        auto *page = new QWidget(tabs);
        auto *lay = new QVBoxLayout(page);
        tightenPage(lay);
        auto *mdRow = new QHBoxLayout();
        mdRow->setSpacing(4);
        m_masterDisplay = makeCombo(
            page, {tr("Rec.2020, 1000 Nits, D65, ST.2084, Full")});
        m_masterDisplay->setEnabled(false);
        auto *customize = new QPushButton(tr("Customize…"), page);
        customize->setEnabled(false);
        customize->setFixedHeight(kFieldH);
        mdRow->addWidget(m_masterDisplay, 1);
        mdRow->addWidget(customize);
        auto *form = makeForm();
        form->addRow(tr("Master Display:"), mdRow);
        lay->addLayout(form);
        lay->addWidget(hLine(page));
        m_360 = new QCheckBox(tr("360 Output"), page);
        lay->addWidget(m_360);
        auto *s3dRow = new QHBoxLayout();
        s3dRow->setSpacing(8);
        m_stereo3d = makeCombo(page, {tr("Off"), tr("Side by side"), tr("Top/Bottom"),
                                      tr("Anaglyph")});
        m_swapLR = new QCheckBox(tr("Swap Left/Right"), page);
        m_swapLR->setEnabled(false);
        s3dRow->addWidget(m_stereo3d, 1);
        s3dRow->addWidget(m_swapLR);
        auto *form2 = makeForm();
        form2->addRow(tr("Stereoscopic 3D mode:"), s3dRow);
        auto *ctRow = new QHBoxLayout();
        ctRow->setSpacing(6);
        m_crosstalk = new QSlider(Qt::Horizontal, page);
        m_crosstalk->setRange(0, 1000);
        m_crosstalk->setEnabled(false);
        m_crosstalkVal = new QLabel(QStringLiteral("0,000"), page);
        m_crosstalkVal->setEnabled(false);
        m_crosstalkVal->setFixedWidth(40);
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

void ProjectPropertiesDialog::onMatchMediaVideoSettings()
{
    MatchMediaVideoSettingsDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    const MediaProbeInfo &info = dlg.result();
    applyMatchedMedia(info.width, info.height, info.frameRate, info.fieldOrder);
}

void ProjectPropertiesDialog::applyMatchedMedia(int width, int height, double frameRate,
                                                const QString &fieldOrder)
{
    if (width > 0) {
        m_width->setValue(width);
    }
    if (height > 0) {
        m_height->setValue(height);
    }

    if (frameRate > 0.0) {
        static const double kFps[] = {23.976, 24.0, 25.0, 29.97, 30.0, 50.0, 59.94, 60.0};
        int best = 0;
        double bestDiff = 1e9;
        for (int i = 0; i < 8; ++i) {
            const double d = std::abs(kFps[i] - frameRate);
            if (d < bestDiff) {
                bestDiff = d;
                best = i;
            }
        }
        m_frameRate->setCurrentIndex(best);
    }

    const QString fo = fieldOrder.toLower();
    if (fo.contains(QLatin1String("tt")) || fo.contains(QLatin1String("tb"))
        || fo.contains(QLatin1String("top"))) {
        m_fieldOrder->setCurrentIndex(1); // Upper field first
    } else if (fo.contains(QLatin1String("bb")) || fo.contains(QLatin1String("bt"))
               || fo.contains(QLatin1String("bottom"))) {
        m_fieldOrder->setCurrentIndex(2); // Lower field first
    } else {
        m_fieldOrder->setCurrentIndex(0); // progressive
    }

    // Reflect custom template label like Vegas
    const QString fpsTxt = m_frameRate->currentText().section(QLatin1Char(' '), 0, 0);
    const QString custom =
        tr("Custom (%1x%2; %3 fps)").arg(m_width->value()).arg(m_height->value()).arg(fpsTxt);
    const int existing = m_template->findText(custom);
    if (existing >= 0) {
        m_template->setCurrentIndex(existing);
    } else {
        m_template->insertItem(0, custom);
        m_template->setCurrentIndex(0);
    }

    markDirty();
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
