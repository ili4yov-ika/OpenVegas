#include "ui/CustomizeKeyboardDialog.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace openvegas {
namespace {

constexpr int kRoleCmdKey = Qt::UserRole + 1;
constexpr int kRoleShortcutIndex = Qt::UserRole + 2;

QString cmdKey(KeyboardContext ctx, const QString &id)
{
    return keyboardContextId(ctx) + QLatin1Char('/') + id;
}

} // namespace

CustomizeKeyboardDialog::CustomizeKeyboardDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("customizeKeyboardDialog"));
    setWindowTitle(tr("Customize Keyboard"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    resize(720, 560);
    setMinimumSize(560, 420);
    buildUi();
    rebuildCommandTree();
}

void CustomizeKeyboardDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto *mapRow = new QHBoxLayout();
    mapRow->addWidget(new QLabel(tr("Keyboard map:"), this));
    m_mapCombo = new QComboBox(this);
    m_mapCombo->addItem(tr("[Default]"));
    m_mapCombo->setMinimumWidth(180);
    mapRow->addWidget(m_mapCombo);
    auto *saveAs = new QPushButton(tr("Save As…"), this);
    saveAs->setEnabled(false);
    saveAs->setToolTip(tr("Custom named maps — planned"));
    mapRow->addWidget(saveAs);
    m_deleteMapBtn = new QPushButton(tr("Delete"), this);
    m_deleteMapBtn->setEnabled(false);
    mapRow->addWidget(m_deleteMapBtn);
    mapRow->addStretch(1);
    root->addLayout(mapRow);

    root->addWidget(new QLabel(tr("Show commands containing:"), this));
    m_filter = new QLineEdit(this);
    m_filter->setClearButtonEnabled(true);
    root->addWidget(m_filter);

    auto *ctxGrid = new QGridLayout();
    ctxGrid->setHorizontalSpacing(6);
    ctxGrid->setVerticalSpacing(6);
    m_contextGroup = new QButtonGroup(this);
    m_contextGroup->setExclusive(true);
    for (int i = 0; i < int(KeyboardContext::Count); ++i) {
        const auto ctx = static_cast<KeyboardContext>(i);
        auto *btn = new QPushButton(keyboardContextLabel(ctx), this);
        btn->setCheckable(true);
        btn->setAutoDefault(false);
        btn->setMinimumHeight(28);
        m_contextGroup->addButton(btn, i);
        ctxGrid->addWidget(btn, i / 4, i % 4);
    }
    if (auto *b = m_contextGroup->button(int(KeyboardContext::TrackView))) {
        b->setChecked(true);
    }
    root->addLayout(ctxGrid);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Command"), tr("Shortcut")});
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setStretchLastSection(true);
    m_tree->header()->resizeSection(0, 360);
    root->addWidget(m_tree, 1);

    auto *bottom = new QGridLayout();
    bottom->addWidget(new QLabel(tr("Shortcut keys:"), this), 0, 0);
    m_seqEdit = new QKeySequenceEdit(this);
    bottom->addWidget(m_seqEdit, 0, 1, 1, 3);

    m_addBtn = new QPushButton(tr("Add"), this);
    m_replaceBtn = new QPushButton(tr("Replace"), this);
    m_removeBtn = new QPushButton(tr("Remove"), this);
    m_addBtn->setEnabled(false);
    m_replaceBtn->setEnabled(false);
    m_removeBtn->setEnabled(false);
    bottom->addWidget(m_addBtn, 0, 4);
    bottom->addWidget(m_replaceBtn, 0, 5);
    bottom->addWidget(m_removeBtn, 0, 6);

    bottom->addWidget(new QLabel(tr("Shortcut currently assigned to:"), this), 1, 0, 1, 4);
    m_assignedTo = new QLabel(this);
    m_assignedTo->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    m_assignedTo->setMinimumHeight(48);
    m_assignedTo->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_assignedTo->setWordWrap(true);
    m_assignedTo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bottom->addWidget(m_assignedTo, 2, 0, 1, 6);
    m_locateBtn = new QPushButton(tr("Locate"), this);
    m_locateBtn->setEnabled(false);
    bottom->addWidget(m_locateBtn, 2, 6, Qt::AlignTop);
    root->addLayout(bottom);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(box);

    connect(m_contextGroup, &QButtonGroup::idClicked, this, &CustomizeKeyboardDialog::onContextChanged);
    connect(m_filter, &QLineEdit::textChanged, this, &CustomizeKeyboardDialog::onFilterChanged);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &CustomizeKeyboardDialog::onSelectionChanged);
    connect(m_seqEdit, &QKeySequenceEdit::keySequenceChanged, this,
            &CustomizeKeyboardDialog::onShortcutEdited);
    connect(m_addBtn, &QPushButton::clicked, this, &CustomizeKeyboardDialog::addShortcut);
    connect(m_replaceBtn, &QPushButton::clicked, this, &CustomizeKeyboardDialog::replaceShortcut);
    connect(m_removeBtn, &QPushButton::clicked, this, &CustomizeKeyboardDialog::removeShortcut);
    connect(m_locateBtn, &QPushButton::clicked, this, &CustomizeKeyboardDialog::locateConflict);
    connect(box, &QDialogButtonBox::accepted, this, &CustomizeKeyboardDialog::applyAndClose);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void CustomizeKeyboardDialog::onContextChanged(int contextIndex)
{
    if (contextIndex < 0 || contextIndex >= int(KeyboardContext::Count)) {
        return;
    }
    m_context = static_cast<KeyboardContext>(contextIndex);
    rebuildCommandTree();
}

void CustomizeKeyboardDialog::onFilterChanged(const QString &)
{
    rebuildCommandTree();
}

void CustomizeKeyboardDialog::rebuildCommandTree()
{
    const QString filter = m_filter ? m_filter->text().trimmed() : QString();
    m_tree->clear();

    // Group by first segment of id (Edit / Transport / CursorTo / …)
    QHash<QString, QTreeWidgetItem *> groups;
    const auto cmds = KeyboardMap::instance().commandsFor(m_context);
    for (const KeyboardCommand &c : cmds) {
        if (!filter.isEmpty() && !c.id.contains(filter, Qt::CaseInsensitive)
            && !c.displayName.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        const int dot = c.id.indexOf(QLatin1Char('.'));
        const QString groupName = dot > 0 ? c.id.left(dot) : QStringLiteral("General");
        QTreeWidgetItem *group = groups.value(groupName, nullptr);
        if (!group) {
            group = new QTreeWidgetItem(m_tree, {groupName, QString()});
            group->setFlags(Qt::ItemIsEnabled);
            groups.insert(groupName, group);
        }

        if (c.shortcuts.isEmpty()) {
            auto *item = new QTreeWidgetItem(group, {c.id, QString()});
            item->setData(0, kRoleCmdKey, cmdKey(c.context, c.id));
            item->setData(0, kRoleShortcutIndex, -1);
        } else {
            for (int i = 0; i < c.shortcuts.size(); ++i) {
                auto *item = new QTreeWidgetItem(
                    group,
                    {c.id, c.shortcuts[i].toString(QKeySequence::NativeText)});
                item->setData(0, kRoleCmdKey, cmdKey(c.context, c.id));
                item->setData(0, kRoleShortcutIndex, i);
            }
        }
    }
    m_tree->expandAll();
    onSelectionChanged();
}

QString CustomizeKeyboardDialog::selectedCommandKey() const
{
    const auto items = m_tree->selectedItems();
    if (items.isEmpty()) {
        return {};
    }
    return items.first()->data(0, kRoleCmdKey).toString();
}

KeyboardCommand *CustomizeKeyboardDialog::selectedCommand()
{
    const QString key = selectedCommandKey();
    if (key.isEmpty()) {
        return nullptr;
    }
    const int slash = key.indexOf(QLatin1Char('/'));
    if (slash < 0) {
        return nullptr;
    }
    const KeyboardContext ctx = keyboardContextFromId(key.left(slash));
    const QString id = key.mid(slash + 1);
    return KeyboardMap::instance().findMutable(ctx, id);
}

void CustomizeKeyboardDialog::onSelectionChanged()
{
    KeyboardCommand *c = selectedCommand();
    const auto items = m_tree->selectedItems();
    const bool hasSel = c != nullptr && !items.isEmpty();
    m_removeBtn->setEnabled(hasSel && items.first()->data(0, kRoleShortcutIndex).toInt() >= 0);
    onShortcutEdited();
}

void CustomizeKeyboardDialog::onShortcutEdited()
{
    updateAssignedLabel();
    const QKeySequence seq = m_seqEdit->keySequence();
    KeyboardCommand *c = selectedCommand();
    const bool hasCmd = c != nullptr;
    const bool hasSeq = !seq.isEmpty();
    m_addBtn->setEnabled(hasCmd && hasSeq);
    m_replaceBtn->setEnabled(hasCmd && hasSeq && m_removeBtn->isEnabled());
}

void CustomizeKeyboardDialog::updateAssignedLabel()
{
    const QKeySequence seq = m_seqEdit->keySequence();
    if (seq.isEmpty()) {
        m_assignedTo->clear();
        m_locateBtn->setEnabled(false);
        return;
    }
    const QString except = selectedCommand() ? selectedCommand()->id : QString();
    const QStringList users = KeyboardMap::instance().commandIdsUsing(seq, except);
    if (users.isEmpty()) {
        m_assignedTo->setText(tr("(none)"));
        m_locateBtn->setEnabled(false);
    } else {
        m_assignedTo->setText(users.join(QStringLiteral("\n")));
        m_locateBtn->setEnabled(true);
    }
}

void CustomizeKeyboardDialog::addShortcut()
{
    KeyboardCommand *c = selectedCommand();
    const QKeySequence seq = m_seqEdit->keySequence();
    if (!c || seq.isEmpty()) {
        return;
    }
    for (const QKeySequence &s : c->shortcuts) {
        if (s == seq) {
            return;
        }
    }
    c->shortcuts.push_back(seq);
    KeyboardMap::instance().notifyChanged();
    rebuildCommandTree();
}

void CustomizeKeyboardDialog::replaceShortcut()
{
    KeyboardCommand *c = selectedCommand();
    const auto items = m_tree->selectedItems();
    const QKeySequence seq = m_seqEdit->keySequence();
    if (!c || items.isEmpty() || seq.isEmpty()) {
        return;
    }
    const int idx = items.first()->data(0, kRoleShortcutIndex).toInt();
    if (idx < 0 || idx >= c->shortcuts.size()) {
        return;
    }
    c->shortcuts[idx] = seq;
    KeyboardMap::instance().notifyChanged();
    rebuildCommandTree();
}

void CustomizeKeyboardDialog::removeShortcut()
{
    KeyboardCommand *c = selectedCommand();
    const auto items = m_tree->selectedItems();
    if (!c || items.isEmpty()) {
        return;
    }
    const int idx = items.first()->data(0, kRoleShortcutIndex).toInt();
    if (idx < 0 || idx >= c->shortcuts.size()) {
        return;
    }
    c->shortcuts.removeAt(idx);
    KeyboardMap::instance().notifyChanged();
    rebuildCommandTree();
}

void CustomizeKeyboardDialog::locateConflict()
{
    const QStringList lines = m_assignedTo->text().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.isEmpty()) {
        return;
    }
    // Format: Context.Command.Id — switch context and select
    const QString first = lines.first();
    const int dot = first.indexOf(QLatin1Char('.'));
    if (dot <= 0) {
        return;
    }
    const QString ctxId = first.left(dot);
    const QString cmdId = first.mid(dot + 1);
    const KeyboardContext ctx = keyboardContextFromId(ctxId);
    if (auto *b = m_contextGroup->button(int(ctx))) {
        b->setChecked(true);
    }
    m_context = ctx;
    rebuildCommandTree();
    const QString key = cmdKey(ctx, cmdId);
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *g = m_tree->topLevelItem(i);
        for (int j = 0; j < g->childCount(); ++j) {
            QTreeWidgetItem *it = g->child(j);
            if (it->data(0, kRoleCmdKey).toString() == key) {
                m_tree->setCurrentItem(it);
                m_tree->scrollToItem(it);
                return;
            }
        }
    }
}

void CustomizeKeyboardDialog::applyAndClose()
{
    KeyboardMap::instance().save();
    KeyboardMap::instance().notifyChanged();
    accept();
}

} // namespace openvegas
