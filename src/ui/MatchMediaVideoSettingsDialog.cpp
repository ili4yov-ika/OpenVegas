#include "ui/MatchMediaVideoSettingsDialog.h"
#include "io/SamplePaths.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace openvegas {
namespace {

const char *kMediaFilter =
    "All Media Files (*.mp4 *.mpg *.mpeg *.m2t *.m2ts *.mts *.mov *.avi *.mkv "
    "*.wmv *.webm *.mxf *.ts *.m4v *.wav *.wave *.aiff *.mp3 *.flac *.ogg *.jpg "
    "*.jpeg *.png *.tif *.tiff *.bmp);;"
    "Video Files (*.mp4 *.mpg *.mpeg *.m2t *.m2ts *.mts *.mov *.avi *.mkv *.wmv "
    "*.webm *.mxf *.ts *.m4v);;"
    "Audio Files (*.wav *.wave *.aiff *.mp3 *.flac *.ogg);;"
    "Image Files (*.jpg *.jpeg *.png *.tif *.tiff *.bmp);;"
    "All Files (*.*)";

QLabel *roField(QWidget *parent)
{
    auto *l = new QLabel(QStringLiteral("—"), parent);
    l->setObjectName(QStringLiteral("mmField"));
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setMinimumWidth(120);
    return l;
}

} // namespace

MatchMediaVideoSettingsDialog::MatchMediaVideoSettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Match Media Video Settings"));
    setModal(true);
    setMinimumSize(720, 480);
    resize(820, 560);
    setObjectName(QStringLiteral("matchMediaVideoSettingsDialog"));
    setStyleSheet(QStringLiteral(
        "#matchMediaVideoSettingsDialog { background:#2a2a2a; color:#e0e0e0; }"
        "QLabel { color:#ddd; }"
        "QLabel#mmField {"
        "  background:#1a1a1a; color:#eee; border:1px solid #555; padding:2px 6px;"
        "  min-height:18px; }"
        "QLineEdit, QComboBox {"
        "  background:#1a1a1a; color:#eee; border:1px solid #555; padding:2px 6px;"
        "  min-height:20px; }"
        "QComboBox::drop-down { width:16px; border:none; }"
        "QComboBox QAbstractItemView {"
        "  background:#1a1a1a; color:#eee; selection-background-color:#0078d7; }"
        "QListView {"
        "  background:#1e1e1e; color:#e0e0e0; border:1px solid #444;"
        "  alternate-background-color:#252525; }"
        "QListView::item:selected { background:#0078d7; color:#fff; }"
        "QCheckBox { color:#ddd; spacing:6px; }"
        "QPushButton { background:#3a3a3a; color:#eee; border:1px solid #555;"
        "  padding:4px 14px; min-height:22px; }"
        "QPushButton:hover { background:#4a4a4a; }"
        "QPushButton:disabled { color:#666; }"
        "QFrame#mmDetails { background:#252525; border:1px solid #444; }"));
    buildUi();

    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    QString start = s.value(QStringLiteral("paths/lastMatchMediaDir")).toString();
    if (start.isEmpty() || !QDir(start).exists()) {
        start = SamplePaths::vegProjectDir();
    }
    if (start.isEmpty() || !QDir(start).exists()) {
        start = SamplePaths::samplesDir();
    }
    if (start.isEmpty() || !QDir(start).exists()) {
        start = QDir::homePath();
    }
    navigateTo(start);
}

void MatchMediaVideoSettingsDialog::setStartDirectory(const QString &dir)
{
    if (!dir.isEmpty() && QDir(dir).exists()) {
        navigateTo(dir);
    }
}

void MatchMediaVideoSettingsDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // Path bar
    auto *pathRow = new QHBoxLayout();
    pathRow->setSpacing(4);
    m_pathCombo = new QComboBox(this);
    m_pathCombo->setEditable(true);
    m_pathCombo->setInsertPolicy(QComboBox::NoInsert);
    pathRow->addWidget(m_pathCombo, 1);
    auto *upBtn = new QPushButton(tr("Up"), this);
    upBtn->setFixedWidth(48);
    pathRow->addWidget(upBtn);
    root->addLayout(pathRow);

    m_model = new QFileSystemModel(this);
    m_model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    m_model->setNameFilterDisables(false);

    m_view = new QListView(this);
    m_view->setModel(m_model);
    m_view->setViewMode(QListView::ListMode);
    m_view->setUniformItemSizes(true);
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    root->addWidget(m_view, 1);

    // Details strip (Vegas)
    auto *details = new QFrame(this);
    details->setObjectName(QStringLiteral("mmDetails"));
    auto *grid = new QGridLayout(details);
    grid->setContentsMargins(8, 8, 8, 8);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(4);

    auto addLabeled = [&](int row, int col, const QString &label, QLabel *&field) {
        auto *lab = new QLabel(label, details);
        field = roField(details);
        grid->addWidget(lab, row, col * 2);
        grid->addWidget(field, row, col * 2 + 1);
    };

    addLabeled(0, 0, tr("File type:"), m_fileType);
    addLabeled(1, 0, tr("Streams:"), m_streams);
    addLabeled(2, 0, tr("Video:"), m_video);
    m_videoTime = roField(details);
    grid->addWidget(m_videoTime, 3, 1);

    addLabeled(0, 1, tr("Audio:"), m_audio);
    m_audioTime = roField(details);
    grid->addWidget(m_audioTime, 1, 3);

    m_openSequence = new QCheckBox(tr("Open sequence"), details);
    grid->addWidget(new QLabel(tr("Stills:"), details), 2, 2);
    grid->addWidget(m_openSequence, 2, 3);

    m_firstImage = new QLineEdit(details);
    m_firstImage->setReadOnly(true);
    m_lastImage = new QLineEdit(details);
    m_lastImage->setReadOnly(true);
    grid->addWidget(new QLabel(tr("First image:"), details), 3, 2);
    grid->addWidget(m_firstImage, 3, 3);

    auto *rightBtns = new QVBoxLayout();
    rightBtns->setSpacing(4);
    auto *customBtn = new QPushButton(tr("Custom…"), details);
    customBtn->setEnabled(false);
    auto *aboutBtn = new QPushButton(tr("About…"), details);
    aboutBtn->setEnabled(false);
    rightBtns->addWidget(customBtn);
    rightBtns->addWidget(aboutBtn);
    rightBtns->addStretch(1);
    grid->addWidget(new QLabel(tr("Last image:"), details), 0, 4);
    grid->addWidget(m_lastImage, 0, 5);
    grid->addLayout(rightBtns, 1, 4, 3, 2);

    root->addWidget(details);

    // File name / filter / buttons
    auto *nameRow = new QHBoxLayout();
    nameRow->addWidget(new QLabel(tr("File name:"), this));
    m_fileName = new QComboBox(this);
    m_fileName->setEditable(true);
    m_fileName->setInsertPolicy(QComboBox::NoInsert);
    nameRow->addWidget(m_fileName, 1);
    root->addLayout(nameRow);

    auto *bot = new QHBoxLayout();
    bot->setSpacing(6);
    m_filter = new QComboBox(this);
    for (const QString &part : QString::fromLatin1(kMediaFilter).split(QStringLiteral(";;"))) {
        m_filter->addItem(part);
    }
    bot->addWidget(m_filter, 1);
    auto *openBtn = new QPushButton(tr("Open"), this);
    openBtn->setDefault(true);
    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    bot->addWidget(openBtn);
    bot->addWidget(cancelBtn);
    root->addLayout(bot);

    connect(upBtn, &QPushButton::clicked, this, [this]() {
        const QModelIndex idx = m_model->index(m_model->rootPath());
        const QModelIndex parent = idx.parent();
        if (parent.isValid()) {
            navigateTo(m_model->filePath(parent));
        }
    });
    connect(m_pathCombo, &QComboBox::activated, this, [this](int) {
        navigateTo(m_pathCombo->currentText());
    });
    connect(m_pathCombo->lineEdit(), &QLineEdit::returnPressed, this, [this]() {
        navigateTo(m_pathCombo->currentText());
    });
    connect(m_view, &QListView::clicked, this, &MatchMediaVideoSettingsDialog::onSelectionChanged);
    connect(m_view, &QListView::doubleClicked, this, [this](const QModelIndex &index) {
        if (!index.isValid()) {
            return;
        }
        if (m_model->isDir(index)) {
            navigateTo(m_model->filePath(index));
        } else {
            onSelectionChanged(index);
            acceptSelection();
        }
    });
    auto applyFilter = [this]() {
        const QString t = m_filter->currentText();
        const int l = t.indexOf(QLatin1Char('('));
        const int r = t.lastIndexOf(QLatin1Char(')'));
        QStringList patterns;
        if (l >= 0 && r > l) {
            patterns = t.mid(l + 1, r - l - 1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        }
        if (patterns.isEmpty() || patterns.contains(QStringLiteral("*.*"))) {
            m_model->setNameFilters({});
        } else {
            m_model->setNameFilters(patterns);
        }
    };
    connect(m_filter, &QComboBox::currentIndexChanged, this, [applyFilter](int) { applyFilter(); });
    connect(openBtn, &QPushButton::clicked, this, &MatchMediaVideoSettingsDialog::acceptSelection);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    applyFilter();
}

void MatchMediaVideoSettingsDialog::navigateTo(const QString &dir)
{
    QString path = QDir::cleanPath(dir);
    if (!QDir(path).exists()) {
        return;
    }
    const QModelIndex idx = m_model->setRootPath(path);
    m_view->setRootIndex(idx);
    const int found = m_pathCombo->findText(path);
    if (found < 0) {
        m_pathCombo->addItem(path);
    }
    m_pathCombo->setCurrentText(path);
    clearDetails();
}

void MatchMediaVideoSettingsDialog::onSelectionChanged(const QModelIndex &index)
{
    if (!index.isValid() || m_model->isDir(index)) {
        clearDetails();
        m_currentPath.clear();
        return;
    }
    const QString path = m_model->filePath(index);
    m_currentPath = path;
    m_fileName->setCurrentText(QFileInfo(path).fileName());
    refreshDetails(path);
}

void MatchMediaVideoSettingsDialog::clearDetails()
{
    m_fileType->setText(QStringLiteral("—"));
    m_streams->setText(QStringLiteral("—"));
    m_video->setText(QStringLiteral("—"));
    m_videoTime->setText(QString());
    m_audio->setText(QStringLiteral("—"));
    m_audioTime->setText(QString());
    m_firstImage->clear();
    m_lastImage->clear();
}

void MatchMediaVideoSettingsDialog::refreshDetails(const QString &path)
{
    const MediaProbeInfo info = MediaProbe::probe(path);
    if (!info.ok) {
        m_fileType->setText(info.error.isEmpty() ? tr("Unknown") : info.error);
        m_streams->setText(QStringLiteral("0"));
        m_video->setText(QStringLiteral("—"));
        m_videoTime->clear();
        m_audio->setText(QStringLiteral("—"));
        m_audioTime->clear();
        return;
    }
    m_fileType->setText(info.fileTypeLabel());
    m_streams->setText(QString::number(info.streamCount));
    m_video->setText(info.hasVideo ? info.videoSummary() : QStringLiteral("—"));
    m_videoTime->setText(info.hasVideo ? info.timecode() : QString());
    m_audio->setText(info.hasAudio ? info.audioSummary() : QStringLiteral("—"));
    m_audioTime->setText(info.hasAudio ? info.timecode() : QString());
}

void MatchMediaVideoSettingsDialog::acceptSelection()
{
    QString path = m_currentPath;
    if (path.isEmpty()) {
        const QString name = m_fileName->currentText().trimmed();
        if (!name.isEmpty()) {
            path = QDir(m_model->rootPath()).filePath(name);
        }
    }
    if (path.isEmpty() || !QFileInfo::exists(path) || QFileInfo(path).isDir()) {
        return;
    }

    m_result = MediaProbe::probe(path);
    if (!m_result.ok || !m_result.hasVideo) {
        // Allow open only when video stream present (match *video* settings)
        m_fileType->setText(m_result.error.isEmpty() ? tr("No video stream") : m_result.error);
        return;
    }

    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    s.setValue(QStringLiteral("paths/lastMatchMediaDir"), QFileInfo(path).absolutePath());
    accept();
}

} // namespace openvegas
