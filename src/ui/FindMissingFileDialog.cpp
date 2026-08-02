#include "ui/FindMissingFileDialog.h"
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

FindMissingFileDialog::FindMissingFileDialog(const QString &missingPath, QWidget *parent)
    : QDialog(parent)
    , m_expectedName(QFileInfo(missingPath).fileName())
{
    setWindowTitle(tr("Find Missing File: %1").arg(QDir::toNativeSeparators(missingPath)));
    setModal(true);
    setMinimumSize(720, 480);
    resize(860, 560);
    setObjectName(QStringLiteral("findMissingFileDialog"));
    setStyleSheet(QStringLiteral(
        "#findMissingFileDialog { background:#2a2a2a; color:#e0e0e0; }"
        "QLabel { color:#ddd; }"
        "QLabel#mmField {"
        "  background:#1a1a1a; color:#eee; border:1px solid #555; padding:2px 6px;"
        "  min-height:18px; }"
        "QLineEdit, QComboBox {"
        "  background:#1a1a1a; color:#eee; border:1px solid #555; padding:2px 6px;"
        "  min-height:20px; }"
        "QListView {"
        "  background:#1e1e1e; color:#e0e0e0; border:1px solid #444;"
        "  alternate-background-color:#252525; }"
        "QListView::item:selected { background:#0078d7; color:#fff; }"
        "QPushButton { background:#3a3a3a; color:#eee; border:1px solid #555;"
        "  padding:4px 14px; min-height:22px; }"
        "QPushButton:hover { background:#4a4a4a; }"
        "QPushButton:disabled { color:#666; }"
        "QFrame#mmDetails { background:#252525; border:1px solid #444; }"));
    buildUi();

    QString start = QFileInfo(missingPath).absolutePath();
    if (!QDir(start).exists()) {
        QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
        start = s.value(QStringLiteral("paths/lastFindMissingDir")).toString();
    }
    if (start.isEmpty() || !QDir(start).exists()) {
        start = SamplePaths::vegProjectDir();
    }
    if (start.isEmpty() || !QDir(start).exists()) {
        start = QDir::homePath();
    }
    navigateTo(start);
    if (!m_expectedName.isEmpty()) {
        m_fileName->setCurrentText(m_expectedName);
    }
}

void FindMissingFileDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto *pathRow = new QHBoxLayout();
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
    m_view->setUniformItemSizes(true);
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    root->addWidget(m_view, 1);

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
    addLabeled(0, 1, tr("Audio:"), m_audio);

    auto *stillLab = new QLabel(tr("Stills:"), details);
    auto *seq = new QCheckBox(tr("Open sequence"), details);
    seq->setEnabled(false);
    grid->addWidget(stillLab, 1, 2);
    grid->addWidget(seq, 1, 3);

    auto *customBtn = new QPushButton(tr("Custom…"), details);
    customBtn->setEnabled(false);
    auto *aboutBtn = new QPushButton(tr("About…"), details);
    aboutBtn->setEnabled(false);
    auto *rightBtns = new QVBoxLayout();
    rightBtns->addWidget(customBtn);
    rightBtns->addWidget(aboutBtn);
    rightBtns->addStretch(1);
    grid->addLayout(rightBtns, 0, 4, 3, 1);
    root->addWidget(details);

    auto *nameRow = new QHBoxLayout();
    nameRow->addWidget(new QLabel(tr("File name:"), this));
    m_fileName = new QComboBox(this);
    m_fileName->setEditable(true);
    m_fileName->setInsertPolicy(QComboBox::NoInsert);
    nameRow->addWidget(m_fileName, 1);
    root->addLayout(nameRow);

    auto *bot = new QHBoxLayout();
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
    connect(m_view, &QListView::clicked, this, &FindMissingFileDialog::onSelectionChanged);
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
    connect(openBtn, &QPushButton::clicked, this, &FindMissingFileDialog::acceptSelection);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    applyFilter();
}

void FindMissingFileDialog::navigateTo(const QString &dir)
{
    QString path = QDir::cleanPath(dir);
    if (!QDir(path).exists()) {
        return;
    }
    const QModelIndex idx = m_model->setRootPath(path);
    m_view->setRootIndex(idx);
    if (m_pathCombo->findText(path) < 0) {
        m_pathCombo->addItem(path);
    }
    m_pathCombo->setCurrentText(path);
    clearDetails();
}

void FindMissingFileDialog::onSelectionChanged(const QModelIndex &index)
{
    if (!index.isValid() || m_model->isDir(index)) {
        clearDetails();
        m_selectedPath.clear();
        return;
    }
    const QString path = m_model->filePath(index);
    m_selectedPath = path;
    m_fileName->setCurrentText(QFileInfo(path).fileName());
    refreshDetails(path);
}

void FindMissingFileDialog::clearDetails()
{
    m_fileType->setText(QStringLiteral("—"));
    m_streams->setText(QStringLiteral("—"));
    m_video->setText(QStringLiteral("—"));
    m_audio->setText(QStringLiteral("—"));
}

void FindMissingFileDialog::refreshDetails(const QString &path)
{
    const MediaProbeInfo info = MediaProbe::probe(path);
    if (!info.ok) {
        clearDetails();
        m_fileType->setText(QFileInfo(path).suffix().toUpper());
        return;
    }
    m_fileType->setText(info.fileTypeLabel());
    m_streams->setText(QString::number(info.streamCount));
    m_video->setText(info.hasVideo ? info.videoSummary() : QStringLiteral("—"));
    m_audio->setText(info.hasAudio ? info.audioSummary() : QStringLiteral("—"));
}

void FindMissingFileDialog::acceptSelection()
{
    QString path = m_selectedPath;
    if (path.isEmpty()) {
        const QString name = m_fileName->currentText().trimmed();
        if (!name.isEmpty()) {
            path = QDir(m_model->rootPath()).filePath(name);
        }
    }
    if (path.isEmpty() || !QFileInfo::exists(path) || QFileInfo(path).isDir()) {
        return;
    }
    m_selectedPath = QDir::cleanPath(path);
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    s.setValue(QStringLiteral("paths/lastFindMissingDir"), QFileInfo(m_selectedPath).absolutePath());
    accept();
}

} // namespace openvegas
