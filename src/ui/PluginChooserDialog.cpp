#include "ui/PluginChooserDialog.h"
#include "ui_PluginChooserDialog.h"
#include "plugins/AudioPluginRegistry.h"

#include <QHBoxLayout>
#include <QTreeWidget>
#include <QSplitter>
#include <QListWidget>

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
    split->addWidget(tree);
    split->addWidget(ui->pluginList);
    split->setStretchFactor(1, 1);
    ui->verticalLayout->insertWidget(2, split);

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
        auto *vst = new QTreeWidgetItem(tree, {tr("VST")});
        vst->setData(0, Qt::UserRole, QStringLiteral("All"));
        auto add = [&](QTreeWidgetItem *parent, const QString &label, const QString &cat) {
            auto *it = new QTreeWidgetItem(parent, {label});
            it->setData(0, Qt::UserRole, cat);
            return it;
        };
        add(vst, tr("All"), QStringLiteral("All"));
        add(vst, tr("VEGAS"), QStringLiteral("VEGAS"));
        add(vst, tr("Third Party"), QStringLiteral("Third Party"));
        add(vst, tr("Track Optimized FX"), QStringLiteral("Track Optimized"));
        add(vst, tr("VST"), QStringLiteral("VST"));
        add(vst, tr("5.1 FX"), QStringLiteral("5.1 FX"));
        tree->expandAll();
        tree->setCurrentItem(vst->child(0));
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
        ui->sourceLabel->setText(
            tr("Source: %1").arg(AudioPluginRegistry::instance().sourceSummary()));
    } else if (m_scanner) {
        m_ofxPlugins = m_scanner->scanOfx();
        ui->sourceLabel->setText(tr("Source: %1").arg(m_scanner->resolvedSource()));
    } else {
        ui->sourceLabel->setText(tr("Source: (none)"));
    }
    applyFilter();
}

void PluginChooserDialog::applyFilter()
{
    ui->pluginList->clear();
    const QString text = ui->filterEdit->text();
    if (m_audioMode) {
        const QVector<AudioPluginDesc> list =
            AudioPluginRegistry::instance().filtered(m_currentCategory, text);
        for (const AudioPluginDesc &d : list) {
            QString label = d.name;
            if (d.format == PluginFormat::Vst3) {
                label += QStringLiteral("  [VST3]");
            } else if (d.format == PluginFormat::Vst2) {
                label += QStringLiteral("  [VST2]");
            } else if (d.format == PluginFormat::Vst1) {
                label += QStringLiteral("  [VST1]");
            }
            auto *item = new QListWidgetItem(label, ui->pluginList);
            item->setToolTip(d.path.isEmpty() ? d.id : d.path);
            item->setData(Qt::UserRole, d.id);
            item->setData(Qt::UserRole + 1, d.name);
            item->setData(Qt::UserRole + 2, int(d.format));
            item->setData(Qt::UserRole + 3, d.path);
            item->setData(Qt::UserRole + 4, d.category);
            item->setData(Qt::UserRole + 5, d.vendor);
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
