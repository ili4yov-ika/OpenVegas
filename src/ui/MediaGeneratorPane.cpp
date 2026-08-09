#include "ui/MediaGeneratorPane.h"

#include "io/MediaMime.h"
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
#include <QPainter>
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

QColor c(int r, int g, int b) { return QColor(r, g, b); }

/**
 * Drag-out payload roles for the plugin list / preset grid (see
 * GeneratorDragListWidget below). Every plugin row / preset tile carries a
 * payload — kind "titles" for Titles & Text (extra = animation key), kind
 * "generator" for everything else (extra = MediaGeneratorParams payload, see
 * mediaGeneratorParamsToPayload()).
 */
constexpr int kDragKindRole = Qt::UserRole + 20;
constexpr int kDragNameRole = Qt::UserRole + 21;
constexpr int kDragExtraRole = Qt::UserRole + 22;

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
            m_pressItem = itemAt(event->pos());
            m_pressPos = event->pos();
        }
        QListWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_pressItem || !(event->buttons() & Qt::LeftButton)) {
            QListWidget::mouseMoveEvent(event);
            return;
        }
        if ((event->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance()) {
            QListWidget::mouseMoveEvent(event);
            return;
        }
        QListWidgetItem *item = m_pressItem;
        m_pressItem = nullptr; // one-shot: don't re-arm until the next press
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
    QListWidgetItem *m_pressItem = nullptr;
    QPoint m_pressPos;
};

MediaGeneratorPane::Preset makePreset(const QString &name, MediaGeneratorPattern pat,
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
 * fg = text color; presetIcon() renders real animated text for these (see the
 * animationKey doc on MediaGeneratorPane::Preset). Background comes from
 * titlesTextPresetVisuals() (real recovered data), not a caller-supplied argument, so
 * the catalog can never drift from the same source video/TitlesTextApply.cpp uses to
 * build the actual placed event: transparent for 48 of the 51 presets, real opaque
 * fills for Drop Split (#00FFFF)/Menace (#FFFFFF)/Rough Day (#FFFF00) — presetIcon()
 * shows either honestly, via a checkerboard for the transparent ones. animationLabel
 * defaults to displayName when omitted (the common case: a preset named "Bounce" uses
 * the "Bounce" animation) — pass it explicitly for the default preset, which Vegas
 * displays as "Sample Text" in the grid but maps to the "None" animation. sampleText is
 * for the 25 Title-N presets only, whose caption ("Title01") differs from their real
 * placed text — see Preset::sampleText.
 */
MediaGeneratorPane::Preset makeTitlesTextPreset(const QString &displayName, QColor fg,
                                                const QString &animationLabel = QString(),
                                                const QString &sampleText = QString())
{
    MediaGeneratorPane::Preset p;
    p.name = displayName;
    p.animationKey = animationKeyForLabel(animationLabel.isEmpty() ? displayName : animationLabel);
    p.c0 = titlesTextPresetVisuals(p.animationKey).backgroundColor;
    p.c1 = fg;
    p.sampleText = sampleText;
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
            makePreset(QStringLiteral("(Default)"), MediaGeneratorPattern::Checker, c(0x11, 0x11, 0x11), c(0xee, 0xee, 0xee), 12),
            makePreset(QStringLiteral("Large Tiles"), MediaGeneratorPattern::Checker, c(0x11, 0x11, 0x11), c(0xee, 0xee, 0xee), 20),
            makePreset(QStringLiteral("Small Tiles"), MediaGeneratorPattern::Checker, c(0x11, 0x11, 0x11), c(0xee, 0xee, 0xee), 6),
            makePreset(QStringLiteral("Horizontal Blinds"), MediaGeneratorPattern::HBlinds),
            makePreset(QStringLiteral("Vertical Blinds"), MediaGeneratorPattern::VBlinds),
            makePreset(QStringLiteral("Grille"), MediaGeneratorPattern::Grille),
            makePreset(QStringLiteral("Fence"), MediaGeneratorPattern::Fence),
            makePreset(QStringLiteral("Ridges"), MediaGeneratorPattern::Ridges),
            makePreset(QStringLiteral("Bumps"), MediaGeneratorPattern::Bumps),
            makePreset(QStringLiteral("Plaid"), MediaGeneratorPattern::Plaid),
            makePreset(QStringLiteral("Letterbox"), MediaGeneratorPattern::Letterbox),
            makePreset(QStringLiteral("Split Screen"), MediaGeneratorPattern::SplitScreen),
            makePreset(QStringLiteral("Horizon"), MediaGeneratorPattern::Horizon),
        };
        add(std::move(p));
    }

    auto simple = [&](const QString &name, const QStringList &cats, const QString &desc,
                      MediaGeneratorPattern pat, QColor a, QColor b, bool gpu = false) {
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
           QStringLiteral("Linear and radial color gradients."), MediaGeneratorPattern::Gradient,
           c(0x20, 0x40, 0x80), c(0xc0, 0x60, 0x30), true);
    simple(QStringLiteral("Credit Roll"), {QStringLiteral("Titles and Text")},
           QStringLiteral("Scrolling credit / title roll."), MediaGeneratorPattern::Gradient,
           c(0x10, 0x10, 0x10), c(0x40, 0x40, 0x50));
    simple(QStringLiteral("Noise Texture"), {QStringLiteral("Creative"), QStringLiteral("Utility")},
           QStringLiteral("Procedural noise texture generator."), MediaGeneratorPattern::Bumps,
           c(0x30, 0x30, 0x30), c(0x90, 0x90, 0x90), true);
    simple(QStringLiteral("Solid Color"), {QStringLiteral("Utility")},
           QStringLiteral("Solid color media event."), MediaGeneratorPattern::Gradient,
           c(0x00, 0x78, 0xd7), c(0x00, 0x78, 0xd7));
    simple(QStringLiteral("Test Pattern"), {QStringLiteral("Utility")},
           QStringLiteral("Broadcast / calibration test patterns."), MediaGeneratorPattern::Grille,
           c(0x11, 0x11, 0x11), c(0xee, 0xee, 0xee));
    {
        Plugin p;
        p.name = QStringLiteral("Titles & Text");
        p.categories = {QStringLiteral("Titles and Text"), QStringLiteral("Creative")};
        p.grouping = QStringLiteral("OpenVegas");
        p.version = QStringLiteral("1.0");
        p.description = QStringLiteral("Titles and text overlays.");
        p.gpu = true;
        // All 53 real Vegas presets (catalog grid order: Default, Title01–25, then the 25
        // named animations alphabetically, then Placeholder/Subtitles) — recovered from
        // SAMPLES/veg_project/project_titles-and-text.veg (real AnimationName + TextColor
        // + Background + Scale + sample text per instance; see
        // video/TitlesTextApply.cpp's presetTable() for the same data feeding the actual
        // placed event, and makeTitlesTextPreset() for where the background comes from —
        // no bg argument here: transparent for 48 of the 51 presets (shown via a real
        // checkerboard, not an invented solid fill), real opaque fills for Drop
        // Split/Menace/Rough Day.
        p.presets = {
            makeTitlesTextPreset(QStringLiteral("Sample Text"), c(0xff, 0xff, 0xff),
                                 QStringLiteral("None")),
            makeTitlesTextPreset(QStringLiteral("Title01"), c(0xff, 0xff, 0xff), {},
                                 QStringLiteral("UNIQUE\n\nTYPOGRAPHY")),
            makeTitlesTextPreset(QStringLiteral("Title02"), c(0xff, 0xff, 0xff), {},
                                 QStringLiteral("DREAM BIG\n\nAND DARE\n\nTO FAIL")),
            makeTitlesTextPreset(QStringLiteral("Title03"), c(0xff, 0xff, 0xff), {},
                                 QStringLiteral("FREE CUSTOMER\n\nSUPPORT")),
            makeTitlesTextPreset(
                QStringLiteral("Title04"), c(0xff, 0xff, 0xff), {},
                QStringLiteral("The timeline editing tools make editing fast and easy.\n\n"
                               "But more importantly, they bring out your creativity,\n\n"
                               "because ideas flow freely when you're not\n\n"
                               "preoccupied by clumsy editing tools.")),
            makeTitlesTextPreset(QStringLiteral("Title05"), c(0xff, 0xff, 0xff), {},
                                 QStringLiteral("THE\n\nWORLD\n\nFASHION\n\nFESTIVAL 2022")),
            makeTitlesTextPreset(QStringLiteral("Title06"), c(0xff, 0xff, 0xff), {},
                                 QStringLiteral("VERY\n\nEASY\nTO USE")),
            makeTitlesTextPreset(QStringLiteral("Title07"), c(0xff, 0xff, 0xff), {},
                                 QStringLiteral("VEGAS TITLES\n\n TITLES & TEXT PACK")),
            makeTitlesTextPreset(QStringLiteral("Title08"), c(0xff, 0xff, 0xff), {},
                                 QStringLiteral("20:00\n\nLIVE\n\nSTREAM")),
            makeTitlesTextPreset(QStringLiteral("Title09"), c(0xff, 0xff, 0xff), {},
                                 QStringLiteral("WELL\nORGANIZED\nPRESETS")),
            makeTitlesTextPreset(QStringLiteral("Title10"), c(0xff, 0xff, 0xff), {},
                                 QStringLiteral("BUSINESS CORPORATE\nTitle Presets Pack")),
            makeTitlesTextPreset(QStringLiteral("Title11"), c(0xff, 0xff, 0xff), {},
                                 QStringLiteral("AWESOME\nTYPOGRAPHY\nfor Titles & Text")),
            makeTitlesTextPreset(QStringLiteral("Title12"), c(0xff, 0xff, 0xff), {},
                                 QStringLiteral("SMOOTH\nMOVEMENT\n\nAND CLEAN\n  DESIGN")),
            makeTitlesTextPreset(
                QStringLiteral("Title13"), c(0xff, 0xff, 0xff), {},
                QStringLiteral("HAPPY          \nHOLIDAYS\n   CELEBRATION\n OPEN NOW!")),
            makeTitlesTextPreset(QStringLiteral("Title14"), c(0xff, 0xff, 0xff), {},
                                 QStringLiteral("SMOOTH TITLE\nANIMATION\nPACKAGE")),
            makeTitlesTextPreset(QStringLiteral("Title15"), c(0xff, 0xff, 0xff), {},
                                 QStringLiteral("#ONLY ON\n\nTitles & Text")),
            makeTitlesTextPreset(QStringLiteral("Title16"), c(0xff, 0xff, 0xff), {},
                                 QStringLiteral("UNIQUE TITLES\nFOR YOUR PROJECTS")),
            makeTitlesTextPreset(QStringLiteral("Title17"), c(0xff, 0xff, 0xff), {},
                                 QStringLiteral("VEGAS MARKET\nCREATIVE COMMUNITY")),
            makeTitlesTextPreset(QStringLiteral("Title18"), c(0xff, 0xff, 0xff), {},
                                 QStringLiteral("VEGAS LIBRARY\nTitles & Text Pack")),
            makeTitlesTextPreset(QStringLiteral("Title19"), c(0xff, 0xff, 0xff), {},
                                 QStringLiteral("DREAM BIG\n\nAND DARE\n\nTO FAIL")),
            makeTitlesTextPreset(QStringLiteral("Title20"), c(0xff, 0xff, 0xff), {},
                                 QStringLiteral("TITLES & TEXT\nPACK FOR\nYOUR PROJECTS")),
            makeTitlesTextPreset(QStringLiteral("Title21"), c(0xff, 0xff, 0xff), {},
                                 QStringLiteral("The Good\n\nDesign\n\nAwards\n\n2022")),
            makeTitlesTextPreset(QStringLiteral("Title22"), c(0xff, 0xff, 0xff), {},
                                 QStringLiteral("VEGAS\n\nPRESENTS")),
            makeTitlesTextPreset(QStringLiteral("Title23"), c(0xff, 0xff, 0xff), {},
                                 QStringLiteral("MODERN\n\nUNIQUE\n\nDESIGN")),
            makeTitlesTextPreset(QStringLiteral("Title24"), c(0xff, 0xff, 0xff), {},
                                 QStringLiteral("FOR YOUR\nPROMO")),
            makeTitlesTextPreset(QStringLiteral("Title25"), c(0xff, 0xff, 0xff), {},
                                 QStringLiteral("EVENT")),
            makeTitlesTextPreset(QStringLiteral("Action Flip"), c(0xff, 0x00, 0x00)),
            makeTitlesTextPreset(QStringLiteral("Bounce"), c(0x00, 0x80, 0x00)),
            makeTitlesTextPreset(QStringLiteral("Coming at You"), c(0xff, 0xff, 0x00)),
            makeTitlesTextPreset(QStringLiteral("Double Flash Glow"), c(0xff, 0x00, 0xff)),
            makeTitlesTextPreset(QStringLiteral("Drop Split"), c(0x00, 0x00, 0x00)),
            makeTitlesTextPreset(QStringLiteral("Dropping Words"), c(0xff, 0xff, 0xff)),
            makeTitlesTextPreset(QStringLiteral("Earthquake"), c(0x27, 0x10, 0xe7)),
            makeTitlesTextPreset(QStringLiteral("Fall Down"), c(0x00, 0xff, 0xff)),
            makeTitlesTextPreset(QStringLiteral("Float and Pop"), c(0xff, 0x00, 0x80)),
            makeTitlesTextPreset(QStringLiteral("Fly In"), c(0xff, 0xff, 0xff)),
            makeTitlesTextPreset(QStringLiteral("Fly in from Right"), c(0x80, 0x00, 0x80)),
            makeTitlesTextPreset(QStringLiteral("Jump"), c(0xff, 0x00, 0xff)),
            makeTitlesTextPreset(QStringLiteral("Menace"), c(0x00, 0x00, 0x00)),
            makeTitlesTextPreset(QStringLiteral("Popup"), c(0xff, 0xff, 0xff)),
            makeTitlesTextPreset(QStringLiteral("Rolling Glow and Enlarge"), c(0x00, 0x80, 0x00)),
            // Real recovered TextColor came back fully transparent (alpha 0) — approximated
            // black like its Menace/Drop Split shake/drop siblings (see TitlesTextApply.cpp).
            makeTitlesTextPreset(QStringLiteral("Rough Day"), c(0x00, 0x00, 0x00)),
            makeTitlesTextPreset(QStringLiteral("Scroll"), c(0xff, 0xff, 0xff)),
            makeTitlesTextPreset(QStringLiteral("Scroll Left"), c(0xff, 0x40, 0x40)),
            makeTitlesTextPreset(QStringLiteral("Slide"), c(0x00, 0xff, 0xff)),
            makeTitlesTextPreset(QStringLiteral("Slide Down"), c(0x00, 0x80, 0xff)),
            makeTitlesTextPreset(QStringLiteral("Slide Left"), c(0x00, 0x80, 0x80)),
            makeTitlesTextPreset(QStringLiteral("Slide Right"), c(0x00, 0x00, 0xff)),
            makeTitlesTextPreset(QStringLiteral("Slide Up"), c(0x00, 0x00, 0x80)),
            makeTitlesTextPreset(QStringLiteral("Speedy"), c(0xff, 0xff, 0x00)),
            makeTitlesTextPreset(QStringLiteral("Twist In"), c(0xff, 0xff, 0x00)),
            makeTitlesTextPreset(QStringLiteral("Placeholder"), c(0xff, 0x00, 0x00),
                                 QStringLiteral("None")),
            makeTitlesTextPreset(QStringLiteral("Subtitles"), c(0xff, 0xff, 0xff),
                                 QStringLiteral("None")),
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
        const bool valid = row >= 0 && row < presets.size();
        const QString animationKey = valid ? presets[row].animationKey : QString();
        // Title-N presets place their real (possibly multi-line) sample text, not the
        // "Title01" catalog caption — see Preset::sampleText.
        const QString content = (valid && !presets[row].sampleText.isEmpty())
                                    ? presets[row].sampleText
                                    : item->text();
        emit presetActivated(m_plugins[m_currentIndex].name, content, animationKey);
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
        const QString previewText = p.sampleText.isEmpty() ? p.name : p.sampleText;
        TitlesTextParams tp;
        tp.text = previewText;
        tp.fontFamily = QStringLiteral("Verdana");
        // Authored against a 1080-tall reference frame (see renderTitlesText) — scale
        // inversely with both the longest line (width) and the line count (height) so
        // preset text stays small with real margin around it, like Vegas's own preset
        // grid, instead of stretching edge-to-edge.
        int longestLine = 4;
        int lineCount = 0;
        for (const QString &line : previewText.split(QLatin1Char('\n'))) {
            longestLine = std::max(longestLine, int(line.size()));
            ++lineCount;
        }
        lineCount = std::max(1, lineCount);
        tp.fontSize = std::clamp(std::min(1400.0 / longestLine, 620.0 / lineCount), 40.0, 170.0);
        tp.bold = true;
        tp.alignment = TitlesTextAlignment::Center;
        tp.anchor = TitlesTextAnchor::MiddleCenter;
        tp.locationX = 0.5;
        tp.locationY = 0.5;
        tp.textColor = p.c1;
        // p.c0 is real recovered data (titlesTextPresetVisuals(), via makeTitlesTextPreset())
        // — opaque only for Drop Split/Menace/Rough Day, so renderTitlesText() paints that
        // fill itself below; the other 48 presets are genuinely transparent, composited
        // over a checkerboard here so the preview shows that honestly.
        tp.backgroundColor = p.c0;
        tp.animationName = p.animationKey;
        if (p.c0.alpha() > 0) {
            return QIcon(QPixmap::fromImage(renderTitlesText(tp, QSize(100, 62), progress)));
        }
        QImage img = checkerboardBackground(QSize(100, 62));
        QPainter compositor(&img);
        compositor.drawImage(0, 0, renderTitlesText(tp, QSize(100, 62), progress));
        compositor.end();
        return QIcon(QPixmap::fromImage(img));
    }

    // Same renderer the timeline uses for the real event (video/MediaGeneratorApply.h) —
    // the thumbnail is guaranteed to match what a drag/drop actually produces.
    MediaGeneratorParams gp;
    gp.pattern = p.pattern;
    gp.c0 = p.c0;
    gp.c1 = p.c1;
    gp.tile = p.tile;
    const QImage img = renderMediaGeneratorPattern(gp, QSize(100, 62));
    QPixmap pm = QPixmap::fromImage(img);

    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, false);
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
        // Drag the plugin row itself → its default (first) preset.
        if (!p.presets.isEmpty()) {
            const Preset &def = p.presets.first();
            if (!def.animationKey.isEmpty()) {
                item->setData(kDragKindRole, QStringLiteral("titles"));
                item->setData(kDragNameRole, def.sampleText.isEmpty() ? def.name : def.sampleText);
                item->setData(kDragExtraRole, def.animationKey);
            } else {
                MediaGeneratorParams gp;
                gp.pluginName = p.name;
                gp.pattern = def.pattern;
                gp.c0 = def.c0;
                gp.c1 = def.c1;
                gp.tile = def.tile;
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
            item->setData(kDragExtraRole, pr.animationKey);
        } else {
            MediaGeneratorParams gp;
            gp.pluginName = p.name;
            gp.pattern = pr.pattern;
            gp.c0 = pr.c0;
            gp.c1 = pr.c1;
            gp.tile = pr.tile;
            item->setData(kDragKindRole, QStringLiteral("generator"));
            item->setData(kDragNameRole, pr.name);
            item->setData(kDragExtraRole, mediaGeneratorParamsToPayload(gp));
        }
        item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
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
