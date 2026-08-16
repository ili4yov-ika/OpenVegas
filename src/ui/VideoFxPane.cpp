#include "ui/VideoFxPane.h"
#include "ui/IconFactory.h"
#include "audio/BuiltinDsp.h"
#include "io/MediaMime.h"
#include "plugins/OfxHost.h"
#include "plugins/VegasVideoPluginCatalog.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCursor>
#include <QElapsedTimer>
#include <QEvent>
#include <QTimer>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
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
#include <algorithm>
#include <utility>

namespace openvegas {

namespace {

QColor c(int r, int g, int b) { return QColor(r, g, b); }

/** Plug-in display name carried by a draggable row/tile. */
constexpr int kDragPluginRole = Qt::UserRole + 30;
/** Preset carried by a draggable tile; empty on a plug-in row. */
constexpr int kDragPresetRole = Qt::UserRole + 31;

/**
 * List that starts a timeline drag instead of an internal item move.
 *
 * Used for both halves of the pane — a plug-in row drags the effect with its default
 * preset, a preset tile drags the effect with that preset — because from the timeline's
 * point of view the two are the same payload.
 */
class FxDragListWidget : public QListWidget {
public:
    using QListWidget::QListWidget;

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
        const QString plugin = item->data(kDragPluginRole).toString();
        if (plugin.isEmpty()) {
            return;
        }
        // name = plug-in, extra = preset — the timeline turns that into an FxSlot.
        QMimeData *md = MediaMime::fromSynthetic(QStringLiteral("videofx"), plugin,
                                                 item->data(kDragPresetRole).toString());
        auto *drag = new QDrag(this);
        drag->setMimeData(md);
        const QIcon icon = item->icon();
        if (!icon.isNull()) {
            const QPixmap pm = icon.pixmap(QSize(100, 62));
            drag->setPixmap(pm);
            drag->setHotSpot(QPoint(pm.width() / 2, pm.height() / 2));
        }
        drag->exec(Qt::CopyAction, Qt::CopyAction);
    }

private:
    QListWidgetItem *m_pressItem = nullptr;
    QPoint m_pressPos;
};

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

    auto addBuiltin = [&](const QString &name, const QStringList &cats, const QString &desc) {
        Plugin p;
        p.name = name;
        p.categories = cats;
        p.grouping = QStringLiteral("OpenVegas");
        p.version = QStringLiteral("1.0");
        p.description = desc;
        p.presets = {grad(QStringLiteral("(Default)"), c(0x30, 0x30, 0x38), c(0x50, 0x50, 0x60))};
        add(std::move(p));
    };

    addBuiltin(QStringLiteral("Pan/Crop"), {QStringLiteral("Utility")},
               QStringLiteral("Pan, crop, and mask (built-in)."));
    addBuiltin(QStringLiteral("Color Corrector"), {QStringLiteral("Color")},
               QStringLiteral("Primary color correction (built-in)."));
    addBuiltin(QStringLiteral("Color Grading"), {QStringLiteral("Color")},
               QStringLiteral("Lift / gamma / gain color grading (built-in)."));

    QHash<QString, int> known;
    for (int i = 0; i < m_plugins.size(); ++i) {
        known.insert(m_plugins[i].name.toLower(), i);
    }

    QVector<VegasVideoPluginEntry> catalog;
    if (m_scanner) {
        VegasVideoPluginCatalog::invalidateCache();
        catalog = VegasVideoPluginCatalog::discoverUsingScanner(*m_scanner);
    }
    if (catalog.isEmpty()) {
        catalog = VegasVideoPluginCatalog::discover();
    }

    for (const VegasVideoPluginEntry &e : catalog) {
        // Transitions and media generators live in their own panes. VEGAS groups several of
        // them plain "VEGAS", same as real effects, so only the declared OFX context tells
        // them apart — without this the list was padded with Page Roll, Push, Slide, Swap…
        if (!e.isVideoFx()) {
            continue;
        }
        const QString key = e.displayName.toLower();
        if (known.contains(key)) {
            m_plugins[known.value(key)].path = e.binaryPath;
            continue;
        }
        Plugin p;
        p.name = e.displayName;
        p.fullLabel = e.vegasLabel;
        p.categories = e.categories;
        p.grouping = e.grouping.isEmpty() ? QStringLiteral("VEGAS") : e.grouping;
        p.version = QStringLiteral("1.0");
        p.description = e.description.isEmpty() ? e.effectId : e.description;
        p.path = e.binaryPath;
        // Every plug-in leads with "(Default)" in VEGAS, then its named presets; the
        // bundle's preset package only lists the named ones.
        p.presets = {grad(QStringLiteral("(Default)"), c(0x30, 0x30, 0x38), c(0x50, 0x50, 0x60))};
        for (const QString &presetName : e.presets) {
            if (presetName.compare(QStringLiteral("(Default)"), Qt::CaseInsensitive) == 0) {
                continue;
            }
            Preset pr = grad(presetName, c(0x28, 0x28, 0x32), c(0x48, 0x48, 0x58));
            pr.params = e.presetParams.value(presetName);
            p.presets.push_back(std::move(pr));
        }
        known.insert(key, m_plugins.size());
        add(std::move(p));
    }

    // VEGAS lists the pane alphabetically; ours came out in bundle-scan order.
    std::sort(m_plugins.begin(), m_plugins.end(), [](const Plugin &a, const Plugin &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
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

    m_pluginList = new FxDragListWidget(m_splitter);
    m_pluginList->setObjectName(QStringLiteral("fxPluginList"));
    m_pluginList->setUniformItemSizes(true);
    m_pluginList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *mainCol = new QWidget(m_splitter);
    mainCol->setObjectName(QStringLiteral("fxMain"));
    auto *mainLay = new QVBoxLayout(mainCol);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);

    m_presetGrid = new FxDragListWidget(mainCol);
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
    m_presetGrid->viewport()->setMouseTracking(true);
    m_presetGrid->viewport()->installEventFilter(this);
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

    connect(m_pluginList, &QListWidget::itemClicked, this, [](QListWidgetItem *item) {
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

QIcon VideoFxPane::presetIcon(const Preset &p, double progress) const
{
    QPixmap pm(100, 62);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // VEGAS previews every preset by rendering the effect over a sample photo, so that is
    // what this does — through the real plug-in. When it can't render, the tile falls back
    // to the clean sample rather than to an invented approximation of the effect.
    const QPixmap &sample = presetSampleImage();
    if (!sample.isNull()) {
        const QSize target = sample.size().scaled(pm.size(), Qt::KeepAspectRatioByExpanding);
        const QPixmap clean =
            sample.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const QPoint at((pm.width() - clean.width()) / 2, (pm.height() - clean.height()) / 2);
        painter.drawPixmap(at, clean);

        if (!p.rendered.isNull()) {
            // 0 = fully applied (the resting tile), 0…1 strips the effect left-to-right,
            // 1…2 paints it back on. The effected image is drawn over the clean one,
            // clipped to the part that should still (or already) carry it.
            const double t = std::clamp(progress, 0.0, 2.0);
            const int w = pm.width();
            QRect band;
            if (t <= 1.0) {
                const int x = int(std::lround(t * w));
                band = QRect(x, 0, w - x, pm.height()); // effect survives to the right
            } else {
                const int x = int(std::lround((t - 1.0) * w));
                band = QRect(0, 0, x, pm.height()); // effect returns from the left
            }
            if (!band.isEmpty()) {
                painter.save();
                painter.setClipRect(band);
                painter.drawPixmap(at, p.rendered);
                painter.restore();
            }
        }
    } else if (p.radial) {
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

const QPixmap &VideoFxPane::presetSampleImage()
{
    // Loaded once: the pane rebuilds its tiles on every search keystroke and category switch.
    static const QPixmap sample(QStringLiteral(":/images/eye_preview.png"));
    return sample;
}

bool VideoFxPane::eventFilter(QObject *watched, QEvent *event)
{
    if (m_presetGrid && watched == m_presetGrid->viewport()) {
        if (event->type() == QEvent::MouseMove) {
            const auto *me = static_cast<QMouseEvent *>(event);
            const QModelIndex idx = m_presetGrid->indexAt(me->pos());
            if (idx.isValid()) {
                startHoverAnimation(idx.row());
            } else {
                stopHoverAnimation();
            }
        } else if (event->type() == QEvent::Leave) {
            stopHoverAnimation();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void VideoFxPane::renderPresetPreviews(int catalogIndex)
{
    if (catalogIndex < 0 || catalogIndex >= m_plugins.size()) {
        return;
    }
    Plugin &plugin = m_plugins[catalogIndex];
    if (plugin.path.isEmpty()) {
        return; // nothing installed to render through
    }
    const QPixmap &sample = presetSampleImage();
    if (sample.isNull()) {
        return;
    }

    const QSize target = sample.size().scaled(QSize(100, 62), Qt::KeepAspectRatioByExpanding);
    const QImage base =
        sample.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
            .toImage()
            .convertToFormat(QImage::Format_ARGB32);

    // Some effects are far too slow to preview — the AI ones load models on first render.
    // Rather than special-casing them by name, time the first preset and give up on the
    // rest of the effect if it blew the budget. The tiles then stay on the clean sample.
    constexpr qint64 kBudgetMs = 400;
    QElapsedTimer clock;

    for (Preset &preset : plugin.presets) {
        if (preset.renderAttempted) {
            continue;
        }
        preset.renderAttempted = true;

        FxSlot slot = VegasVideoPluginCatalog::slotFromDisplayName(plugin.name);
        if (slot.format != PluginFormat::Ofx) {
            continue; // builtins have no plug-in to run
        }
        if (!preset.params.isEmpty()) {
            slot.state = packFxParams(preset.params);
        }

        QImage frame = base;
        clock.start();
        const bool ok = OfxHost::instance().processSlot(slot, &frame, 0.0);
        const qint64 elapsed = clock.elapsed();
        if (ok && frame.size() == base.size()) {
            preset.rendered = QPixmap::fromImage(frame);
        }
        if (elapsed > kBudgetMs) {
            for (Preset &rest : plugin.presets) {
                rest.renderAttempted = true;
            }
            break;
        }
    }
}

void VideoFxPane::startHoverAnimation(int row)
{
    if (m_hoverRow == row) {
        return;
    }
    m_hoverRow = row;
    m_hoverProgress = 0.0;
    if (!m_hoverTimer) {
        m_hoverTimer = new QTimer(this);
        m_hoverTimer->setInterval(40);
        connect(m_hoverTimer, &QTimer::timeout, this, [this] {
            if (m_hoverRow < 0 || m_currentIndex < 0) {
                stopHoverAnimation();
                return;
            }
            const QVector<Preset> &presets = m_plugins[m_currentIndex].presets;
            if (m_hoverRow >= presets.size() || m_hoverRow >= m_presetGrid->count()) {
                stopHoverAnimation();
                return;
            }
            // 0 → 2 → wrap: strip the effect, put it back, repeat while hovered.
            m_hoverProgress += 0.05;
            if (m_hoverProgress >= 2.0) {
                m_hoverProgress = 0.0;
            }
            m_presetGrid->item(m_hoverRow)
                ->setIcon(presetIcon(presets[m_hoverRow], m_hoverProgress));
        });
    }
    m_hoverTimer->start();
}

void VideoFxPane::stopHoverAnimation()
{
    if (m_hoverTimer) {
        m_hoverTimer->stop();
    }
    const int row = m_hoverRow;
    m_hoverRow = -1;
    m_hoverProgress = 0.0;
    if (row < 0 || m_currentIndex < 0 || m_currentIndex >= m_plugins.size()) {
        return;
    }
    const QVector<Preset> &presets = m_plugins[m_currentIndex].presets;
    if (row < presets.size() && row < m_presetGrid->count()) {
        m_presetGrid->item(row)->setIcon(presetIcon(presets[row])); // back to fully applied
    }
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
        item->setData(kDragPluginRole, p.name); // dragging the row = effect at its default preset
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
    stopHoverAnimation();
    m_currentIndex = catalogIndex;
    // Lazily: only the effect being shown is rendered, and only once.
    renderPresetPreviews(catalogIndex);
    const Plugin &p = m_plugins[catalogIndex];

    m_presetGrid->clear();
    for (const Preset &pr : p.presets) {
        auto *item = new QListWidgetItem(presetIcon(pr), pr.name, m_presetGrid);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        item->setSizeHint(QSize(108, 88));
        item->setData(kDragPluginRole, p.name);
        item->setData(kDragPresetRole, pr.name);
    }
    if (m_presetGrid->count() > 0) {
        m_presetGrid->setCurrentRow(0);
    }

    // Mirrors VEGAS's own status line, which quotes the plug-in's full label (brand and
    // all) rather than the trimmed name shown in the list. "GPU Accelerated" is
    // deliberately absent: the manifest doesn't say, and only the loaded binary does.
    m_metaLine1->setText(tr("%1: OFX, 32-bit floating point, Grouping %2, Version %3")
                             .arg(p.fullLabel.isEmpty() ? p.name : p.fullLabel, p.grouping,
                                  p.version));
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
