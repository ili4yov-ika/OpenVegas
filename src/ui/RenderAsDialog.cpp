#include "ui/RenderAsDialog.h"
#include "ui_RenderAsDialog.h"

#include "model/ProjectModel.h"

#include <QAction>
#include <QCursor>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>


#include <cmath>

namespace openvegas {

namespace {
constexpr int kRoleTemplateName = Qt::UserRole + 1;
constexpr int kRoleFavorite = Qt::UserRole + 2;
} // namespace

RenderAsDialog::RenderAsDialog(ProjectModel *project, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::RenderAsDialog)
    , m_project(project)
{
    ui->setupUi(this);
    setWindowTitle(tr("Render As"));
    resize(920, 640);
    ui->customizeButton->setEnabled(false);

    // Vegas-like resizable Formats | Templates
    if (auto *lists = ui->listsRow) {
        auto *splitter = new QSplitter(Qt::Horizontal, this);
        splitter->addWidget(ui->groupFormats);
        splitter->addWidget(ui->groupTemplates);
        splitter->setStretchFactor(0, 2);
        splitter->setStretchFactor(1, 3);
        while (lists->count() > 0) {
            delete lists->takeAt(0);
        }
        lists->addWidget(splitter);
    }

    applyProjectDefaults();
    loadOptionsFromSettings();

    // Filters menu
    auto *filtersMenu = new QMenu(this);
    auto makeFilter = [&](const QString &text, bool *flag) {
        auto *a = filtersMenu->addAction(text);
        a->setCheckable(true);
        a->setChecked(*flag);
        connect(a, &QAction::toggled, this, [this, flag](bool on) {
            *flag = on;
            updateFiltersButtonText();
            rebuildFormatList();
            rebuildTemplateList();
            saveOptionsToSettings();
        });
        return a;
    };
    makeFilter(tr("Show favorites only"), &m_filterFavoritesOnly);
    makeFilter(tr("Match project settings"), &m_filterMatchProject);
    makeFilter(tr("Audio templates only"), &m_filterAudioOnly);
    makeFilter(tr("Video templates only"), &m_filterVideoOnly);
    filtersMenu->addSeparator();
    makeFilter(tr("Match project audio channel count"), &m_filterMatchChannels);
    makeFilter(tr("Match project audio sample rate"), &m_filterMatchSampleRate);
    makeFilter(tr("Match project video frame rate"), &m_filterMatchFrameRate);
    makeFilter(tr("Match project video frame size"), &m_filterMatchFrameSize);
    makeFilter(tr("Match project pixel aspect ratio"), &m_filterMatchAspect);
    makeFilter(tr("Match project field order"), &m_filterMatchFieldOrder);
    ui->filtersButton->setMenu(filtersMenu);
    updateFiltersButtonText();

    // Render options menu
    auto *optMenu = new QMenu(this);
    auto makeOpt = [&](const QString &text, bool *flag) {
        auto *a = optMenu->addAction(text);
        a->setCheckable(true);
        a->setChecked(*flag);
        connect(a, &QAction::toggled, this, [this, flag](bool on) {
            *flag = on;
            saveOptionsToSettings();
        });
    };
    makeOpt(tr("Render loop region only"), &m_optLoopRegion);
    makeOpt(tr("Stretch video to fill output frame size (do not letterbox)"), &m_optStretchFill);
    makeOpt(tr("Use project output rotation setting"), &m_optUseRotation);
    makeOpt(tr("Save project markers in rendered media file"), &m_optSaveMarkers);
    makeOpt(tr("Save project as path reference in rendered media file"), &m_optSavePathRef);
    makeOpt(tr("Save loudness log file next to media file"), &m_optLoudnessLog);
    makeOpt(tr("Enable multichannel mapping"), &m_optMultichannel);
    makeOpt(tr("Swap Video Files on Render"), &m_optSwapVideo);
    makeOpt(tr("Render with AI Auto Reframe"), &m_optAiReframe);
    ui->renderOptionsButton->setMenu(optMenu);

    connect(ui->searchEdit, &QLineEdit::textChanged, this, [this]() {
        rebuildFormatList();
        rebuildTemplateList();
    });
    connect(ui->formatList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *, QListWidgetItem *) {
        onFormatChanged();
    });
    connect(ui->templateList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *, QListWidgetItem *) {
        onTemplateChanged();
    });
    connect(ui->templateList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!item) {
            return;
        }
        const QPoint pos = ui->templateList->mapFromGlobal(QCursor::pos());
        const QRect r = ui->templateList->visualItemRect(item);
        if (pos.x() - r.left() < 48) {
            toggleFavorite(item);
        }
    });
    connect(ui->folderEdit, &QLineEdit::textChanged, this, [this]() {
        updateFreeSpace();
        updateRenderEnabled();
    });
    connect(ui->nameEdit, &QLineEdit::textChanged, this, [this]() { updateRenderEnabled(); });
    connect(ui->browseButton, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Select Output Folder"),
                                                              ui->folderEdit->text());
        if (!dir.isEmpty()) {
            ui->folderEdit->setText(QDir::toNativeSeparators(dir));
        }
    });
    connect(ui->projectLocationButton, &QPushButton::clicked, this, [this]() {
        if (!m_projectDir.isEmpty()) {
            ui->folderEdit->setText(QDir::toNativeSeparators(m_projectDir));
        }
    });
    connect(ui->helpButton, &QToolButton::clicked, this, [this]() {
        QMessageBox::information(this, tr("Help unavailable"),
                                 tr("No help is currently available for this subject."));
    });
    connect(ui->aboutButton, &QPushButton::clicked, this, &RenderAsDialog::showAboutPlugin);
    connect(ui->customizeButton, &QPushButton::clicked, this, [this]() {
        QMessageBox::information(this, tr("Customize Template"),
                                 tr("Template customization is not implemented yet."));
    });
    connect(ui->renderButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(ui->cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    m_format = QStringLiteral("AVC/AAC MP4");
    rebuildFormatList();
    rebuildTemplateList();
    updateFreeSpace();
    updateEstimatedSize();
    updateRenderEnabled();
}

RenderAsDialog::~RenderAsDialog()
{
    saveOptionsToSettings();
    delete ui;
}

QString RenderAsDialog::outputFolder() const
{
    return ui->folderEdit->text().trimmed();
}

QString RenderAsDialog::outputFileName() const
{
    return ui->nameEdit->text().trimmed();
}

QString RenderAsDialog::outputPath() const
{
    return QDir(outputFolder()).filePath(outputFileName());
}

bool RenderAsDialog::isWaveMicrosoft() const
{
    return m_format.compare(QStringLiteral("Wave (Microsoft)"), Qt::CaseInsensitive) == 0;
}

void RenderAsDialog::applyProjectDefaults()
{
    const QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    QString folder = videos.isEmpty()
                         ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                         : videos;

    m_projectBaseName = QStringLiteral("Untitled");
    if (m_project) {
        const QString path = m_project->projectPath();
        if (!path.isEmpty()) {
            const QFileInfo fi(path);
            m_projectDir = fi.absolutePath();
            m_projectBaseName = fi.completeBaseName();
            if (m_projectBaseName.isEmpty()) {
                m_projectBaseName = QStringLiteral("Untitled");
            }
        } else {
            m_projectBaseName = m_project->projectTitle();
        }
    }
    if (m_projectDir.isEmpty()) {
        m_projectDir = folder;
    }

    ui->folderEdit->setText(QDir::toNativeSeparators(folder));
    const QString ext = RenderTemplateCatalog::defaultExtensionFor(QStringLiteral("AVC/AAC MP4"));
    ui->nameEdit->setText(m_projectBaseName + ext);
}

void RenderAsDialog::loadOptionsFromSettings()
{
    QSettings s;
    s.beginGroup(QStringLiteral("render"));
    m_filterFavoritesOnly = s.value(QStringLiteral("filterFavoritesOnly"), false).toBool();
    m_filterMatchProject = s.value(QStringLiteral("filterMatchProject"), false).toBool();
    m_filterAudioOnly = s.value(QStringLiteral("filterAudioOnly"), false).toBool();
    m_filterVideoOnly = s.value(QStringLiteral("filterVideoOnly"), false).toBool();
    m_filterMatchChannels = s.value(QStringLiteral("filterMatchChannels"), false).toBool();
    m_filterMatchSampleRate = s.value(QStringLiteral("filterMatchSampleRate"), false).toBool();
    m_filterMatchFrameRate = s.value(QStringLiteral("filterMatchFrameRate"), false).toBool();
    m_filterMatchFrameSize = s.value(QStringLiteral("filterMatchFrameSize"), false).toBool();
    m_filterMatchAspect = s.value(QStringLiteral("filterMatchAspect"), false).toBool();
    m_filterMatchFieldOrder = s.value(QStringLiteral("filterMatchFieldOrder"), false).toBool();

    m_optLoopRegion = s.value(QStringLiteral("optLoopRegion"), false).toBool();
    m_optStretchFill = s.value(QStringLiteral("optStretchFill"), false).toBool();
    m_optUseRotation = s.value(QStringLiteral("optUseRotation"), false).toBool();
    m_optSaveMarkers = s.value(QStringLiteral("optSaveMarkers"), false).toBool();
    m_optSavePathRef = s.value(QStringLiteral("optSavePathRef"), false).toBool();
    m_optLoudnessLog = s.value(QStringLiteral("optLoudnessLog"), false).toBool();
    m_optMultichannel = s.value(QStringLiteral("optMultichannel"), false).toBool();
    m_optSwapVideo = s.value(QStringLiteral("optSwapVideo"), false).toBool();
    m_optAiReframe = s.value(QStringLiteral("optAiReframe"), false).toBool();
    s.endGroup();
}

void RenderAsDialog::saveOptionsToSettings() const
{
    QSettings s;
    s.beginGroup(QStringLiteral("render"));
    s.setValue(QStringLiteral("filterFavoritesOnly"), m_filterFavoritesOnly);
    s.setValue(QStringLiteral("filterMatchProject"), m_filterMatchProject);
    s.setValue(QStringLiteral("filterAudioOnly"), m_filterAudioOnly);
    s.setValue(QStringLiteral("filterVideoOnly"), m_filterVideoOnly);
    s.setValue(QStringLiteral("filterMatchChannels"), m_filterMatchChannels);
    s.setValue(QStringLiteral("filterMatchSampleRate"), m_filterMatchSampleRate);
    s.setValue(QStringLiteral("filterMatchFrameRate"), m_filterMatchFrameRate);
    s.setValue(QStringLiteral("filterMatchFrameSize"), m_filterMatchFrameSize);
    s.setValue(QStringLiteral("filterMatchAspect"), m_filterMatchAspect);
    s.setValue(QStringLiteral("filterMatchFieldOrder"), m_filterMatchFieldOrder);

    s.setValue(QStringLiteral("optLoopRegion"), m_optLoopRegion);
    s.setValue(QStringLiteral("optStretchFill"), m_optStretchFill);
    s.setValue(QStringLiteral("optUseRotation"), m_optUseRotation);
    s.setValue(QStringLiteral("optSaveMarkers"), m_optSaveMarkers);
    s.setValue(QStringLiteral("optSavePathRef"), m_optSavePathRef);
    s.setValue(QStringLiteral("optLoudnessLog"), m_optLoudnessLog);
    s.setValue(QStringLiteral("optMultichannel"), m_optMultichannel);
    s.setValue(QStringLiteral("optSwapVideo"), m_optSwapVideo);
    s.setValue(QStringLiteral("optAiReframe"), m_optAiReframe);
    s.endGroup();
}

void RenderAsDialog::updateFiltersButtonText()
{
    const bool any = m_filterFavoritesOnly || m_filterMatchProject || m_filterAudioOnly
                     || m_filterVideoOnly || m_filterMatchChannels || m_filterMatchSampleRate
                     || m_filterMatchFrameRate || m_filterMatchFrameSize || m_filterMatchAspect
                     || m_filterMatchFieldOrder;
    ui->filtersButton->setText(any ? tr("Filters On") : tr("Filters Off"));
}

bool RenderAsDialog::templatePassesFilters(const RenderFormat &fmt, const RenderTemplate &tpl) const
{
    if (m_filterFavoritesOnly
        && !RenderTemplateCatalog::isFavorite(fmt.name, tpl.name)) {
        return false;
    }
    if (m_filterAudioOnly && !fmt.audioOnly && !tpl.audioOnly) {
        return false;
    }
    if (m_filterVideoOnly && (fmt.audioOnly || tpl.audioOnly)) {
        return false;
    }
    if (!m_project) {
        return true;
    }
    const bool match = m_filterMatchProject || m_filterMatchChannels || m_filterMatchSampleRate
                       || m_filterMatchFrameRate || m_filterMatchFrameSize || m_filterMatchAspect
                       || m_filterMatchFieldOrder;
    if (!match) {
        return true;
    }
    if (m_filterMatchProject || m_filterMatchSampleRate) {
        if (tpl.sampleRate > 0
            && std::abs(int(tpl.sampleRate) - int(m_project->sampleRate())) > 1) {
            return false;
        }
    }
    if (m_filterMatchProject || m_filterMatchChannels) {
        if (tpl.channels > 0 && tpl.channels != 2) {
            // Project default stereo for filter match when channel meta exists
            return false;
        }
    }
    if (m_filterMatchProject || m_filterMatchFrameRate) {
        if (tpl.fps > 0.0 && std::abs(tpl.fps - m_project->frameRate()) > 0.05) {
            return false;
        }
    }
    if (m_filterMatchProject || m_filterMatchFrameSize) {
        if (tpl.width > 0 && tpl.height > 0
            && (tpl.width != m_project->frameWidth() || tpl.height != m_project->frameHeight())) {
            return false;
        }
    }
    // Aspect / field order: no separate project fields yet — treat as pass-through.
    Q_UNUSED(m_filterMatchAspect);
    Q_UNUSED(m_filterMatchFieldOrder);
    return true;
}

void RenderAsDialog::rebuildFormatList()
{
    const QString q = ui->searchEdit->text().trimmed().toLower();
    const QString prev = m_format;
    const bool filtersActive = m_filterFavoritesOnly || m_filterMatchProject || m_filterAudioOnly
                               || m_filterVideoOnly || m_filterMatchChannels || m_filterMatchSampleRate
                               || m_filterMatchFrameRate || m_filterMatchFrameSize;

    ui->formatList->blockSignals(true);
    ui->formatList->clear();

    int selectRow = -1;
    for (const RenderFormat &fmt : RenderTemplateCatalog::formats()) {
        const bool matchSelf = q.isEmpty() || fmt.name.toLower().contains(q);
        int visibleTpl = 0;
        bool matchTpl = false;
        for (const RenderTemplate &tpl : fmt.templates) {
            if (!templatePassesFilters(fmt, tpl)) {
                continue;
            }
            if (!q.isEmpty() && !tpl.name.toLower().contains(q) && !matchSelf) {
                continue;
            }
            ++visibleTpl;
            if (!q.isEmpty() && tpl.name.toLower().contains(q)) {
                matchTpl = true;
            }
        }
        if (!q.isEmpty() && !matchSelf && !matchTpl) {
            continue;
        }
        if (filtersActive && visibleTpl == 0) {
            continue;
        }

        auto *item = new QListWidgetItem(fmt.name);
        ui->formatList->addItem(item);
        if (fmt.name == prev) {
            selectRow = ui->formatList->count() - 1;
        }
    }
    ui->formatList->blockSignals(false);
    if (ui->formatList->count() == 0) {
        m_format.clear();
        m_templateName.clear();
        ui->templateList->clear();
        syncTemplateInfo();
        updateRenderEnabled();
        return;
    }
    if (selectRow < 0) {
        selectRow = 0;
    }
    ui->formatList->setCurrentRow(selectRow);
    onFormatChanged();
}

void RenderAsDialog::rebuildTemplateList()
{
    const QString q = ui->searchEdit->text().trimmed().toLower();
    const RenderFormat *fmt = RenderTemplateCatalog::findFormat(m_format);
    const QString prevTpl = m_templateName;

    ui->templateList->blockSignals(true);
    ui->templateList->clear();
    int selectRow = -1;
    if (fmt) {
        for (const RenderTemplate &tpl : fmt->templates) {
            if (!templatePassesFilters(*fmt, tpl)) {
                continue;
            }
            if (!q.isEmpty() && !tpl.name.toLower().contains(q)
                && !fmt->name.toLower().contains(q)) {
                continue;
            }
            const bool fav = RenderTemplateCatalog::isFavorite(fmt->name, tpl.name);
            auto *item = new QListWidgetItem(templateListLabel(*fmt, tpl, fav));
            item->setData(kRoleTemplateName, tpl.name);
            item->setData(kRoleFavorite, fav);
            ui->templateList->addItem(item);
            if (tpl.name == prevTpl) {
                selectRow = ui->templateList->count() - 1;
            }
        }
    }
    ui->templateList->blockSignals(false);
    if (ui->templateList->count() == 0) {
        m_templateName.clear();
        syncTemplateInfo();
        updateRenderEnabled();
        return;
    }
    if (selectRow < 0) {
        selectRow = 0;
    }
    ui->templateList->setCurrentRow(selectRow);
    onTemplateChanged();
}

void RenderAsDialog::onFormatChanged()
{
    auto *item = ui->formatList->currentItem();
    m_format = item ? item->text() : QString();
    updateNameExtension();
    rebuildTemplateList();
}

void RenderAsDialog::onTemplateChanged()
{
    auto *item = ui->templateList->currentItem();
    m_templateName = item ? item->data(kRoleTemplateName).toString() : QString();
    syncTemplateInfo();
    updateEstimatedSize();
    updateRenderEnabled();
}

void RenderAsDialog::toggleFavorite(QListWidgetItem *item)
{
    if (!item) {
        return;
    }
    const QString name = item->data(kRoleTemplateName).toString();
    const bool fav = !item->data(kRoleFavorite).toBool();
    RenderTemplateCatalog::setFavorite(m_format, name, fav);
    item->setData(kRoleFavorite, fav);
    if (const RenderFormat *fmt = RenderTemplateCatalog::findFormat(m_format)) {
        for (const RenderTemplate &tpl : fmt->templates) {
            if (tpl.name == name) {
                item->setText(templateListLabel(*fmt, tpl, fav));
                break;
            }
        }
    }
    if (m_filterFavoritesOnly) {
        rebuildTemplateList();
    }
}

bool RenderAsDialog::templateMatchesProject(const RenderTemplate &tpl) const
{
    if (!m_project) {
        return false;
    }
    if (tpl.audioOnly || (tpl.width <= 0 && tpl.height <= 0 && tpl.fps <= 0.0)) {
        if (tpl.sampleRate > 0
            && std::abs(int(tpl.sampleRate) - int(m_project->sampleRate())) <= 1) {
            return true;
        }
        return false;
    }
    bool ok = true;
    if (tpl.fps > 0.0) {
        ok = ok && std::abs(tpl.fps - m_project->frameRate()) <= 0.05;
    }
    if (tpl.width > 0 && tpl.height > 0) {
        ok = ok && tpl.width == m_project->frameWidth() && tpl.height == m_project->frameHeight();
    }
    if (tpl.sampleRate > 0) {
        ok = ok && std::abs(int(tpl.sampleRate) - int(m_project->sampleRate())) <= 1;
    }
    return ok;
}

QString RenderAsDialog::templateListLabel(const RenderFormat &fmt, const RenderTemplate &tpl,
                                         bool fav) const
{
    Q_UNUSED(fmt);
    const QString match = templateMatchesProject(tpl) ? QStringLiteral("= ") : QStringLiteral("  ");
    const QString star = fav ? QStringLiteral("★ ") : QStringLiteral("☆ ");
    return match + star + tpl.name;
}

void RenderAsDialog::showAboutPlugin()
{
    const RenderFormat *fmt = RenderTemplateCatalog::findFormat(m_format);
    const QString title = fmt ? fmt->aboutTitle : tr("File Format Plug-In");
    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("About %1").arg(title));
    dlg->resize(480, 320);
    auto *layout = new QVBoxLayout(dlg);
    auto *tabs = new QTabWidget(dlg);
    auto *general = new QWidget(tabs);
    auto *gl = new QVBoxLayout(general);
    auto *body = new QLabel(general);
    body->setWordWrap(true);
    body->setTextFormat(Qt::RichText);
    body->setOpenExternalLinks(true);
    QString extra = fmt && !fmt->aboutExtra.isEmpty()
                        ? QStringLiteral("<p>%1</p>").arg(fmt->aboutExtra.toHtmlEscaped())
                        : QString();
    body->setText(tr("<p><b>%1</b><br/>Version OpenVegas</p>"
                     "<p>Copyright (c) OpenVegas contributors. All rights reserved.</p>"
                     "%2"
                     "<p>Unauthorized reproduction or distribution of this software, "
                     "or any portion of it, may result in severe civil and criminal penalties.</p>"
                     "<p><a href=\"https://github.com/ili4yov-ika/OpenVegas\">"
                     "github.com/ili4yov-ika/OpenVegas</a></p>")
                      .arg(title.toHtmlEscaped(), extra));
    gl->addWidget(body);
    gl->addStretch(1);
    tabs->addTab(general, tr("General"));
    layout->addWidget(tabs);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, dlg);
    connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    layout->addWidget(buttons);
    dlg->exec();
}

void RenderAsDialog::syncTemplateInfo()
{
    const RenderFormat *fmt = RenderTemplateCatalog::findFormat(m_format);
    QString info;
    if (fmt) {
        for (const RenderTemplate &tpl : fmt->templates) {
            if (tpl.name == m_templateName) {
                info = tpl.info;
                break;
            }
        }
    }
    ui->templateInfoLabel->setText(info);
}

void RenderAsDialog::updateNameExtension()
{
    QString name = ui->nameEdit->text().trimmed();
    if (name.isEmpty()) {
        name = m_projectBaseName;
    }
    const QFileInfo fi(name);
    const QString base = fi.completeBaseName().isEmpty() ? m_projectBaseName : fi.completeBaseName();
    const QString ext = RenderTemplateCatalog::defaultExtensionFor(m_format);
    ui->nameEdit->setText(base + ext);
}

void RenderAsDialog::updateFreeSpace()
{
    const QString folder = ui->folderEdit->text().trimmed();
    QStorageInfo info(folder.isEmpty() ? QDir::rootPath() : folder);
    if (!info.isValid()) {
        info.setPath(QDir::rootPath());
    }
    const qint64 bytes = info.bytesAvailable();
    QString text;
    if (bytes < 0) {
        text = tr("Free disk space: —");
    } else {
        const double gb = double(bytes) / (1024.0 * 1024.0 * 1024.0);
        text = tr("Free disk space: %1 GB").arg(gb, 0, 'f', 0);
    }
    ui->freeSpaceLabel->setText(text);
}

void RenderAsDialog::updateEstimatedSize()
{
    if (!ui->estimatedSizeLabel) {
        return;
    }
    const RenderFormat *fmt = RenderTemplateCatalog::findFormat(m_format);
    qint64 bytes = 0;
    if (fmt && m_project) {
        for (const RenderTemplate &tpl : fmt->templates) {
            if (tpl.name == m_templateName) {
                bytes = RenderTemplateCatalog::estimateBytes(tpl, m_project->timelineEndSec());
                break;
            }
        }
    }
    if (bytes <= 0) {
        ui->estimatedSizeLabel->clear();
        return;
    }
    const double gb = double(bytes) / (1024.0 * 1024.0 * 1024.0);
    if (gb >= 1.0) {
        ui->estimatedSizeLabel->setText(tr("Estimated file size: %1 GB").arg(gb, 0, 'f', 0));
    } else {
        const double mb = double(bytes) / (1024.0 * 1024.0);
        ui->estimatedSizeLabel->setText(tr("Estimated file size: %1 MB").arg(mb, 0, 'f', 0));
    }
}

void RenderAsDialog::updateRenderEnabled()
{
    ui->renderButton->setEnabled(!m_format.isEmpty() && !m_templateName.isEmpty()
                                 && !outputFolder().isEmpty() && !outputFileName().isEmpty());
}

} // namespace openvegas
