#include "ui/TransitionsPane.h"

#include "io/MediaMime.h"
#include "video/TransitionApply.h"

#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QCursor>
#include <QDateTime>
#include <QDrag>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QListView>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>

#include <algorithm>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <utility>

namespace openvegas {

namespace {

QColor accent() { return QColor(0x1a, 0x4a, 0x8a); }
QColor light() { return QColor(0xc0, 0xd0, 0xe0); }

/** Preset tiles carry the transition group id + preset name for drag-out. */
constexpr int kDragPluginIdRole = Qt::UserRole + 20;
constexpr int kDragPresetRole = Qt::UserRole + 21;

/** Static poster frame of a preset tile — mid-transition, like Vegas's own thumbnails. */
constexpr double kPosterProgress = 0.45;

/**
 * Drag-out list for the preset grid. Drives press → move → QDrag by hand rather than
 * relying on QAbstractItemView's built-in drag detection, which was measured (see
 * ISSUES_AND_PLANS.md 2026-08-10) never to fire for IconMode tiles like these.
 */
class TransitionDragListWidget : public QListWidget {
public:
    explicit TransitionDragListWidget(QWidget *parent = nullptr)
        : QListWidget(parent)
    {
        setDragDropMode(QAbstractItemView::DragOnly);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_pressItem = itemAt(event->pos());
            m_pressPos = event->pos();
        }
        QListWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_pressItem || !(event->buttons() & Qt::LeftButton)
            || (event->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance()) {
            QListWidget::mouseMoveEvent(event);
            return;
        }
        QListWidgetItem *item = m_pressItem;
        m_pressItem = nullptr; // one-shot until the next press
        const QString pluginId = item->data(kDragPluginIdRole).toString();
        if (pluginId.isEmpty()) {
            return; // group without a real renderer — nothing to drop yet
        }
        // extra = plugin id, name = preset; the timeline turns that into an instance.
        QMimeData *md = MediaMime::fromSynthetic(QStringLiteral("transition"),
                                                 item->data(kDragPresetRole).toString(), pluginId);
        auto *drag = new QDrag(this);
        drag->setMimeData(md);
        const QIcon icon = item->icon();
        if (!icon.isNull()) {
            const QPixmap pm = icon.pixmap(QSize(96, 58));
            drag->setPixmap(pm);
            drag->setHotSpot(QPoint(pm.width() / 2, pm.height() / 2));
        }
        drag->exec(Qt::CopyAction, Qt::CopyAction);
    }

private:
    QListWidgetItem *m_pressItem = nullptr;
    QPoint m_pressPos;
};

TransitionsPane::Preset makePreset(const QString &name, TransitionsPane::Thumb t,
                                   QColor a = accent())
{
    TransitionsPane::Preset p;
    p.name = name;
    p.thumb = t;
    p.accent = a;
    return p;
}

} // namespace

TransitionsPane::TransitionsPane(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("transitionsPane"));
    m_activeCategory = QStringLiteral("All Plug-ins");
    loadCatalog();
    loadFavorites();
    buildUi();
    rebuildPluginList();
    restoreSettings();
}

void TransitionsPane::loadCatalog()
{
    m_plugins.clear();

    // Chips and placeholder art, the two things the shared catalog has no opinion about.
    // Everything else — which groups exist, what they are called, which presets they
    // offer — comes from transitionCatalog(). Keeping a second hand-written list here is
    // what left Star Wipe showing "(Default) / Variant A / Variant B" against eighteen
    // real presets, and dropped Venetian Blinds off the list altogether.
    struct Chrome {
        const char *name;
        Thumb thumb;
        const char *categories; // comma separated
    };
    static const Chrome kChrome[] = {
        {"3D Blinds", Thumb::SimpleBlinds, "3D Effects,Reveals"},
        {"3D Cascade", Thumb::Push, "3D Effects"},
        {"3D Fly In/Out", Thumb::Push, "3D Effects"},
        {"3D Shuffle", Thumb::SlotMachine, "3D Effects"},
        {"Barn Door", Thumb::Wipe, "Reveals,Wipes"},
        {"Clock Wipe", Thumb::Iris, "Wipes"},
        {"Cross Effect", Thumb::Dissolve, "Fades,Reveals"},
        {"Dissolve", Thumb::Dissolve, "Fades"},
        {"Flash", Thumb::Dissolve, "Fades"},
        {"GL Transition", Thumb::Spin, "3D Effects,Reveals"},
        {"Gradient Wipe", Thumb::Wipe, "Wipes"},
        {"Iris", Thumb::Iris, "Wipes,Reveals"},
        {"Linear Wipe", Thumb::Wipe, "Wipes"},
        {"Page Loop", Thumb::Page, "Loops and Peels"},
        {"Page Peel", Thumb::Page, "Loops and Peels"},
        {"Page Roll", Thumb::Page, "Loops and Peels"},
        {"Portals", Thumb::Iris, "Reveals,3D Effects"},
        {"Push", Thumb::Push, "Reveals"},
        {"Slide", Thumb::Push, "Reveals,Wipes"},
        {"Spiral", Thumb::Spin, "Wipes,Reveals"},
        {"Split", Thumb::Wipe, "Wipes,Reveals"},
        {"Squeeze", Thumb::Push, "Reveals"},
        {"Star Wipe", Thumb::Iris, "Wipes"},
        {"Swap", Thumb::Push, "Reveals"},
        {"Venetian Blinds", Thumb::SimpleBlinds, "Reveals,Wipes"},
        {"Warp Flow", Thumb::Spin, "3D Effects,Reveals"},
        {"Zoom", Thumb::Iris, "Reveals,3D Effects"},
    };

    for (const TransitionPluginInfo &info : transitionCatalog()) {
        Plugin p;
        p.name = info.name;
        p.format = info.format;
        p.description = info.description.isEmpty() ? tr("Transition plug-in.")
                                                   : info.description;
        p.pluginId = info.id;

        Thumb thumb = Thumb::Gradient;
        for (const Chrome &c : kChrome) {
            if (info.name == QLatin1String(c.name)) {
                thumb = c.thumb;
                p.categories = QString::fromLatin1(c.categories).split(QLatin1Char(','));
                break;
            }
        }
        for (const TransitionPresetInfo &pr : info.presets) {
            p.presets.push_back(makePreset(pr.name, thumb));
        }
        if (p.presets.isEmpty()) {
            // A group VEGAS ships no presets for still needs one tile to drag.
            p.presets.push_back(makePreset(tr("(Default)"), thumb));
        }
        m_plugins.push_back(std::move(p));
    }

    std::sort(m_plugins.begin(), m_plugins.end(), [](const Plugin &a, const Plugin &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
}

void TransitionsPane::loadFavorites()
{
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    const QStringList favs = s.value(QStringLiteral("transitions/favorites")).toStringList();
    for (Plugin &p : m_plugins) {
        p.favorite = favs.contains(p.name);
    }
}

void TransitionsPane::saveFavorites()
{
    QStringList favs;
    for (const Plugin &p : m_plugins) {
        if (p.favorite) {
            favs << p.name;
        }
    }
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    s.setValue(QStringLiteral("transitions/favorites"), favs);
}

void TransitionsPane::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *tb = new QWidget(this);
    tb->setObjectName(QStringLiteral("fxToolbar"));
    tb->setFixedHeight(28);
    auto *tbLay = new QHBoxLayout(tb);
    tbLay->setContentsMargins(6, 4, 6, 4);

    auto *searchHost = new QWidget(tb);
    searchHost->setObjectName(QStringLiteral("fxSearch"));
    auto *searchLay = new QHBoxLayout(searchHost);
    searchLay->setContentsMargins(8, 0, 8, 0);
    searchLay->setSpacing(6);
    auto *searchIco = new QLabel(QStringLiteral("⌕"), searchHost);
    searchIco->setObjectName(QStringLiteral("fxSearchIco"));
    m_search = new QLineEdit(searchHost);
    m_search->setObjectName(QStringLiteral("fxSearchEdit"));
    m_search->setPlaceholderText(tr("Search..."));
    m_search->setClearButtonEnabled(true);
    m_search->setFrame(false);
    searchLay->addWidget(searchIco);
    searchLay->addWidget(m_search, 1);
    tbLay->addWidget(searchHost);
    root->addWidget(tb);

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &) {
        applySearchAndCategory();
    });

    auto *chipsScroll = new QScrollArea(this);
    chipsScroll->setObjectName(QStringLiteral("fxChipsScroll"));
    chipsScroll->setWidgetResizable(true);
    chipsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    chipsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    chipsScroll->setFixedHeight(28);
    chipsScroll->setFrameShape(QFrame::NoFrame);

    auto *chipsHost = new QWidget;
    chipsHost->setObjectName(QStringLiteral("fxChips"));
    auto *chipsLay = new QHBoxLayout(chipsHost);
    chipsLay->setContentsMargins(6, 4, 6, 4);
    chipsLay->setSpacing(2);
    m_chipGroup = new QButtonGroup(this);
    m_chipGroup->setExclusive(true);

    // Categories from Vegas screenshot (+ Loops and Peels from SAMPLES)
    const QStringList chips = {QStringLiteral("All Plug-ins"), QStringLiteral("3D Effects"),
                               QStringLiteral("Reveals"),      QStringLiteral("Wipes"),
                               QStringLiteral("Fades"),        QStringLiteral("Loops and Peels"),
                               QStringLiteral("★")};
    for (int i = 0; i < chips.size(); ++i) {
        auto *btn = new QPushButton(chips[i], chipsHost);
        btn->setObjectName(chips[i] == QLatin1String("★") ? QStringLiteral("fxChipStar")
                                                          : QStringLiteral("fxChip"));
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFlat(true);
        if (i == 0) {
            btn->setChecked(true);
        }
        m_chipGroup->addButton(btn, i);
        chipsLay->addWidget(btn);
    }
    chipsLay->addStretch(1);
    chipsScroll->setWidget(chipsHost);
    root->addWidget(chipsScroll);

    connect(m_chipGroup, &QButtonGroup::idClicked, this, [this, chips](int id) {
        if (id >= 0 && id < chips.size()) {
            m_activeCategory = chips[id];
            applySearchAndCategory();
        }
    });

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("fxSplitter"));
    m_splitter->setHandleWidth(3);
    m_splitter->setChildrenCollapsible(false);

    m_pluginList = new QListWidget(m_splitter);
    m_pluginList->setObjectName(QStringLiteral("fxPluginList"));
    m_pluginList->setUniformItemSizes(true);
    m_pluginList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *mainCol = new QWidget(m_splitter);
    mainCol->setObjectName(QStringLiteral("fxMain"));
    auto *mainLay = new QVBoxLayout(mainCol);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);

    m_presetGrid = new TransitionDragListWidget(mainCol);
    m_presetGrid->setObjectName(QStringLiteral("fxPresetGrid"));
    m_presetGrid->setViewMode(QListView::IconMode);
    m_presetGrid->setResizeMode(QListView::Adjust);
    m_presetGrid->setMovement(QListView::Static);
    m_presetGrid->setUniformItemSizes(true);
    m_presetGrid->setSpacing(10);
    m_presetGrid->setIconSize(QSize(130, 78));
    m_presetGrid->setGridSize(QSize(140, 104));
    m_presetGrid->setWordWrap(true);
    m_presetGrid->setSelectionMode(QAbstractItemView::SingleSelection);
    m_presetGrid->setMouseTracking(true);
    mainLay->addWidget(m_presetGrid, 1);

    // Hovering a tile plays the transition as a looping demo, like Vegas's own dock.
    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setInterval(66); // ~15 fps is plenty for a 130x78 thumbnail
    connect(m_hoverTimer, &QTimer::timeout, this, &TransitionsPane::onHoverTick);
    connect(m_presetGrid, &QListWidget::entered, this, &TransitionsPane::onPresetHoverEntered);
    connect(m_presetGrid, &QListWidget::itemPressed, this, [this](QListWidgetItem *item) {
        // Stop repainting the icon the moment a press might turn into a drag: the drag
        // pixmap must be a stable frame, and the model must not mutate mid-gesture.
        if (!m_hoverTimer->isActive()) {
            return;
        }
        m_hoverTimer->stop();
        if (item && m_hoverRow >= 0) {
            item->setIcon(presetIconAt(m_hoverRow, kPosterProgress));
        }
        m_hoverRow = -1;
    });

    auto *meta = new QWidget(mainCol);
    meta->setObjectName(QStringLiteral("fxMeta"));
    auto *metaLay = new QVBoxLayout(meta);
    metaLay->setContentsMargins(8, 4, 8, 4);
    metaLay->setSpacing(2);
    m_metaLine1 = new QLabel(meta);
    m_metaLine1->setObjectName(QStringLiteral("fxMetaLine"));
    m_metaLine1->setWordWrap(true);
    m_metaLine2 = new QLabel(meta);
    m_metaLine2->setObjectName(QStringLiteral("fxMetaLine"));
    m_metaLine2->setWordWrap(true);
    metaLay->addWidget(m_metaLine1);
    metaLay->addWidget(m_metaLine2);
    mainLay->addWidget(meta);

    m_splitter->addWidget(m_pluginList);
    m_splitter->addWidget(mainCol);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({168, 520});
    root->addWidget(m_splitter, 1);

    connect(m_pluginList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *cur, QListWidgetItem *) {
                if (cur) {
                    showPlugin(cur->data(Qt::UserRole).toInt());
                }
            });
    connect(m_pluginList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        if (m_currentIndex >= 0) {
            emit transitionActivated(m_plugins[m_currentIndex].name);
        }
    });
    connect(m_presetGrid, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (!item || m_currentIndex < 0) {
            return;
        }
        emit presetActivated(m_plugins[m_currentIndex].name, item->text());
    });
    connect(m_pluginList, &QListWidget::itemPressed, this, [this](QListWidgetItem *item) {
        if (!item) {
            return;
        }
        const QPoint pos = m_pluginList->mapFromGlobal(QCursor::pos());
        const QRect r = m_pluginList->visualItemRect(item);
        if (pos.x() - r.left() < 18) {
            const int idx = item->data(Qt::UserRole).toInt();
            if (idx >= 0 && idx < m_plugins.size()) {
                m_plugins[idx].favorite = !m_plugins[idx].favorite;
                saveFavorites();
                const bool sel = (idx == m_currentIndex);
                applySearchAndCategory();
                if (sel) {
                    for (int i = 0; i < m_pluginList->count(); ++i) {
                        if (m_pluginList->item(i)->data(Qt::UserRole).toInt() == idx) {
                            m_pluginList->setCurrentRow(i);
                            break;
                        }
                    }
                }
            }
        }
    });
}

QIcon TransitionsPane::presetIconAt(int row, double progress) const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_plugins.size()) {
        return QIcon();
    }
    const Plugin &plugin = m_plugins[m_currentIndex];
    if (row < 0 || row >= plugin.presets.size()) {
        return QIcon();
    }
    if (plugin.pluginId.isEmpty()) {
        // Group without a renderer yet — keep the hand-drawn placeholder art.
        return presetIcon(plugin.presets[row]);
    }
    const TransitionInstance t =
        makeTransitionInstance(plugin.pluginId, plugin.presets[row].name);
    return QIcon(QPixmap::fromImage(renderTransitionPreview(t, QSize(130, 78), progress)));
}

void TransitionsPane::onPresetHoverEntered(const QModelIndex &index)
{
    if (!index.isValid() || m_currentIndex < 0 || m_currentIndex >= m_plugins.size()) {
        return;
    }
    if (m_plugins[m_currentIndex].pluginId.isEmpty()) {
        return; // nothing to animate without a real renderer
    }
    m_hoverRow = index.row();
    m_hoverStartMs = QDateTime::currentMSecsSinceEpoch();
    m_hoverTimer->start();
}

void TransitionsPane::onHoverTick()
{
    if (m_hoverRow < 0 || m_currentIndex < 0 || m_currentIndex >= m_plugins.size()) {
        m_hoverTimer->stop();
        return;
    }
    QListWidgetItem *item = m_presetGrid->item(m_hoverRow);
    if (!item) {
        m_hoverTimer->stop();
        m_hoverRow = -1;
        return;
    }
    // Ground-truth the cursor each tick instead of trusting enter/leave coverage: moving to
    // another tile, to empty space, or off the widget all have to restore the poster.
    const QPoint viewportPos = m_presetGrid->viewport()->mapFromGlobal(QCursor::pos());
    const QModelIndex under = m_presetGrid->indexAt(viewportPos);
    if (!under.isValid() || under.row() != m_hoverRow) {
        item->setIcon(presetIconAt(m_hoverRow, kPosterProgress));
        m_hoverTimer->stop();
        m_hoverRow = -1;
        return;
    }
    constexpr qint64 kLoopMs = 1800;
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_hoverStartMs;
    item->setIcon(presetIconAt(m_hoverRow, double(elapsed % kLoopMs) / double(kLoopMs)));
}

QIcon TransitionsPane::presetIcon(const Preset &p) const
{
    const int W = 130;
    const int H = 78;
    QPixmap pm(W, H);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, true);

    auto drawAB = [&](const QRect &aRect, const QRect &bRect) {
        painter.fillRect(aRect, p.accent);
        painter.fillRect(bRect, light());
        painter.setPen(Qt::white);
        QFont f = painter.font();
        f.setBold(true);
        f.setPixelSize(18);
        painter.setFont(f);
        painter.drawText(aRect, Qt::AlignCenter, QStringLiteral("A"));
        painter.setPen(QColor(0x33, 0x33, 0x33));
        painter.drawText(bRect, Qt::AlignCenter, QStringLiteral("B"));
    };

    painter.fillRect(pm.rect(), QColor(0x18, 0x18, 0x18));

    switch (p.thumb) {
    case Thumb::SimpleBlinds: {
        const int n = 7;
        const int bw = W / n;
        for (int i = 0; i < n; ++i) {
            const QRect r(i * bw, 0, bw, H);
            if (i % 2 == 0) {
                painter.fillRect(r, p.accent);
                painter.setPen(Qt::white);
            } else {
                painter.fillRect(r, light());
                painter.setPen(QColor(0x33, 0x33, 0x33));
            }
            QFont f = painter.font();
            f.setBold(true);
            f.setPixelSize(14);
            painter.setFont(f);
            painter.drawText(r, Qt::AlignCenter, (i % 2 == 0) ? QStringLiteral("A") : QStringLiteral("B"));
        }
        break;
    }
    case Thumb::LeftToRight: {
        painter.fillRect(0, 0, W / 2 - 4, H, p.accent);
        painter.fillRect(W / 2 + 4, 0, W / 2 - 4, H, light());
        // Perspective bars
        for (int i = 0; i < 5; ++i) {
            const int x0 = W / 2 - 10 + i * 4;
            const int x1 = W / 2 + 10 + i * 8;
            QPolygon poly;
            poly << QPoint(x0, 8) << QPoint(x1, 4) << QPoint(x1, H - 4) << QPoint(x0, H - 8);
            painter.setBrush(i % 2 ? light() : p.accent);
            painter.setPen(Qt::NoPen);
            painter.drawPolygon(poly);
        }
        painter.setPen(Qt::white);
        QFont f = painter.font();
        f.setBold(true);
        f.setPixelSize(16);
        painter.setFont(f);
        painter.drawText(QRect(0, 0, W / 2, H), Qt::AlignCenter, QStringLiteral("A"));
        painter.setPen(QColor(0x33, 0x33, 0x33));
        painter.drawText(QRect(W / 2, 0, W / 2, H), Qt::AlignCenter, QStringLiteral("B"));
        break;
    }
    case Thumb::SlotMachine: {
        const int rowH = H / 4;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 6; ++col) {
                const bool a = (row + col) % 2 == 0;
                painter.fillRect(col * (W / 6), row * rowH, W / 6, rowH, a ? p.accent : light());
            }
        }
        break;
    }
    case Thumb::Spin: {
        painter.fillRect(pm.rect(), p.accent);
        painter.setBrush(light());
        painter.setPen(Qt::NoPen);
        painter.translate(W / 2.0, H / 2.0);
        painter.rotate(28);
        painter.drawRoundedRect(QRectF(-36, -28, 72, 56), 4, 4);
        painter.resetTransform();
        painter.setPen(Qt::white);
        QFont f = painter.font();
        f.setBold(true);
        f.setPixelSize(16);
        painter.setFont(f);
        painter.drawText(QRect(8, 0, 40, H), Qt::AlignCenter, QStringLiteral("A"));
        painter.setPen(QColor(0x33, 0x33, 0x33));
        painter.drawText(QRect(W / 2 - 10, H / 2 - 12, 40, 24), Qt::AlignCenter, QStringLiteral("B"));
        break;
    }
    case Thumb::Wipe:
        drawAB(QRect(0, 0, int(W * 0.55), H), QRect(int(W * 0.55), 0, int(W * 0.45), H));
        break;
    case Thumb::Dissolve: {
        QLinearGradient g(0, 0, W, 0);
        g.setColorAt(0, p.accent);
        g.setColorAt(1, light());
        painter.fillRect(pm.rect(), g);
        painter.setPen(Qt::white);
        QFont f = painter.font();
        f.setBold(true);
        f.setPixelSize(18);
        painter.setFont(f);
        painter.drawText(QRect(0, 0, W / 2, H), Qt::AlignCenter, QStringLiteral("A"));
        painter.setPen(QColor(0x33, 0x33, 0x33));
        painter.drawText(QRect(W / 2, 0, W / 2, H), Qt::AlignCenter, QStringLiteral("B"));
        break;
    }
    case Thumb::Push:
        drawAB(QRect(-20, 0, W / 2 + 20, H), QRect(W / 2 - 10, 0, W / 2 + 20, H));
        break;
    case Thumb::Iris: {
        painter.fillRect(pm.rect(), p.accent);
        painter.setBrush(light());
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPoint(W / 2, H / 2), 28, 22);
        painter.setPen(QColor(0x33, 0x33, 0x33));
        QFont f = painter.font();
        f.setBold(true);
        f.setPixelSize(16);
        painter.setFont(f);
        painter.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("B"));
        break;
    }
    case Thumb::Page: {
        painter.fillRect(pm.rect(), light());
        QPolygon fold;
        fold << QPoint(W / 2, 0) << QPoint(W, 0) << QPoint(W, H) << QPoint(W / 2 + 20, H);
        painter.setBrush(p.accent);
        painter.setPen(Qt::NoPen);
        painter.drawPolygon(fold);
        painter.setPen(Qt::white);
        QFont f = painter.font();
        f.setBold(true);
        f.setPixelSize(16);
        painter.setFont(f);
        painter.drawText(QRect(W / 2, 0, W / 2, H), Qt::AlignCenter, QStringLiteral("A"));
        painter.setPen(QColor(0x33, 0x33, 0x33));
        painter.drawText(QRect(0, 0, W / 2, H), Qt::AlignCenter, QStringLiteral("B"));
        break;
    }
    case Thumb::Gradient:
    default: {
        QLinearGradient g(0, 0, W, H);
        g.setColorAt(0, p.accent);
        g.setColorAt(0.4, p.accent);
        g.setColorAt(0.4, light());
        g.setColorAt(0.55, light());
        g.setColorAt(0.55, p.accent);
        g.setColorAt(1, p.accent);
        painter.fillRect(pm.rect(), g);
        break;
    }
    }

    painter.setPen(QColor(0x33, 0x33, 0x33));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(0, 0, W - 1, H - 1);
    painter.end();
    return QIcon(pm);
}

void TransitionsPane::rebuildPluginList()
{
    applySearchAndCategory();
}

void TransitionsPane::applySearchAndCategory()
{
    const QString filter = m_search ? m_search->text().trimmed() : QString();
    const int prev = m_currentIndex;
    m_pluginList->clear();

    for (int i = 0; i < m_plugins.size(); ++i) {
        const Plugin &p = m_plugins[i];
        if (m_activeCategory == QLatin1String("★")) {
            if (!p.favorite) {
                continue;
            }
        } else if (m_activeCategory != QLatin1String("All Plug-ins") && !m_activeCategory.isEmpty()) {
            if (!p.categories.contains(m_activeCategory)) {
                continue;
            }
        }
        if (!filter.isEmpty() && !p.name.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        const QString star = p.favorite ? QStringLiteral("★") : QStringLiteral("☆");
        auto *item = new QListWidgetItem(QStringLiteral("%1  %2").arg(star, p.name), m_pluginList);
        item->setData(Qt::UserRole, i);
        item->setToolTip(p.description);
    }

    if (prev >= 0) {
        for (int row = 0; row < m_pluginList->count(); ++row) {
            if (m_pluginList->item(row)->data(Qt::UserRole).toInt() == prev) {
                m_pluginList->setCurrentRow(row);
                return;
            }
        }
    }
    if (m_pluginList->count() > 0) {
        m_pluginList->setCurrentRow(0);
    } else {
        m_presetGrid->clear();
        m_metaLine1->clear();
        m_metaLine2->clear();
        m_currentIndex = -1;
    }
}

void TransitionsPane::showPlugin(int catalogIndex)
{
    if (catalogIndex < 0 || catalogIndex >= m_plugins.size()) {
        return;
    }
    m_currentIndex = catalogIndex;
    const Plugin &p = m_plugins[catalogIndex];

    m_hoverTimer->stop();
    m_hoverRow = -1;
    m_presetGrid->clear();
    for (int i = 0; i < p.presets.size(); ++i) {
        const Preset &pr = p.presets[i];
        auto *item = new QListWidgetItem(pr.name, m_presetGrid);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        item->setSizeHint(QSize(140, 104));
        if (!p.pluginId.isEmpty()) {
            item->setData(kDragPluginIdRole, p.pluginId);
            item->setData(kDragPresetRole, pr.name);
            item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
        }
        // Set the icon after the drag data so presetIconAt() can read it back.
        item->setIcon(presetIconAt(i, kPosterProgress));
    }
    if (m_presetGrid->count() > 0) {
        m_presetGrid->setCurrentRow(0);
    }

    m_metaLine1->setText(tr("%1: %2, 32-bit floating point").arg(p.name, p.format));
    m_metaLine2->setText(tr("Description: %1").arg(p.description));
}

void TransitionsPane::saveSettings() const
{
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    s.setValue(QStringLiteral("transitions/category"), m_activeCategory);
    if (m_currentIndex >= 0 && m_currentIndex < m_plugins.size()) {
        s.setValue(QStringLiteral("transitions/plugin"), m_plugins[m_currentIndex].name);
    }
    if (m_search) {
        s.setValue(QStringLiteral("transitions/search"), m_search->text());
    }
    if (m_splitter) {
        s.setValue(QStringLiteral("transitions/splitter"), m_splitter->saveState());
    }
}

void TransitionsPane::restoreSettings()
{
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    if (m_splitter) {
        const QByteArray st = s.value(QStringLiteral("transitions/splitter")).toByteArray();
        if (!st.isEmpty()) {
            m_splitter->restoreState(st);
        }
    }
    const QString cat =
        s.value(QStringLiteral("transitions/category"), QStringLiteral("All Plug-ins")).toString();
    m_activeCategory = cat;
    if (m_chipGroup) {
        for (QAbstractButton *b : m_chipGroup->buttons()) {
            if (b->text() == cat) {
                b->setChecked(true);
                break;
            }
        }
    }
    if (m_search) {
        const QString q = s.value(QStringLiteral("transitions/search")).toString();
        if (!q.isEmpty()) {
            m_search->setText(q);
        }
    }
    applySearchAndCategory();

    const QString plugin =
        s.value(QStringLiteral("transitions/plugin"), QStringLiteral("3D Blinds")).toString();
    bool found = false;
    for (int i = 0; i < m_plugins.size(); ++i) {
        if (m_plugins[i].name != plugin) {
            continue;
        }
        for (int row = 0; row < m_pluginList->count(); ++row) {
            if (m_pluginList->item(row)->data(Qt::UserRole).toInt() == i) {
                m_pluginList->setCurrentRow(row);
                found = true;
                break;
            }
        }
        if (!found) {
            showPlugin(i);
            found = true;
        }
        break;
    }
    if (!found && m_pluginList->count() > 0) {
        m_pluginList->setCurrentRow(0);
    } else if (!found && !m_plugins.isEmpty()) {
        showPlugin(0);
    }
}

} // namespace openvegas
