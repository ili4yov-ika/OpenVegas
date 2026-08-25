#pragma once

#include "plugins/PluginDiscovery.h"

#include <QDialog>
#include <QVector>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPushButton;

namespace openvegas {

/**
 * What OpenVegas found on this machine, the first time it runs.
 *
 * A fresh install knows nothing about where VEGAS, its Shared Plug-Ins or anyone's VST
 * folders are, and the alternative to asking is a Preferences page the user has to think
 * to open. This sweeps the usual places, shows what turned up with a count beside each so
 * "found the folder" is not confused with "found anything in it", and lets folders be
 * added or unticked before anything is saved.
 *
 * Nothing here is irreversible: it writes the same settings Preferences does, and can be
 * reopened from there.
 */
class FirstRunDialog : public QDialog {
    Q_OBJECT
public:
    explicit FirstRunDialog(QWidget *parent = nullptr);

    /** The rows still ticked when the dialog was accepted. */
    QVector<PluginDiscovery::Found> selected() const;

    /**
     * Run the sweep and show the dialog when this is the first start.
     *
     * Returns true when setup ran and was accepted. Marks the first run done either way,
     * so a user who closes it is not asked again at every launch.
     */
    static bool runIfNeeded(QWidget *parent);

private:
    void rescan();
    void addFolderManually();
    void populate(const QVector<PluginDiscovery::Found> &found);
    QTreeWidgetItem *groupFor(PluginDiscovery::Kind kind);

    QTreeWidget *m_tree = nullptr;
    QLabel *m_summary = nullptr;
    QVector<PluginDiscovery::Found> m_found;
};

} // namespace openvegas
