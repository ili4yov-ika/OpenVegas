#include "model/SnapshotCommand.h"

namespace openvegas {

SnapshotCommand::SnapshotCommand(ProjectModel *model, ProjectSnapshot before, ProjectSnapshot after,
                                 const QString &text, std::function<void()> onApplied,
                                 QUndoCommand *parent)
    : QUndoCommand(text, parent)
    , m_model(model)
    , m_before(std::move(before))
    , m_after(std::move(after))
    , m_onApplied(std::move(onApplied))
{
}

void SnapshotCommand::undo()
{
    if (!m_model) {
        return;
    }
    m_before.apply(*m_model);
    if (m_onApplied) {
        m_onApplied();
    }
}

void SnapshotCommand::redo()
{
    if (!m_model) {
        return;
    }
    // push() always calls redo(); document is already at "after".
    if (m_firstRedo) {
        m_firstRedo = false;
        return;
    }
    m_after.apply(*m_model);
    if (m_onApplied) {
        m_onApplied();
    }
}

} // namespace openvegas
