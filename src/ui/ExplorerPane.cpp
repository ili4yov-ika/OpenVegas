#include "ui/ExplorerPane.h"
#include "ui/IconFactory.h"
#include "io/MediaMime.h"
#include "io/MediaThumbCache.h"

#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDrag>
#include <QDir>
#include <QFile>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <QStyle>

#include <algorithm>

namespace openvegas {

namespace {

enum class NodeKind { Favorites, Recent, Shortcut, Computer, Drive, Folder };

struct SideNode {
    QString name;
    QString path;
    NodeKind kind = NodeKind::Folder;
    bool populated = false;
    SideNode *parent = nullptr;
    QVector<SideNode *> children;

    ~SideNode() { qDeleteAll(children); }
};

bool isMediaFile(const QString &path)
{
    return MediaMime::isMediaFile(path);
}

QIcon makeFolderIcon()
{
    QPixmap pm(48, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xd4, 0xb0, 0x3a));
    p.drawRoundedRect(0, 0, 18, 6, 1, 1);
    p.setBrush(QColor(0xe8, 0xc8, 0x4a));
    p.drawRoundedRect(0, 5, 48, 35, 2, 2);
    p.setBrush(QColor(0xc9, 0xa2, 0x27));
    p.drawRoundedRect(0, 18, 48, 22, 2, 2);
    p.end();
    return QIcon(pm);
}

} // namespace

class ExplorerSidebarModel : public QAbstractItemModel {
public:
    explicit ExplorerSidebarModel(QObject *parent = nullptr)
        : QAbstractItemModel(parent)
    {
        m_root = new SideNode;
        m_root->name = QStringLiteral("root");
        m_root->populated = true;

        auto addRoot = [this](const QString &name, NodeKind kind, const QString &path = {}) {
            auto *n = new SideNode;
            n->name = name;
            n->kind = kind;
            n->path = path;
            n->parent = m_root;
            m_root->children.push_back(n);
            return n;
        };

        addRoot(tr("Favorites"), NodeKind::Favorites);
        addRoot(tr("Recent Places"), NodeKind::Recent);
        addRoot(tr("Desktop"), NodeKind::Shortcut,
                QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
        auto *libs = addRoot(tr("Libraries"), NodeKind::Shortcut);
        auto addLib = [libs](const QString &name, QStandardPaths::StandardLocation loc) {
            auto *n = new SideNode;
            n->name = name;
            n->kind = NodeKind::Folder;
            n->path = QStandardPaths::writableLocation(loc);
            n->parent = libs;
            n->populated = true;
            libs->children.push_back(n);
        };
        addLib(tr("Documents"), QStandardPaths::DocumentsLocation);
        addLib(tr("Music"), QStandardPaths::MusicLocation);
        addLib(tr("Pictures"), QStandardPaths::PicturesLocation);
        addLib(tr("Videos"), QStandardPaths::MoviesLocation);
        libs->populated = true;

        addRoot(tr("Computer"), NodeKind::Computer);
    }

    ~ExplorerSidebarModel() override { delete m_root; }

    QModelIndex index(int row, int column, const QModelIndex &parent) const override
    {
        if (column != 0 || row < 0) {
            return {};
        }
        SideNode *p = nodeFromIndex(parent);
        if (!p || row >= p->children.size()) {
            return {};
        }
        return createIndex(row, 0, p->children[row]);
    }

    QModelIndex parent(const QModelIndex &child) const override
    {
        SideNode *n = nodeFromIndex(child);
        if (!n || !n->parent || n->parent == m_root) {
            return {};
        }
        SideNode *gp = n->parent->parent;
        const int row = gp ? gp->children.indexOf(n->parent) : m_root->children.indexOf(n->parent);
        if (row < 0) {
            return {};
        }
        return createIndex(row, 0, n->parent);
    }

    int rowCount(const QModelIndex &parent) const override
    {
        SideNode *n = nodeFromIndex(parent);
        if (!n) {
            return 0;
        }
        const_cast<ExplorerSidebarModel *>(this)->ensurePopulated(n);
        return n->children.size();
    }

    int columnCount(const QModelIndex &) const override { return 1; }

    QVariant data(const QModelIndex &index, int role) const override
    {
        SideNode *n = nodeFromIndex(index);
        if (!n) {
            return {};
        }
        if (role == Qt::DisplayRole) {
            return n->name;
        }
        if (role == Qt::DecorationRole) {
            return iconFor(n);
        }
        if (role == Qt::UserRole) {
            return n->path;
        }
        if (role == Qt::UserRole + 1) {
            return static_cast<int>(n->kind);
        }
        return {};
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        if (!index.isValid()) {
            return Qt::NoItemFlags;
        }
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    }

    bool hasChildren(const QModelIndex &parent) const override
    {
        SideNode *n = nodeFromIndex(parent);
        if (!n) {
            return true;
        }
        if (n->kind == NodeKind::Favorites || n->kind == NodeKind::Recent) {
            const_cast<ExplorerSidebarModel *>(this)->ensurePopulated(n);
            return !n->children.isEmpty();
        }
        if (n->kind == NodeKind::Shortcut && !n->path.isEmpty() && n->children.isEmpty()
            && n->name != tr("Libraries")) {
            return false;
        }
        if (n->kind == NodeKind::Computer || n->kind == NodeKind::Drive || n->kind == NodeKind::Folder) {
            return true;
        }
        if (!n->children.isEmpty()) {
            return true;
        }
        return n->kind == NodeKind::Shortcut && n->name == tr("Libraries");
    }

    QModelIndex indexForPath(const QString &path) const
    {
        if (path.isEmpty()) {
            return {};
        }
        const QString norm = QDir::cleanPath(path);
        return findPath(m_root, norm);
    }

    void refreshRecent()
    {
        for (SideNode *n : m_root->children) {
            if (n->kind == NodeKind::Recent) {
                beginResetModel();
                qDeleteAll(n->children);
                n->children.clear();
                n->populated = false;
                ensurePopulated(n);
                endResetModel();
                return;
            }
        }
    }

    void addFavorite(const QString &path)
    {
        const QString clean = QDir::cleanPath(path);
        if (clean.isEmpty() || !QFileInfo::exists(clean)) {
            return;
        }
        QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
        QStringList favs = s.value(QStringLiteral("explorer/favorites")).toStringList();
        for (const QString &f : favs) {
            if (QDir::cleanPath(f).compare(clean, Qt::CaseInsensitive) == 0) {
                return;
            }
        }
        favs << clean;
        s.setValue(QStringLiteral("explorer/favorites"), favs);
        for (SideNode *n : m_root->children) {
            if (n->kind == NodeKind::Favorites) {
                beginResetModel();
                qDeleteAll(n->children);
                n->children.clear();
                n->populated = false;
                ensurePopulated(n);
                endResetModel();
                return;
            }
        }
    }

private:
    SideNode *m_root = nullptr;
    QFileIconProvider m_icons;

    SideNode *nodeFromIndex(const QModelIndex &index) const
    {
        if (!index.isValid()) {
            return m_root;
        }
        return static_cast<SideNode *>(index.internalPointer());
    }

    QIcon iconFor(SideNode *n) const
    {
        if (n->kind == NodeKind::Favorites) {
            return IconFactory::iconFromSvgBody(
                QStringLiteral("<path d='M8 1.5l1.8 3.7 4.1.6-3 2.9.7 4.1L8 11.2 4.4 13l.7-4.1-3-2.9 4.1-.6z' "
                               "fill='#e0b030'/>"),
                14, QColor(0xe0, 0xb0, 0x30));
        }
        if (n->kind == NodeKind::Computer) {
            return IconFactory::iconFromSvgBody(
                QStringLiteral("<rect x='2' y='3' width='12' height='8' rx='1' fill='none' stroke='currentColor' "
                               "stroke-width='1.2'/><path d='M5 13h6M8 11v2' stroke='currentColor' "
                               "stroke-width='1.2'/>"),
                14);
        }
        if (n->kind == NodeKind::Drive) {
            return m_icons.icon(QFileIconProvider::Drive);
        }
        if (!n->path.isEmpty()) {
            return m_icons.icon(QFileInfo(n->path));
        }
        return m_icons.icon(QFileIconProvider::Folder);
    }

    void ensurePopulated(SideNode *n)
    {
        if (!n || n->populated) {
            return;
        }
        n->populated = true;

        if (n->kind == NodeKind::Favorites) {
            QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
            const QStringList favs = s.value(QStringLiteral("explorer/favorites")).toStringList();
            for (const QString &p : favs) {
                if (!QFileInfo::exists(p)) {
                    continue;
                }
                auto *c = new SideNode;
                c->name = QFileInfo(p).fileName().isEmpty() ? p : QFileInfo(p).fileName();
                c->path = p;
                c->kind = NodeKind::Folder;
                c->parent = n;
                c->populated = false;
                n->children.push_back(c);
            }
            return;
        }

        if (n->kind == NodeKind::Recent) {
            QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
            const QStringList recent = s.value(QStringLiteral("explorer/recent")).toStringList();
            for (const QString &p : recent) {
                if (!QFileInfo::exists(p)) {
                    continue;
                }
                auto *c = new SideNode;
                c->name = QFileInfo(p).fileName().isEmpty() ? p : QFileInfo(p).fileName();
                c->path = p;
                c->kind = NodeKind::Folder;
                c->parent = n;
                c->populated = false;
                n->children.push_back(c);
            }
            return;
        }

        if (n->kind == NodeKind::Computer) {
            const auto vols = QStorageInfo::mountedVolumes();
            for (const QStorageInfo &vol : vols) {
                if (!vol.isValid() || !vol.isReady()) {
                    continue;
                }
                auto *c = new SideNode;
                QString label = vol.displayName();
                if (label.isEmpty()) {
                    label = vol.rootPath();
                }
                c->name = label;
                c->path = vol.rootPath();
                c->kind = NodeKind::Drive;
                c->parent = n;
                n->children.push_back(c);
            }
            return;
        }

        if ((n->kind == NodeKind::Drive || n->kind == NodeKind::Folder) && !n->path.isEmpty()) {
            QDir dir(n->path);
            const auto entries =
                dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
            for (const QFileInfo &fi : entries) {
                auto *c = new SideNode;
                c->name = fi.fileName();
                c->path = fi.absoluteFilePath();
                c->kind = NodeKind::Folder;
                c->parent = n;
                n->children.push_back(c);
            }
        }
    }

    QModelIndex findPath(SideNode *n, const QString &norm) const
    {
        if (!n) {
            return {};
        }
        if (!n->path.isEmpty()
            && QDir::cleanPath(n->path).compare(norm, Qt::CaseInsensitive) == 0) {
            return indexOfNode(n);
        }

        // Prefer Libraries / Desktop / Favorites / Recent before walking drives
        if (n == m_root) {
            for (SideNode *c : n->children) {
                if (c->kind == NodeKind::Computer) {
                    continue;
                }
                QModelIndex found = findPath(c, norm);
                if (found.isValid()) {
                    return found;
                }
            }
            for (SideNode *c : n->children) {
                if (c->kind == NodeKind::Computer) {
                    return findPathUnder(c, norm);
                }
            }
            return {};
        }

        if (n->kind == NodeKind::Favorites || n->kind == NodeKind::Recent
            || (n->kind == NodeKind::Shortcut && n->name == tr("Libraries"))) {
            const_cast<ExplorerSidebarModel *>(this)->ensurePopulated(n);
            for (SideNode *c : n->children) {
                QModelIndex found = findPath(c, norm);
                if (found.isValid()) {
                    return found;
                }
            }
        }
        return {};
    }

    QModelIndex findPathUnder(SideNode *n, const QString &norm) const
    {
        if (!n) {
            return {};
        }
        if (!n->path.isEmpty()
            && QDir::cleanPath(n->path).compare(norm, Qt::CaseInsensitive) == 0) {
            return indexOfNode(n);
        }
        if (n->path.isEmpty() && n->kind != NodeKind::Computer) {
            return {};
        }
        if (!n->path.isEmpty()) {
            const QString base = QDir::cleanPath(n->path);
            if (!norm.startsWith(base, Qt::CaseInsensitive)) {
                return {};
            }
            if (norm.size() > base.size() && !norm.mid(base.size()).startsWith(QLatin1Char('/'))
                && !(base.endsWith(QLatin1Char('/')) || base.endsWith(QLatin1Char('\\')))) {
                // e.g. D: vs D:/foo — allow if base is drive root
                if (!(base.size() == 2 && base.at(1) == QLatin1Char(':'))) {
                    return {};
                }
            }
        }
        const_cast<ExplorerSidebarModel *>(this)->ensurePopulated(n);
        for (SideNode *c : n->children) {
            QModelIndex found = findPathUnder(c, norm);
            if (found.isValid()) {
                return found;
            }
        }
        return {};
    }

    QModelIndex indexOfNode(SideNode *n) const
    {
        if (!n || !n->parent) {
            return {};
        }
        const int row = n->parent->children.indexOf(n);
        if (row < 0) {
            return {};
        }
        return createIndex(row, 0, n);
    }
};

class ExplorerGridDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(104, 92);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        painter->save();
        const QRect r = option.rect.adjusted(2, 2, -2, -2);
        if (option.state & QStyle::State_Selected) {
            painter->fillRect(option.rect.adjusted(1, 1, -1, -1), QColor(0x00, 0x78, 0xd7, 60));
            painter->setPen(QColor(0x00, 0x78, 0xd7));
            painter->drawRect(option.rect.adjusted(1, 1, -1, -1));
        } else if (option.state & QStyle::State_MouseOver) {
            painter->fillRect(option.rect.adjusted(1, 1, -1, -1), QColor(0xff, 0xff, 0xff, 12));
        }

        const QString path = index.data(QFileSystemModel::FilePathRole).toString();
        const QFileInfo fi(path);
        QRect thumb(r.left() + (r.width() - 88) / 2, r.top() + 2, 88, 50);
        if (fi.isDir()) {
            const QIcon ico = makeFolderIcon();
            ico.paint(painter, QRect(r.center().x() - 24, r.top() + 8, 48, 40));
        } else if (isMediaFile(path)) {
            const QString kind = MediaMime::guessKind(path);
            MediaThumbCache::instance().iconFor(path, QSize(88, 50), kind).paint(painter, thumb);
        } else {
            const QIcon ico = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
            ico.paint(painter, thumb);
        }

        QRect textR(r.left(), thumb.bottom() + 4, r.width(), r.height() - thumb.height() - 6);
        painter->setPen(QColor(0xc0, 0xc0, 0xc0));
        QFont f = option.font;
        f.setPixelSize(10);
        painter->setFont(f);
        const QString name = index.data(Qt::DisplayRole).toString();
        painter->drawText(textR, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, name);
        painter->restore();
    }
};

class ExplorerFileList : public QListView {
public:
    using QListView::QListView;

protected:
    void startDrag(Qt::DropActions supportedActions) override
    {
        const QModelIndexList indexes = selectedIndexes();
        QStringList paths;
        for (const QModelIndex &idx : indexes) {
            if (!idx.isValid()) {
                continue;
            }
            const QString path = idx.data(QFileSystemModel::FilePathRole).toString();
            if (path.isEmpty()) {
                continue;
            }
            if (QFileInfo(path).isDir() || MediaMime::isMediaFile(path)) {
                paths << path;
            }
        }
        QMimeData *md = MediaMime::fromLocalPaths(paths);
        if (!md) {
            return;
        }
        auto *drag = new QDrag(this);
        drag->setMimeData(md);
        if (const QModelIndex first = indexes.value(0); first.isValid()) {
            const QIcon ico = qvariant_cast<QIcon>(first.data(Qt::DecorationRole));
            if (!ico.isNull()) {
                const QPixmap pm = ico.pixmap(QSize(88, 50));
                drag->setPixmap(pm);
                drag->setHotSpot(QPoint(pm.width() / 2, pm.height() / 2));
            }
        }
        drag->exec(supportedActions, Qt::CopyAction);
    }
};

ExplorerPane::ExplorerPane(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("explorerPane"));
    buildUi();
    restoreSettings();
}

void ExplorerPane::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_toolbar = new QWidget(this);
    m_toolbar->setObjectName(QStringLiteral("explorerToolbar"));
    m_toolbar->setFixedHeight(26);
    auto *tb = new QHBoxLayout(m_toolbar);
    tb->setContentsMargins(4, 2, 4, 2);
    tb->setSpacing(2);

    auto mkNav = [this, tb](const QString &tip, const QString &svg) {
        auto *b = IconFactory::toolButton(m_toolbar, tip, svg);
        b->setObjectName(QStringLiteral("explorerNavBtn"));
        b->setFixedSize(22, 20);
        tb->addWidget(b);
        return b;
    };
    m_btnBack = mkNav(tr("Back"),
                      QStringLiteral("<path d='M10 3L4 8l6 5' fill='none' stroke='currentColor' "
                                     "stroke-width='1.4' stroke-linecap='round'/>"));
    m_btnForward = mkNav(tr("Forward"),
                         QStringLiteral("<path d='M6 3l6 5-6 5' fill='none' stroke='currentColor' "
                                        "stroke-width='1.4' stroke-linecap='round'/>"));
    m_btnUp = mkNav(tr("Up one level"),
                    QStringLiteral("<path d='M3 10l5-6 5 6M8 4v9' fill='none' stroke='currentColor' "
                                   "stroke-width='1.3' stroke-linecap='round'/>"));
    m_btnRefresh = mkNav(tr("Refresh"),
                         QStringLiteral("<path d='M12.5 8a4.5 4.5 0 11-1.3-3.1M11 2.5v3h3' fill='none' "
                                        "stroke='currentColor' stroke-width='1.3'/>"));

    connect(m_btnBack, &QToolButton::clicked, this, &ExplorerPane::goBack);
    connect(m_btnForward, &QToolButton::clicked, this, &ExplorerPane::goForward);
    connect(m_btnUp, &QToolButton::clicked, this, &ExplorerPane::goUp);
    connect(m_btnRefresh, &QToolButton::clicked, this, &ExplorerPane::refresh);

    auto *sep = new QFrame(m_toolbar);
    sep->setObjectName(QStringLiteral("toolbarSep"));
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedWidth(6);
    tb->addWidget(sep);

    auto *pathHost = new QWidget(m_toolbar);
    pathHost->setObjectName(QStringLiteral("explorerPath"));
    m_pathLay = new QHBoxLayout(pathHost);
    m_pathLay->setContentsMargins(6, 0, 6, 0);
    m_pathLay->setSpacing(2);
    tb->addWidget(pathHost, 1);

    tb->addWidget(IconFactory::toolButton(m_toolbar, tr("Views"), IconFactory::svgViews()));
    tb->addWidget(IconFactory::toolButton(m_toolbar, tr("Search"), IconFactory::svgSearch()));

    root->addWidget(m_toolbar);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("explorerSplitter"));
    m_splitter->setHandleWidth(3);
    m_splitter->setChildrenCollapsible(false);

    m_sidebar = new ExplorerSidebarModel(this);
    m_tree = new QTreeView(m_splitter);
    m_tree->setObjectName(QStringLiteral("explorerTree"));
    m_tree->setModel(m_sidebar);
    m_tree->setHeaderHidden(true);
    m_tree->setAnimated(true);
    m_tree->setIndentation(14);
    m_tree->setIconSize(QSize(14, 14));
    m_tree->setUniformRowHeights(true);
    m_tree->setExpandsOnDoubleClick(true);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_tree, &QTreeView::clicked, this, &ExplorerPane::onTreeClicked);
    connect(m_tree, &QTreeView::activated, this, &ExplorerPane::onTreeActivated);

    // Expand Computer by default
    for (int i = 0; i < m_sidebar->rowCount({}); ++i) {
        const QModelIndex idx = m_sidebar->index(i, 0, {});
        if (idx.data(Qt::DisplayRole).toString() == tr("Computer")) {
            m_tree->expand(idx);
            break;
        }
    }

    m_listFs = new QFileSystemModel(this);
    m_listFs->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    m_listFs->setReadOnly(true);

    m_list = new ExplorerFileList(m_splitter);
    m_list->setObjectName(QStringLiteral("explorerGrid"));
    m_list->setModel(m_listFs);
    m_list->setViewMode(QListView::IconMode);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setUniformItemSizes(true);
    m_list->setSpacing(8);
    m_list->setIconSize(QSize(88, 50));
    m_list->setGridSize(QSize(104, 92));
    m_list->setWordWrap(true);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);
    m_list->setDefaultDropAction(Qt::CopyAction);
    m_list->setItemDelegate(new ExplorerGridDelegate(m_list));
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListView::activated, this, &ExplorerPane::onListActivated);
    connect(m_list, &QListView::doubleClicked, this, &ExplorerPane::onListDoubleClicked);
    connect(m_list, &QWidget::customContextMenuRequested, this, &ExplorerPane::onListContextMenu);
    connect(m_tree, &QWidget::customContextMenuRequested, this, &ExplorerPane::onTreeContextMenu);
    connect(&MediaThumbCache::instance(), &MediaThumbCache::thumbnailReady, this,
            &ExplorerPane::onThumbnailReady);

    m_splitter->addWidget(m_tree);
    m_splitter->addWidget(m_list);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({190, 500});

    root->addWidget(m_splitter, 1);
}

void ExplorerPane::saveSettings() const
{
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    if (!m_currentPath.isEmpty()) {
        s.setValue(QStringLiteral("explorer/path"), m_currentPath);
    }
    if (m_splitter) {
        s.setValue(QStringLiteral("explorer/splitter"), m_splitter->saveState());
    }
}

void ExplorerPane::restoreSettings()
{
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    if (m_splitter) {
        const QByteArray st = s.value(QStringLiteral("explorer/splitter")).toByteArray();
        if (!st.isEmpty()) {
            m_splitter->restoreState(st);
        }
    }
    const QString path = s.value(QStringLiteral("explorer/path")).toString();
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        navigateTo(path, true);
    } else {
        navigateTo(defaultStartPath(), true);
    }
}

QString ExplorerPane::defaultStartPath() const
{
    const QString movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (!movies.isEmpty() && QDir(movies).exists()) {
        return movies;
    }
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (!home.isEmpty()) {
        return home;
    }
    return QDir::rootPath();
}

void ExplorerPane::navigateTo(const QString &path, bool recordHistory)
{
    QString clean = QDir::cleanPath(path);
    if (clean.isEmpty() || !QFileInfo::exists(clean)) {
        return;
    }
    if (QFileInfo(clean).isFile()) {
        clean = QFileInfo(clean).absolutePath();
    }

    m_currentPath = clean;
    const QModelIndex root = m_listFs->setRootPath(clean);
    m_list->setRootIndex(root);

    if (recordHistory) {
        if (m_historyIndex >= 0 && m_historyIndex < m_history.size() - 1) {
            m_history = m_history.mid(0, m_historyIndex + 1);
        }
        if (m_history.isEmpty() || m_history.last() != clean) {
            m_history.push_back(clean);
            m_historyIndex = m_history.size() - 1;
        }
        // Persist recent folders
        QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
        QStringList recent = s.value(QStringLiteral("explorer/recent")).toStringList();
        recent.removeAll(clean);
        recent.prepend(clean);
        while (recent.size() > 12) {
            recent.removeLast();
        }
        s.setValue(QStringLiteral("explorer/recent"), recent);
    }

    updateBreadcrumb();
    updateNavButtons();

    const QModelIndex treeIdx = m_sidebar->indexForPath(clean);
    if (treeIdx.isValid()) {
        QModelIndex p = treeIdx.parent();
        while (p.isValid()) {
            m_tree->expand(p);
            p = p.parent();
        }
        m_tree->setCurrentIndex(treeIdx);
        m_tree->scrollTo(treeIdx);
    }
}

void ExplorerPane::updateBreadcrumb()
{
    rebuildBreadcrumbButtons();
}

void ExplorerPane::rebuildBreadcrumbButtons()
{
    while (QLayoutItem *it = m_pathLay->takeAt(0)) {
        if (auto *w = it->widget()) {
            w->deleteLater();
        }
        delete it;
    }

    auto addCrumb = [this](const QString &label, const QString &path, bool current) {
        if (m_pathLay->count() > 0) {
            auto *sep = new QLabel(QStringLiteral("›"), m_pathLay->parentWidget());
            sep->setObjectName(QStringLiteral("explorerPathSep"));
            m_pathLay->addWidget(sep);
        }
        auto *btn = new QToolButton(m_pathLay->parentWidget());
        btn->setObjectName(current ? QStringLiteral("explorerCrumbCurrent")
                                   : QStringLiteral("explorerCrumb"));
        btn->setText(label);
        btn->setAutoRaise(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(path.isEmpty() ? label : path);
        if (!current && !path.isEmpty()) {
            connect(btn, &QToolButton::clicked, this, [this, path]() { navigateTo(path); });
        }
        m_pathLay->addWidget(btn);
    };

    addCrumb(tr("Computer"), QString(), m_currentPath.isEmpty());

    QString accumulated;
    QString remain = QDir::fromNativeSeparators(m_currentPath);

#ifdef Q_OS_WIN
    if (remain.size() >= 2 && remain.at(1) == QLatin1Char(':')) {
        accumulated = remain.left(2) + QLatin1Char('/'); // D:/
        QString driveLabel = accumulated;
        for (const QStorageInfo &vol : QStorageInfo::mountedVolumes()) {
            if (QDir::cleanPath(vol.rootPath()).compare(QDir::cleanPath(accumulated),
                                                        Qt::CaseInsensitive)
                == 0) {
                driveLabel = vol.displayName().isEmpty() ? accumulated : vol.displayName();
                break;
            }
        }
        const bool onlyDrive = QDir::cleanPath(remain).length() <= 3;
        addCrumb(driveLabel, QDir::cleanPath(accumulated), onlyDrive);
        remain = remain.mid(2);
        while (remain.startsWith(QLatin1Char('/'))) {
            remain = remain.mid(1);
        }
    }
#else
    accumulated = QStringLiteral("/");
    addCrumb(QStringLiteral("/"), accumulated, remain == QLatin1String("/") || remain.isEmpty());
    if (remain.startsWith(QLatin1Char('/'))) {
        remain = remain.mid(1);
    }
#endif

    const QStringList parts = remain.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (int i = 0; i < parts.size(); ++i) {
#ifdef Q_OS_WIN
        if (accumulated.endsWith(QLatin1Char('/'))) {
            accumulated += parts[i];
        } else {
            accumulated += QLatin1Char('/') + parts[i];
        }
#else
        if (accumulated == QLatin1String("/")) {
            accumulated += parts[i];
        } else {
            accumulated += QLatin1Char('/') + parts[i];
        }
#endif
        addCrumb(parts[i], QDir::cleanPath(accumulated), i == parts.size() - 1);
    }

    m_pathLay->addStretch(1);
}

void ExplorerPane::updateNavButtons()
{
    m_btnBack->setEnabled(m_historyIndex > 0);
    m_btnForward->setEnabled(m_historyIndex >= 0 && m_historyIndex < m_history.size() - 1);
    QDir dir(m_currentPath);
    m_btnUp->setEnabled(dir.cdUp());
}

void ExplorerPane::goBack()
{
    if (m_historyIndex <= 0) {
        return;
    }
    --m_historyIndex;
    navigateTo(m_history[m_historyIndex], false);
}

void ExplorerPane::goForward()
{
    if (m_historyIndex < 0 || m_historyIndex >= m_history.size() - 1) {
        return;
    }
    ++m_historyIndex;
    navigateTo(m_history[m_historyIndex], false);
}

void ExplorerPane::goUp()
{
    QDir dir(m_currentPath);
    if (dir.cdUp()) {
        navigateTo(dir.absolutePath());
    }
}

void ExplorerPane::refresh()
{
    if (!m_currentPath.isEmpty()) {
        m_listFs->setRootPath(QString());
        navigateTo(m_currentPath, false);
    }
    m_sidebar->refreshRecent();
}

void ExplorerPane::onTreeClicked(const QModelIndex &index)
{
    onTreeActivated(index);
}

void ExplorerPane::onTreeActivated(const QModelIndex &index)
{
    const QString path = index.data(Qt::UserRole).toString();
    if (!path.isEmpty()) {
        navigateTo(path);
    }
}

void ExplorerPane::onListActivated(const QModelIndex &index)
{
    onListDoubleClicked(index);
}

void ExplorerPane::onListDoubleClicked(const QModelIndex &index)
{
    const QString path = m_listFs->filePath(index);
    if (path.isEmpty()) {
        return;
    }
    if (QFileInfo(path).isDir()) {
        navigateTo(path);
        return;
    }
    if (isMediaFile(path)) {
        emit mediaActivated(path);
        emit importRequested({path});
    }
}

void ExplorerPane::onThumbnailReady(const QString &)
{
    if (m_list && m_list->viewport()) {
        m_list->viewport()->update();
    }
}

QStringList ExplorerPane::selectedListPaths() const
{
    QStringList paths;
    if (!m_list || !m_listFs) {
        return paths;
    }
    const QModelIndexList idxs = m_list->selectionModel() ? m_list->selectionModel()->selectedIndexes()
                                                          : QModelIndexList{};
    for (const QModelIndex &idx : idxs) {
        if (!idx.isValid() || idx.column() != 0) {
            continue;
        }
        const QString path = m_listFs->filePath(idx);
        if (!path.isEmpty()) {
            paths << path;
        }
    }
    paths.removeDuplicates();
    return paths;
}

void ExplorerPane::onListContextMenu(const QPoint &pos)
{
    if (!m_list) {
        return;
    }
    const QModelIndex idx = m_list->indexAt(pos);
    const QPoint global = m_list->viewport()->mapToGlobal(pos);
    if (!idx.isValid()) {
        showEmptyAreaMenu(global);
        return;
    }
    if (m_list->selectionModel() && !m_list->selectionModel()->isSelected(idx)) {
        m_list->setCurrentIndex(idx);
    }
    const QStringList paths = selectedListPaths();
    if (paths.isEmpty()) {
        showEmptyAreaMenu(global);
        return;
    }
    const QFileInfo fi(paths.first());
    if (paths.size() == 1 && fi.isDir()) {
        showFolderMenu(paths.first(), global);
    } else if (std::all_of(paths.cbegin(), paths.cend(), [](const QString &p) { return isMediaFile(p); })) {
        showFileMenu(paths, global);
    } else if (paths.size() == 1) {
        showFolderMenu(paths.first(), global);
    } else {
        showEmptyAreaMenu(global);
    }
}

void ExplorerPane::onTreeContextMenu(const QPoint &pos)
{
    if (!m_tree) {
        return;
    }
    const QModelIndex idx = m_tree->indexAt(pos);
    const QPoint global = m_tree->viewport()->mapToGlobal(pos);
    if (!idx.isValid()) {
        showEmptyAreaMenu(global);
        return;
    }
    const QString path = idx.data(Qt::UserRole).toString();
    if (path.isEmpty()) {
        showEmptyAreaMenu(global);
        return;
    }
    showFolderMenu(path, global);
}

void ExplorerPane::showEmptyAreaMenu(const QPoint &globalPos)
{
    QMenu menu(this);
    menu.addAction(tr("Refresh"), this, &ExplorerPane::refresh);
    auto *renameAct = menu.addAction(tr("Rename"));
    renameAct->setShortcut(Qt::Key_F2);
    renameAct->setEnabled(false);
    auto *cutAct = menu.addAction(tr("Cut"));
    cutAct->setShortcut(QKeySequence::Cut);
    cutAct->setEnabled(false);
    auto *copyAct = menu.addAction(tr("Copy"));
    copyAct->setShortcut(QKeySequence::Copy);
    copyAct->setEnabled(false);
    auto *removeAct = menu.addAction(tr("Remove"));
    removeAct->setShortcut(QKeySequence::Delete);
    removeAct->setEnabled(false);
    menu.addSeparator();
    menu.addAction(tr("Add to Favorites"), this, [this]() { addFavorite(m_currentPath); });
    menu.addSeparator();
    menu.addAction(tr("Explorer..."), this, [this]() { openInOsExplorer(m_currentPath); });
    menu.exec(globalPos);
}

void ExplorerPane::showFolderMenu(const QString &folderPath, const QPoint &globalPos)
{
    QMenu menu(this);
    menu.addAction(tr("Refresh"), this, &ExplorerPane::refresh);
    auto *renameAct = menu.addAction(tr("Rename"), this, [this, folderPath]() { renamePath(folderPath); });
    renameAct->setShortcut(Qt::Key_F2);
    auto *cutAct = menu.addAction(tr("Cut"), this, [this, folderPath]() { copyPaths({folderPath}, true); });
    cutAct->setShortcut(QKeySequence::Cut);
    auto *copyAct =
        menu.addAction(tr("Copy"), this, [this, folderPath]() { copyPaths({folderPath}, false); });
    copyAct->setShortcut(QKeySequence::Copy);
    auto *removeAct =
        menu.addAction(tr("Remove"), this, [this, folderPath]() { removePaths({folderPath}); });
    removeAct->setShortcut(QKeySequence::Delete);
    menu.addSeparator();
    menu.addAction(tr("Add to Favorites"), this, [this, folderPath]() { addFavorite(folderPath); });
    menu.addSeparator();
    menu.addAction(tr("Explorer..."), this, [this, folderPath]() { openInOsExplorer(folderPath); });
    menu.exec(globalPos);
}

void ExplorerPane::showFileMenu(const QStringList &paths, const QPoint &globalPos)
{
    if (paths.isEmpty()) {
        return;
    }
    const QString primary = paths.first();
    QMenu menu(this);
    menu.addAction(tr("Add to Timeline"), this, [this, paths]() { emit addToTimelineRequested(paths); });
    menu.addAction(tr("Add to Project Media"), this, [this, paths]() { emit importRequested(paths); });
    menu.addSeparator();
    auto *previewAct =
        menu.addAction(tr("Start Preview"), this, [this, primary]() { emit previewMediaRequested(primary); });
    previewAct->setShortcut(Qt::Key_Return);
    previewAct->setEnabled(paths.size() == 1);
    auto *trimAct =
        menu.addAction(tr("Trim"), this, [this, primary]() { emit trimMediaRequested(primary); });
    trimAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
    trimAct->setEnabled(paths.size() == 1);
    menu.addSeparator();
    menu.addAction(tr("Refresh"), this, &ExplorerPane::refresh);
    auto *renameAct = menu.addAction(tr("Rename"), this, [this, primary]() { renamePath(primary); });
    renameAct->setShortcut(Qt::Key_F2);
    renameAct->setEnabled(paths.size() == 1);
    auto *cutAct = menu.addAction(tr("Cut"), this, [this, paths]() { copyPaths(paths, true); });
    cutAct->setShortcut(QKeySequence::Cut);
    auto *copyAct = menu.addAction(tr("Copy"), this, [this, paths]() { copyPaths(paths, false); });
    copyAct->setShortcut(QKeySequence::Copy);
    auto *removeAct = menu.addAction(tr("Remove"), this, [this, paths]() { removePaths(paths); });
    removeAct->setShortcut(QKeySequence::Delete);
    menu.addSeparator();
    menu.addAction(tr("Explorer..."), this, [this, primary]() { openInOsExplorer(primary); });
    auto *propsAct =
        menu.addAction(tr("Properties"), this, [this, primary]() { showProperties(primary); });
    propsAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
    propsAct->setEnabled(paths.size() == 1);
    menu.exec(globalPos);
}

void ExplorerPane::renamePath(const QString &path)
{
    const QFileInfo fi(path);
    if (!fi.exists()) {
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename"), tr("New name:"), QLineEdit::Normal,
                                               fi.fileName(), &ok);
    if (!ok || name.isEmpty() || name == fi.fileName()) {
        return;
    }
    const QString dest = fi.dir().absoluteFilePath(name);
    if (!QFile::rename(path, dest)) {
        QMessageBox::warning(this, tr("Rename"), tr("Could not rename:\n%1").arg(path));
        return;
    }
    MediaThumbCache::instance().invalidate(path);
    refresh();
}

void ExplorerPane::removePaths(const QStringList &paths)
{
    if (paths.isEmpty()) {
        return;
    }
    const auto reply = QMessageBox::question(
        this, tr("Remove"),
        paths.size() == 1 ? tr("Permanently delete:\n%1?").arg(paths.first())
                          : tr("Permanently delete %1 items?").arg(paths.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }
    for (const QString &path : paths) {
        QFileInfo fi(path);
        bool ok = false;
        if (fi.isDir()) {
            ok = QDir(path).removeRecursively();
        } else {
            ok = QFile::remove(path);
        }
        if (ok) {
            MediaThumbCache::instance().invalidate(path);
        }
    }
    refresh();
}

void ExplorerPane::copyPaths(const QStringList &paths, bool cut)
{
    Q_UNUSED(cut);
    auto *md = new QMimeData;
    QList<QUrl> urls;
    for (const QString &p : paths) {
        urls << QUrl::fromLocalFile(p);
    }
    md->setUrls(urls);
    QApplication::clipboard()->setMimeData(md);
}

void ExplorerPane::openInOsExplorer(const QString &path)
{
    const QFileInfo fi(path);
    const QString target = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
    if (!target.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(target));
    }
}

void ExplorerPane::showProperties(const QString &path)
{
    const QFileInfo fi(path);
    const QString text =
        tr("Name: %1\nPath: %2\nSize: %3 bytes\nModified: %4")
            .arg(fi.fileName(), fi.absoluteFilePath(), QString::number(fi.size()),
                 fi.lastModified().toString(QStringLiteral("yyyy-MM-dd hh:mm")));
    QMessageBox::information(this, tr("Properties"), text);
}

void ExplorerPane::addFavorite(const QString &path)
{
    if (m_sidebar) {
        m_sidebar->addFavorite(path);
    }
}

} // namespace openvegas
