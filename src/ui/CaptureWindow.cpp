#include "ui/CaptureWindow.h"

#include "capture/CaptureSources.h"
#include "ui/CaptureSourceCard.h"
#include "ui/CaptureSourceList.h"
#include "ui/CaptureWindowTree.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

namespace openvegas {

namespace {

/**
 * The colours the picker cycles through, straight off the mockup.
 *
 * They are identifiers, not decoration: an audio input is drawn in the colour of the video
 * source it belongs to, and that pairing is the only thing on screen that says this
 * microphone is part of that camera.
 */
const QColor kAccents[] = {
    QColor(0xFF, 0x44, 0x00), QColor(0x00, 0xBB, 0xFF), QColor(0xFF, 0xEA, 0x00),
    QColor(0x33, 0xFF, 0x00), QColor(0x00, 0xFF, 0xC4),
};
constexpr int kAccentCount = int(std::size(kAccents));

/** The name of a column. Nothing folds away under it, so it carries no arrow. */
QLabel *columnLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setStyleSheet(QStringLiteral("font-weight: bold; color: #C8C8C8;"));
    return label;
}

/** A group of rows inside a column, headed the way the window tree heads its monitors. */
QLabel *groupLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(QStringLiteral("▾ %1").arg(text), parent);
    label->setStyleSheet(QStringLiteral("font-weight: bold; color: #C8C8C8;"));
    return label;
}

/** A device name reduced to the words worth comparing between a camera and its mic. */
QString comparableName(const QString &name)
{
    static const QStringList noise = {
        QStringLiteral("microphone"), QStringLiteral("mic"),    QStringLiteral("audio"),
        QStringLiteral("input"),      QStringLiteral("output"), QStringLiteral("line"),
        QStringLiteral("in"),         QStringLiteral("capture"), QStringLiteral("device"),
    };
    QString text = name.toLower();
    text.replace(QRegularExpression(QStringLiteral("[^a-z0-9 ]")), QStringLiteral(" "));
    QStringList words;
    for (const QString &word : text.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (!noise.contains(word) && word.size() > 2) {
            words << word;
        }
    }
    return words.join(QLatin1Char(' '));
}

} // namespace

CaptureWindow::CaptureWindow(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("captureWindow"));
    setWindowTitle(tr("OpenVegas Capture"));
    setWindowFlags(Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    setModal(false);
    resize(700, 730);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    // ── Two columns ─────────────────────────────────────────────────────
    auto *columns = new QSplitter(Qt::Horizontal, this);
    columns->setChildrenCollapsible(false);
    columns->setHandleWidth(6);

    // Left: what the take is made of.
    auto *left = new QWidget(columns);
    auto *leftCol = new QVBoxLayout(left);
    leftCol->setContentsMargins(0, 0, 0, 0);
    leftCol->setSpacing(4);
    leftCol->addWidget(columnLabel(tr("Sources"), left));
    leftCol->addWidget(groupLabel(tr("Video sources"), left));

    m_videoList = new CaptureSourceList(left);
    auto *videoScroll = new QScrollArea(left);
    videoScroll->setWidgetResizable(true);
    videoScroll->setFrameShape(QFrame::NoFrame);
    videoScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    videoScroll->setWidget(m_videoList);
    leftCol->addWidget(videoScroll, 3);

    leftCol->addWidget(groupLabel(tr("Audio sources"), left));
    m_audioBox = new QWidget(left);
    m_audioLayout = new QVBoxLayout(m_audioBox);
    m_audioLayout->setContentsMargins(0, 0, 0, 0);
    m_audioLayout->setSpacing(1);
    m_audioLayout->addStretch(1);
    auto *audioScroll = new QScrollArea(left);
    audioScroll->setWidgetResizable(true);
    audioScroll->setFrameShape(QFrame::NoFrame);
    audioScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    audioScroll->setWidget(m_audioBox);
    leftCol->addWidget(audioScroll, 2);

    // Right: everything open, to drag across.
    auto *right = new QWidget(columns);
    auto *rightCol = new QVBoxLayout(right);
    rightCol->setContentsMargins(0, 0, 0, 0);
    rightCol->setSpacing(4);
    rightCol->addWidget(columnLabel(tr("Windows"), right));

    m_windowTree = new CaptureWindowTree(right);
    rightCol->addWidget(m_windowTree, 3);

    m_windowPreview = new QLabel(right);
    m_windowPreview->setMinimumHeight(96);
    m_windowPreview->setAlignment(Qt::AlignCenter);
    m_windowPreview->setWordWrap(true);
    m_windowPreview->setStyleSheet(
        QStringLiteral("background: #101010; border: 1px solid #FFFFFF; color: #DDDDDD;"));
    m_windowPreview->setText(tr("Pick a window to see it."));
    rightCol->addWidget(m_windowPreview, 2);

    columns->addWidget(left);
    columns->addWidget(right);
    columns->setStretchFactor(0, 3);
    columns->setStretchFactor(1, 2);
    // Stretch factors only decide how extra width is shared out; the widths themselves come
    // from the size hints, and a tree's hint is wide enough to swallow the column that
    // matters. The sources are what this window is about, so they get the room.
    columns->setSizes({430, 250});
    root->addWidget(columns, 1);

    // ── Settings ────────────────────────────────────────────────────────
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
    m_recordBtn->setMinimumWidth(120);
    buttons->addWidget(m_rescanBtn);
    buttons->addStretch(1);
    buttons->addWidget(m_elapsed);
    buttons->addStretch(1);
    buttons->addWidget(m_recordBtn);
    root->addLayout(buttons);

    // ── Stored settings ─────────────────────────────────────────────────
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    const QString saved = s.value(QStringLiteral("capture/folder")).toString();
    m_folder->setText(saved.isEmpty()
                          ? QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation))
                                .filePath(QStringLiteral("OpenVegas Capture"))
                          : saved);

    // ── Wiring ──────────────────────────────────────────────────────────
    connect(browse, &QPushButton::clicked, this, &CaptureWindow::chooseFolder);
    connect(m_rescanBtn, &QPushButton::clicked, this, &CaptureWindow::rescan);
    connect(m_recordBtn, &QPushButton::clicked, this, &CaptureWindow::toggleRecording);
    connect(m_reference, &QComboBox::currentIndexChanged, this, [this](int) { rebuildPlan(); });
    connect(m_fit, &QComboBox::currentIndexChanged, this, [this](int) { rebuildPlan(); });
    connect(m_size, &QComboBox::currentIndexChanged, this, [this](int) { rebuildPlan(); });
    connect(m_takeName, &QLineEdit::textChanged, this, [this](const QString &) { rebuildPlan(); });

    connect(m_videoList, &CaptureSourceList::reorderRequested, this,
            &CaptureWindow::moveVideoSource);
    connect(m_videoList, &CaptureSourceList::windowDropped, this,
            &CaptureWindow::addWindowSource);
    connect(m_windowTree, &CaptureWindowTree::windowSelected, this,
            &CaptureWindow::showWindowPreview);

    connect(&m_recorder, &CaptureRecorder::sourceFailed, this,
            [this](const QString &name, const QString &detail) {
                m_summary->setText(tr("%1 stopped: %2").arg(name, detail));
            });

    m_tick = new QTimer(this);
    m_tick->setInterval(500);
    connect(m_tick, &QTimer::timeout, this, [this]() {
        const qint64 secs = (QDateTime::currentMSecsSinceEpoch() - m_startedMs) / 1000;
        const QString clock = QStringLiteral("%1:%2")
                                  .arg(secs / 60, 2, 10, QChar(u'0'))
                                  .arg(secs % 60, 2, 10, QChar(u'0'));
        m_elapsed->setText(clock);
        m_tray->setStatusText(clock);
    });

    m_tray = new CaptureTrayIcon(this);
    connect(m_tray, &CaptureTrayIcon::showWindowRequested, this, [this]() {
        show();
        raise();
        activateWindow();
    });
    connect(m_tray, &CaptureTrayIcon::toggleRecordingRequested, this,
            &CaptureWindow::toggleRecording);
    m_tray->show();

    if (m_hotkey.setShortcut(QKeySequence(Qt::AltModifier | Qt::Key_F8))) {
        connect(&m_hotkey, &GlobalHotkey::activated, this, &CaptureWindow::toggleRecording);
    }

    rescan();
}

CaptureWindow::~CaptureWindow()
{
    stopLivePreviews();
}

// ── Scanning ────────────────────────────────────────────────────────────────

void CaptureWindow::rescan()
{
    stopLivePreviews();

    m_video.clear();
    m_audio.clear();
    for (const CaptureSource &source : CaptureSources::all()) {
        if (source.isAudio()) {
            m_audio.push_back(source);
        } else if (source.kind != CaptureSource::Kind::Window) {
            // Windows start on the right-hand inventory, not in the take. They join the
            // left column only when someone drags one across.
            m_video.push_back({source, false});
        }
    }
    m_audioChecked.assign(m_audio.size(), false);

    m_windowTree->setContents(CaptureSources::screens(), CaptureSources::windows());

    rebuildVideoCards();
    rebuildAudioRows();
    rebuildPlan();
    startLivePreviews();
}

QColor CaptureWindow::accentFor(int videoIndex) const
{
    if (videoIndex < 0) {
        return QColor(0x50, 0x50, 0x50);
    }
    return kAccents[videoIndex % kAccentCount];
}

QString CaptureWindow::pairedAudioName(const CaptureSource &video) const
{
    // A heuristic, and only ever used for display: which colour a row is drawn in and what
    // is written under a card. Nothing here decides what gets recorded, so a camera whose
    // microphone is named differently loses a line of text and nothing else.
    const QString wanted = comparableName(video.name);
    if (wanted.isEmpty()) {
        return {};
    }
    for (const CaptureSource &audio : m_audio) {
        const QString other = comparableName(audio.name);
        if (other.isEmpty()) {
            continue;
        }
        if (other.contains(wanted) || wanted.contains(other)) {
            return audio.name;
        }
    }
    return {};
}

void CaptureWindow::rebuildVideoCards()
{
    for (auto *card : m_cards) {
        card->deleteLater();
    }
    m_cards.clear();

    QVBoxLayout *layout = m_videoList->cardLayout();
    while (QLayoutItem *item = layout->takeAt(0)) {
        delete item;
    }

    for (int i = 0; i < m_video.size(); ++i) {
        auto *card = new CaptureSourceCard(m_video[i].source, i, accentFor(i), m_videoList);
        card->setPairedAudio(pairedAudioName(m_video[i].source));
        card->setSelected(i == m_selected);
        card->setChecked(m_video[i].checked);
        connect(card, &CaptureSourceCard::clicked, this, &CaptureWindow::selectVideoSource);
        // Written straight back into the row, so the tick and the source it belongs to are
        // one thing from here on and reordering cannot separate them.
        connect(card, &CaptureSourceCard::checkedChanged, this, [this](int index, bool on) {
            if (index >= 0 && index < m_video.size()) {
                m_video[index].checked = on;
            }
            rebuildPlan();
        });
        m_cards.push_back(card);
        layout->addWidget(card);
    }
    layout->addStretch(1);
}

void CaptureWindow::rebuildAudioRows()
{
    for (auto *check : m_audioChecks) {
        if (QWidget *row = check->parentWidget()) {
            row->deleteLater();
        }
    }
    m_audioChecks.clear();

    while (QLayoutItem *item = m_audioLayout->takeAt(0)) {
        delete item;
    }

    for (int i = 0; i < m_audio.size(); ++i) {
        const CaptureSource &source = m_audio[i];

        // The colour says which video source this input belongs to; an input that belongs
        // to none of them stays grey rather than borrowing a colour it has no claim on.
        int owner = -1;
        for (int v = 0; v < m_video.size(); ++v) {
            if (pairedAudioName(m_video[v].source) == source.name) {
                owner = v;
                break;
            }
        }
        QColor accent = accentFor(owner);
        accent.setAlphaF(owner >= 0 ? 0.5 : 0.25);

        auto *row = new QWidget(m_audioBox);
        row->setAutoFillBackground(true);
        QPalette pal = row->palette();
        pal.setColor(QPalette::Window, accent);
        row->setPalette(pal);

        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(6, 2, 6, 2);
        lay->setSpacing(8);

        auto *check = new QCheckBox(source.name, row);
        check->setChecked(m_audioChecked.value(i, false));
        connect(check, &QCheckBox::toggled, this, [this, i](bool on) {
            if (i < m_audioChecked.size()) {
                m_audioChecked[i] = on;
            }
            rebuildPlan();
        });
        lay->addWidget(check, 1);

        QStringList facts;
        if (source.sampleRate > 0) {
            facts << tr("%1 Hz").arg(source.sampleRate);
        }
        if (source.channels > 0) {
            facts << tr("%n ch", "", source.channels);
        }
        lay->addWidget(new QLabel(facts.join(QStringLiteral(", ")), row));

        m_audioChecks.push_back(check);
        m_audioLayout->addWidget(row);
    }
    m_audioLayout->addStretch(1);
}

// ── Live pictures ───────────────────────────────────────────────────────────

void CaptureWindow::startLivePreviews()
{
    stopLivePreviews();
    if (m_recorder.isRecording() || !isVisible()) {
        return;
    }
    for (int i = 0; i < m_cards.size(); ++i) {
        auto *live = new CaptureLivePreview(this);
        CaptureSourceCard *card = m_cards[i];
        connect(live, &CaptureLivePreview::frameReady, card, &CaptureSourceCard::setPreview);
        connect(live, &CaptureLivePreview::failed, card,
                [card](const QString &why) { card->setPreviewNote(why); });
        live->start(m_video[i].source, card->previewSize());
        m_livePreviews.push_back(live);
    }
}

void CaptureWindow::stopLivePreviews()
{
    for (auto *live : m_livePreviews) {
        live->stop();
        live->deleteLater();
    }
    m_livePreviews.clear();
    if (m_windowLive) {
        m_windowLive->stop();
        m_windowLive->deleteLater();
        m_windowLive = nullptr;
    }
}

void CaptureWindow::showWindowPreview(const CaptureSource &window)
{
    if (m_windowLive) {
        m_windowLive->stop();
        m_windowLive->deleteLater();
        m_windowLive = nullptr;
    }
    if (window.id.isEmpty() || m_recorder.isRecording()) {
        return;
    }
    m_windowPreview->setText(tr("Opening %1…").arg(window.name));

    m_windowLive = new CaptureLivePreview(this);
    connect(m_windowLive, &CaptureLivePreview::frameReady, this, [this](const QImage &frame) {
        QPixmap pm = QPixmap::fromImage(frame);
        pm.setDevicePixelRatio(devicePixelRatioF());
        m_windowPreview->setPixmap(pm);
    });
    connect(m_windowLive, &CaptureLivePreview::failed, this, [this](const QString &why) {
        m_windowPreview->setPixmap(QPixmap());
        m_windowPreview->setText(why);
    });
    const qreal dpr = devicePixelRatioF();
    m_windowLive->start(window, QSize(int(m_windowPreview->width() * dpr),
                                      int(m_windowPreview->height() * dpr)));
}

// ── The two gestures ────────────────────────────────────────────────────────

void CaptureWindow::moveVideoSource(int fromIndex, int toIndex)
{
    if (fromIndex < 0 || fromIndex >= m_video.size()) {
        return;
    }
    const int to = qBound(0, toIndex, m_video.size() - 1);
    if (to == fromIndex) {
        return;
    }
    // One move, because the tick travels inside the row.
    m_video.move(fromIndex, to);
    if (m_selected == fromIndex) {
        m_selected = to;
    }

    rebuildVideoCards();
    rebuildAudioRows();
    rebuildPlan();
    startLivePreviews();
}

void CaptureWindow::addWindowSource(const QString &windowId)
{
    const CaptureSource window = m_windowTree->windowById(windowId);
    if (window.id.isEmpty()) {
        return;
    }
    for (const PickedSource &existing : m_video) {
        if (existing.source.id == window.id) {
            m_summary->setText(tr("%1 is already on the list.").arg(window.name));
            return;
        }
    }
    // Ticked on arrival: dragging a window across is already the decision to record it,
    // and making someone tick it afterwards would be asking the same question twice.
    m_video.push_back({window, true});

    rebuildVideoCards();
    rebuildAudioRows();
    rebuildPlan();
    startLivePreviews();
}

void CaptureWindow::selectVideoSource(int index)
{
    m_selected = index;
    for (int i = 0; i < m_cards.size(); ++i) {
        m_cards[i]->setSelected(i == index);
    }
}

// ── Plan ────────────────────────────────────────────────────────────────────

void CaptureWindow::rebuildPlan()
{
    m_plan = CapturePlan();
    m_plan.takeName = m_takeName->text().trimmed();
    m_plan.fit = CaptureFit(m_fit->currentData().toInt());
    m_plan.forcedSize = m_size->currentData().toSize();

    for (const PickedSource &picked : m_video) {
        if (picked.checked) {
            m_plan.sources.push_back(picked.source);
        }
    }
    for (int i = 0; i < m_audio.size(); ++i) {
        if (m_audioChecked.value(i, false)) {
            m_plan.sources.push_back(m_audio[i]);
        }
    }

    const QString wasChosen = m_reference->currentText();
    {
        const QSignalBlocker block(m_reference);
        m_reference->clear();
        m_reference->addItem(tr("According to the first video source on the list"), -1);
        for (int i = 0; i < m_plan.sources.size(); ++i) {
            if (m_plan.sources[i].isVideo()) {
                m_reference->addItem(m_plan.sources[i].name, i);
            }
        }
        const int back = m_reference->findText(wasChosen);
        m_reference->setCurrentIndex(back >= 0 ? back : 0);
    }
    m_plan.referenceIndex = m_reference->currentData().toInt();

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
    const QString hotkeyNote =
        m_hotkey.isRegistered()
            ? tr("  ·  %1 starts and stops from anywhere")
                  .arg(m_hotkey.shortcut().toString(QKeySequence::NativeText))
            : tr("  ·  Alt+F8 is taken by another program");
    const QString why = m_plan.validate();
    if (!why.isEmpty()) {
        m_summary->setText(why);
        m_recordBtn->setEnabled(false);
        return;
    }

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
    m_summary->setText(parts.join(QStringLiteral(" · ")) + hotkeyNote);
    m_recordBtn->setEnabled(true);
}

// ── Folder ──────────────────────────────────────────────────────────────────

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

// ── Recording ───────────────────────────────────────────────────────────────

void CaptureWindow::setRecordingUi(bool recording)
{
    m_tray->setRecording(recording);
    if (recording) {
        // Every preview lets go of its device first: a camera that is already open cannot
        // be opened again, and the take is what the device is for.
        stopLivePreviews();
        for (auto *card : m_cards) {
            card->setPreviewNote(tr("Recording."));
        }
    } else {
        startLivePreviews();
    }
    m_recordBtn->setText(recording ? tr("Stop Recording") : tr("Start Recording"));
    m_rescanBtn->setEnabled(!recording);
    m_reference->setEnabled(!recording);
    m_fit->setEnabled(!recording);
    m_size->setEnabled(!recording && m_plan.fit != CaptureFit::Native);
    m_takeName->setEnabled(!recording);
    m_folder->setEnabled(!recording);
    m_videoList->setEnabled(!recording);
    m_windowTree->setEnabled(!recording);
    m_audioBox->setEnabled(!recording);
}

void CaptureWindow::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (!m_recorder.isRecording() && m_livePreviews.isEmpty()) {
        startLivePreviews();
    }
}

void CaptureWindow::hideEvent(QHideEvent *event)
{
    // Minimised to the tray is the normal way to run a take. Nothing is watching the
    // pictures then, and leaving a decoder per source running would be paying for them.
    stopLivePreviews();
    QDialog::hideEvent(event);
}

void CaptureWindow::closeEvent(QCloseEvent *event)
{
    if (m_recorder.isRecording()) {
        toggleRecording();
    }
    stopLivePreviews();
    m_tray->hide();
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

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HHmmss"));
    const QString folder = QDir(m_folder->text()).filePath(stamp);

    // Stopped before the recorder opens anything, not after: the two would otherwise be
    // asking the same camera for a stream at the same moment.
    stopLivePreviews();

    QString error;
    if (!m_recorder.start(m_plan, folder, &error)) {
        QMessageBox::warning(this, tr("OpenVegas Capture"), error);
        startLivePreviews();
        return;
    }
    m_startedMs = QDateTime::currentMSecsSinceEpoch();
    m_tick->start();
    setRecordingUi(true);
    m_summary->setText(tr("Recording to %1").arg(QDir::toNativeSeparators(folder)));
}

} // namespace openvegas
