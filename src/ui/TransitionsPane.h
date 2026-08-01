#pragma once

#include <QWidget>
#include <QString>
#include <QVector>
#include <QColor>

class QLineEdit;
class QListWidget;
class QLabel;
class QButtonGroup;
class QSplitter;

namespace openvegas {

/** Vegas-style Transitions dock: search, chips, plug-in list, preset grid. */
class TransitionsPane : public QWidget {
    Q_OBJECT
public:
    explicit TransitionsPane(QWidget *parent = nullptr);

    void saveSettings() const;
    void restoreSettings();

    enum class Thumb {
        Gradient,
        SimpleBlinds,
        LeftToRight,
        SlotMachine,
        Spin,
        Wipe,
        Dissolve,
        Push,
        Iris,
        Page
    };

    struct Preset {
        QString name;
        Thumb thumb = Thumb::Gradient;
        QColor accent = QColor(0x1a, 0x4a, 0x8a);
    };

    struct Plugin {
        QString name;
        QStringList categories;
        QString format; // e.g. DXT / OFX
        QString description;
        QVector<Preset> presets;
        bool favorite = false;
    };

signals:
    void transitionActivated(const QString &name);
    void presetActivated(const QString &transitionName, const QString &presetName);

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
