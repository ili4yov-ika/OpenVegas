#pragma once

#include "plugins/PluginScanner.h"

#include <QWidget>
#include <QString>
#include <QVector>
#include <QColor>

class QLineEdit;
class QListWidget;
class QPixmap;
class QLabel;
class QButtonGroup;
class QHBoxLayout;
class QSplitter;

namespace openvegas {

/** Vegas-style Video FX dock: search, category chips, plug-in list, preset grid. */
class VideoFxPane : public QWidget {
    Q_OBJECT
public:
    explicit VideoFxPane(PluginScanner *scanner, QWidget *parent = nullptr);

    void refreshFromScanner();
    void saveSettings() const;
    void restoreSettings();

    struct Preset {
        QString name;
        QColor c0;
        QColor c1;
        bool radial = false;
    };
    struct Plugin {
        QString name;
        QStringList categories;
        QString grouping;
        QString version;
        QString description;
        QVector<Preset> presets;
        bool favorite = false;
        QString path;
    };

signals:
    void pluginActivated(const QString &name);
    void presetActivated(const QString &pluginName, const QString &presetName);

private:
    void buildUi();
    void loadCatalog();
    void loadFavorites();
    void saveFavorites();
    void rebuildPluginList();
    void showPlugin(int catalogIndex);
    void applySearchAndCategory();
    QIcon presetIcon(const Preset &p) const;
    /** Shared sample photo every preset tile previews over (VEGAS uses the same idea). */
    static const QPixmap &presetSampleImage();

    PluginScanner *m_scanner = nullptr;
    QVector<Plugin> m_plugins;
    int m_currentIndex = -1;
    QString m_activeCategory;

    QLineEdit *m_search = nullptr;
    QWidget *m_chipsHost = nullptr;
    QHBoxLayout *m_chipsLay = nullptr;
    QButtonGroup *m_chipGroup = nullptr;
    QListWidget *m_pluginList = nullptr;
    QListWidget *m_presetGrid = nullptr;
    QLabel *m_metaLine1 = nullptr;
    QLabel *m_metaLine2 = nullptr;
    QSplitter *m_splitter = nullptr;
};

} // namespace openvegas
