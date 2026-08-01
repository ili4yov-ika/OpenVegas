#pragma once

#include "model/ProjectModel.h"

namespace openvegas {

/** Editable document state for Undo/Redo (excludes playhead, zoom, project path). */
struct ProjectSnapshot {
    QVector<Track> tracks;
    QVector<AssignableFxBus> assignableFx;
    QVector<MixerBus> mixerBuses;
    QVector<MixerInputBus> mixerInputBuses;
    QVector<MixerStripRef> mixerStripOrder;
    QVector<MediaItem> mediaPool;
    QVector<TimelineMarker> markers;
    LoopRegion loopRegion;
    EventClipboard eventClipboard;
    double frameRate = 29.97;
    quint32 sampleRate = 48000;
    double masterVolumeDb = 0.0;
    double tempoBpm = 120.0;
    int frameWidth = 1920;
    int frameHeight = 1080;
    int nextEventId = 1;
    int nextTrackId = 1;
    int nextGroupId = 1;
    int nextMarkerId = 1;
    int nextMarkerNumber = 1;
    int nextAssignableFxId = 1;
    int nextMixerBusId = 1;
    int nextMixerInputBusId = 1;

    static ProjectSnapshot capture(const ProjectModel &model);
    void apply(ProjectModel &model) const;

    bool operator==(const ProjectSnapshot &o) const;
    bool operator!=(const ProjectSnapshot &o) const { return !(*this == o); }
};

} // namespace openvegas
