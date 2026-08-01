#pragma once

#include "ui/KeyboardMap.h"

#include <QDialog>

class QComboBox;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QKeySequenceEdit;
class QLabel;
class QPushButton;
class QButtonGroup;

namespace openvegas {

class CustomizeKeyboardDialog : public QDialog {
    Q_OBJECT
public:
    explicit CustomizeKeyboardDialog(QWidget *parent = nullptr);

private:
    void buildUi();
    void rebuildCommandTree();
    void onContextChanged(int contextIndex);
    void onFilterChanged(const QString &text);
    void onSelectionChanged();
    void onShortcutEdited();
    void updateAssignedLabel();
    void addShortcut();
    void replaceShortcut();
    void removeShortcut();
    void locateConflict();
    void applyAndClose();

    QString selectedCommandKey() const; // "Context/Command.Id"
    KeyboardCommand *selectedCommand();

    KeyboardContext m_context = KeyboardContext::TrackView;
    QComboBox *m_mapCombo = nullptr;
    QLineEdit *m_filter = nullptr;
    QButtonGroup *m_contextGroup = nullptr;
    QTreeWidget *m_tree = nullptr;
    QKeySequenceEdit *m_seqEdit = nullptr;
    QLabel *m_assignedTo = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_replaceBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
    QPushButton *m_locateBtn = nullptr;
    QPushButton *m_deleteMapBtn = nullptr;
};

} // namespace openvegas
