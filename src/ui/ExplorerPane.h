#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>
#include <QVector>

class QTreeView;
class QListView;
class QFileSystemModel;
class QToolButton;
class QLabel;
class QSplitter;
class QHBoxLayout;
class QModelIndex;
class QPoint;

namespace openvegas {

class ExplorerSidebarModel;

/** Vegas-style Explorer dock: sidebar tree + thumbnail grid + path bar. */
class ExplorerPane : public QWidget {
    Q_OBJECT
public:
    explicit ExplorerPane(QWidget *parent = nullptr);

    QString currentPath() const { return m_currentPath; }
    void saveSettings() const;
    void restoreSettings();

signals:
    void mediaActivated(const QString &path);
    void importRequested(const QStringList &paths);
    void addToTimelineRequested(const QStringList &paths);
    void trimMediaRequested(const QString &path);
    void previewMediaRequested(const QString &path);

private slots:
    void goBack();
    void goForward();
    void goUp();
    void refresh();
    void onTreeActivated(const QModelIndex &index);
    void onTreeClicked(const QModelIndex &index);
    void onListActivated(const QModelIndex &index);
    void onListDoubleClicked(const QModelIndex &index);
    void onListContextMenu(const QPoint &pos);
    void onTreeContextMenu(const QPoint &pos);
    void onThumbnailReady(const QString &path);

private:
    void buildUi();
    void navigateTo(const QString &path, bool recordHistory = true);
    void updateBreadcrumb();
    void updateNavButtons();
    void rebuildBreadcrumbButtons();
    QString defaultStartPath() const;
    QStringList selectedListPaths() const;
    void showEmptyAreaMenu(const QPoint &globalPos);
    void showFileMenu(const QStringList &paths, const QPoint &globalPos);
    void showFolderMenu(const QString &folderPath, const QPoint &globalPos);
    void renamePath(const QString &path);
    void removePaths(const QStringList &paths);
    void copyPaths(const QStringList &paths, bool cut);
    void openInOsExplorer(const QString &path);
    void showProperties(const QString &path);
    void addFavorite(const QString &path);

    ExplorerSidebarModel *m_sidebar = nullptr;
    QFileSystemModel *m_listFs = nullptr;
    QTreeView *m_tree = nullptr;
    QListView *m_list = nullptr;
    QSplitter *m_splitter = nullptr;
    QWidget *m_toolbar = nullptr;
    QHBoxLayout *m_pathLay = nullptr;
    QToolButton *m_btnBack = nullptr;
    QToolButton *m_btnForward = nullptr;
    QToolButton *m_btnUp = nullptr;
    QToolButton *m_btnRefresh = nullptr;

    QString m_currentPath;
    QStringList m_history;
    int m_historyIndex = -1;
};

} // namespace openvegas
