#pragma once

#include "io/RenderTemplateCatalog.h"

#include <QDialog>
#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QSplitter>
#include <QString>

class QListWidgetItem;

namespace Ui {
class RenderAsDialog;
}

namespace openvegas {

class ProjectModel;

class RenderAsDialog : public QDialog {
    Q_OBJECT
public:
    explicit RenderAsDialog(ProjectModel *project, QWidget *parent = nullptr);
    ~RenderAsDialog() override;

    QString selectedFormat() const { return m_format; }
    QString selectedTemplate() const { return m_templateName; }
    QString outputFolder() const;
    QString outputFileName() const;
    QString outputPath() const;
    bool isWaveMicrosoft() const;
    bool optionLoopRegionOnly() const { return m_optLoopRegion; }

private:
    void rebuildFormatList();
    void rebuildTemplateList();
    void updateFiltersButtonText();
    void updateFreeSpace();
    void updateEstimatedSize();
    void updateRenderEnabled();
    void updateNameExtension();
    void syncTemplateInfo();
    void applyProjectDefaults();
    void loadOptionsFromSettings();
    void saveOptionsToSettings() const;
    bool templatePassesFilters(const RenderFormat &fmt, const RenderTemplate &tpl) const;
    bool templateMatchesProject(const RenderTemplate &tpl) const;
    QString templateListLabel(const RenderFormat &fmt, const RenderTemplate &tpl, bool fav) const;
    void onFormatChanged();
    void onTemplateChanged();
    void toggleFavorite(QListWidgetItem *item);
    void showAboutPlugin();

    Ui::RenderAsDialog *ui = nullptr;
    ProjectModel *m_project = nullptr;
    QString m_format;
    QString m_templateName;
    QString m_projectDir;
    QString m_projectBaseName;

    bool m_filterFavoritesOnly = false;
    bool m_filterMatchProject = false;
    bool m_filterAudioOnly = false;
    bool m_filterVideoOnly = false;
    bool m_filterMatchChannels = false;
    bool m_filterMatchSampleRate = false;
    bool m_filterMatchFrameRate = false;
    bool m_filterMatchFrameSize = false;
    bool m_filterMatchAspect = false;
    bool m_filterMatchFieldOrder = false;

    bool m_optLoopRegion = false;
    bool m_optStretchFill = false;
    bool m_optUseRotation = false;
    bool m_optSaveMarkers = false;
    bool m_optSavePathRef = false;
    bool m_optLoudnessLog = false;
    bool m_optMultichannel = false;
    bool m_optSwapVideo = false;
    bool m_optAiReframe = false;
};

} // namespace openvegas
