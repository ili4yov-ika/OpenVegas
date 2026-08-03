#include "ui/RenderingProgressDialog.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStorageInfo>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

namespace openvegas {
namespace {

QWidget *makeSectionHeader(QWidget *parent, const QString &title, QToolButton **arrowOut)
{
    auto *row = new QWidget(parent);
    auto *lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 4, 0, 2);
    lay->setSpacing(6);
    auto *arrow = new QToolButton(row);
    arrow->setObjectName(QStringLiteral("renderProgSectionArrow"));
    arrow->setArrowType(Qt::DownArrow);
    arrow->setAutoRaise(true);
    arrow->setFixedSize(16, 16);
    arrow->setFocusPolicy(Qt::NoFocus);
    auto *lab = new QLabel(title, row);
    lab->setObjectName(QStringLiteral("renderProgSectionTitle"));
    lay->addWidget(arrow, 0, Qt::AlignVCenter);
    lay->addWidget(lab, 1, Qt::AlignVCenter);
    *arrowOut = arrow;
    return row;
}

QLabel *makeValueLabel(QWidget *parent)
{
    auto *v = new QLabel(QStringLiteral("—"), parent);
    v->setObjectName(QStringLiteral("renderProgValue"));
    v->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    v->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return v;
}

void addKvRow(QFormLayout *form, const QString &key, QLabel *value)
{
    auto *k = new QLabel(key);
    k->setObjectName(QStringLiteral("renderProgKey"));
    form->addRow(k, value);
}

} // namespace

RenderingProgressDialog::RenderingProgressDialog(const QString &outputPath, QWidget *parent)
    : QDialog(parent)
    , m_outputPath(outputPath)
{
    setObjectName(QStringLiteral("RenderingProgressDialog"));
    setWindowTitle(tr("Rendering: 0%"));
    setWindowFlags((windowFlags() & ~Qt::WindowContextHelpButtonHint) | Qt::Window);
    setModal(true);
    setMinimumWidth(420);
    setMaximumWidth(560);
    buildUi();

    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    m_showPreview->setChecked(s.value(QStringLiteral("render/showPreview"), true).toBool());
    m_openFolder->setChecked(s.value(QStringLiteral("render/openFolderWhenDone"), false).toBool());
    m_closeDone->setChecked(s.value(QStringLiteral("render/closeWhenDone"), false).toBool());
}

void RenderingProgressDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(8);

    m_pathLabel = new QLabel(m_outputPath, this);
    m_pathLabel->setObjectName(QStringLiteral("renderProgPath"));
    m_pathLabel->setWordWrap(true);
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_pathLabel);

    m_bar = new QProgressBar(this);
    m_bar->setObjectName(QStringLiteral("renderProgBar"));
    m_bar->setRange(0, 1000);
    m_bar->setValue(0);
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(8);
    root->addWidget(m_bar);

    auto *stats = new QHBoxLayout();
    stats->setContentsMargins(0, 0, 0, 0);
    stats->setSpacing(8);
    m_pctLabel = new QLabel(tr("0%"), this);
    m_pctLabel->setObjectName(QStringLiteral("renderProgStat"));
    m_renderedLabel = new QLabel(tr("Rendered: —"), this);
    m_renderedLabel->setObjectName(QStringLiteral("renderProgStat"));
    auto *leftCol = new QVBoxLayout();
    leftCol->setSpacing(2);
    leftCol->addWidget(m_pctLabel);
    leftCol->addWidget(m_renderedLabel);

    m_remainingLabel = new QLabel(tr("Remaining: —"), this);
    m_remainingLabel->setObjectName(QStringLiteral("renderProgStat"));
    m_remainingLabel->setAlignment(Qt::AlignRight);
    m_elapsedLabel = new QLabel(tr("Elapsed: 00:00:00.00"), this);
    m_elapsedLabel->setObjectName(QStringLiteral("renderProgStat"));
    m_elapsedLabel->setAlignment(Qt::AlignRight);
    auto *rightCol = new QVBoxLayout();
    rightCol->setSpacing(2);
    rightCol->addWidget(m_remainingLabel);
    rightCol->addWidget(m_elapsedLabel);

    stats->addLayout(leftCol, 1);
    stats->addLayout(rightCol, 1);
    root->addLayout(stats);

    auto *sep = new QFrame(this);
    sep->setObjectName(QStringLiteral("renderProgSep"));
    sep->setFrameShape(QFrame::HLine);
    sep->setFixedHeight(1);
    root->addWidget(sep);

    // Estimated Properties
    root->addWidget(makeSectionHeader(this, tr("Estimated Properties"), &m_estArrow));
    m_estBody = new QWidget(this);
    auto *estForm = new QFormLayout(m_estBody);
    estForm->setContentsMargins(18, 2, 4, 6);
    estForm->setHorizontalSpacing(16);
    estForm->setVerticalSpacing(3);
    estForm->setLabelAlignment(Qt::AlignLeft);
    estForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_freeSpaceVal = makeValueLabel(m_estBody);
    m_durationVal = makeValueLabel(m_estBody);
    m_completionVal = makeValueLabel(m_estBody);
    addKvRow(estForm, tr("Free Disk Space:"), m_freeSpaceVal);
    addKvRow(estForm, tr("Rendering Duration:"), m_durationVal);
    addKvRow(estForm, tr("Time of Completion:"), m_completionVal);
    root->addWidget(m_estBody);
    connect(m_estArrow, &QToolButton::clicked, this, [this]() { toggleSection(m_estBody, m_estArrow); });

    // Performance
    root->addWidget(makeSectionHeader(this, tr("Performance"), &m_perfArrow));
    m_perfBody = new QWidget(this);
    auto *perfForm = new QFormLayout(m_perfBody);
    perfForm->setContentsMargins(18, 2, 4, 6);
    perfForm->setHorizontalSpacing(16);
    perfForm->setVerticalSpacing(3);
    m_avgSpeedVal = makeValueLabel(m_perfBody);
    m_curSpeedVal = makeValueLabel(m_perfBody);
    m_stageVal = makeValueLabel(m_perfBody);
    addKvRow(perfForm, tr("Average Speed:"), m_avgSpeedVal);
    addKvRow(perfForm, tr("Current Speed:"), m_curSpeedVal);
    addKvRow(perfForm, tr("Stage:"), m_stageVal);
    root->addWidget(m_perfBody);
    connect(m_perfArrow, &QToolButton::clicked, this, [this]() { toggleSection(m_perfBody, m_perfArrow); });

    // Options
    root->addWidget(makeSectionHeader(this, tr("Options"), &m_optArrow));
    m_optBody = new QWidget(this);
    auto *optLay = new QVBoxLayout(m_optBody);
    optLay->setContentsMargins(18, 2, 4, 4);
    optLay->setSpacing(4);
    m_showPreview = new QCheckBox(tr("Show Video in Preview Window"), m_optBody);
    m_openFolder = new QCheckBox(tr("Open folder when rendering is complete"), m_optBody);
    m_closeDone = new QCheckBox(tr("Close dialog when rendering is complete"), m_optBody);
    optLay->addWidget(m_showPreview);
    optLay->addWidget(m_openFolder);
    optLay->addWidget(m_closeDone);
    root->addWidget(m_optBody);
    connect(m_optArrow, &QToolButton::clicked, this, [this]() { toggleSection(m_optBody, m_optArrow); });

    root->addStretch(1);

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    btnRow->addStretch(1);
    m_openFolderBtn = new QPushButton(tr("Open Folder"), this);
    m_openBtn = new QPushButton(tr("Open"), this);
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    m_cancelBtn->setDefault(true);
    m_openFolderBtn->setEnabled(false);
    m_openBtn->setEnabled(false);
    btnRow->addWidget(m_openFolderBtn);
    btnRow->addWidget(m_openBtn);
    btnRow->addWidget(m_cancelBtn);
    root->addLayout(btnRow);

    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        if (m_finished) {
            accept();
            return;
        }
        m_canceled = true;
        m_cancelBtn->setEnabled(false);
        m_cancelBtn->setText(tr("Canceling…"));
        emit cancelRequested();
    });
    connect(m_openFolderBtn, &QPushButton::clicked, this, [this]() {
        const QFileInfo fi(m_outputPath);
        QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
    });
    connect(m_openBtn, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_outputPath));
    });
}

void RenderingProgressDialog::toggleSection(QWidget *body, QToolButton *arrow)
{
    if (!body || !arrow) {
        return;
    }
    const bool show = !body->isVisible();
    body->setVisible(show);
    arrow->setArrowType(show ? Qt::DownArrow : Qt::RightArrow);
    adjustSize();
}

bool RenderingProgressDialog::showVideoInPreview() const
{
    return m_showPreview && m_showPreview->isChecked();
}

bool RenderingProgressDialog::openFolderWhenDone() const
{
    return m_openFolder && m_openFolder->isChecked();
}

bool RenderingProgressDialog::closeWhenDone() const
{
    return m_closeDone && m_closeDone->isChecked();
}

void RenderingProgressDialog::beginRender(double lengthSec, qint64 estimatedBytes)
{
    m_lengthSec = std::max(0.05, lengthSec);
    m_estimatedBytes = std::max<qint64>(0, estimatedBytes);
    m_renderedBytes = 0;
    m_lastPercent = 0;
    m_framesDone = 0;
    m_framesTotal = 1;
    m_recentFps = 0.0;
    m_canceled = false;
    m_finished = false;
    m_elapsed.start();
    m_speedSample.start();
    m_speedSampleFrames = 0;

    const QStorageInfo storage(QFileInfo(m_outputPath).absolutePath());
    if (storage.isValid()) {
        m_freeSpaceVal->setText(formatBytes(storage.bytesAvailable()));
    }
    refreshTitle(0);
    refreshTiming();
}

void RenderingProgressDialog::applyProgress(const MediaRenderProgress &p)
{
    if (m_finished) {
        return;
    }

    QString stageName;
    switch (p.stage) {
    case MediaRenderProgress::Stage::Audio:
        stageName = tr("Mixing audio");
        break;
    case MediaRenderProgress::Stage::Frames:
        stageName = tr("Rendering frames");
        break;
    case MediaRenderProgress::Stage::Encode:
        stageName = tr("Encoding");
        break;
    case MediaRenderProgress::Stage::Done:
        stageName = tr("Done");
        break;
    }
    m_stageVal->setText(stageName);

    const int total = std::max(1, p.total);
    const int current = std::clamp(p.current, 0, total);
    double fraction = double(current) / double(total);

    // Weight stages: audio ~8%, frames ~82%, encode ~10% for overall bar.
    double overall = fraction;
    if (p.stage == MediaRenderProgress::Stage::Audio) {
        overall = 0.08 * fraction;
    } else if (p.stage == MediaRenderProgress::Stage::Frames) {
        overall = 0.08 + 0.82 * fraction;
        m_framesDone = current;
        m_framesTotal = total;
        ++m_speedSampleFrames;
        const qint64 ms = m_speedSample.elapsed();
        if (ms >= 400) {
            m_recentFps = (m_speedSampleFrames * 1000.0) / double(ms);
            m_speedSample.restart();
            m_speedSampleFrames = 0;
        }
    } else if (p.stage == MediaRenderProgress::Stage::Encode) {
        overall = 0.90 + 0.10 * fraction;
    } else {
        overall = 1.0;
    }

    const int percent = int(std::lround(overall * 100.0));
    m_lastPercent = std::clamp(percent, 0, 100);
    m_bar->setValue(int(std::lround(overall * 1000.0)));
    refreshTitle(m_lastPercent);

    if (m_estimatedBytes > 0) {
        m_renderedBytes = qint64(overall * double(m_estimatedBytes));
        m_pctLabel->setText(tr("%1% of %2")
                                .arg(m_lastPercent)
                                .arg(formatBytes(m_estimatedBytes)));
        m_renderedLabel->setText(tr("Rendered: %1").arg(formatBytes(m_renderedBytes)));
    } else {
        m_pctLabel->setText(tr("%1%").arg(m_lastPercent));
        m_renderedLabel->setText(p.message.isEmpty() ? tr("Rendered: —") : p.message);
    }

    if (p.stage == MediaRenderProgress::Stage::Frames && m_elapsed.elapsed() > 200) {
        const double avgFps = (m_framesDone * 1000.0) / double(m_elapsed.elapsed());
        m_avgSpeedVal->setText(tr("%1 fps").arg(avgFps, 0, 'f', 2));
        if (m_recentFps > 0.01) {
            m_curSpeedVal->setText(tr("%1 fps").arg(m_recentFps, 0, 'f', 2));
        }
    } else if (p.stage == MediaRenderProgress::Stage::Audio
               || p.stage == MediaRenderProgress::Stage::Encode) {
        m_avgSpeedVal->setText(QStringLiteral("—"));
        m_curSpeedVal->setText(QStringLiteral("—"));
    }

    refreshTiming();
}

void RenderingProgressDialog::refreshTiming()
{
    const double elapsedSec = m_elapsed.isValid() ? m_elapsed.elapsed() / 1000.0 : 0.0;
    m_elapsedLabel->setText(tr("Elapsed: %1").arg(formatDuration(elapsedSec)));

    double etaSec = -1.0;
    if (m_lastPercent > 1 && m_lastPercent < 100 && elapsedSec > 0.25) {
        etaSec = elapsedSec * (100.0 / double(m_lastPercent) - 1.0);
    }
    if (etaSec >= 0.0) {
        m_remainingLabel->setText(tr("Remaining: %1").arg(formatDuration(etaSec)));
        m_durationVal->setText(formatDuration(elapsedSec + etaSec));
        m_completionVal->setText(formatClock(QDateTime::currentDateTime().addSecs(int(etaSec))));
    } else if (m_lastPercent >= 100) {
        m_remainingLabel->setText(tr("Remaining: 00:00:00.00"));
        m_durationVal->setText(formatDuration(elapsedSec));
        m_completionVal->setText(formatClock(QDateTime::currentDateTime()));
    } else {
        m_remainingLabel->setText(tr("Remaining: —"));
        m_durationVal->setText(QStringLiteral("—"));
        m_completionVal->setText(QStringLiteral("—"));
    }
}

void RenderingProgressDialog::refreshTitle(int percent)
{
    setWindowTitle(tr("Rendering: %1%").arg(std::clamp(percent, 0, 100)));
}

void RenderingProgressDialog::finishSuccess(const QString &message)
{
    m_finished = true;
    m_bar->setValue(1000);
    refreshTitle(100);
    m_pctLabel->setText(tr("100%"));
    if (!message.isEmpty()) {
        m_renderedLabel->setText(message);
    }
    m_remainingLabel->setText(tr("Remaining: 00:00:00.00"));
    const double elapsedSec = m_elapsed.isValid() ? m_elapsed.elapsed() / 1000.0 : 0.0;
    m_elapsedLabel->setText(tr("Elapsed: %1").arg(formatDuration(elapsedSec)));
    m_durationVal->setText(formatDuration(elapsedSec));
    m_completionVal->setText(formatClock(QDateTime::currentDateTime()));
    m_stageVal->setText(tr("Done"));
    setFinishedUi(true);

    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    s.setValue(QStringLiteral("render/showPreview"), showVideoInPreview());
    s.setValue(QStringLiteral("render/openFolderWhenDone"), openFolderWhenDone());
    s.setValue(QStringLiteral("render/closeWhenDone"), closeWhenDone());

    if (openFolderWhenDone()) {
        const QFileInfo fi(m_outputPath);
        QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
    }
    if (closeWhenDone()) {
        accept();
    }
}

void RenderingProgressDialog::finishFailure(const QString &error)
{
    m_finished = true;
    m_stageVal->setText(m_canceled ? tr("Canceled") : tr("Failed"));
    if (!error.isEmpty()) {
        m_renderedLabel->setText(error);
    }
    setFinishedUi(false);
}

void RenderingProgressDialog::setFinishedUi(bool ok)
{
    m_openFolderBtn->setEnabled(ok);
    m_openBtn->setEnabled(ok);
    m_cancelBtn->setEnabled(true);
    m_cancelBtn->setText(tr("Close"));
    m_cancelBtn->setDefault(true);
}

void RenderingProgressDialog::reject()
{
    if (!m_finished) {
        m_canceled = true;
        emit cancelRequested();
        return;
    }
    QDialog::reject();
}

void RenderingProgressDialog::closeEvent(QCloseEvent *event)
{
    if (!m_finished) {
        m_canceled = true;
        emit cancelRequested();
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

QString RenderingProgressDialog::formatBytes(qint64 bytes)
{
    const double b = double(std::max<qint64>(0, bytes));
    if (b < 1024.0) {
        return QStringLiteral("%1 B").arg(int(b));
    }
    if (b < 1024.0 * 1024.0) {
        return QStringLiteral("%1 KB").arg(b / 1024.0, 0, 'f', 1);
    }
    if (b < 1024.0 * 1024.0 * 1024.0) {
        return QStringLiteral("%1 MB").arg(b / (1024.0 * 1024.0), 0, 'f', 2);
    }
    return QStringLiteral("%1 GB").arg(b / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

QString RenderingProgressDialog::formatDuration(double sec)
{
    if (!std::isfinite(sec) || sec < 0.0) {
        sec = 0.0;
    }
    const int totalCs = int(std::lround(sec * 100.0));
    const int cs = totalCs % 100;
    const int totalSec = totalCs / 100;
    const int s = totalSec % 60;
    const int m = (totalSec / 60) % 60;
    const int h = totalSec / 3600;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'))
        .arg(cs, 2, 10, QChar('0'));
}

QString RenderingProgressDialog::formatClock(const QDateTime &dt)
{
    return QLocale::system().toString(dt, QLocale::ShortFormat);
}

} // namespace openvegas
