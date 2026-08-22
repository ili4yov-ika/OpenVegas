#include "ui/PluginChooserDialog.h"
#include "ui_PluginChooserDialog.h"
#include "plugins/AudioPluginRegistry.h"
#include "ui/IconFactory.h"
#include "ui/PreferencesDialog.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QListWidget>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>

namespace openvegas {

PluginChooserDialog::PluginChooserDialog(PluginScanner *scanner, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::PluginChooserDialog)
    , m_scanner(scanner)
{
    ui->setupUi(this);
    setWindowTitle(tr("Plug-In Chooser"));

    // Insert category tree to the left of the list (Vegas-style)
    auto *split = new QSplitter(Qt::Horizontal, this);
    auto *tree = new QTreeWidget(split);
    tree->setObjectName(QStringLiteral("categoryTree"));
    tree->setHeaderHidden(true);
    tree->setMinimumWidth(160);
    tree->setMaximumWidth(240);

    // Re-parent existing list into splitter
    ui->verticalLayout->removeWidget(ui->pluginList);
    ui->pluginList->setParent(split);
    ui->pluginList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // VEGAS lays the chooser out in columns: entries fill downwards and wrap into the
    // next column, so the window scrolls sideways instead of becoming one long strip.
    ui->pluginList->setFlow(QListView::TopToBottom);
    ui->pluginList->setWrapping(true);
    ui->pluginList->setResizeMode(QListView::Adjust);
    ui->pluginList->setUniformItemSizes(true);
    // 18px rather than the usual 16: the badges carry lettering, and below this it
    // collapses. Colour is what actually identifies the format at list size — the dark-ink
    // badges (VST2 especially) have too little contrast against #1e1e1e for their glyphs
    // to read — so the exact format is also spelled out in each row's tooltip.
    ui->pluginList->setIconSize(QSize(18, 18));
    ui->pluginList->setGridSize(QSize(216, 22));
    ui->pluginList->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui->pluginList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    split->addWidget(tree);
    split->addWidget(ui->pluginList);
    split->setStretchFactor(1, 1);
    ui->verticalLayout->insertWidget(1, split);

    ui->settingsButton->setIcon(IconFactory::iconFromSvgBody(IconFactory::svgGear()));
    connect(ui->settingsButton, &QToolButton::clicked, this, [this]() { openPluginSettings(); });

    connect(ui->filterEdit, &QLineEdit::textChanged, this, [this](const QString &) { applyFilter(); });
    connect(tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *, QTreeWidgetItem *) {
        onCategoryChanged();
    });
    connect(ui->pluginList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        if (!ui->pluginList->selectedItems().isEmpty()) {
            accept();
        }
    });

    rebuildTree();
    refresh();
}

PluginChooserDialog::~PluginChooserDialog()
{
    delete ui;
}

void PluginChooserDialog::setAudioMode(bool audio)
{
    m_audioMode = audio;
    setWindowTitle(audio ? tr("Plug-In Chooser — Audio") : tr("Plug-In Chooser — Video / OFX"));
    rebuildTree();
    refresh();
}

void PluginChooserDialog::rebuildTree()
{
    auto *tree = findChild<QTreeWidget *>(QStringLiteral("categoryTree"));
    if (!tree) {
        return;
    }
    tree->clear();
    if (m_audioMode) {
        // Folder set as VEGAS shows it. "FX Packages" is deliberately absent: those are
        // saved plug-in chains, which OpenVegas has none of, and an always-empty folder
        // reads as a broken feature rather than an empty one.
        auto *root = new QTreeWidgetItem(tree, {tr("Audio")});
        root->setData(0, Qt::UserRole, QStringLiteral("All"));
        auto add = [&](QTreeWidgetItem *parent, const QString &label, const QString &cat) {
            auto *it = new QTreeWidgetItem(parent, {label});
            it->setData(0, Qt::UserRole, cat);
            return it;
        };
        add(root, tr("All"), QStringLiteral("All"));
        add(root, tr("VEGAS"), QStringLiteral("VEGAS"));
        add(root, tr("Third Party"), QStringLiteral("Third Party"));
        add(root, tr("Automatable"), QStringLiteral("Automatable"));
        add(root, tr("Track Optimized FX"), QStringLiteral("Track Optimized"));
        add(root, tr("VST"), QStringLiteral("VST"));
        add(root, tr("5.1 FX"), QStringLiteral("5.1 FX"));
        tree->expandAll();
        tree->setCurrentItem(root->child(0));
        m_currentCategory = QStringLiteral("All");
    } else {
        auto *ofx = new QTreeWidgetItem(tree, {tr("OFX")});
        ofx->setData(0, Qt::UserRole, QStringLiteral("OFX"));
        auto *all = new QTreeWidgetItem(ofx, {tr("All")});
        all->setData(0, Qt::UserRole, QStringLiteral("OFX"));
        tree->expandAll();
        tree->setCurrentItem(all);
        m_currentCategory = QStringLiteral("OFX");
    }
}

void PluginChooserDialog::onCategoryChanged()
{
    auto *tree = findChild<QTreeWidget *>(QStringLiteral("categoryTree"));
    if (!tree || !tree->currentItem()) {
        return;
    }
    m_currentCategory = tree->currentItem()->data(0, Qt::UserRole).toString();
    applyFilter();
}

void PluginChooserDialog::refresh()
{
    m_ofxPlugins.clear();
    m_audioPlugins.clear();
    if (m_audioMode) {
        AudioPluginRegistry::instance().refresh();
        m_audioPlugins = AudioPluginRegistry::instance().all();
        setSourceHint(AudioPluginRegistry::instance().sourceSummary());
    } else if (m_scanner) {
        m_ofxPlugins = m_scanner->scanOfx();
        setSourceHint(m_scanner->resolvedSource());
    } else {
        setSourceHint(QString());
    }
    applyFilter();
}

void PluginChooserDialog::setSourceHint(const QString &summary)
{
    // The scanned folders used to sit above the list as a wrapping paragraph that pushed
    // everything down. They belong with the button that edits them, not in the way.
    const QString what = summary.trimmed();
    const QString title = m_audioMode ? tr("Audio plug-in search paths…")
                                      : tr("Video plug-in search paths…");
    ui->settingsButton->setToolTip(what.isEmpty() ? title
                                                  : tr("%1\n\nScanned: %2").arg(title, what));
}

void PluginChooserDialog::openPluginSettings()
{
    PreferencesDialog dlg(this);
    // Same button, different settings depending on what this chooser is listing —
    // sending the video chooser to the VST paths would just be misleading.
    if (m_audioMode) {
        dlg.showAudioPluginPaths();
    } else {
        dlg.showVideoPluginPaths();
    }
    if (dlg.exec() == QDialog::Accepted) {
        // Paths may have changed under us — rescan so the list matches the new settings.
        refresh();
    }
}

void PluginChooserDialog::applyFilter()
{
    ui->pluginList->clear();
    const QString text = ui->filterEdit->text();
    if (m_audioMode) {
        const QVector<AudioPluginDesc> list =
            AudioPluginRegistry::instance().filtered(m_currentCategory, text);
        for (const AudioPluginDesc &d : list) {
            // The format used to be spelled out after the name; the badge carries it now,
            // which is what keeps the columns narrow enough to fit several across.
            auto *item = new QListWidgetItem(d.name, ui->pluginList);
            item->setIcon(IconFactory::pluginFormatIcon(d.format, d.isInstrument));
            const QString kind = IconFactory::pluginFormatLabel(d.format, d.isInstrument);
            const QString where = d.path.isEmpty() ? d.id : d.path;
            item->setToolTip(kind.isEmpty()
                                 ? where
                                 : QStringLiteral("%1 — %2\n%3").arg(d.name, kind, where));
            item->setData(Qt::UserRole, d.id);
            item->setData(Qt::UserRole + 1, d.name);
            item->setData(Qt::UserRole + 2, int(d.format));
            item->setData(Qt::UserRole + 3, d.path);
            item->setData(Qt::UserRole + 4, d.category);
            item->setData(Qt::UserRole + 5, d.vendor);
            item->setData(Qt::UserRole + 6, d.isInstrument);
        }
    } else {
        for (const PluginInfo &info : m_ofxPlugins) {
            if (!text.isEmpty() && !info.name.contains(text, Qt::CaseInsensitive)) {
                continue;
            }
            auto *item = new QListWidgetItem(info.name, ui->pluginList);
            item->setToolTip(info.path);
            item->setData(Qt::UserRole, info.name);
        }
    }
}

QVector<AudioPluginDesc> PluginChooserDialog::selectedAudioPlugins() const
{
    QVector<AudioPluginDesc> out;
    for (QListWidgetItem *item : ui->pluginList->selectedItems()) {
        AudioPluginDesc d;
        d.id = item->data(Qt::UserRole).toString();
        d.name = item->data(Qt::UserRole + 1).toString();
        d.format = PluginFormat(item->data(Qt::UserRole + 2).toInt());
        d.path = item->data(Qt::UserRole + 3).toString();
        d.category = item->data(Qt::UserRole + 4).toString();
        d.vendor = item->data(Qt::UserRole + 5).toString();
        d.isInstrument = item->data(Qt::UserRole + 6).toBool();
        out.push_back(d);
    }
    return out;
}

QString PluginChooserDialog::selectedPluginName() const
{
    QListWidgetItem *item = ui->pluginList->currentItem();
    if (!item) {
        return {};
    }
    if (m_audioMode) {
        return item->data(Qt::UserRole + 1).toString();
    }
    return item->data(Qt::UserRole).toString();
}

} // namespace openvegas
