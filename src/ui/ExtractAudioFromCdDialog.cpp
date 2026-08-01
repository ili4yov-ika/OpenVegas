#include "ui/ExtractAudioFromCdDialog.h"

#include <QComboBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QMessageBox>
#include <QDir>
#include <QStandardPaths>
#include <QItemSelectionModel>
#include <QAbstractItemView>
#include <QSlider>
#include <QSettings>
#include <QProgressDialog>
#include <QApplication>
#include <QFileInfo>
#include <QColor>
#include <algorithm>
#include <cmath>

namespace openvegas {

ExtractAudioFromCdDialog::ExtractAudioFromCdDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("ExtractAudioFromCdDialog"));
    setWindowTitle(tr("Extract Audio from CD"));
    setModal(true);
    setMinimumSize(560, 380);
    resize(620, 420);

    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    m_extractOptimization = s.value(QStringLiteral("cdExtract/optimization"), 2).toInt();
    m_extractOptimization = std::clamp(m_extractOptimization, 0, 2);

    buildUi();
    populateDrives();
    updateSelectionUi();

    if (m_driveCombo->count() > 0 && !m_driveCombo->currentData().toString().isEmpty()) {
        refreshTracks();
    }
}

ExtractAudioFromCdDialog::~ExtractAudioFromCdDialog()
{
    CdAudioReader::stopPlayback();
}

void ExtractAudioFromCdDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 8);
    root->setSpacing(8);

    auto *actionRow = new QHBoxLayout();
    actionRow->setSpacing(8);
    auto *actionLabel = new QLabel(tr("Action:"), this);
    actionLabel->setObjectName(QStringLiteral("cdExtractLabel"));
    m_actionCombo = new QComboBox(this);
    m_actionCombo->setObjectName(QStringLiteral("cdExtractCombo"));
    m_actionCombo->addItems({tr("Read by track"), tr("Read by range"), tr("Read entire disc")});
    actionRow->addWidget(actionLabel);
    actionRow->addWidget(m_actionCombo, 1);
    root->addLayout(actionRow);

    auto *tracksLabel = new QLabel(tr("Tracks to extract:"), this);
    tracksLabel->setObjectName(QStringLiteral("cdExtractLabel"));
    root->addWidget(tracksLabel);

    auto *mid = new QHBoxLayout();
    mid->setSpacing(8);

    m_tracks = new QTableWidget(0, 5, this);
    m_tracks->setObjectName(QStringLiteral("cdExtractTable"));
    m_tracks->setHorizontalHeaderLabels(
        {tr("Track"), tr("Type"), tr("Start"), tr("End"), tr("Length")});
    m_tracks->horizontalHeader()->setStretchLastSection(true);
    m_tracks->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tracks->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tracks->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_tracks->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_tracks->verticalHeader()->setVisible(false);
    m_tracks->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tracks->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tracks->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tracks->setAlternatingRowColors(true);
    m_tracks->setShowGrid(false);
    mid->addWidget(m_tracks, 1);

    auto *btnCol = new QVBoxLayout();
    btnCol->setSpacing(6);
    m_okBtn = new QPushButton(tr("OK"), this);
    m_okBtn->setObjectName(QStringLiteral("cdExtractBtn"));
    m_okBtn->setDefault(true);
    m_okBtn->setEnabled(false);
    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    cancelBtn->setObjectName(QStringLiteral("cdExtractBtn"));
    auto *refreshBtn = new QPushButton(tr("Refresh"), this);
    refreshBtn->setObjectName(QStringLiteral("cdExtractBtn"));
    m_playBtn = new QPushButton(tr("Play"), this);
    m_playBtn->setObjectName(QStringLiteral("cdExtractBtn"));
    m_playBtn->setEnabled(false);
    btnCol->addWidget(m_okBtn);
    btnCol->addWidget(cancelBtn);
    btnCol->addWidget(refreshBtn);
    btnCol->addWidget(m_playBtn);
    btnCol->addStretch(1);
    mid->addLayout(btnCol);
    root->addLayout(mid, 1);

    auto *line = new QFrame(this);
    line->setObjectName(QStringLiteral("cdExtractSep"));
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(1);
    root->addWidget(line);

    auto *driveRow = new QHBoxLayout();
    driveRow->setSpacing(8);
    auto *driveLabel = new QLabel(tr("Drive:"), this);
    driveLabel->setObjectName(QStringLiteral("cdExtractLabel"));
    driveLabel->setFixedWidth(48);
    m_driveCombo = new QComboBox(this);
    m_driveCombo->setObjectName(QStringLiteral("cdExtractCombo"));
    driveRow->addWidget(driveLabel);
    driveRow->addWidget(m_driveCombo, 1);
    root->addLayout(driveRow);

    auto *speedRow = new QHBoxLayout();
    speedRow->setSpacing(8);
    auto *speedLabel = new QLabel(tr("Speed:"), this);
    speedLabel->setObjectName(QStringLiteral("cdExtractLabel"));
    speedLabel->setFixedWidth(48);
    m_speedCombo = new QComboBox(this);
    m_speedCombo->setObjectName(QStringLiteral("cdExtractCombo"));
    m_speedCombo->addItem(tr("Max"), 0);
    m_speedCombo->addItem(QStringLiteral("1x"), 1);
    m_speedCombo->addItem(QStringLiteral("2x"), 2);
    m_speedCombo->addItem(QStringLiteral("4x"), 4);
    m_speedCombo->addItem(QStringLiteral("8x"), 8);
    m_speedCombo->addItem(QStringLiteral("16x"), 16);
    m_speedCombo->addItem(QStringLiteral("32x"), 32);
    m_speedCombo->setMinimumWidth(72);
    auto *configureBtn = new QPushButton(tr("Configure…"), this);
    configureBtn->setObjectName(QStringLiteral("cdExtractBtn"));
    auto *ejectBtn = new QPushButton(tr("Eject"), this);
    ejectBtn->setObjectName(QStringLiteral("cdExtractBtn"));
    speedRow->addWidget(speedLabel);
    speedRow->addWidget(m_speedCombo);
    speedRow->addWidget(configureBtn);
    speedRow->addWidget(ejectBtn);
    speedRow->addStretch(1);
    root->addLayout(speedRow);

    m_selectedLength = new QLabel(tr("Selected length: 00:00,00"), this);
    m_selectedLength->setObjectName(QStringLiteral("cdExtractStatus"));
    root->addWidget(m_selectedLength);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_okBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(refreshBtn, &QPushButton::clicked, this, &ExtractAudioFromCdDialog::refreshTracks);
    connect(ejectBtn, &QPushButton::clicked, this, &ExtractAudioFromCdDialog::onEject);
    connect(configureBtn, &QPushButton::clicked, this, &ExtractAudioFromCdDialog::onConfigure);
    connect(m_playBtn, &QPushButton::clicked, this, &ExtractAudioFromCdDialog::onPlay);
    connect(m_tracks, &QTableWidget::itemSelectionChanged, this,
            &ExtractAudioFromCdDialog::updateSelectionUi);
    connect(m_driveCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        m_tracks->setRowCount(0);
        m_toc.clear();
        updateSelectionUi();
    });
    connect(m_actionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ExtractAudioFromCdDialog::onActionChanged);
}

void ExtractAudioFromCdDialog::populateDrives()
{
    m_driveCombo->clear();
    const QVector<CdDriveInfo> drives = CdAudioReader::listOpticalDrives();
    if (drives.isEmpty()) {
        m_driveCombo->addItem(tr("(No CD/DVD drive found)"), QString());
        m_driveCombo->setEnabled(false);
        return;
    }
    for (const CdDriveInfo &d : drives) {
        m_driveCombo->addItem(d.displayName, d.rootPath);
    }
}

void ExtractAudioFromCdDialog::fillTrackTable(const QVector<CdTrackInfo> &tracks)
{
    m_tracks->setRowCount(0);
    for (const CdTrackInfo &t : tracks) {
        if (m_actionCombo->currentIndex() == 0 && !t.isAudio) {
            continue; // Read by track — audio only
        }
        const int row = m_tracks->rowCount();
        m_tracks->insertRow(row);
        auto *num = new QTableWidgetItem(QString::number(t.number));
        num->setTextAlignment(Qt::AlignCenter);
        num->setData(Qt::UserRole, t.lengthSec);
        num->setData(Qt::UserRole + 1, QVariant::fromValue(quint32(t.startLba)));
        num->setData(Qt::UserRole + 2, QVariant::fromValue(quint32(t.endLba)));
        num->setData(Qt::UserRole + 3, t.isAudio);
        m_tracks->setItem(row, 0, num);
        m_tracks->setItem(row, 1,
                          new QTableWidgetItem(t.isAudio ? tr("Audio") : tr("Data")));
        m_tracks->setItem(row, 2, new QTableWidgetItem(formatCdTime(t.startSec)));
        m_tracks->setItem(row, 3, new QTableWidgetItem(formatCdTime(t.startSec + t.lengthSec)));
        m_tracks->setItem(row, 4, new QTableWidgetItem(formatCdTime(t.lengthSec)));
        if (!t.isAudio) {
            for (int c = 0; c < 5; ++c) {
                if (auto *it = m_tracks->item(row, c)) {
                    it->setFlags(it->flags() & ~Qt::ItemIsSelectable & ~Qt::ItemIsEnabled);
                    it->setForeground(QColor(0x66, 0x66, 0x66));
                }
            }
        }
    }
}

QString ExtractAudioFromCdDialog::formatCdTime(double sec)
{
    const int totalCs = static_cast<int>(std::llround(std::max(0.0, sec) * 100.0));
    const int cs = totalCs % 100;
    const int totalSec = totalCs / 100;
    const int s = totalSec % 60;
    const int m = totalSec / 60;
    return QStringLiteral("%1:%2,%3")
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'))
        .arg(cs, 2, 10, QLatin1Char('0'));
}

void ExtractAudioFromCdDialog::refreshTracks()
{
    CdAudioReader::stopPlayback();
    const QString root = m_driveCombo->currentData().toString();
    if (root.isEmpty()) {
        QMessageBox::information(this, tr("Extract Audio from CD"),
                                 tr("No optical drive is available."));
        return;
    }

    QString error;
    QVector<CdTrackInfo> toc;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = CdAudioReader::readToc(root, &toc, &error);
    QApplication::restoreOverrideCursor();

    if (!ok) {
        m_toc.clear();
        m_tracks->setRowCount(0);
        updateSelectionUi();
        QMessageBox::warning(this, tr("Extract Audio from CD"), error);
        return;
    }

    m_toc = toc;
    fillTrackTable(m_toc);

    if (m_actionCombo->currentIndex() == 2) {
        // Read entire disc — select all audio rows
        m_tracks->selectAll();
    } else if (m_tracks->rowCount() > 0) {
        m_tracks->selectRow(0);
    }
    updateSelectionUi();
}

void ExtractAudioFromCdDialog::onActionChanged(int)
{
    if (!m_toc.isEmpty()) {
        fillTrackTable(m_toc);
        if (m_actionCombo->currentIndex() == 2) {
            m_tracks->selectAll();
        }
        updateSelectionUi();
    }
}

void ExtractAudioFromCdDialog::updateSelectionUi()
{
    const auto rows = m_tracks->selectionModel() ? m_tracks->selectionModel()->selectedRows()
                                                 : QModelIndexList{};
    double total = 0.0;
    int audioSel = 0;
    for (const QModelIndex &idx : rows) {
        if (auto *item = m_tracks->item(idx.row(), 0)) {
            if (!item->data(Qt::UserRole + 3).toBool()) {
                continue;
            }
            total += item->data(Qt::UserRole).toDouble();
            ++audioSel;
        }
    }
    m_selectedLength->setText(tr("Selected length: %1").arg(formatCdTime(total)));
    m_okBtn->setEnabled(audioSel > 0);
    m_playBtn->setEnabled(audioSel > 0);
}

QVector<CdTrackInfo> ExtractAudioFromCdDialog::selectedCdTracks() const
{
    QVector<CdTrackInfo> out;
    if (!m_tracks->selectionModel()) {
        return out;
    }

    if (m_actionCombo->currentIndex() == 2) {
        // Entire disc as one continuous audio span (first audio → last audio end)
        CdTrackInfo whole;
        whole.number = 0;
        whole.isAudio = true;
        bool any = false;
        for (const CdTrackInfo &t : m_toc) {
            if (!t.isAudio) {
                continue;
            }
            if (!any) {
                whole.startLba = t.startLba;
                whole.startSec = t.startSec;
                any = true;
            }
            whole.endLba = t.endLba;
            whole.lengthSec = double(whole.endLba - whole.startLba) / double(CdAudioReader::kFramesPerSec);
        }
        if (any) {
            out.push_back(whole);
        }
        return out;
    }

    for (const QModelIndex &idx : m_tracks->selectionModel()->selectedRows()) {
        auto *item = m_tracks->item(idx.row(), 0);
        if (!item || !item->data(Qt::UserRole + 3).toBool()) {
            continue;
        }
        CdTrackInfo t;
        t.number = item->text().toInt();
        t.isAudio = true;
        t.startLba = item->data(Qt::UserRole + 1).toUInt();
        t.endLba = item->data(Qt::UserRole + 2).toUInt();
        t.lengthSec = item->data(Qt::UserRole).toDouble();
        t.startSec = double(t.startLba) / double(CdAudioReader::kFramesPerSec);
        out.push_back(t);
    }
    return out;
}

int ExtractAudioFromCdDialog::speedFactor() const
{
    return m_speedCombo->currentData().toInt();
}

void ExtractAudioFromCdDialog::accept()
{
    CdAudioReader::stopPlayback();
    const QString root = m_driveCombo->currentData().toString();
    QVector<CdTrackInfo> tracks = selectedCdTracks();
    if (root.isEmpty() || tracks.isEmpty()) {
        return;
    }

    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    QString outDir = s.value(QStringLiteral("cdExtract/outputDir")).toString();
    if (outDir.isEmpty() || !QDir(outDir).exists()) {
        outDir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (outDir.isEmpty()) {
            outDir = QDir::tempPath();
        }
        outDir = QDir(outDir).filePath(QStringLiteral("OpenVegas CD Extracts"));
    }
    QDir().mkpath(outDir);

    QProgressDialog progress(tr("Extracting audio from CD…"), tr("Cancel"), 0, 100, this);
    progress.setObjectName(QStringLiteral("CdExtractProgress"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    m_extracted.clear();
    const int trackCount = tracks.size();
    for (int i = 0; i < trackCount; ++i) {
        const CdTrackInfo &t = tracks[i];
        const QString name = (t.number > 0)
                                 ? tr("CD Track %1").arg(t.number, 2, 10, QLatin1Char('0'))
                                 : tr("CD Disc");
        const QString wavPath = QDir(outDir).filePath(name + QStringLiteral(".wav"));

        progress.setLabelText(tr("Extracting %1 (%2 of %3)…")
                                  .arg(name)
                                  .arg(i + 1)
                                  .arg(trackCount));

        QString error;
        const bool ok = CdAudioReader::extractToWav(
            root, t, wavPath, m_extractOptimization, speedFactor(),
            [&](int pct) {
                // Map per-track progress into overall.
                const int overall = int((i * 100 + pct) / trackCount);
                progress.setValue(std::clamp(overall, 0, 100));
                QApplication::processEvents();
                return !progress.wasCanceled();
            },
            &error);

        if (!ok) {
            if (progress.wasCanceled() || error.contains(QStringLiteral("cancel"), Qt::CaseInsensitive)) {
                QMessageBox::information(this, tr("Extract Audio from CD"),
                                         tr("Extraction cancelled."));
            } else {
                QMessageBox::warning(this, tr("Extract Audio from CD"), error);
            }
            return;
        }

        ExtractedFile f;
        f.path = wavPath;
        f.displayName = QFileInfo(wavPath).fileName();
        f.lengthSec = t.lengthSec;
        f.trackNumber = t.number;
        m_extracted.push_back(f);
    }

    progress.setValue(100);
    s.setValue(QStringLiteral("cdExtract/outputDir"), outDir);
    QDialog::accept();
}

void ExtractAudioFromCdDialog::reject()
{
    CdAudioReader::stopPlayback();
    QDialog::reject();
}

void ExtractAudioFromCdDialog::onEject()
{
    CdAudioReader::stopPlayback();
    const QString root = m_driveCombo->currentData().toString();
    if (root.isEmpty()) {
        return;
    }
    m_tracks->setRowCount(0);
    m_toc.clear();
    updateSelectionUi();
    if (!CdAudioReader::eject(root)) {
        QMessageBox::information(
            this, tr("Eject"),
            tr("Could not eject drive %1. Use the drive tray button.")
                .arg(QDir::toNativeSeparators(root).left(2)));
    }
}

void ExtractAudioFromCdDialog::onConfigure()
{
    QDialog dlg(this);
    dlg.setObjectName(QStringLiteral("CdExtractConfigureDialog"));
    dlg.setWindowTitle(tr("Configure"));
    dlg.setModal(true);
    dlg.setFixedSize(420, 160);

    auto *root = new QHBoxLayout(&dlg);
    root->setContentsMargins(12, 12, 10, 10);
    root->setSpacing(12);

    auto *left = new QVBoxLayout();
    left->setSpacing(8);

    auto *title = new QLabel(tr("Audio extract optimization:"), &dlg);
    title->setObjectName(QStringLiteral("cdCfgLabel"));
    left->addWidget(title);

    auto *sliderRow = new QHBoxLayout();
    sliderRow->setSpacing(10);
    auto *slider = new QSlider(Qt::Horizontal, &dlg);
    slider->setObjectName(QStringLiteral("cdCfgSlider"));
    slider->setRange(0, 2);
    slider->setPageStep(1);
    slider->setSingleStep(1);
    slider->setTickPosition(QSlider::TicksBelow);
    slider->setTickInterval(1);
    slider->setValue(m_extractOptimization);
    auto *levelLabel = new QLabel(&dlg);
    levelLabel->setObjectName(QStringLiteral("cdCfgLevel"));
    levelLabel->setMinimumWidth(56);
    levelLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    sliderRow->addWidget(slider, 1);
    sliderRow->addWidget(levelLabel);
    left->addLayout(sliderRow);

    auto *help = new QLabel(&dlg);
    help->setObjectName(QStringLiteral("cdCfgHelp"));
    help->setWordWrap(true);
    help->setMinimumHeight(48);
    left->addWidget(help, 1);

    auto updateTexts = [levelLabel, help](int value) {
        switch (value) {
        case 0:
            levelLabel->setText(QObject::tr("None"));
            help->setText(QObject::tr(
                "Maximum compatibility. Use this if you experience gapping or glitching "
                "while extracting audio."));
            break;
        case 1:
            levelLabel->setText(QObject::tr("Partial"));
            help->setText(QObject::tr(
                "Balanced extraction. Try this if Full causes problems on older CD-ROM drives."));
            break;
        default:
            levelLabel->setText(QObject::tr("Full"));
            help->setText(QObject::tr(
                "Use this if your computer has no problems extracting audio. "
                "Recommended for most new CD-ROM drives."));
            break;
        }
    };
    updateTexts(slider->value());
    QObject::connect(slider, &QSlider::valueChanged, &dlg, updateTexts);

    root->addLayout(left, 1);

    auto *btns = new QVBoxLayout();
    btns->setSpacing(6);
    auto *ok = new QPushButton(tr("OK"), &dlg);
    ok->setObjectName(QStringLiteral("cdExtractBtn"));
    ok->setDefault(true);
    ok->setMinimumWidth(78);
    auto *cancel = new QPushButton(tr("Cancel"), &dlg);
    cancel->setObjectName(QStringLiteral("cdExtractBtn"));
    cancel->setMinimumWidth(78);
    btns->addWidget(ok);
    btns->addWidget(cancel);
    btns->addStretch(1);
    root->addLayout(btns);

    QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        m_extractOptimization = slider->value();
        QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
        s.setValue(QStringLiteral("cdExtract/optimization"), m_extractOptimization);
    }
}

void ExtractAudioFromCdDialog::onPlay()
{
    const QString root = m_driveCombo->currentData().toString();
    const auto tracks = selectedCdTracks();
    if (root.isEmpty() || tracks.isEmpty()) {
        return;
    }
    const int num = tracks.first().number > 0 ? tracks.first().number : 1;
    QString error;
    if (!CdAudioReader::playTrack(root, num, &error)) {
        QMessageBox::warning(this, tr("Play"), error);
    }
}

} // namespace openvegas
