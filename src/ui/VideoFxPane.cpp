#include "ui/VideoFxPane.h"
#include "ui/IconFactory.h"

#include <QButtonGroup>
#include <QCursor>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRadialGradient>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHash>
#include <QAbstractButton>
#include <utility>

namespace openvegas {

namespace {

QColor c(int r, int g, int b) { return QColor(r, g, b); }

VideoFxPane::Preset grad(const QString &name, QColor a, QColor b, bool radial = false)
{
    VideoFxPane::Preset p;
    p.name = name;
    p.c0 = a;
    p.c1 = b;
    p.radial = radial;
    return p;
}

} // namespace

VideoFxPane::VideoFxPane(PluginScanner *scanner, QWidget *parent)
    : QWidget(parent)
    , m_scanner(scanner)
{
    setObjectName(QStringLiteral("videoFxPane"));
    m_activeCategory = QStringLiteral("All Plug-ins");
    loadCatalog();
    loadFavorites();
    buildUi();
    rebuildPluginList();
    restoreSettings();
}

void VideoFxPane::loadCatalog()
{
    m_plugins.clear();

    auto add = [this](Plugin p) { m_plugins.push_back(std::move(p)); };

    const auto stylePresets = QVector<Preset>{
        grad(QStringLiteral("(Default)"), c(0x2a, 0x20, 0x30), c(0x5a, 0x40, 0x60)),
        grad(QStringLiteral("Self-Portrait (Picasso)"), c(0x6a, 0x40, 0x20), c(0xc0, 0x80, 0x40)),
        grad(QStringLiteral("Night Alley Walk"), c(0x10, 0x20, 0x40), c(0x30, 0x60, 0xa0)),
        grad(QStringLiteral("The Great Wave"), c(0x1a, 0x40, 0x60), c(0x80, 0xb0, 0xd0)),
        grad(QStringLiteral("Fruit (Hunt)"), c(0x60, 0x30, 0x10), c(0xc0, 0x60, 0x30)),
        grad(QStringLiteral("Light (Kheron)"), c(0x50, 0x40, 0x20), c(0xe0, 0xc0, 0x80)),
        grad(QStringLiteral("Floral pattern"), c(0x40, 0x20, 0x50), c(0xa0, 0x60, 0x90)),
        grad(QStringLiteral("Sunrise Flowers"), c(0x80, 0x40, 0x20), c(0xe0, 0xa0, 0x50)),
        grad(QStringLiteral("Abstract Painting"), c(0x20, 0x30, 0x60), c(0x80, 0x60, 0xa0)),
        grad(QStringLiteral("Black and White"), c(0x22, 0x22, 0x22), c(0x88, 0x88, 0x88)),
        grad(QStringLiteral("The Starry Night"), c(0x10, 0x20, 0x50), c(0x40, 0x60, 0xc0)),
        grad(QStringLiteral("The Weeping Woman"), c(0x60, 0x30, 0x20), c(0xc0, 0x70, 0x50)),
        grad(QStringLiteral("Bark pattern"), c(0x3a, 0x28, 0x18), c(0x8a, 0x60, 0x40)),
        grad(QStringLiteral("Leaf pattern"), c(0x1a, 0x40, 0x20), c(0x50, 0xa0, 0x40)),
        grad(QStringLiteral("Rick and Morty"), c(0x20, 0x60, 0x40), c(0x60, 0xc0, 0x80)),
        grad(QStringLiteral("Candy"), c(0x80, 0x20, 0x60), c(0xe0, 0x80, 0xc0)),
        grad(QStringLiteral("Mosaic"), c(0x40, 0x40, 0x60), c(0xa0, 0xa0, 0xc0)),
        grad(QStringLiteral("Pointillism"), c(0xc0, 0xa0, 0x60), c(0x40, 0x30, 0x20), true),
        grad(QStringLiteral("Rain Princess"), c(0x20, 0x40, 0x60), c(0x80, 0xa0, 0xc0)),
        grad(QStringLiteral("Udnie (Picasso)"), c(0x60, 0x20, 0x40), c(0xc0, 0x60, 0x80)),
        grad(QStringLiteral("Scream (Munch)"), c(0x80, 0x60, 0x40), c(0xe0, 0xc0, 0x80)),
        grad(QStringLiteral("Simpsons"), c(0xc0, 0xa0, 0x20), c(0x40, 0x60, 0xc0)),
    };

    auto simple = [&](const QString &name, const QStringList &cats, const QString &group,
                      const QString &desc, QColor a, QColor b) {
        Plugin p;
        p.name = name;
        p.categories = cats;
        p.grouping = group;
        p.version = QStringLiteral("1.0");
        p.description = desc;
        p.presets = {grad(QStringLiteral("(Default)"), a, b)};
        add(std::move(p));
    };

    simple(QStringLiteral("360 Stabilization"), {QStringLiteral("360°"), QStringLiteral("Utility")},
           QStringLiteral("OpenVegas/360"), QStringLiteral("Stabilize 360° footage."),
           c(0x20, 0x40, 0x60), c(0x40, 0x80, 0xa0));
    simple(QStringLiteral("Add Noise"), {QStringLiteral("Creative"), QStringLiteral("Utility")},
           QStringLiteral("OpenVegas"), QStringLiteral("Add film grain / noise."),
           c(0x30, 0x30, 0x30), c(0x70, 0x70, 0x70));
    simple(QStringLiteral("AI Auto Reframe"), {QStringLiteral("AI/ML")},
           QStringLiteral("OpenVegas/AI"), QStringLiteral("Auto-reframe for vertical video."),
           c(0x20, 0x30, 0x50), c(0x50, 0x70, 0xb0));
    simple(QStringLiteral("AI Colorization"), {QStringLiteral("AI/ML"), QStringLiteral("Color")},
           QStringLiteral("OpenVegas/AI"), QStringLiteral("Colorize black-and-white footage."),
           c(0x40, 0x20, 0x10), c(0xc0, 0x80, 0x40));
    simple(QStringLiteral("AI Dehaze"), {QStringLiteral("AI/ML")},
           QStringLiteral("OpenVegas/AI"), QStringLiteral("Remove haze and fog."),
           c(0x50, 0x60, 0x70), c(0xa0, 0xb0, 0xc0));
    simple(QStringLiteral("AI Sharpen"), {QStringLiteral("AI/ML")},
           QStringLiteral("OpenVegas/AI"), QStringLiteral("AI-based sharpening."),
           c(0x30, 0x30, 0x40), c(0x90, 0x90, 0xa0));
    simple(QStringLiteral("AI Smart Mask 2.0"), {QStringLiteral("AI/ML")},
           QStringLiteral("OpenVegas/AI"), QStringLiteral("Intelligent subject masking."),
           c(0x20, 0x40, 0x30), c(0x40, 0xa0, 0x70));
    simple(QStringLiteral("AI Smoothen"), {QStringLiteral("AI/ML")},
           QStringLiteral("OpenVegas/AI"), QStringLiteral("Skin / detail smoothening."),
           c(0x60, 0x40, 0x40), c(0xc0, 0xa0, 0xa0));

    {
        Plugin p;
        p.name = QStringLiteral("AI Style Transfer");
        p.categories = {QStringLiteral("AI/ML"), QStringLiteral("Creative")};
        p.grouping = QStringLiteral("OpenVegas/AI");
        p.version = QStringLiteral("1.0");
        p.description =
            QStringLiteral("Transforming the appearance of famous paintings to user-supplied clips.");
        p.presets = stylePresets;
        add(std::move(p));
    }

    simple(QStringLiteral("AI Upscale"), {QStringLiteral("AI/ML")},
           QStringLiteral("OpenVegas/AI"), QStringLiteral("Upscale resolution with AI."),
           c(0x20, 0x20, 0x40), c(0x60, 0x60, 0xc0));
    simple(QStringLiteral("AI Z-Depth"), {QStringLiteral("AI/ML")},
           QStringLiteral("OpenVegas/AI"), QStringLiteral("Estimate depth map."),
           c(0x10, 0x10, 0x30), c(0x80, 0x80, 0xc0));
    simple(QStringLiteral("AutoLooks"), {QStringLiteral("Color"), QStringLiteral("Creative")},
           QStringLiteral("OpenVegas"), QStringLiteral("Automatic look matching."),
           c(0x40, 0x30, 0x20), c(0xc0, 0x90, 0x50));
    simple(QStringLiteral("Bézier Masking"), {QStringLiteral("Utility")},
           QStringLiteral("OpenVegas"), QStringLiteral("Bézier mask tool."),
           c(0x30, 0x40, 0x50), c(0x60, 0x80, 0xa0));
    simple(QStringLiteral("Black and White"), {QStringLiteral("Color")},
           QStringLiteral("OpenVegas"), QStringLiteral("Convert to monochrome."),
           c(0x18, 0x18, 0x18), c(0x90, 0x90, 0x90));
    simple(QStringLiteral("Black Bar Fill"), {QStringLiteral("Utility")},
           QStringLiteral("OpenVegas"), QStringLiteral("Fill letterbox bars."),
           c(0x10, 0x10, 0x10), c(0x40, 0x40, 0x40));
    simple(QStringLiteral("Brightness and Contrast"), {QStringLiteral("Color"), QStringLiteral("Utility")},
           QStringLiteral("OpenVegas"), QStringLiteral("Adjust brightness and contrast."),
           c(0x20, 0x20, 0x20), c(0xd0, 0xd0, 0xd0));
    simple(QStringLiteral("Channel Blend"), {QStringLiteral("Color"), QStringLiteral("Creative")},
           QStringLiteral("OpenVegas"), QStringLiteral("Blend color channels."),
           c(0x60, 0x20, 0x20), c(0x20, 0x20, 0x60));
    simple(QStringLiteral("Color Corrector"), {QStringLiteral("Color")},
           QStringLiteral("OpenVegas"), QStringLiteral("Primary color correction."),
           c(0x40, 0x20, 0x10), c(0x20, 0x40, 0x60));
    simple(QStringLiteral("Gaussian Blur"), {QStringLiteral("Blur")},
           QStringLiteral("OpenVegas"), QStringLiteral("Gaussian blur filter."),
           c(0x40, 0x40, 0x50), c(0xa0, 0xa0, 0xb0));

    // Append scanned OFX (Third Party) if available
    if (m_scanner) {
        const auto scanned = m_scanner->scanOfx();
        QHash<QString, int> known;
        for (int i = 0; i < m_plugins.size(); ++i) {
            known.insert(m_plugins[i].name.toLower(), i);
        }
        for (const PluginInfo &info : scanned) {
            if (info.name.startsWith(QLatin1Char('(')) || info.path.isEmpty()) {
                // skip stubs / empty markers unless unique
                if (info.path.isEmpty()) {
                    continue;
                }
            }
            const QString key = info.name.toLower();
            if (known.contains(key)) {
                m_plugins[known.value(key)].path = info.path;
                continue;
            }
            Plugin p;
            p.name = info.name;
            p.categories = {QStringLiteral("Third Party")};
            p.grouping = QStringLiteral("OFX");
            p.version = QStringLiteral("—");
            p.description = info.path;
            p.path = info.path;
            p.presets = {grad(QStringLiteral("(Default)"), c(0x30, 0x30, 0x38), c(0x50, 0x50, 0x60))};
            known.insert(key, m_plugins.size());
            add(std::move(p));
        }
    }
}

void VideoFxPane::loadFavorites()
{
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    const QStringList favs = s.value(QStringLiteral("videoFx/favorites")).toStringList();
    for (Plugin &p : m_plugins) {
        p.favorite = favs.contains(p.name);
    }
}

void VideoFxPane::saveFavorites()
{
    QStringList favs;
    for (const Plugin &p : m_plugins) {
        if (p.favorite) {
            favs << p.name;
        }
    }
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    s.setValue(QStringLiteral("videoFx/favorites"), favs);
}

void VideoFxPane::refreshFromScanner()
{
    const QString current = m_currentIndex >= 0 ? m_plugins[m_currentIndex].name : QString();
    loadCatalog();
    loadFavorites();
    rebuildPluginList();
    for (int i = 0; i < m_plugins.size(); ++i) {
        if (m_plugins[i].name == current) {
            showPlugin(i);
            return;
        }
    }
    if (!m_plugins.isEmpty()) {
        showPlugin(0);
    }
}

void VideoFxPane::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Search toolbar
    auto *tb = new QWidget(this);
    tb->setObjectName(QStringLiteral("fxToolbar"));
    tb->setFixedHeight(28);
    auto *tbLay = new QHBoxLayout(tb);
    tbLay->setContentsMargins(6, 4, 6, 4);
    tbLay->setSpacing(0);

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

    // Category chips
    auto *chipsScroll = new QScrollArea(this);
    chipsScroll->setObjectName(QStringLiteral("fxChipsScroll"));
    chipsScroll->setWidgetResizable(true);
    chipsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    chipsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    chipsScroll->setFixedHeight(28);
    chipsScroll->setFrameShape(QFrame::NoFrame);

    m_chipsHost = new QWidget;
    m_chipsHost->setObjectName(QStringLiteral("fxChips"));
    m_chipsLay = new QHBoxLayout(m_chipsHost);
    m_chipsLay->setContentsMargins(6, 4, 6, 4);
    m_chipsLay->setSpacing(2);
    m_chipGroup = new QButtonGroup(this);
    m_chipGroup->setExclusive(true);

    const QStringList chips = {QStringLiteral("All Plug-ins"), QStringLiteral("AI/ML"),
                               QStringLiteral("Creative"),     QStringLiteral("Color"),
                               QStringLiteral("Utility"),      QStringLiteral("Blur"),
                               QStringLiteral("360°"),         QStringLiteral("Third Party"),
                               QStringLiteral("★")};
    for (int i = 0; i < chips.size(); ++i) {
        auto *btn = new QPushButton(chips[i], m_chipsHost);
        btn->setObjectName(chips[i] == QLatin1String("★") ? QStringLiteral("fxChipStar")
                                                          : QStringLiteral("fxChip"));
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFlat(true);
        if (i == 0) {
            btn->setChecked(true);
        }
        m_chipGroup->addButton(btn, i);
        m_chipsLay->addWidget(btn);
    }
    m_chipsLay->addStretch(1);
    chipsScroll->setWidget(m_chipsHost);
    root->addWidget(chipsScroll);

    connect(m_chipGroup, &QButtonGroup::idClicked, this, [this, chips](int id) {
        if (id >= 0 && id < chips.size()) {
            m_activeCategory = chips[id];
            applySearchAndCategory();
        }
    });

    // Body: list + presets
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

    m_presetGrid = new QListWidget(mainCol);
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
                const int idx = cur->data(Qt::UserRole).toInt();
                showPlugin(idx);
            });

    connect(m_pluginList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!item) {
            return;
        }
        // Click on star area (left ~18px): toggle favorite
        // Approximate: if item text starts handling via separate mechanism
        Q_UNUSED(item);
    });

    connect(m_pluginList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (!item || m_currentIndex < 0) {
            return;
        }
        emit pluginActivated(m_plugins[m_currentIndex].name);
    });

    connect(m_presetGrid, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (!item || m_currentIndex < 0) {
            return;
        }
        emit presetActivated(m_plugins[m_currentIndex].name, item->text());
    });

    // Star toggle via middle-click or context — use itemChanged won't work.
    // Intercept clicks: custom — use Qt::UserRole+1 and check keyboard modifier Alt,
    // or provide context menu. Simpler: clicking the leading star character via
    // custom widget. Use itemPressed + position.
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
                    // reselect
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

QIcon VideoFxPane::presetIcon(const Preset &p) const
{
    QPixmap pm(100, 62);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing);
    if (p.radial) {
        QRadialGradient g(30, 30, 70);
        g.setColorAt(0, p.c0);
        g.setColorAt(1, p.c1);
        painter.fillRect(pm.rect(), g);
    } else {
        QLinearGradient g(0, 0, 100, 62);
        g.setColorAt(0, p.c0);
        g.setColorAt(1, p.c1);
        painter.fillRect(pm.rect(), g);
    }
    painter.setPen(QColor(0x33, 0x33, 0x33));
    painter.drawRect(0, 0, 99, 61);
    painter.end();
    return QIcon(pm);
}

void VideoFxPane::rebuildPluginList()
{
    applySearchAndCategory();
}

void VideoFxPane::applySearchAndCategory()
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
        item->setToolTip(p.path.isEmpty() ? p.description : p.path);
    }

    // Restore selection if still visible
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

void VideoFxPane::showPlugin(int catalogIndex)
{
    if (catalogIndex < 0 || catalogIndex >= m_plugins.size()) {
        return;
    }
    m_currentIndex = catalogIndex;
    const Plugin &p = m_plugins[catalogIndex];

    m_presetGrid->clear();
    for (const Preset &pr : p.presets) {
        auto *item = new QListWidgetItem(presetIcon(pr), pr.name, m_presetGrid);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        item->setSizeHint(QSize(108, 88));
    }
    if (m_presetGrid->count() > 0) {
        m_presetGrid->setCurrentRow(0);
    }

    m_metaLine1->setText(tr("%1: OFX, 32-bit floating point, Grouping: %2, Version %3")
                             .arg(p.name, p.grouping, p.version));
    m_metaLine2->setText(tr("Description: %1").arg(p.description));
}

void VideoFxPane::saveSettings() const
{
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    s.setValue(QStringLiteral("videoFx/category"), m_activeCategory);
    if (m_currentIndex >= 0 && m_currentIndex < m_plugins.size()) {
        s.setValue(QStringLiteral("videoFx/plugin"), m_plugins[m_currentIndex].name);
    }
    if (m_search) {
        s.setValue(QStringLiteral("videoFx/search"), m_search->text());
    }
    if (m_splitter) {
        s.setValue(QStringLiteral("videoFx/splitter"), m_splitter->saveState());
    }
}

void VideoFxPane::restoreSettings()
{
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    if (m_splitter) {
        const QByteArray st = s.value(QStringLiteral("videoFx/splitter")).toByteArray();
        if (!st.isEmpty()) {
            m_splitter->restoreState(st);
        }
    }

    const QString cat = s.value(QStringLiteral("videoFx/category"), QStringLiteral("All Plug-ins")).toString();
    m_activeCategory = cat;
    if (m_chipGroup) {
        const QList<QAbstractButton *> buttons = m_chipGroup->buttons();
        for (QAbstractButton *b : buttons) {
            if (b->text() == cat) {
                b->setChecked(true);
                break;
            }
        }
    }

    if (m_search) {
        const QString q = s.value(QStringLiteral("videoFx/search")).toString();
        if (!q.isEmpty()) {
            m_search->setText(q);
        }
    }

    applySearchAndCategory();

    const QString plugin = s.value(QStringLiteral("videoFx/plugin"), QStringLiteral("AI Style Transfer"))
                               .toString();
    bool found = false;
    for (int i = 0; i < m_plugins.size(); ++i) {
        if (m_plugins[i].name == plugin) {
            // Select in list if visible
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
    }
    if (!found && m_pluginList->count() > 0) {
        m_pluginList->setCurrentRow(0);
    } else if (!found && !m_plugins.isEmpty()) {
        showPlugin(0);
    }
}

} // namespace openvegas
