#include "ui/FirstRunDialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace openvegas {

namespace {

constexpr int kKindRole = Qt::UserRole + 1;
constexpr int kPathRole = Qt::UserRole + 2;
constexpr int kPreferredRole = Qt::UserRole + 3;

} // namespace

FirstRunDialog::FirstRunDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("firstRunDialog"));
    setWindowTitle(tr("Set up OpenVegas"));
    setModal(true);
    resize(720, 520);

    auto *root = new QVBoxLayout(this);

    auto *intro = new QLabel(
        tr("OpenVegas looked in the usual places for VEGAS Pro and for plug-in folders.\n"
           "Untick anything you would rather it left alone, or add a folder it missed."),
        this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("firstRunTree"));
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Location"), tr("Found")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->setRootIsDecorated(true);
    root->addWidget(m_tree, 1);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    root->addWidget(m_summary);

    auto *buttons = new QHBoxLayout();
    auto *addBtn = new QPushButton(tr("Add Folder…"), this);
    auto *rescanBtn = new QPushButton(tr("Scan Again"), this);
    buttons->addWidget(addBtn);
    buttons->addWidget(rescanBtn);
    buttons->addStretch(1);
    root->addLayout(buttons);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    box->button(QDialogButtonBox::Ok)->setText(tr("Use These"));
    box->button(QDialogButtonBox::Cancel)->setText(tr("Skip"));
    root->addWidget(box);

    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(addBtn, &QPushButton::clicked, this, &FirstRunDialog::addFolderManually);
    connect(rescanBtn, &QPushButton::clicked, this, &FirstRunDialog::rescan);

    rescan();
}

QTreeWidgetItem *FirstRunDialog::groupFor(PluginDiscovery::Kind kind)
{
    const QString label = PluginDiscovery::kindLabel(kind);
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if (item->text(0) == label) {
            return item;
        }
    }
    auto *item = new QTreeWidgetItem(m_tree, {label});
    item->setFirstColumnSpanned(true);
    item->setExpanded(true);
    QFont f = item->font(0);
    f.setBold(true);
    item->setFont(0, f);
    item->setData(0, kKindRole, int(kind));
    return item;
}

void FirstRunDialog::populate(const QVector<PluginDiscovery::Found> &found)
{
    m_tree->clear();
    for (const PluginDiscovery::Found &f : found) {
        QTreeWidgetItem *group = groupFor(f.kind);
        auto *row = new QTreeWidgetItem(group);
        row->setText(0, QDir::toNativeSeparators(f.path));
        // An empty folder is worth showing but not worth ticking by default: it is almost
        // always a leftover from an uninstall, and a setup screen that ticks it teaches
        // the user their answer did not matter.
        const bool empty = f.count == 0;
        row->setText(1, f.count < 0 ? QString() : tr("%n item(s)", "", f.count));
        row->setCheckState(0, empty ? Qt::Unchecked : Qt::Checked);
        row->setData(0, kKindRole, int(f.kind));
        row->setData(0, kPathRole, f.path);
        row->setData(0, kPreferredRole, f.preferred);
        if (empty) {
            row->setToolTip(0, tr("Nothing plug-in shaped in this folder."));
        }
    }

    int ticked = 0;
    for (const PluginDiscovery::Found &f : found) {
        if (f.count != 0) {
            ++ticked;
        }
    }
    m_summary->setText(found.isEmpty()
                           ? tr("Nothing found. You can add folders by hand, or set them "
                                "later in Options → Preferences → Plug-Ins.")
                           : tr("%1 location(s) found, %2 ticked. Everything here can be "
                                "changed later in Options → Preferences.")
                                 .arg(found.size())
                                 .arg(ticked));
}

void FirstRunDialog::rescan()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_found = PluginDiscovery::scan();
    QApplication::restoreOverrideCursor();
    populate(m_found);
}

void FirstRunDialog::addFolderManually()
{
    const QString dir =
        QFileDialog::getExistingDirectory(this, tr("Add a plug-in folder"), QDir::homePath());
    if (dir.isEmpty()) {
        return;
    }
    // Which kind it is depends on what is in it, and the user is better placed to say than
    // a guess from the folder's name — so it goes under the group that already holds the
    // most, defaulting to OFX when there is nothing to go on.
    PluginDiscovery::Found f;
    f.path = QDir::fromNativeSeparators(QDir::cleanPath(dir));
    f.kind = PluginDiscovery::Kind::Ofx;
    const QDir d(f.path);
    if (!d.entryList({QStringLiteral("*.vst3")}, QDir::Files | QDir::Dirs).isEmpty()) {
        f.kind = PluginDiscovery::Kind::Vst3;
    } else if (!d.entryList({QStringLiteral("*.ofx.bundle"), QStringLiteral("*.ofx")},
                            QDir::Files | QDir::Dirs)
                    .isEmpty()) {
        f.kind = PluginDiscovery::Kind::Ofx;
    } else if (!d.entryList({QStringLiteral("*.dll")}, QDir::Files).isEmpty()) {
        f.kind = PluginDiscovery::Kind::Vst2;
    }
    for (const PluginDiscovery::Found &existing : m_found) {
        if (existing.path.compare(f.path, Qt::CaseInsensitive) == 0) {
            return; // already listed
        }
    }
    m_found.push_back(f);
    populate(m_found);
}

QVector<PluginDiscovery::Found> FirstRunDialog::selected() const
{
    QVector<PluginDiscovery::Found> out;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *group = m_tree->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j) {
            QTreeWidgetItem *row = group->child(j);
            if (row->checkState(0) != Qt::Checked) {
                continue;
            }
            PluginDiscovery::Found f;
            f.kind = PluginDiscovery::Kind(row->data(0, kKindRole).toInt());
            f.path = row->data(0, kPathRole).toString();
            f.preferred = row->data(0, kPreferredRole).toBool();
            out.push_back(f);
        }
    }
    return out;
}

bool FirstRunDialog::runIfNeeded(QWidget *parent)
{
    if (!PluginDiscovery::needsFirstRun()) {
        return false;
    }
    FirstRunDialog dlg(parent);
    const bool accepted = dlg.exec() == QDialog::Accepted;
    if (accepted) {
        PluginDiscovery::apply(dlg.selected());
    }
    // Marked either way: a user who skipped setup meant to skip it, and asking again at
    // every launch would be nagging rather than helping.
    PluginDiscovery::markFirstRunDone();
    return accepted;
}

} // namespace openvegas
