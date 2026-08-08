#pragma once

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

    enum class Pattern {
        Gradient,
        Checker,
        HBlinds,
        VBlinds,
        Grille,
        Fence,
        Ridges,
        Bumps,
        Plaid,
        Letterbox,
        SplitScreen,
        Horizon
    };

    struct Preset {
        QString name;
        Pattern pattern = Pattern::Gradient;
        QColor c0;
        QColor c1;
        int tile = 8;
        /**
         * Non-empty for a real Titles & Text animation preset (a
         * titlesTextAnimationPresets() key) — presetIcon() renders real animated text
         * for these instead of the abstract pattern above.
         */
        QString animationKey;
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
