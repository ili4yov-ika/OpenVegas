#include "model/ProjectModel.h"

namespace openvegas {

/** Minimal definition for video unit tests (avoid linking full ProjectModel). */
QString ProjectModel::mediaPathForEvent(const TrackEvent &ev) const
{
    return ev.mediaPath;
}

} // namespace openvegas
