#pragma once

#include "video/MediaGeneratorApply.h"

#include <QWidget>
#include <QString>
#include <QVector>
#include <QColor>
#include <QModelIndex>

class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QButtonGroup;
class QHBoxLayout;
class QSplitter;
class QTimer;

namespace openvegas {

/** Vegas-style Media Generator dock (search, chips, list, preset grid). */
class MediaGeneratorPane : public QWidget {
    Q_OBJECT
public:
    explicit MediaGeneratorPane(QWidget *parent = nullptr);

    void saveSettings() const;
    void restoreSettings();

    struct Preset {
        QString name;
        MediaGeneratorPattern pattern = MediaGeneratorPattern::Gradient;
        QColor c0;
        QColor c1;
        int tile = 8;
        /**
         * Non-empty for a real Titles & Text animation preset (a
         * titlesTextAnimationPresets() key) — presetIcon() renders real animated text
         * for these instead of the abstract pattern above.
         */
        QString animationKey;
        /**
         * Real placed/preview text when it differs from the catalog caption `name`
         * (only the 25 Title-N presets — Vegas captions them "Title01" etc. but each
         * places its own real marketing sample line, e.g. "UNIQUE\n\nTYPOGRAPHY").
         * Empty ⇒ falls back to `name` (true for every other Titles & Text preset,
         * where the caption already equals its real sample text).
         */
        QString sampleText;
    };

    struct Plugin {
        QString name;
        QStringList categories;
        QString grouping;
        QString version;
        QString description;
        QVector<Preset> presets;
        bool favorite = false;
        bool gpu = false;
    };

signals:
    void generatorActivated(const QString &name);
    /** animationKey is non-empty only for a real Titles & Text preset (see Preset::animationKey). */
    void presetActivated(const QString &generatorName, const QString &presetName,
                         const QString &animationKey = QString());

private:
    void buildUi();
    void loadCatalog();
    void loadFavorites();
    void saveFavorites();
    void rebuildPluginList();
    void showPlugin(int catalogIndex);
    void applySearchAndCategory();
    QIcon presetIcon(const Preset &p, double progress = 1.0) const;
    void onPresetHoverEntered(const QModelIndex &index);
    void onHoverTick();

    QVector<Plugin> m_plugins;
    int m_currentIndex = -1;
    QString m_activeCategory;

    QLineEdit *m_search = nullptr;
    QButtonGroup *m_chipGroup = nullptr;
    QListWidget *m_pluginList = nullptr;
    QListWidget *m_presetGrid = nullptr;
    QLabel *m_metaLine1 = nullptr;
    QLabel *m_metaLine2 = nullptr;
    QSplitter *m_splitter = nullptr;

    /** Animated hover preview for the preset under the cursor (see onHoverTick()). */
    QTimer *m_hoverTimer = nullptr;
    int m_hoverRow = -1;
    qint64 m_hoverStartMs = 0;
};

} // namespace openvegas
