#pragma once

#include <QWidget>
#include <QString>
#include <QVector>
#include <QColor>

class QLineEdit;
class QListWidget;
class QLabel;
class QButtonGroup;
class QHBoxLayout;
class QSplitter;

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
    void presetActivated(const QString &generatorName, const QString &presetName);

private:
    void buildUi();
    void loadCatalog();
    void loadFavorites();
    void saveFavorites();
    void rebuildPluginList();
    void showPlugin(int catalogIndex);
    void applySearchAndCategory();
    QIcon presetIcon(const Preset &p) const;

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
};

} // namespace openvegas
