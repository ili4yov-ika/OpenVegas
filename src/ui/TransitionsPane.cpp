#include "ui/TransitionsPane.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCursor>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QListView>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QVBoxLayout>
#include <utility>

namespace openvegas {

namespace {

QColor accent() { return QColor(0x1a, 0x4a, 0x8a); }
QColor light() { return QColor(0xc0, 0xd0, 0xe0); }

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
    auto add = [this](Plugin p) { m_plugins.push_back(std::move(p)); };

    auto addSimple = [&](const QString &name, const QStringList &cats, Thumb thumb,
                         const QString &fmt = QStringLiteral("DXT")) {
        Plugin p;
        p.name = name;
        p.categories = cats;
        p.format = fmt;
        p.description = tr("Transition plug-in.");
        p.presets = {makePreset(QStringLiteral("(Default)"), thumb),
                     makePreset(QStringLiteral("Variant A"), thumb, QColor(0x20, 0x60, 0xa0)),
                     makePreset(QStringLiteral("Variant B"), thumb, QColor(0x40, 0x30, 0x70))};
        add(std::move(p));
    };

    {
        Plugin p;
        p.name = QStringLiteral("3D Blinds");
        p.categories = {QStringLiteral("3D Effects"), QStringLiteral("Reveals")};
        p.format = QStringLiteral("DXT");
        p.description = QStringLiteral("3D blinds transition between clips.");
        p.presets = {
            makePreset(QStringLiteral("Simple"), Thumb::SimpleBlinds),
            makePreset(QStringLiteral("Left to Right"), Thumb::LeftToRight),
            makePreset(QStringLiteral("Slot Machine"), Thumb::SlotMachine),
            makePreset(QStringLiteral("Spin"), Thumb::Spin),
        };
        add(std::move(p));
    }

    addSimple(QStringLiteral("3D Cascade"), {QStringLiteral("3D Effects")}, Thumb::Push);
    addSimple(QStringLiteral("3D Fly In/Out"), {QStringLiteral("3D Effects")}, Thumb::Push);
    addSimple(QStringLiteral("3D Shuffle"), {QStringLiteral("3D Effects")}, Thumb::SlotMachine);
    addSimple(QStringLiteral("Barn Door"), {QStringLiteral("Reveals"), QStringLiteral("Wipes")}, Thumb::Wipe);
    addSimple(QStringLiteral("Clock Wipe"), {QStringLiteral("Wipes")}, Thumb::Iris);
    addSimple(QStringLiteral("Cross Effect"), {QStringLiteral("Fades"), QStringLiteral("Reveals")},
              Thumb::Dissolve);
    addSimple(QStringLiteral("Dissolve"), {QStringLiteral("Fades")}, Thumb::Dissolve);
    addSimple(QStringLiteral("Flash"), {QStringLiteral("Fades")}, Thumb::Dissolve, QStringLiteral("OFX"));
    addSimple(QStringLiteral("GL Transition"), {QStringLiteral("3D Effects"), QStringLiteral("Reveals")},
              Thumb::Spin, QStringLiteral("OFX"));
    addSimple(QStringLiteral("Gradient Wipe"), {QStringLiteral("Wipes")}, Thumb::Wipe);
    addSimple(QStringLiteral("Iris"), {QStringLiteral("Wipes"), QStringLiteral("Reveals")}, Thumb::Iris);
    addSimple(QStringLiteral("Linear Wipe"), {QStringLiteral("Wipes")}, Thumb::Wipe);
    addSimple(QStringLiteral("Page Loop"), {QStringLiteral("Loops and Peels")}, Thumb::Page);
    addSimple(QStringLiteral("Page Peel"), {QStringLiteral("Loops and Peels")}, Thumb::Page);
    addSimple(QStringLiteral("Page Roll"), {QStringLiteral("Loops and Peels")}, Thumb::Page);
    addSimple(QStringLiteral("Portals"), {QStringLiteral("Reveals"), QStringLiteral("3D Effects")},
              Thumb::Iris, QStringLiteral("OFX"));
    addSimple(QStringLiteral("Push"), {QStringLiteral("Reveals")}, Thumb::Push);
    addSimple(QStringLiteral("Slide"), {QStringLiteral("Reveals"), QStringLiteral("Wipes")}, Thumb::Push);
    addSimple(QStringLiteral("Spiral"), {QStringLiteral("Wipes"), QStringLiteral("Reveals")}, Thumb::Spin);
    addSimple(QStringLiteral("Split"), {QStringLiteral("Wipes"), QStringLiteral("Reveals")}, Thumb::Wipe);
    addSimple(QStringLiteral("Squeeze"), {QStringLiteral("Reveals")}, Thumb::Push);
    addSimple(QStringLiteral("Star Wipe"), {QStringLiteral("Wipes")}, Thumb::Iris);
    addSimple(QStringLiteral("Swap"), {QStringLiteral("Reveals")}, Thumb::Push);
    addSimple(QStringLiteral("Venetian Blinds"), {QStringLiteral("Wipes"), QStringLiteral("Reveals")},
              Thumb::SimpleBlinds);
    addSimple(QStringLiteral("Warp Flow"), {QStringLiteral("3D Effects"), QStringLiteral("Reveals")},
              Thumb::Spin, QStringLiteral("OFX"));
    addSimple(QStringLiteral("Zoom"), {QStringLiteral("Reveals"), QStringLiteral("3D Effects")}, Thumb::Iris);
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

    m_presetGrid = new QListWidget(mainCol);
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

    m_presetGrid->clear();
    for (const Preset &pr : p.presets) {
        auto *item = new QListWidgetItem(presetIcon(pr), pr.name, m_presetGrid);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        item->setSizeHint(QSize(140, 104));
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
