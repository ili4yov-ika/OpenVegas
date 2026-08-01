#include "model/ProjectSnapshot.h"

namespace openvegas {

ProjectSnapshot ProjectSnapshot::capture(const ProjectModel &model)
{
    ProjectSnapshot s;
    s.tracks = model.tracks();
    s.assignableFx = model.assignableFxBuses();
    s.mixerBuses = model.mixerBuses();
    s.mixerInputBuses = model.mixerInputBuses();
    s.mixerStripOrder = model.mixerStripOrder();
    s.mediaPool = model.mediaPool();
    s.markers = model.markers();
    s.loopRegion = model.loopRegion();
    s.eventClipboard = model.eventClipboard();
    s.frameRate = model.frameRate();
    s.sampleRate = model.sampleRate();
    s.masterVolumeDb = model.masterVolumeDb();
    s.tempoBpm = model.tempoBpm();
    s.frameWidth = model.frameWidth();
    s.frameHeight = model.frameHeight();
    s.nextEventId = model.nextEventId();
    s.nextTrackId = model.nextTrackId();
    s.nextGroupId = model.nextGroupIdValue();
    s.nextMarkerId = model.nextMarkerId();
    s.nextMarkerNumber = model.nextMarkerNumber();
    s.nextAssignableFxId = model.nextAssignableFxId();
    s.nextMixerBusId = model.nextMixerBusId();
    s.nextMixerInputBusId = model.nextMixerInputBusId();
    return s;
}

void ProjectSnapshot::apply(ProjectModel &model) const
{
    model.tracks() = tracks;
    model.assignableFxBuses() = assignableFx;
    model.mixerBuses() = mixerBuses;
    model.mixerInputBuses() = mixerInputBuses;
    model.mixerStripOrder() = mixerStripOrder;
    model.mediaPool() = mediaPool;
    model.markers() = markers;
    model.loopRegion() = loopRegion;
    model.setEventClipboard(eventClipboard);
    model.setFrameRate(frameRate);
    model.setSampleRate(sampleRate);
    model.setMasterVolumeDb(masterVolumeDb);
    model.setTempoBpm(tempoBpm);
    model.setFrameSize(frameWidth, frameHeight);
    model.setIdCounters(nextEventId, nextTrackId, nextGroupId, nextMarkerId, nextMarkerNumber,
                        nextAssignableFxId, nextMixerBusId, nextMixerInputBusId);
}

bool ProjectSnapshot::operator==(const ProjectSnapshot &o) const
{
    return tracks == o.tracks && assignableFx == o.assignableFx && mixerBuses == o.mixerBuses
           && mixerInputBuses == o.mixerInputBuses && mixerStripOrder == o.mixerStripOrder
           && mediaPool == o.mediaPool && markers == o.markers
           && loopRegion.active == o.loopRegion.active
           && loopRegion.startSec == o.loopRegion.startSec && loopRegion.endSec == o.loopRegion.endSec
           && eventClipboard.items == o.eventClipboard.items
           && eventClipboard.anchorSec == o.eventClipboard.anchorSec && frameRate == o.frameRate
           && sampleRate == o.sampleRate && masterVolumeDb == o.masterVolumeDb
           && tempoBpm == o.tempoBpm && frameWidth == o.frameWidth
           && frameHeight == o.frameHeight && nextEventId == o.nextEventId
           && nextTrackId == o.nextTrackId && nextGroupId == o.nextGroupId
           && nextMarkerId == o.nextMarkerId && nextMarkerNumber == o.nextMarkerNumber
           && nextAssignableFxId == o.nextAssignableFxId && nextMixerBusId == o.nextMixerBusId
           && nextMixerInputBusId == o.nextMixerInputBusId;
}

} // namespace openvegas
