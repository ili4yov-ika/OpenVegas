#include "ui/MediaGeneratorPane.h"

#include "io/MediaMime.h"
#include "video/TitlesTextApply.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCursor>
#include <QDateTime>
#include <QDrag>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QListWidget>
#include <QListView>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRadialGradient>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace openvegas {

namespace {

QColor c(int r, int g, int b) { return QColor(r, g, b); }

/**
 * Drag-out payload roles for the plugin list / preset grid (see
 * GeneratorDragListWidget below). kDragKindRole is left unset for entries with
 * no real render backend (everything except Titles & Text today), so they
 * simply don't drag — matching the double-click fallback (status-bar toast).
 */
constexpr int kDragKindRole = Qt::UserRole + 20;
constexpr int kDragNameRole = Qt::UserRole + 21;
constexpr int kDragExtraRole = Qt::UserRole + 22;

/** Drag-out only list widget: emits a MediaMime synthetic payload (kind/name/extra
 *  from item data) so the timeline can create the matching generator event on drop. */
class GeneratorDragListWidget : public QListWidget {
public:
    explicit GeneratorDragListWidget(QWidget *parent = nullptr)
        : QListWidget(parent)
    {
        setDragEnabled(true);
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

    void startDrag(Qt::DropActions supportedActions) override
    {
        const QList<QListWidgetItem *> items = selectedItems();
        if (items.isEmpty()) {
            return;
        }
        QMimeData *md = mimeData(items);
        if (!md) {
            return;
        }
        auto *drag = new QDrag(this);
        drag->setMimeData(md);
        const QIcon icon = items.first()->icon();
        if (!icon.isNull()) {
            const QPixmap pm = icon.pixmap(QSize(80, 50));
            drag->setPixmap(pm);
            drag->setHotSpot(QPoint(pm.width() / 2, pm.height() / 2));
        }
        drag->exec(supportedActions, Qt::CopyAction);
    }
};

MediaGeneratorPane::Preset makePreset(const QString &name, MediaGeneratorPane::Pattern pat,
                                      QColor a = c(0x22, 0x22, 0x22), QColor b = c(0xee, 0xee, 0xee),
                                      int tile = 8)
{
    MediaGeneratorPane::Preset p;
    p.name = name;
    p.pattern = pat;
    p.c0 = a;
    p.c1 = b;
    p.tile = tile;
    return p;
}

/** Looks up the real preset key by label so callers only need to type the human-
 *  readable name — never a hand-typed (and possibly wrong) internal key. */
QString animationKeyForLabel(const QString &label)
{
    for (const TitlesTextPresetEntry &e : titlesTextAnimationPresets()) {
        if (e.label.compare(label, Qt::CaseInsensitive) == 0) {
            return e.key;
        }
    }
    return {};
}

/**
 * c0 = background, c1 = text color; presetIcon() renders real animated text for these
 * (see the animationKey doc on MediaGeneratorPane::Preset). animationLabel defaults to
 * displayName when omitted (the common case: a preset named "Bounce" uses the "Bounce"
 * animation) — pass it explicitly for the default preset, which Vegas displays as
 * "Sample Text" in the grid but maps to the "None" animation.
 */
MediaGeneratorPane::Preset makeTitlesTextPreset(const QString &displayName, QColor bg, QColor fg,
                                                const QString &animationLabel = QString())
{
    MediaGeneratorPane::Preset p;
    p.name = displayName;
    p.animationKey = animationKeyForLabel(animationLabel.isEmpty() ? displayName : animationLabel);
    p.c0 = bg;
    p.c1 = fg;
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

    {
        Plugin p;
        p.name = QStringLiteral("Checkerboard");
        p.categories = {QStringLiteral("Utility"), QStringLiteral("Creative")};
        p.grouping = QStringLiteral("OpenVegas");
        p.version = QStringLiteral("1.0");
        p.description = QStringLiteral("From Magix Computer Products Intl. Co.");
        p.gpu = true;
        p.presets = {
            makePreset(QStringLiteral("(Default)"), Pattern::Checker, c(0x11, 0x11, 0x11), c(0xee, 0xee, 0xee), 12),
            makePreset(QStringLiteral("Large Tiles"), Pattern::Checker, c(0x11, 0x11, 0x11), c(0xee, 0xee, 0xee), 20),
            makePreset(QStringLiteral("Small Tiles"), Pattern::Checker, c(0x11, 0x11, 0x11), c(0xee, 0xee, 0xee), 6),
            makePreset(QStringLiteral("Horizontal Blinds"), Pattern::HBlinds),
            makePreset(QStringLiteral("Vertical Blinds"), Pattern::VBlinds),
            makePreset(QStringLiteral("Grille"), Pattern::Grille),
            makePreset(QStringLiteral("Fence"), Pattern::Fence),
            makePreset(QStringLiteral("Ridges"), Pattern::Ridges),
            makePreset(QStringLiteral("Bumps"), Pattern::Bumps),
            makePreset(QStringLiteral("Plaid"), Pattern::Plaid),
            makePreset(QStringLiteral("Letterbox"), Pattern::Letterbox),
            makePreset(QStringLiteral("Split Screen"), Pattern::SplitScreen),
            makePreset(QStringLiteral("Horizon"), Pattern::Horizon),
        };
        add(std::move(p));
    }

    auto simple = [&](const QString &name, const QStringList &cats, const QString &desc, Pattern pat,
                      QColor a, QColor b, bool gpu = false) {
        Plugin p;
        p.name = name;
        p.categories = cats;
        p.grouping = QStringLiteral("OpenVegas");
        p.version = QStringLiteral("1.0");
        p.description = desc;
        p.gpu = gpu;
        p.presets = {makePreset(QStringLiteral("(Default)"), pat, a, b)};
        add(std::move(p));
    };

    simple(QStringLiteral("Color Gradient"), {QStringLiteral("Creative"), QStringLiteral("Utility")},
           QStringLiteral("Linear and radial color gradients."), Pattern::Gradient,
           c(0x20, 0x40, 0x80), c(0xc0, 0x60, 0x30), true);
    simple(QStringLiteral("Credit Roll"), {QStringLiteral("Titles and Text")},
           QStringLiteral("Scrolling credit / title roll."), Pattern::Gradient,
           c(0x10, 0x10, 0x10), c(0x40, 0x40, 0x50));
    simple(QStringLiteral("Noise Texture"), {QStringLiteral("Creative"), QStringLiteral("Utility")},
           QStringLiteral("Procedural noise texture generator."), Pattern::Bumps,
           c(0x30, 0x30, 0x30), c(0x90, 0x90, 0x90), true);
    simple(QStringLiteral("Solid Color"), {QStringLiteral("Utility")},
           QStringLiteral("Solid color media event."), Pattern::Gradient,
           c(0x00, 0x78, 0xd7), c(0x00, 0x78, 0xd7));
    simple(QStringLiteral("Test Pattern"), {QStringLiteral("Utility")},
           QStringLiteral("Broadcast / calibration test patterns."), Pattern::Grille,
           c(0x11, 0x11, 0x11), c(0xee, 0xee, 0xee));
    {
        Plugin p;
        p.name = QStringLiteral("Titles & Text");
        p.categories = {QStringLiteral("Titles and Text"), QStringLiteral("Creative")};
        p.grouping = QStringLiteral("OpenVegas");
        p.version = QStringLiteral("1.0");
        p.description = QStringLiteral("Titles and text overlays.");
        p.gpu = true;
        p.presets = {
            makeTitlesTextPreset(QStringLiteral("Sample Text"), c(0x1a, 0x1a, 0x28),
                                 c(0xf0, 0xf0, 0xf0), QStringLiteral("None")),
            makeTitlesTextPreset(QStringLiteral("Bounce"), c(0xff, 0xd0, 0x40), c(0x10, 0x10, 0x10)),
            makeTitlesTextPreset(QStringLiteral("Earthquake"), c(0x30, 0x50, 0xc0), c(0xf0, 0xf0, 0xf0)),
            makeTitlesTextPreset(QStringLiteral("Fly In"), c(0x20, 0x80, 0x40), c(0xf0, 0xf0, 0xf0)),
            makeTitlesTextPreset(QStringLiteral("Fly in from Right"), c(0x80, 0x30, 0x30),
                                 c(0xf0, 0xf0, 0xf0)),
            makeTitlesTextPreset(QStringLiteral("Slide Left"), c(0x00, 0xc0, 0xd0), c(0x10, 0x10, 0x10)),
            makeTitlesTextPreset(QStringLiteral("Slide Right"), c(0x40, 0x40, 0x40), c(0xf0, 0xf0, 0xf0)),
            makeTitlesTextPreset(QStringLiteral("Slide Up"), c(0x10, 0x10, 0x10), c(0xf0, 0xf0, 0xf0)),
            makeTitlesTextPreset(QStringLiteral("Slide Down"), c(0xe0, 0xe0, 0xe0), c(0x10, 0x10, 0x10)),
            makeTitlesTextPreset(QStringLiteral("Speedy"), c(0xff, 0xe0, 0x00), c(0x10, 0x10, 0x10)),
            makeTitlesTextPreset(QStringLiteral("Twist In"), c(0x60, 0x20, 0x80), c(0xf0, 0xf0, 0xf0)),
            makeTitlesTextPreset(QStringLiteral("Drop Split"), c(0x00, 0xd0, 0xd0), c(0x10, 0x10, 0x10)),
            makeTitlesTextPreset(QStringLiteral("Popup"), c(0x10, 0x10, 0x10), c(0xf0, 0xf0, 0xf0)),
            makeTitlesTextPreset(QStringLiteral("Scroll"), c(0x20, 0x20, 0x30), c(0xf0, 0xf0, 0xf0)),
            makeTitlesTextPreset(QStringLiteral("Menace"), c(0xf0, 0xf0, 0xf0), c(0x10, 0x10, 0x10)),
            makeTitlesTextPreset(QStringLiteral("Rough Day"), c(0xff, 0xe0, 0x20), c(0x10, 0x10, 0x10)),
            makeTitlesTextPreset(QStringLiteral("Coming at You"), c(0x30, 0x30, 0x30),
                                 c(0xf0, 0xf0, 0xf0)),
            makeTitlesTextPreset(QStringLiteral("Double Flash Glow"), c(0x20, 0x20, 0x20),
                                 c(0xf0, 0xf0, 0xf0)),
        };
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
        const QString animationKey = (row >= 0 && row < presets.size())
                                         ? presets[row].animationKey
                                         : QString();
        emit presetActivated(m_plugins[m_currentIndex].name, item->text(), animationKey);
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

    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setInterval(66); // ~15 fps — cheap for a single 100x62 icon re-render
    connect(m_hoverTimer, &QTimer::timeout, this, &MediaGeneratorPane::onHoverTick);
    connect(m_presetGrid, &QListWidget::entered, this, &MediaGeneratorPane::onPresetHoverEntered);
}

QIcon MediaGeneratorPane::presetIcon(const Preset &p, double progress) const
{
    if (!p.animationKey.isEmpty()) {
        TitlesTextParams tp;
        tp.text = p.name;
        tp.fontFamily = QStringLiteral("Verdana");
        // Authored against a 1080-tall reference frame (see renderTitlesText) — scale
        // inversely with name length so longer preset names don't overflow the 100x62
        // icon too badly.
        tp.fontSize = std::clamp(3200.0 / std::max(4, int(p.name.size())), 150.0, 420.0);
        tp.bold = true;
        tp.alignment = TitlesTextAlignment::Center;
        tp.anchor = TitlesTextAnchor::MiddleCenter;
        tp.locationX = 0.5;
        tp.locationY = 0.5;
        tp.textColor = p.c1;
        tp.backgroundColor = p.c0;
        tp.animationName = p.animationKey;
        const QImage img = renderTitlesText(tp, QSize(100, 62), progress);
        return QIcon(QPixmap::fromImage(img));
    }

    QPixmap pm(100, 62);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, false);

    auto fillGrad = [&]() {
        QLinearGradient g(0, 0, 100, 62);
        g.setColorAt(0, p.c0);
        g.setColorAt(1, p.c1);
        painter.fillRect(pm.rect(), g);
    };

    switch (p.pattern) {
    case Pattern::Checker: {
        const int t = qMax(4, p.tile);
        for (int y = 0; y < 62; y += t) {
            for (int x = 0; x < 100; x += t) {
                const bool dark = ((x / t) + (y / t)) % 2 == 0;
                painter.fillRect(x, y, t, t, dark ? p.c0 : p.c1);
            }
        }
        break;
    }
    case Pattern::HBlinds:
        for (int y = 0; y < 62; y += 8) {
            painter.fillRect(0, y, 100, 4, p.c0);
            painter.fillRect(0, y + 4, 100, 4, p.c1);
        }
        break;
    case Pattern::VBlinds:
        for (int x = 0; x < 100; x += 8) {
            painter.fillRect(x, 0, 4, 62, p.c0);
            painter.fillRect(x + 4, 0, 4, 62, p.c1);
        }
        break;
    case Pattern::Grille:
        painter.fillRect(pm.rect(), p.c1);
        for (int x = 0; x < 100; x += 6) {
            painter.fillRect(x, 0, 2, 62, p.c0);
        }
        break;
    case Pattern::Fence:
        painter.fillRect(pm.rect(), QColor(0xaa, 0xaa, 0xaa));
        for (int y = 0; y < 62; y += 8) {
            painter.fillRect(0, y, 100, 3, QColor(0x33, 0x33, 0x33));
        }
        break;
    case Pattern::Ridges: {
        QLinearGradient g(0, 0, 100, 0);
        g.setColorAt(0, QColor(0x1a, 0x1a, 0x1a));
        g.setColorAt(0.5, QColor(0x88, 0x88, 0x88));
        g.setColorAt(1, QColor(0x1a, 0x1a, 0x1a));
        painter.fillRect(pm.rect(), g);
        break;
    }
    case Pattern::Bumps: {
        painter.fillRect(pm.rect(), QColor(0x22, 0x22, 0x22));
        for (int y = 8; y < 62; y += 16) {
            for (int x = 8; x < 100; x += 16) {
                QRadialGradient g(x, y, 10);
                g.setColorAt(0, QColor(0xcc, 0xcc, 0xcc));
                g.setColorAt(1, QColor(0x22, 0x22, 0x22));
                painter.setPen(Qt::NoPen);
                painter.setBrush(g);
                painter.drawEllipse(QPointF(x, y), 8, 8);
            }
        }
        break;
    }
    case Pattern::Plaid:
        for (int x = 0; x < 100; x += 16) {
            painter.fillRect(x, 0, 8, 62, QColor(0x20, 0x40, 0x60));
            painter.fillRect(x + 8, 0, 8, 62, QColor(0x80, 0x20, 0x40));
        }
        break;
    case Pattern::Letterbox:
        painter.fillRect(pm.rect(), QColor(0xcc, 0xcc, 0xcc));
        painter.fillRect(0, 0, 100, 11, Qt::black);
        painter.fillRect(0, 51, 100, 11, Qt::black);
        break;
    case Pattern::SplitScreen:
        painter.fillRect(0, 0, 50, 62, QColor(0x20, 0x60, 0xa0));
        painter.fillRect(50, 0, 50, 62, QColor(0xa0, 0x40, 0x20));
        break;
    case Pattern::Horizon: {
        QLinearGradient g(0, 0, 0, 62);
        g.setColorAt(0, QColor(0x40, 0x60, 0xa0));
        g.setColorAt(0.45, QColor(0x40, 0x60, 0xa0));
        g.setColorAt(0.55, QColor(0xc0, 0x80, 0x40));
        g.setColorAt(1, QColor(0xc0, 0x80, 0x40));
        painter.fillRect(pm.rect(), g);
        break;
    }
    case Pattern::Gradient:
    default:
        fillGrad();
        break;
    }

    painter.setPen(QColor(0x33, 0x33, 0x33));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(0, 0, 99, 61);
    painter.end();
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
        if (p.name.compare(QStringLiteral("Titles & Text"), Qt::CaseInsensitive) == 0) {
            // Drag the plugin row itself → default (Sample Text / None) preset.
            item->setData(kDragKindRole, QStringLiteral("titles"));
            item->setData(kDragNameRole, QString());
            item->setData(kDragExtraRole, QString());
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
            item->setData(kDragNameRole, pr.name);
            item->setData(kDragExtraRole, pr.animationKey);
            item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
        }
    }
    if (m_presetGrid->count() > 0) {
        m_presetGrid->setCurrentRow(0);
    }

    QString tech = tr("%1: OFX, 32-bit floating point").arg(p.name);
    if (p.gpu) {
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
