#include "ui/MediaGeneratorPane.h"

#include "video/VegasGenerators.h"

#include "io/MediaMime.h"
#include "video/GeneratorPresetData.h"
#include "video/MediaGeneratorApply.h"
#include "video/TitlesTextApply.h"

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
#include <QListWidget>
#include <QListView>
#include <QMimeData>
#include <QMouseEvent>
#include <QHash>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace openvegas {

namespace {

/**
 * Drag-out payload roles for the plugin list / preset grid (see
 * GeneratorDragListWidget below). Every plugin row / preset tile carries a
 * payload — kind "titles" for Titles & Text (extra = the VEGAS preset's name), kind
 * "generator" for everything else (extra = MediaGeneratorParams payload, see
 * mediaGeneratorParamsToPayload()).
 */
constexpr int kDragKindRole = Qt::UserRole + 20;
constexpr int kDragNameRole = Qt::UserRole + 21;
constexpr int kDragExtraRole = Qt::UserRole + 22;

/** Width of the leading star glyph in a plug-in row — a press this far in toggles it. */
constexpr int kStarColumnWidth = 18;

/** Drag-out only list widget: emits a MediaMime synthetic payload (kind/name/extra
 *  from item data) so the timeline can create the matching generator event on drop.
 *
 *  Drives the press→move→QDrag sequence itself (mousePressEvent/mouseMoveEvent) rather
 *  than leaning on QAbstractItemView's own automatic drag-start detection
 *  (dragEnabled()/startDrag()) — that heuristic disambiguates a plain selection click
 *  from a press-and-drag by tracking internal state that a freshly-clicked, not
 *  previously-selected IconMode tile doesn't reliably satisfy, so drags out of this
 *  grid could silently never start. Explicit tracking here matches the same
 *  press/track-distance/launch pattern TimelineView already uses for its own internal
 *  clip dragging, so it doesn't depend on that Qt-internal heuristic at all. */
class GeneratorDragListWidget : public QListWidget {
public:
    explicit GeneratorDragListWidget(QWidget *parent = nullptr)
        : QListWidget(parent)
    {
        setDragDropMode(QAbstractItemView::DragOnly);
    }

protected:
    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override
    {
        if (items.isEmpty()) {
            return nullptr;
        }
        QListWidgetItem *item = items.first();
        const QString kind = item->data(kDragKindRole).toString();
        if (kind.isEmpty()) {
            return nullptr;
        }
        return MediaMime::fromSynthetic(kind, item->data(kDragNameRole).toString(),
                                        item->data(kDragExtraRole).toString());
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_pressIndex = indexAt(event->pos());
            m_pressPos = event->pos();
        }
        QListWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_pressIndex.isValid() || !(event->buttons() & Qt::LeftButton)) {
            QListWidget::mouseMoveEvent(event);
            return;
        }
        if ((event->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance()) {
            QListWidget::mouseMoveEvent(event);
            return;
        }
        QListWidgetItem *item = itemFromIndex(m_pressIndex);
        m_pressIndex = QPersistentModelIndex(); // one-shot: don't re-arm until the next press
        if (!item) {
            return; // the list was rebuilt under the press — nothing left to drag
        }
        QMimeData *md = mimeData({item});
        if (!md) {
            return;
        }
        auto *drag = new QDrag(this);
        drag->setMimeData(md);
        const QIcon icon = item->icon();
        if (!icon.isNull()) {
            const QPixmap pm = icon.pixmap(QSize(80, 50));
            drag->setPixmap(pm);
            drag->setHotSpot(QPoint(pm.width() / 2, pm.height() / 2));
        }
        drag->exec(Qt::CopyAction, Qt::CopyAction);
    }

private:
    /**
     * The pressed row, as an index rather than a pointer.
     *
     * A press can rebuild the list (toggling the favourite star does), which deletes every
     * item — including the one that was pressed. A stored `QListWidgetItem *` would be left
     * pointing at freed memory and the next mouse move would read it; a persistent index just
     * goes invalid.
     */
    QPersistentModelIndex m_pressIndex;
    QPoint m_pressPos;
};

MediaGeneratorPane::Preset makePreset(const QString &name, MediaGeneratorPattern pat,
                                      QColor a, QColor b, int tile)
{
    MediaGeneratorPane::Preset p;
    p.name = name;
    p.pattern = pat;
    p.c0 = a;
    p.c1 = b;
    p.tile = tile;
    return p;
}

} // namespace

MediaGeneratorPane::MediaGeneratorPane(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("mediaGeneratorPane"));
    m_activeCategory = QStringLiteral("All Plug-ins");
    loadCatalog();
    loadFavorites();
    buildUi();
    rebuildPluginList();
    restoreSettings();
}

void MediaGeneratorPane::loadCatalog()
{
    m_plugins.clear();

    auto add = [this](Plugin p) { m_plugins.push_back(std::move(p)); };

    // The six non-text generators and every preset VEGAS ships for them, from the stock
    // table (tools/gen_generator_presets.py): names and values are VEGAS's, and each preset
    // goes through mediaGeneratorFromVegas() — the same mapping a project's generator takes
    // on import — so what is placed from here and what is opened from a .veg agree.
    struct Listing {
        const char *name;
        QStringList categories;
        const char *description;
        bool gpu;
    };
    static const Listing kListings[] = {
        {"Checkerboard", {QStringLiteral("Utility"), QStringLiteral("Creative")},
         "From Magix Computer Products Intl. Co.", true},
        {"Color Gradient", {QStringLiteral("Creative"), QStringLiteral("Utility")},
         "Linear and radial color gradients.", true},
        {"Credit Roll", {QStringLiteral("Titles and Text")}, "Scrolling credit / title roll.",
         false},
        {"Noise Texture", {QStringLiteral("Creative"), QStringLiteral("Utility")},
         "Procedural noise texture generator.", true},
        {"Solid Color", {QStringLiteral("Utility")}, "Solid color media event.", false},
        {"Test Pattern", {QStringLiteral("Utility")}, "Broadcast / calibration test patterns.",
         false},
    };
    for (const StockGeneratorGroup &group : stockGeneratorPresets()) {
        Plugin p;
        p.name = group.name;
        p.grouping = QStringLiteral("OpenVegas");
        p.version = QStringLiteral("1.0");
        for (const Listing &l : kListings) {
            if (group.name == QLatin1String(l.name)) {
                p.categories = l.categories;
                p.description = QString::fromLatin1(l.description);
                p.gpu = l.gpu;
            }
        }
        for (const StockGeneratorPreset &stock : group.presets) {
            const MediaGeneratorParams gp =
                mediaGeneratorFromVegas(group.pluginId, stock.name, stock.params);
            Preset preset = makePreset(stock.name, gp.pattern, gp.c0, gp.c1, gp.tile);
            preset.vegasPluginId = group.pluginId;
            preset.vegasParams = stock.params;
            p.presets.push_back(preset);
        }
        add(std::move(p));
    }
    {
        Plugin p;
        p.name = QStringLiteral("Titles & Text");
        p.categories = {QStringLiteral("Titles and Text"), QStringLiteral("Creative")};
        p.grouping = QStringLiteral("OpenVegas");
        p.version = QStringLiteral("1.0");
        p.description = QStringLiteral("Titles and text overlays.");
        p.gpu = true;
        // VEGAS's own presets in its browser's order — "(Default)" (captioned "Sample Text"
        // in its thumbnail), Title01–25, the named animations, Placeholder, Subtitles — with
        // the values VEGAS's preset package gives them (titlesTextStockPresets()).
        for (const TitlesTextStockPreset &stock : titlesTextStockPresets()) {
            Preset preset;
            preset.name = stock.name;
            preset.animationKey = stock.params.animationName;
            preset.c0 = stock.params.backgroundColor;
            preset.c1 = stock.params.textColor;
            preset.sampleText = stock.params.text;
            preset.titlesParams = stock.params;
            p.presets.push_back(preset);
        }
        add(std::move(p));
    }
}

void MediaGeneratorPane::loadFavorites()
{
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    const QStringList favs = s.value(QStringLiteral("mediaGen/favorites")).toStringList();
    for (Plugin &p : m_plugins) {
        p.favorite = favs.contains(p.name);
    }
}

void MediaGeneratorPane::saveFavorites()
{
    QStringList favs;
    for (const Plugin &p : m_plugins) {
        if (p.favorite) {
            favs << p.name;
        }
    }
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    s.setValue(QStringLiteral("mediaGen/favorites"), favs);
}

void MediaGeneratorPane::buildUi()
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

    const QStringList chips = {QStringLiteral("All Plug-ins"), QStringLiteral("Creative"),
                               QStringLiteral("Titles and Text"), QStringLiteral("Utility"),
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

    m_pluginList = new GeneratorDragListWidget(m_splitter);
    m_pluginList->setObjectName(QStringLiteral("fxPluginList"));
    m_pluginList->setUniformItemSizes(true);
    m_pluginList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *mainCol = new QWidget(m_splitter);
    mainCol->setObjectName(QStringLiteral("fxMain"));
    auto *mainLay = new QVBoxLayout(mainCol);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);

    m_presetGrid = new GeneratorDragListWidget(mainCol);
    m_presetGrid->setObjectName(QStringLiteral("fxPresetGrid"));
    m_presetGrid->setViewMode(QListView::IconMode);
    m_presetGrid->setResizeMode(QListView::Adjust);
    m_presetGrid->setMovement(QListView::Static);
    m_presetGrid->setUniformItemSizes(true);
    m_presetGrid->setSpacing(8);
    m_presetGrid->setIconSize(QSize(100, 62));
    m_presetGrid->setGridSize(QSize(108, 88));
    m_presetGrid->setWordWrap(true);
    m_presetGrid->setSelectionMode(QAbstractItemView::SingleSelection);
    m_presetGrid->setMouseTracking(true);
    mainLay->addWidget(m_presetGrid, 1);

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
                if (!cur) {
                    return;
                }
                showPlugin(cur->data(Qt::UserRole).toInt());
            });
    connect(m_pluginList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        if (m_currentIndex >= 0) {
            emit generatorActivated(m_plugins[m_currentIndex].name);
        }
    });
    connect(m_presetGrid, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (!item || m_currentIndex < 0) {
            return;
        }
        const int row = m_presetGrid->row(item);
        const QVector<Preset> &presets = m_plugins[m_currentIndex].presets;
        const bool valid = row >= 0 && row < presets.size();
        emit presetActivated(m_plugins[m_currentIndex].name,
                             valid ? presets[row].name : item->text());
    });
    // The row's leading character is its favourite star, so a press in that column toggles it,
    // the way Vegas's own dock does. There is no separate star widget to click, hence the hit
    // test against the item rect — in viewport coordinates, which is what visualItemRect gives.
    connect(m_pluginList, &QListWidget::itemPressed, this, [this](QListWidgetItem *item) {
        if (!item || !(QApplication::mouseButtons() & Qt::LeftButton)) {
            return;
        }
        const QPoint pos = m_pluginList->viewport()->mapFromGlobal(QCursor::pos());
        const QRect r = m_pluginList->visualItemRect(item);
        if (pos.x() - r.left() >= kStarColumnWidth) {
            return;
        }
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx < 0 || idx >= m_plugins.size()) {
            return;
        }
        m_plugins[idx].favorite = !m_plugins[idx].favorite;
        saveFavorites();
        // Rebuilding drops every item, `item` included, and this runs inside the list's own
        // mouse-press handling — so it waits until that has finished. applySearchAndCategory()
        // restores the selection from m_currentIndex, which by then is the row just pressed.
        QTimer::singleShot(0, this, [this] { applySearchAndCategory(); });
    });

    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setInterval(66); // ~15 fps — cheap for a single 100x62 icon re-render
    connect(m_hoverTimer, &QTimer::timeout, this, &MediaGeneratorPane::onHoverTick);
    connect(m_presetGrid, &QListWidget::entered, this, &MediaGeneratorPane::onPresetHoverEntered);
    // A press that turns into a drag-out must hand startDrag() a stable, static pixmap —
    // and must not have the model mutating under Qt's press-to-drag distance tracking in
    // between. The hover timer does both (repaints the icon to a mid-tween animation
    // frame every 66ms) for as long as the cursor sits over an animated tile, which is
    // exactly the window where a press-and-hold-to-drag gesture starts. Stop it the
    // instant the tile is pressed and restore the resting frame.
    connect(m_presetGrid, &QListWidget::itemPressed, this, [this](QListWidgetItem *item) {
        if (!m_hoverTimer->isActive()) {
            return;
        }
        m_hoverTimer->stop();
        if (item && m_hoverRow >= 0 && m_currentIndex >= 0 && m_currentIndex < m_plugins.size()) {
            const QVector<Preset> &presets = m_plugins[m_currentIndex].presets;
            if (m_hoverRow < presets.size()) {
                item->setIcon(presetIcon(presets[m_hoverRow], 1.0));
            }
        }
        m_hoverRow = -1;
    });
}

QIcon MediaGeneratorPane::presetIcon(const Preset &p, double progress) const
{
    if (!p.animationKey.isEmpty()) {
        // Use the complete package entry. VEGAS lays each thumbnail out using the preset's
        // own font, anchor, location, tracking, line spacing and effects; synthesising a
        // centered Verdana label made the browser disagree with both VEGAS and placement.
        TitlesTextParams tp = p.titlesParams;
        if (tp.text.isEmpty()) {
            tp.text = p.sampleText.isEmpty() ? p.name : p.sampleText;
        }
        if (tp.animationName.isEmpty()) {
            tp.animationName = p.animationKey;
        }
        const QSize thumb(100, 62);
        const QSize big = thumb * 4;
        const QImage rendered = renderTitlesText(tp, big, progress);
        QImage img = tp.backgroundColor.alpha() > 0
                         ? rendered.scaled(thumb, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                         : checkerboardBackground(thumb);
        if (tp.backgroundColor.alpha() <= 0) {
            QPainter compositor(&img);
            compositor.setRenderHint(QPainter::SmoothPixmapTransform, true);
            compositor.drawImage(QRect(QPoint(0, 0), thumb),
                                 rendered.scaled(thumb, Qt::IgnoreAspectRatio,
                                                 Qt::SmoothTransformation));
        }
        return QIcon(QPixmap::fromImage(img));
    }

    // Drawn the way the placed event is (renderMediaGenerator(): VEGAS's own plug-in when
    // installed, else our renderers), from the preset's VEGAS values — so every preset shows
    // its own picture, as in VEGAS, rather than one stand-in pattern per generator. Drawn
    // at four times the size and reduced, so thin tiles and small credit text survive, and
    // over a checkerboard where the preset is transparent, as VEGAS shows it.
    static QHash<QString, QPixmap> cache;
    const QString key = p.vegasPluginId + QLatin1Char('|') + p.name;
    if (const auto it = cache.constFind(key); it != cache.constEnd()) {
        return QIcon(it.value());
    }
    const QSize thumb(100, 62);
    const QSize big = thumb * 4;
    MediaGeneratorParams gp = mediaGeneratorFromVegas(p.vegasPluginId, p.name, p.vegasParams);
    gp.c0 = p.c0;
    gp.c1 = p.c1;
    gp.pattern = p.pattern;
    gp.tile = p.tile;
    QImage drawn = p.vegasPluginId.compare(QLatin1String("{02FD3B60-986B-11D4-9F6F-00485481BD31}"),
                                           Qt::CaseInsensitive) == 0
                       ? renderCreditRollStill(p.vegasParams, big)
                       : renderMediaGenerator(gp, big);
    QImage img = checkerboardBackground(thumb);
    {
        QPainter compositor(&img);
        compositor.setRenderHint(QPainter::SmoothPixmapTransform, true);
        compositor.drawImage(QRect(QPoint(0, 0), thumb),
                             drawn.scaled(thumb, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    }
    QPixmap pm = QPixmap::fromImage(img);

    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QColor(0x33, 0x33, 0x33));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(0, 0, 99, 61);
    painter.end();
    cache.insert(key, pm);
    return QIcon(pm);
}

void MediaGeneratorPane::rebuildPluginList()
{
    applySearchAndCategory();
}

void MediaGeneratorPane::applySearchAndCategory()
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
        // Drag the plugin row itself → its default (first) preset.
        if (!p.presets.isEmpty()) {
            const Preset &def = p.presets.first();
            if (!def.animationKey.isEmpty()) {
                item->setData(kDragKindRole, QStringLiteral("titles"));
                item->setData(kDragNameRole, def.sampleText.isEmpty() ? def.name : def.sampleText);
                item->setData(kDragExtraRole, def.name); // the VEGAS preset
            } else {
                MediaGeneratorParams gp;
                gp.pluginName = p.name;
                gp.pattern = def.pattern;
                gp.c0 = def.c0;
                gp.c1 = def.c1;
                gp.tile = def.tile;
                gp.vegasPluginId = def.vegasPluginId;
                gp.vegasPreset = def.name;
                item->setData(kDragKindRole, QStringLiteral("generator"));
                item->setData(kDragNameRole, QString());
                item->setData(kDragExtraRole, mediaGeneratorParamsToPayload(gp));
            }
            item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
        }
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

void MediaGeneratorPane::showPlugin(int catalogIndex)
{
    if (catalogIndex < 0 || catalogIndex >= m_plugins.size()) {
        return;
    }
    m_hoverTimer->stop();
    m_hoverRow = -1;
    m_currentIndex = catalogIndex;
    const Plugin &p = m_plugins[catalogIndex];

    m_presetGrid->clear();
    for (const Preset &pr : p.presets) {
        auto *item = new QListWidgetItem(presetIcon(pr), pr.name, m_presetGrid);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        item->setSizeHint(QSize(108, 88));
        if (!pr.animationKey.isEmpty()) {
            item->setData(kDragKindRole, QStringLiteral("titles"));
            item->setData(kDragNameRole, pr.sampleText.isEmpty() ? pr.name : pr.sampleText);
            item->setData(kDragExtraRole, pr.name); // the VEGAS preset
        } else {
            MediaGeneratorParams gp;
            gp.pluginName = p.name;
            gp.pattern = pr.pattern;
            gp.c0 = pr.c0;
            gp.c1 = pr.c1;
            gp.tile = pr.tile;
            gp.vegasPluginId = pr.vegasPluginId;
            gp.vegasPreset = pr.name;
            item->setData(kDragKindRole, QStringLiteral("generator"));
            item->setData(kDragNameRole, pr.name);
            item->setData(kDragExtraRole, mediaGeneratorParamsToPayload(gp));
        }
        item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
    }
    if (m_presetGrid->count() > 0) {
        m_presetGrid->setCurrentRow(0);
    }

    // Color Gradient and Credit Roll are still DirectX plug-ins in VEGAS 22 — their id is a
    // CLSID rather than "{Svfx:…}" — and calling them OFX would be wrong.
    const bool legacy = !p.presets.isEmpty() && !p.presets.first().vegasPluginId.isEmpty()
                        && !p.presets.first().vegasPluginId.startsWith(QLatin1String("{Svfx:"));
    QString tech = legacy ? tr("%1: DirectX (legacy plug-in)").arg(p.name)
                          : tr("%1: OFX, 32-bit floating point").arg(p.name);
    if (p.gpu && !legacy) {
        tech += QStringLiteral(", GPU Accelerated");
    }
    tech += tr(", Grouping: %1, Version %2").arg(p.grouping, p.version);
    m_metaLine1->setText(tech);
    m_metaLine2->setText(tr("Description: %1").arg(p.description));
}

void MediaGeneratorPane::onPresetHoverEntered(const QModelIndex &index)
{
    if (!index.isValid() || m_currentIndex < 0 || m_currentIndex >= m_plugins.size()) {
        return;
    }
    const int row = index.row();
    const QVector<Preset> &presets = m_plugins[m_currentIndex].presets;
    if (row < 0 || row >= presets.size() || presets[row].animationKey.isEmpty()) {
        return; // static (pattern-based) preset — nothing to animate
    }
    m_hoverRow = row;
    m_hoverStartMs = QDateTime::currentMSecsSinceEpoch();
    m_hoverTimer->start();
}

void MediaGeneratorPane::onHoverTick()
{
    if (m_hoverRow < 0 || m_currentIndex < 0 || m_currentIndex >= m_plugins.size()) {
        m_hoverTimer->stop();
        return;
    }
    const QVector<Preset> &presets = m_plugins[m_currentIndex].presets;
    QListWidgetItem *item = m_presetGrid->item(m_hoverRow);
    if (!item || m_hoverRow >= presets.size()) {
        m_hoverTimer->stop();
        m_hoverRow = -1;
        return;
    }

    // Ground truth each tick rather than relying on enter/leave signal coverage: if the
    // cursor isn't over this item anymore (moved to another item, empty space, or off
    // the widget entirely), stop and restore the static icon.
    const QPoint viewportPos = m_presetGrid->viewport()->mapFromGlobal(QCursor::pos());
    const QModelIndex under = m_presetGrid->indexAt(viewportPos);
    if (!under.isValid() || under.row() != m_hoverRow) {
        item->setIcon(presetIcon(presets[m_hoverRow], 1.0));
        m_hoverTimer->stop();
        m_hoverRow = -1;
        return;
    }

    constexpr qint64 kLoopMs = 1400;
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_hoverStartMs;
    const double progress = double(elapsed % kLoopMs) / double(kLoopMs);
    item->setIcon(presetIcon(presets[m_hoverRow], progress));
}

void MediaGeneratorPane::saveSettings() const
{
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    s.setValue(QStringLiteral("mediaGen/category"), m_activeCategory);
    if (m_currentIndex >= 0 && m_currentIndex < m_plugins.size()) {
        s.setValue(QStringLiteral("mediaGen/plugin"), m_plugins[m_currentIndex].name);
    }
    if (m_search) {
        s.setValue(QStringLiteral("mediaGen/search"), m_search->text());
    }
    if (m_splitter) {
        s.setValue(QStringLiteral("mediaGen/splitter"), m_splitter->saveState());
    }
}

void MediaGeneratorPane::restoreSettings()
{
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    if (m_splitter) {
        const QByteArray st = s.value(QStringLiteral("mediaGen/splitter")).toByteArray();
        if (!st.isEmpty()) {
            m_splitter->restoreState(st);
        }
    }

    const QString cat =
        s.value(QStringLiteral("mediaGen/category"), QStringLiteral("All Plug-ins")).toString();
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
        const QString q = s.value(QStringLiteral("mediaGen/search")).toString();
        if (!q.isEmpty()) {
            m_search->setText(q);
        }
    }

    applySearchAndCategory();

    const QString plugin =
        s.value(QStringLiteral("mediaGen/plugin"), QStringLiteral("Checkerboard")).toString();
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
