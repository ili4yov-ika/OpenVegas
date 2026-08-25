#include "ui/CaptureWindow.h"

#include "capture/CaptureSources.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace openvegas {

namespace {

constexpr int kIndexRole = Qt::UserRole + 1;

QString describe(const CaptureSource &s)
{
    if (s.isAudio()) {
        return QObject::tr("%1 Hz, %2 ch").arg(s.sampleRate).arg(s.channels);
    }
    if (!s.nativeSize.isValid() || s.nativeSize.isEmpty()) {
        return QObject::tr("size unknown");
    }
    return QObject::tr("%1×%2 @ %3")
        .arg(s.nativeSize.width())
        .arg(s.nativeSize.height())
        .arg(s.frameRate, 0, 'g', 4);
}

QString groupName(CaptureSource::Kind kind)
{
    switch (kind) {
    case CaptureSource::Kind::Screen:
        return QObject::tr("Screens");
    case CaptureSource::Kind::Window:
        return QObject::tr("Windows");
    case CaptureSource::Kind::Camera:
        return QObject::tr("Cameras and capture cards");
    case CaptureSource::Kind::Audio:
        return QObject::tr("Audio inputs");
    }
    return QString();
}

} // namespace

CaptureWindow::CaptureWindow(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("captureWindow"));
    setWindowTitle(tr("OpenVegas Capture"));
    // What is being recorded is usually the rest of the screen, so this cannot be modal.
    setWindowFlags(Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    setModal(false);
    resize(680, 560);

    auto *root = new QVBoxLayout(this);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("captureSources"));
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Source"), tr("Reports")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    root->addWidget(m_tree, 1);

    auto *form = new QFormLayout();
    m_reference = new QComboBox(this);
    m_fit = new QComboBox(this);
    m_fit->addItem(tr("Fit inside (letterbox)"), int(CaptureFit::Letterbox));
    m_fit->addItem(tr("Fill and crop"), int(CaptureFit::Crop));
    m_fit->addItem(tr("Leave each at its own size"), int(CaptureFit::Native));
    m_size = new QComboBox(this);
    m_size->addItem(tr("Same as the reference source"), QSize());
    for (const QSize &s : {QSize(3840, 2160), QSize(2560, 1440), QSize(1920, 1080),
                           QSize(1280, 720)}) {
        m_size->addItem(QStringLiteral("%1 × %2").arg(s.width()).arg(s.height()), s);
    }
    m_takeName = new QLineEdit(tr("Take"), this);

    auto *folderRow = new QWidget(this);
    auto *folderLay = new QHBoxLayout(folderRow);
    folderLay->setContentsMargins(0, 0, 0, 0);
    m_folder = new QLineEdit(folderRow);
    auto *browse = new QPushButton(tr("Browse…"), folderRow);
    folderLay->addWidget(m_folder, 1);
    folderLay->addWidget(browse);

    form->addRow(tr("Resolution follows"), m_reference);
    form->addRow(tr("Other video sources"), m_fit);
    form->addRow(tr("Record at"), m_size);
    form->addRow(tr("Take name"), m_takeName);
    form->addRow(tr("Save to"), folderRow);
    root->addLayout(form);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    root->addWidget(m_summary);

    auto *buttons = new QHBoxLayout();
    m_rescanBtn = new QPushButton(tr("Scan Again"), this);
    m_elapsed = new QLabel(this);
    m_recordBtn = new QPushButton(tr("Start Recording"), this);
    m_recordBtn->setDefault(true);
    buttons->addWidget(m_rescanBtn);
    buttons->addStretch(1);
    buttons->addWidget(m_elapsed);
    buttons->addWidget(m_recordBtn);
    root->addLayout(buttons);

    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    const QString saved = s.value(QStringLiteral("capture/folder")).toString();
    m_folder->setText(saved.isEmpty()
                          ? QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation))
                                .filePath(QStringLiteral("OpenVegas Captures"))
                          : saved);

    connect(browse, &QPushButton::clicked, this, &CaptureWindow::chooseFolder);
    connect(m_rescanBtn, &QPushButton::clicked, this, &CaptureWindow::refreshSources);
    connect(m_recordBtn, &QPushButton::clicked, this, &CaptureWindow::toggleRecording);
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *, int) {
        rebuildPlan();
    });
    connect(m_reference, &QComboBox::currentIndexChanged, this, [this](int) { rebuildPlan(); });
    connect(m_fit, &QComboBox::currentIndexChanged, this, [this](int) { rebuildPlan(); });
    connect(m_size, &QComboBox::currentIndexChanged, this, [this](int) { rebuildPlan(); });
    connect(m_takeName, &QLineEdit::textChanged, this, [this](const QString &) { rebuildPlan(); });

    connect(&m_recorder, &CaptureRecorder::sourceFailed, this,
            [this](const QString &name, const QString &detail) {
                // Said plainly and immediately: a source that dropped out mid-take is
                // worth knowing about now, not when the files are opened.
                m_summary->setText(tr("%1 stopped: %2").arg(name, detail));
            });

    m_tick = new QTimer(this);
    m_tick->setInterval(500);
    connect(m_tick, &QTimer::timeout, this, [this]() {
        const qint64 secs = (QDateTime::currentMSecsSinceEpoch() - m_startedMs) / 1000;
        m_elapsed->setText(QStringLiteral("%1:%2")
                               .arg(secs / 60, 2, 10, QChar(u'0'))
                               .arg(secs % 60, 2, 10, QChar(u'0')));
    });

    refreshSources();
}

void CaptureWindow::refreshSources()
{
    m_available = CaptureSources::all();

    const QSignalBlocker block(m_tree);
    m_tree->clear();
    QHash<int, QTreeWidgetItem *> groups;
    for (int i = 0; i < m_available.size(); ++i) {
        const CaptureSource &s = m_available[i];
        QTreeWidgetItem *group = groups.value(int(s.kind), nullptr);
        if (!group) {
            group = new QTreeWidgetItem(m_tree, {groupName(s.kind)});
            group->setFirstColumnSpanned(true);
            group->setExpanded(true);
            QFont f = group->font(0);
            f.setBold(true);
            group->setFont(0, f);
            groups.insert(int(s.kind), group);
        }
        auto *row = new QTreeWidgetItem(group, {s.name, describe(s)});
        row->setCheckState(0, Qt::Unchecked);
        row->setData(0, kIndexRole, i);
    }
    rebuildPlan();
}

void CaptureWindow::rebuildPlan()
{
    m_plan = CapturePlan();
    m_plan.takeName = m_takeName->text().trimmed();
    m_plan.fit = CaptureFit(m_fit->currentData().toInt());
    m_plan.forcedSize = m_size->currentData().toSize();

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *group = m_tree->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j) {
            QTreeWidgetItem *row = group->child(j);
            if (row->checkState(0) != Qt::Checked) {
                continue;
            }
            const int at = row->data(0, kIndexRole).toInt();
            if (at >= 0 && at < m_available.size()) {
                m_plan.sources.push_back(m_available[at]);
            }
        }
    }

    // The reference list only offers video sources that are actually ticked, because
    // pointing it at something that is not being recorded says nothing.
    const QString wasChosen = m_reference->currentText();
    {
        const QSignalBlocker block(m_reference);
        m_reference->clear();
        m_reference->addItem(tr("Largest video source"), -1);
        for (int i = 0; i < m_plan.sources.size(); ++i) {
            if (m_plan.sources[i].isVideo()) {
                m_reference->addItem(m_plan.sources[i].name, i);
            }
        }
        const int back = m_reference->findText(wasChosen);
        m_reference->setCurrentIndex(back >= 0 ? back : 0);
    }
    m_plan.referenceIndex = m_reference->currentData().toInt();

    // Forcing a size only means something when the others are being fitted to it.
    m_size->setEnabled(m_plan.fit != CaptureFit::Native);

    updateSummary();
}

void CaptureWindow::updateSummary()
{
    if (m_plan.sources.isEmpty()) {
        m_summary->setText(tr("Tick the sources to record."));
        m_recordBtn->setEnabled(false);
        return;
    }
    const QString why = m_plan.validate();
    if (!why.isEmpty()) {
        m_summary->setText(why);
        m_recordBtn->setEnabled(false);
        return;
    }

    // Spelling out what will happen, because these are exactly the decisions that are
    // annoying to discover after a take: the size everything lands at, the one audio
    // format they all share, and how many files come out.
    QStringList parts;
    const QSize res = m_plan.resolution();
    if (res.isValid()) {
        parts << tr("video %1×%2 at %3 fps")
                     .arg(res.width())
                     .arg(res.height())
                     .arg(m_plan.frameRate(), 0, 'g', 4);
    }
    if (m_plan.sampleRate() > 0) {
        parts << tr("audio %1 Hz, %2 ch, %3-bit")
                     .arg(m_plan.sampleRate())
                     .arg(m_plan.channels())
                     .arg(m_plan.bitDepth());
    }
    parts << tr("%n file(s), one per source", "", int(m_plan.outputs().size()));
    m_summary->setText(parts.join(QStringLiteral(" · ")));
    m_recordBtn->setEnabled(true);
}

void CaptureWindow::chooseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Save takes to"),
                                                          m_folder->text());
    if (dir.isEmpty()) {
        return;
    }
    m_folder->setText(dir);
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    s.setValue(QStringLiteral("capture/folder"), dir);
}

void CaptureWindow::setRecordingUi(bool recording)
{
    m_recordBtn->setText(recording ? tr("Stop Recording") : tr("Start Recording"));
    m_tree->setEnabled(!recording);
    m_reference->setEnabled(!recording);
    m_fit->setEnabled(!recording);
    m_size->setEnabled(!recording && m_plan.fit != CaptureFit::Native);
    m_takeName->setEnabled(!recording);
    m_folder->setEnabled(!recording);
    m_rescanBtn->setEnabled(!recording);
}

void CaptureWindow::closeEvent(QCloseEvent *event)
{
    // A take left running behind a closed window would keep writing with nothing on screen
    // saying so, and the files would never be offered for import. Finish it instead.
    if (m_recorder.isRecording()) {
        toggleRecording();
    }
    QDialog::closeEvent(event);
}

void CaptureWindow::toggleRecording()
{
    if (m_recorder.isRecording()) {
        m_recorder.stop();
        m_tick->stop();
        setRecordingUi(false);
        const QStringList files = m_recorder.recordedFiles();
        m_summary->setText(tr("Recorded %n file(s).", "", int(files.size())));
        emit takeRecorded(files);
        return;
    }

    // Each take goes in its own dated folder: a second take with the same name would
    // otherwise overwrite the first, and -y means it would do so without asking.
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HHmmss"));
    const QString folder = QDir(m_folder->text()).filePath(stamp);

    QString error;
    if (!m_recorder.start(m_plan, folder, &error)) {
        QMessageBox::warning(this, tr("OpenVegas Capture"), error);
        return;
    }
    m_startedMs = QDateTime::currentMSecsSinceEpoch();
    m_tick->start();
    setRecordingUi(true);
    m_summary->setText(tr("Recording to %1").arg(QDir::toNativeSeparators(folder)));
}

} // namespace openvegas
