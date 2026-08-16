#pragma once

#include "plugins/PluginScanner.h"

#include <QWidget>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <QColor>
#include <QPixmap>

class QLineEdit;
class QListWidget;
class QTimer;
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

    /** Watches the preset grid's viewport to drive the hover wipe animation. */
    bool eventFilter(QObject *watched, QEvent *event) override;

    void saveSettings() const;
    void restoreSettings();

    struct Preset {
        QString name;
        QColor c0;
        QColor c1;
        bool radial = false;
        /** Values this preset sets, from the bundle's PresetPackage. */
        QVariantMap params;
        /**
         * The sample photo with this preset actually rendered through the plug-in.
         * Null when the effect could not be rendered — the tile then shows the clean
         * sample rather than an invented approximation.
         */
        QPixmap rendered;
        bool renderAttempted = false;
    };
    struct Plugin {
        /** Trimmed name shown in the list ("AI Upscale"). */
        QString name;
        /** Full OfxPropLabel including the brand ("VEGAS AI Upscale"), for the status line. */
        QString fullLabel;
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
    /**
     * Tile for one preset.
     *
     * `progress` drives the hover wipe: **0 is the resting state — effect fully applied**,
     * 0…1 strips it away left-to-right, 1…2 brings it back left-to-right, then wraps.
     * (1.0 is the fully *clean* frame, which is why it must not be the default.)
     */
    QIcon presetIcon(const Preset &p, double progress = 0.0) const;
    /** Shared sample photo every preset tile previews over (VEGAS uses the same idea). */
    static const QPixmap &presetSampleImage();
    /** Render the sample through the real plug-in for every preset of the current effect. */
    void renderPresetPreviews(int catalogIndex);
    void startHoverAnimation(int row);
    void stopHoverAnimation();

    PluginScanner *m_scanner = nullptr;
    QVector<Plugin> m_plugins;
    int m_currentIndex = -1;
    /** Preset tile the cursor is over, or -1. Drives the wipe animation. */
    int m_hoverRow = -1;
    double m_hoverProgress = 1.0;
    QTimer *m_hoverTimer = nullptr;
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
