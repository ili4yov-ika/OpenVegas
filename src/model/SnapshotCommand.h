#pragma once

#include "model/ProjectSnapshot.h"

#include <QUndoCommand>
#include <functional>

namespace openvegas {

class ProjectModel;

/** Restores a full document snapshot on undo/redo. */
class SnapshotCommand : public QUndoCommand {
public:
    SnapshotCommand(ProjectModel *model, ProjectSnapshot before, ProjectSnapshot after,
                    const QString &text, std::function<void()> onApplied,
                    QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

private:
    ProjectModel *m_model = nullptr;
    ProjectSnapshot m_before;
    ProjectSnapshot m_after;
    std::function<void()> m_onApplied;
    bool m_firstRedo = true;
};

} // namespace openvegas
