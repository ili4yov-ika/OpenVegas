#pragma once

#include "plugins/AudioPluginTypes.h"

#include <QColor>
#include <QVector>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace openvegas {

enum class TrackKind {
    Video,
    Audio
};

/** Clip media family — restricts which track kinds it can live on. */
enum class EventMediaKind {
    Video,
    Still,
    Title,
    Audio,
    Midi
};

inline bool isVideoFamily(EventMediaKind kind)
{
    return kind == EventMediaKind::Video || kind == EventMediaKind::Still
           || kind == EventMediaKind::Title;
}

inline bool isAudioFamily(EventMediaKind kind)
{
    return kind == EventMediaKind::Audio || kind == EventMediaKind::Midi;
}

inline bool canPlaceEventOnTrack(EventMediaKind mediaKind, TrackKind trackKind)
{
    return trackKind == TrackKind::Video ? isVideoFamily(mediaKind) : isAudioFamily(mediaKind);
}

inline EventMediaKind defaultMediaKindForTrack(TrackKind trackKind)
{
    return trackKind == TrackKind::Video ? EventMediaKind::Video : EventMediaKind::Audio;
}

/** Vegas-style defaults when inserting media without an explicit length. */
inline constexpr double kDefaultStillLengthSec = 5.0;
/** mediaDropRequested / addMediaAt: create new track(s) instead of reusing existing. */
inline constexpr int kDropCreateNewTracks = -2;
inline constexpr double kDefaultMediaLengthSec = 8.0;

inline double defaultLengthForMediaKind(const QString &kind)
{
    const QString k = kind.toLower();
    if (k == QLatin1String("still") || k == QLatin1String("image")) {
        return kDefaultStillLengthSec;
    }
    return kDefaultMediaLengthSec;
}

/** Ruler time format (Vegas Pro “Ruler…” menu). */
enum class RulerTimeFormat {
    Samples,
    Time,
    Seconds,
    TimeFrames,
    AbsoluteFrames,
    MeasuresBeats,
    Feet16mm,
    Feet35mm,
    SMPTE_IVTC,
    SMPTE_Film,
    SMPTE_EBU,
    SMPTE_NonDrop,
    SMPTE_Drop,
    SMPTE_30,
    AudioCDTime
};

/** Timeline grid spacing (Options / marker-bar “Grid Spacing”). */
enum class TimelineGridSpacing {
    RulerMarks = 0,
    Seconds,
    HalfSeconds,
    QuarterSeconds,
    Measures,
    HalfMeasures,
    QuarterNotes,
    EighthNotes,
    SixteenthNotes,
    ThirtySecondNotes,
    SixtyFourthNotes,
    Frames,
    HalfFrames
};

/** Vegas-style fade curve shapes (File→… / event fade handle popup). */
enum class FadeCurveType {
    Fast = 0,   // ease-out on fade-in / quick drop on fade-out
    Linear = 1,
    Slow = 2,   // ease-in on fade-in
    Smooth = 3, // S-curve
    Sharp = 4   // sharper S-curve
};

inline int fadeCurveCount()
{
    return 5;
}

/** Automation envelope point (volume / pan / FX param). */
enum class AutomationPointType {
    Linear = 0,
    Smooth,
    Hold
};

struct AutomationPoint {
    double timeSec = 0.0;
    double value = 0.0;
    AutomationPointType type = AutomationPointType::Linear;

    bool operator==(const AutomationPoint &o) const
    {
        return timeSec == o.timeSec && value == o.value && type == o.type;
    }
    bool operator!=(const AutomationPoint &o) const { return !(*this == o); }
};

/**
 * One automation lane. targetId examples:
 *  - "event.gain" (dB), "track.volume" (dB), "track.pan" (−1…+1)
 *  - "fx:0:band0.gain" (slot index + param id)
 */
struct AutomationLane {
    QString targetId;
    QVector<AutomationPoint> points;

    bool operator==(const AutomationLane &o) const
    {
        return targetId == o.targetId && points == o.points;
    }
    bool operator!=(const AutomationLane &o) const { return !(*this == o); }

    /** Interpolate lane at timeline time; returns fallback if empty. */
    double evaluate(double timeSec, double fallback) const
    {
        if (points.isEmpty()) {
            return fallback;
        }
        if (timeSec <= points.front().timeSec) {
            return points.front().value;
        }
        if (timeSec >= points.back().timeSec) {
            return points.back().value;
        }
        for (int i = 0; i + 1 < points.size(); ++i) {
            const auto &a = points[i];
            const auto &b = points[i + 1];
            if (timeSec < a.timeSec || timeSec > b.timeSec) {
                continue;
            }
            if (a.type == AutomationPointType::Hold || b.timeSec <= a.timeSec) {
                return a.value;
            }
            const double t = (timeSec - a.timeSec) / (b.timeSec - a.timeSec);
            if (a.type == AutomationPointType::Smooth) {
                const double s = t * t * (3.0 - 2.0 * t);
                return a.value + (b.value - a.value) * s;
            }
            return a.value + (b.value - a.value) * t;
        }
        return fallback;
    }
};

enum class AutomationWriteMode {
    Off = 0,
    Read,
    Touch,
    Latch,
    Write
};

enum class VideoKeyframeType {
    Linear = 0,
    Hold,
    Slow,
    Fast,
    Smooth,
    Sharp
};

/** One Vegas Event Pan/Crop Position keyframe (pixel units in project frame). */
struct PanCropKeyframe {
    double timeSec = 0.0;
    double width = 1920.0;
    double height = 1080.0;
    double xCenter = 960.0;
    double yCenter = 540.0;
    double angleDeg = 0.0;
    double rotationXCenter = 960.0;
    double rotationYCenter = 540.0;
    double smoothness = 0.0;
    VideoKeyframeType type = VideoKeyframeType::Linear;

    bool operator==(const PanCropKeyframe &o) const
    {
        return timeSec == o.timeSec && width == o.width && height == o.height
               && xCenter == o.xCenter && yCenter == o.yCenter && angleDeg == o.angleDeg
               && rotationXCenter == o.rotationXCenter && rotationYCenter == o.rotationYCenter
               && smoothness == o.smoothness && type == o.type;
    }
    bool operator!=(const PanCropKeyframe &o) const { return !(*this == o); }
};

/** Bezier anchor in project-frame pixels (in/out tangents relative to point). */
struct MaskAnchor {
    double x = 0.0;
    double y = 0.0;
    double inX = 0.0;
    double inY = 0.0;
    double outX = 0.0;
    double outY = 0.0;

    bool operator==(const MaskAnchor &o) const
    {
        return x == o.x && y == o.y && inX == o.inX && inY == o.inY && outX == o.outX
               && outY == o.outY;
    }
    bool operator!=(const MaskAnchor &o) const { return !(*this == o); }
};

enum class MaskPathMode { Positive = 0, Negative = 1, Disabled = 2 };

struct MaskPath {
    QVector<MaskAnchor> anchors;
    bool closed = true;
    MaskPathMode mode = MaskPathMode::Positive;

    bool operator==(const MaskPath &o) const
    {
        return anchors == o.anchors && closed == o.closed && mode == o.mode;
    }
    bool operator!=(const MaskPath &o) const { return !(*this == o); }
};

struct MaskKeyframe {
    double timeSec = 0.0;
    QVector<MaskPath> paths;
    VideoKeyframeType type = VideoKeyframeType::Linear;

    bool operator==(const MaskKeyframe &o) const
    {
        return timeSec == o.timeSec && paths == o.paths && type == o.type;
    }
    bool operator!=(const MaskKeyframe &o) const { return !(*this == o); }
};

struct EventPanCropState {
    QVector<PanCropKeyframe> positionKeyframes;
    QVector<MaskKeyframe> maskKeyframes;
    bool maskEnabled = false;
    bool maintainAspectRatio = true;
    bool stretchToFillFrame = true;
    double workspaceZoom = 30.9;
    double workspaceX = 0.0;
    double workspaceY = 0.0;
    int gridSpacing = 16;

    bool operator==(const EventPanCropState &o) const
    {
        return positionKeyframes == o.positionKeyframes && maskKeyframes == o.maskKeyframes
               && maskEnabled == o.maskEnabled && maintainAspectRatio == o.maintainAspectRatio
               && stretchToFillFrame == o.stretchToFillFrame && workspaceZoom == o.workspaceZoom
               && workspaceX == o.workspaceX && workspaceY == o.workspaceY
               && gridSpacing == o.gridSpacing;
    }
    bool operator!=(const EventPanCropState &o) const { return !(*this == o); }

    static PanCropKeyframe identityKeyframe(int frameW, int frameH)
    {
        PanCropKeyframe k;
        k.width = frameW;
        k.height = frameH;
        k.xCenter = frameW * 0.5;
        k.yCenter = frameH * 0.5;
        k.rotationXCenter = k.xCenter;
        k.rotationYCenter = k.yCenter;
        return k;
    }

    void ensureDefault(int frameW, int frameH)
    {
        if (positionKeyframes.isEmpty()) {
            positionKeyframes.push_back(identityKeyframe(frameW, frameH));
        }
        if (maskKeyframes.isEmpty()) {
            MaskKeyframe mk;
            mk.timeSec = 0.0;
            maskKeyframes.push_back(mk);
        }
    }

    int positionIndexAt(double timeSec) const
    {
        if (positionKeyframes.isEmpty()) {
            return -1;
        }
        int best = 0;
        for (int i = 0; i < positionKeyframes.size(); ++i) {
            if (positionKeyframes[i].timeSec <= timeSec + 1e-9) {
                best = i;
            }
        }
        return best;
    }

    int maskIndexAt(double timeSec) const
    {
        if (maskKeyframes.isEmpty()) {
            return -1;
        }
        int best = 0;
        for (int i = 0; i < maskKeyframes.size(); ++i) {
            if (maskKeyframes[i].timeSec <= timeSec + 1e-9) {
                best = i;
            }
        }
        return best;
    }
};

struct TrackEvent {
    int id = 0;
    QString name;
    /** Resolved filesystem path for thumbs / waveforms (may be empty). */
    QString mediaPath;
    double startSec = 0.0;
    double lengthSec = 1.0;
    /**
     * Offset into source media at event start (Vegas EDL StreamStart), seconds.
     * For reversed subclips with META start=0, kept at 0 so playback maps length→0.
     */
    double mediaStartSec = 0.0;
    /**
     * Length of the source take window (Vegas EDL StreamLength), seconds.
     * 0 = unknown (fall back to event length / probed file duration).
     * When looped and event length exceeds this, media wraps (Vegas “drag past edge”).
     */
    double mediaLengthSec = 0.0;
    /** Vegas event switch “Loop” (EDL Looped); default true. */
    bool looped = true;
    /** Play source backwards over the event (Vegas SubClip reverse / “(reversed)”). */
    bool reversed = false;
    bool selected = false;
    /** Solo fade-in / fade-out duration (seconds). Crossfade from overlap is drawn separately. */
    double fadeInSec = 0.0;
    double fadeOutSec = 0.0;
    FadeCurveType fadeInCurve = FadeCurveType::Smooth;
    FadeCurveType fadeOutCurve = FadeCurveType::Smooth;
    /** Video event opacity 0…1 (Vegas-style level envelope). */
    double opacity = 1.0;
    /** Audio event gain in dB (typical Vegas range roughly −60…+12). */
    double gainDb = 0.0;
    EventMediaKind mediaKind = EventMediaKind::Video;
    /** 0 = ungrouped. Same id links video+audio (and other) events into one group. */
    int groupId = 0;
    /**
     * Audio channel mapping into the source stream (Vegas FirstChannel / Channels).
     * 0-based first channel; channelCount 0 = default stereo pair (0,2) when unknown.
     */
    int firstChannel = 0;
    int channelCount = 0;
    /** In-order event FX chain (Pan/Crop, OFX, VST, builtin audio…). */
    QVector<FxSlot> fxChain = {};
    /** Vegas Event Pan/Crop (video events; keyframed). */
    EventPanCropState panCrop;
    /** Event-level automation (e.g. gain envelope). */
    QVector<AutomationLane> automationLanes;

    bool operator==(const TrackEvent &o) const
    {
        return id == o.id && name == o.name && mediaPath == o.mediaPath && startSec == o.startSec
               && lengthSec == o.lengthSec && mediaStartSec == o.mediaStartSec
               && mediaLengthSec == o.mediaLengthSec && looped == o.looped && reversed == o.reversed
               && selected == o.selected && fadeInSec == o.fadeInSec
               && fadeOutSec == o.fadeOutSec && fadeInCurve == o.fadeInCurve
               && fadeOutCurve == o.fadeOutCurve && opacity == o.opacity && gainDb == o.gainDb
               && mediaKind == o.mediaKind && groupId == o.groupId && firstChannel == o.firstChannel
               && channelCount == o.channelCount && fxChain == o.fxChain && panCrop == o.panCrop
               && automationLanes == o.automationLanes;
    }
    bool operator!=(const TrackEvent &o) const { return !(*this == o); }

    /** Local time on the event (0…lengthSec) for envelopes / Pan-Crop KF. */
    double eventLocalSec(double timelineSec) const { return timelineSec - startSec; }

    /**
     * One loop cycle of source media (seconds). Prefer StreamLength; else event length.
     * Callers may substitute probed file duration when this returns lengthSec as a fallback.
     */
    double sourceCycleSec() const
    {
        if (mediaLengthSec > 1e-6) {
            return mediaLengthSec;
        }
        return std::max(0.05, lengthSec);
    }

    /**
     * Source media time for decode.
     * Forward: mediaStart + local (optional loop wrap on cycle).
     * Reverse SubClip: EDL StreamStart is an in-point on the reversed item;
     * original = cycle - fmod(mediaStart + local, cycle) (Vegas/FCPX timeMap).
     */
    double sourceTimeSec(double timelineSec) const
    {
        double local = eventLocalSec(timelineSec);
        if (local < 0.0) {
            local = 0.0;
        }
        const double cycle = sourceCycleSec();
        if (reversed) {
            // Walk the reverse-subclip timeline starting at mediaStartSec.
            double r = mediaStartSec + local;
            const double rem = (mediaStartSec > 1e-3 && mediaStartSec < cycle)
                                   ? (cycle - mediaStartSec)
                                   : cycle;
            if (looped && mediaLengthSec > 1e-6
                && lengthSec > std::min(cycle, rem) + 1e-6) {
                r = std::fmod(r, cycle);
                if (r < 0.0) {
                    r += cycle;
                }
            } else {
                // Clamp to the take window [mediaStart, mediaStart+length] within cycle.
                const double takeEnd = mediaStartSec
                                       + std::min(lengthSec, std::max(0.0, cycle - mediaStartSec));
                if (r > takeEnd) {
                    r = takeEnd;
                }
                if (r > cycle) {
                    r = cycle;
                }
            }
            // r==0 maps to end of original (cycle); keep a tiny epsilon below cycle for decoders.
            if (r <= 1e-12) {
                return std::max(0.0, cycle);
            }
            return std::max(0.0, cycle - r);
        }
        if (looped && mediaLengthSec > 1e-6 && lengthSec > mediaLengthSec + 1e-6) {
            local = std::fmod(local, cycle);
            if (local < 0.0) {
                local += cycle;
            }
        } else if (local > cycle) {
            local = cycle;
        }
        return std::max(0.0, mediaStartSec + local);
    }

    /**
     * Event-local times (0…lengthSec) where a media-source marker is visible
     * (accounts for reverse SubClip + loop wraps).
     */
    QVector<double> eventLocalsForMediaTime(double mediaSec) const
    {
        QVector<double> out;
        const double cycle = sourceCycleSec();
        if (cycle < 1e-9 || lengthSec < 1e-9) {
            return out;
        }
        double M = std::clamp(mediaSec, 0.0, cycle);
        if (reversed) {
            const double target = (M <= 1e-9) ? 0.0 : (cycle - M);
            for (int k = -2; k < 128; ++k) {
                const double local = target - mediaStartSec + double(k) * cycle;
                if (local >= -1e-6 && local <= lengthSec + 1e-6) {
                    out.push_back(std::clamp(local, 0.0, lengthSec));
                }
            }
        } else if (looped && mediaLengthSec > 1e-6 && lengthSec > mediaLengthSec + 1e-6) {
            double rem = M - mediaStartSec;
            while (rem < 0.0) {
                rem += cycle;
            }
            while (rem >= cycle) {
                rem -= cycle;
            }
            for (int k = 0; k < 128; ++k) {
                const double local = rem + double(k) * cycle;
                if (local > lengthSec + 1e-6) {
                    break;
                }
                if (local >= -1e-6) {
                    out.push_back(std::clamp(local, 0.0, lengthSec));
                }
            }
        } else {
            const double local = M - mediaStartSec;
            if (local >= -1e-6 && local <= lengthSec + 1e-6) {
                out.push_back(std::clamp(local, 0.0, lengthSec));
            }
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end(),
                              [](double a, double b) { return std::abs(a - b) < 1e-5; }),
                  out.end());
        return out;
    }
};

/** One Vegas Track Motion / Shadow / Glow keyframe (height-normalized units). */
struct TrackMotionKeyframe {
    double timeSec = 0.0;
    double positionX = 0.0;
    double positionY = 0.0;
    /** Width/height in project-height units (1.0 = full frame height). */
    double width = 16.0 / 9.0;
    double height = 1.0;
    double rotationZ = 0.0;     // radians
    double orientationZ = 0.0;  // radians
    double smoothness = 0.05;
    VideoKeyframeType type = VideoKeyframeType::Linear;

    bool operator==(const TrackMotionKeyframe &o) const
    {
        return timeSec == o.timeSec && positionX == o.positionX && positionY == o.positionY
               && width == o.width && height == o.height && rotationZ == o.rotationZ
               && orientationZ == o.orientationZ && smoothness == o.smoothness && type == o.type;
    }
    bool operator!=(const TrackMotionKeyframe &o) const { return !(*this == o); }
};

struct TrackFXKeyframe {
    double timeSec = 0.0;
    double positionX = 0.0;
    double positionY = 0.0;
    double width = 1.0;
    double height = 1.0;
    double blur = 0.05;
    double intensity = 0.0;
    QColor color = QColor(0, 0, 0);
    double smoothness = 0.05;
    VideoKeyframeType type = VideoKeyframeType::Linear;

    bool operator==(const TrackFXKeyframe &o) const
    {
        return timeSec == o.timeSec && positionX == o.positionX && positionY == o.positionY
               && width == o.width && height == o.height && blur == o.blur
               && intensity == o.intensity && color == o.color && smoothness == o.smoothness
               && type == o.type;
    }
    bool operator!=(const TrackFXKeyframe &o) const { return !(*this == o); }
};

struct TrackMotionState {
    QVector<TrackMotionKeyframe> motionKeyframes;
    QVector<TrackFXKeyframe> shadowKeyframes;
    QVector<TrackFXKeyframe> glowKeyframes;
    bool shadowEnabled = false;
    bool glowEnabled = false;
    QString compositingMode = QStringLiteral("Source Alpha");
    /** UI workspace (not serialized from VEG). */
    double workspaceZoom = 50.0;
    double workspaceX = 0.0;
    double workspaceY = 0.0;
    int snapGrid = 10;
    int snapRotation = 5;

    bool operator==(const TrackMotionState &o) const
    {
        return motionKeyframes == o.motionKeyframes && shadowKeyframes == o.shadowKeyframes
               && glowKeyframes == o.glowKeyframes && shadowEnabled == o.shadowEnabled
               && glowEnabled == o.glowEnabled && compositingMode == o.compositingMode
               && workspaceZoom == o.workspaceZoom && workspaceX == o.workspaceX
               && workspaceY == o.workspaceY && snapGrid == o.snapGrid
               && snapRotation == o.snapRotation;
    }
    bool operator!=(const TrackMotionState &o) const { return !(*this == o); }

    static TrackMotionKeyframe identityKeyframe(double aspect = 16.0 / 9.0)
    {
        TrackMotionKeyframe k;
        k.width = aspect;
        k.height = 1.0;
        return k;
    }

    void ensureDefault(double aspect = 16.0 / 9.0)
    {
        if (motionKeyframes.isEmpty()) {
            motionKeyframes.push_back(identityKeyframe(aspect));
        }
    }

    /** Nearest motion KF at or before timeSec (or first). */
    int motionIndexAt(double timeSec) const
    {
        if (motionKeyframes.isEmpty()) {
            return -1;
        }
        int best = 0;
        for (int i = 0; i < motionKeyframes.size(); ++i) {
            if (motionKeyframes[i].timeSec <= timeSec + 1e-9) {
                best = i;
            }
        }
        return best;
    }
};

struct Track {
    int id = 0;
    QString name;
    TrackKind kind = TrackKind::Video;
    int height = 96;
    bool muted = false;
    bool solo = false;
    /** Track fader level in dB (0 = unity). */
    double volumeDb = 0.0;
    /** Pan −1…+1 (0 = center). */
    float pan = 0.f;
    /** Mixer bus id to route into; −1 = Master. */
    int busId = -1;
    AutomationWriteMode automationMode = AutomationWriteMode::Read;
    /** Track FX chain (Vegas default audio: Noise Gate / EQ / Compressor). */
    QVector<FxSlot> fxChain = {};
    QVector<AutomationLane> automationLanes;
    /** Vegas Track Motion (video tracks). */
    TrackMotionState motion;
    /**
     * Vegas Track Display Color. Invalid → derived from TrackColors::palette by track index.
     * Applied to header rail and event chrome.
     */
    QColor displayColor;
    QVector<TrackEvent> events;

    bool operator==(const Track &o) const
    {
        return id == o.id && name == o.name && kind == o.kind && height == o.height
               && muted == o.muted && solo == o.solo && volumeDb == o.volumeDb && pan == o.pan
               && busId == o.busId && automationMode == o.automationMode && fxChain == o.fxChain
               && automationLanes == o.automationLanes && motion == o.motion
               && displayColor == o.displayColor && events == o.events;
    }
    bool operator!=(const Track &o) const { return !(*this == o); }
};

/** Vegas Mixing Console Assignable FX bus (not a timeline track). */
struct AssignableFxBus {
    int id = 0;
    /** 1-based display number in the mixer (FX 1, FX 2…). */
    int number = 0;
    QString name;
    QVector<FxSlot> fxChain;

    bool operator==(const AssignableFxBus &o) const
    {
        return id == o.id && number == o.number && name == o.name && fxChain == o.fxChain;
    }
    bool operator!=(const AssignableFxBus &o) const { return !(*this == o); }
};

/** Vegas Mixing Console audio bus (Bus A, Bus B… — not a timeline track). */
struct MixerBus {
    int id = 0;
    /** 0-based letter index: 0 → A, 1 → B, … */
    int letterIndex = 0;
    QString name;
    double volumeDb = 0.0;
    float pan = 0.f;
    bool muted = false;
    bool solo = false;
    QVector<FxSlot> fxChain;

    bool operator==(const MixerBus &o) const
    {
        return id == o.id && letterIndex == o.letterIndex && name == o.name
               && volumeDb == o.volumeDb && pan == o.pan && muted == o.muted && solo == o.solo
               && fxChain == o.fxChain;
    }
    bool operator!=(const MixerBus &o) const { return !(*this == o); }
};

/** Vegas Mixing Console input bus (Input A, Input B… — hardware input strip). */
struct MixerInputBus {
    int id = 0;
    int letterIndex = 0;
    QString name;
    QVector<FxSlot> fxChain;

    bool operator==(const MixerInputBus &o) const
    {
        return id == o.id && letterIndex == o.letterIndex && name == o.name && fxChain == o.fxChain;
    }
    bool operator!=(const MixerInputBus &o) const { return !(*this == o); }
};

/** Channel strip identity in Mixing Console left-to-right order. */
enum class MixerStripKind {
    AudioTrack = 0,
    AudioBus,
    InputBus,
    AssignableFx,
    Master
};

struct MixerStripRef {
    MixerStripKind kind = MixerStripKind::Master;
    /** track.id / bus.id; Master uses 0. */
    int id = 0;

    bool operator==(const MixerStripRef &o) const { return kind == o.kind && id == o.id; }
    bool operator!=(const MixerStripRef &o) const { return !(*this == o); }
};

/** Point marker: timeline ruler or Event Media Markers (Trimmer / Beat Detection). */
struct TimelineMarker {
    int id = 0;
    int number = 0;
    double timeSec = 0.0;
    QString label;
    bool selected = false;

    bool operator==(const TimelineMarker &o) const
    {
        return id == o.id && number == o.number && timeSec == o.timeSec && label == o.label
               && selected == o.selected;
    }
    bool operator!=(const TimelineMarker &o) const { return !(*this == o); }
};

struct MediaItem {
    QString path;
    QString displayName;
    QString kind; // video | audio | still
    bool missing = false;
    /** Event Media Markers (Vegas Trimmer); not the project timeline ruler. */
    QVector<TimelineMarker> markers;

    bool operator==(const MediaItem &o) const
    {
        return path == o.path && displayName == o.displayName && kind == o.kind
               && missing == o.missing && markers == o.markers;
    }
    bool operator!=(const MediaItem &o) const { return !(*this == o); }
};

/** Loop / time-selection region on the marker lane. When inactive, UI shows a seed handle. */
struct LoopRegion {
    bool active = false;
    double startSec = 0.0;
    double endSec = 0.0;
};

/** One event in the timeline edit clipboard (Cut/Copy/Paste). */
struct ClipboardEvent {
    TrackEvent ev;
    TrackKind trackKind = TrackKind::Video;
    /** Track index relative to the topmost copied track (0 = first). */
    int trackDelta = 0;

    bool operator==(const ClipboardEvent &o) const
    {
        return ev == o.ev && trackKind == o.trackKind && trackDelta == o.trackDelta;
    }
    bool operator!=(const ClipboardEvent &o) const { return !(*this == o); }
};

struct EventClipboard {
    QVector<ClipboardEvent> items;
    double anchorSec = 0.0;
    bool empty() const { return items.isEmpty(); }
};

struct VegOpenResult;

class ProjectModel {
public:
    explicit ProjectModel();

    void loadDemoProject();
    void loadEmptyProject();
    /**
     * Apply VegReader result into media pool + timeline.
     * Prefers Vegas EDL CSV sidecar when present; else binary timings; else heuristics.
     * @return true if timeline came from EDL sidecar.
     */
    bool applyVegImport(const VegOpenResult &veg, const QString &openedPath);

    QVector<Track> &tracks() { return m_tracks; }
    const QVector<Track> &tracks() const { return m_tracks; }

    QVector<AssignableFxBus> &assignableFxBuses() { return m_assignableFx; }
    const QVector<AssignableFxBus> &assignableFxBuses() const { return m_assignableFx; }
    /** Create Assignable FX bus with the given plug-in chain; returns bus index. */
    int addAssignableFxBus(const QVector<FxSlot> &chain);
    AssignableFxBus *findAssignableFxBus(int id);
    const AssignableFxBus *findAssignableFxBus(int id) const;

    QVector<MixerBus> &mixerBuses() { return m_mixerBuses; }
    const QVector<MixerBus> &mixerBuses() const { return m_mixerBuses; }
    /** Create audio bus (Bus A, Bus B…); returns bus index. */
    int addMixerBus();
    MixerBus *findMixerBus(int id);
    const MixerBus *findMixerBus(int id) const;

    QVector<MixerInputBus> &mixerInputBuses() { return m_mixerInputBuses; }
    const QVector<MixerInputBus> &mixerInputBuses() const { return m_mixerInputBuses; }
    /** Create input bus (Input A, Input B…); returns bus index. */
    int addMixerInputBus();
    MixerInputBus *findMixerInputBus(int id);
    const MixerInputBus *findMixerInputBus(int id) const;

    /** Left-to-right Mixing Console strip order (Vegas rules enforced on move). */
    QVector<MixerStripRef> &mixerStripOrder() { return m_mixerStripOrder; }
    const QVector<MixerStripRef> &mixerStripOrder() const { return m_mixerStripOrder; }
    void rebuildDefaultMixerStripOrder();
    void ensureMixerStripOrder();
    /** True when order has any Bus / Input / Assignable FX (open console after veg import). */
    bool hasMixerExtras() const;
    /**
     * Move strip at fromIndex to insertIndex (0 = leftmost).
     * Vegas: audio tracks stay as a contiguous left block; Bus/Input/FX/Master only after them.
     */
    bool moveMixerStrip(int fromIndex, int insertIndex);
    static bool isValidMixerStripOrder(const QVector<MixerStripRef> &order);

    QVector<MediaItem> &mediaPool() { return m_mediaPool; }
    const QVector<MediaItem> &mediaPool() const { return m_mediaPool; }
    MediaItem *findMediaItemByPath(const QString &path);
    const MediaItem *findMediaItemByPath(const QString &path) const;
    /** Ensure a pool entry for path; returns pointer into m_mediaPool. */
    MediaItem *ensureMediaItem(const QString &path, const QString &displayName = QString(),
                               const QString &kind = QString());
    /** Fill MediaItem::markers from waveform peaks (Beat Detection). Returns count added. */
    int detectBeatsIntoMediaItem(MediaItem *item, double t0 = 0.0, double t1 = -1.0);
    /** For reverse-fades-fx sample: seed beats on sample_for_project_audio if empty. */
    void seedSampleAudioBeatMarkersIfNeeded(const QString &openedPath);

    QVector<TimelineMarker> &markers() { return m_markers; }
    const QVector<TimelineMarker> &markers() const { return m_markers; }

    QString projectPath() const { return m_projectPath; }
    QString projectTitle() const;

    double frameRate() const { return m_frameRate; }
    void setFrameRate(double fps) { m_frameRate = fps; }
    quint32 sampleRate() const { return m_sampleRate; }
    void setSampleRate(quint32 hz) { m_sampleRate = hz; }
    double masterVolumeDb() const { return m_masterVolumeDb; }
    void setMasterVolumeDb(double db) { m_masterVolumeDb = db; }
    double tempoBpm() const { return m_tempoBpm; }
    void setTempoBpm(double bpm) { m_tempoBpm = bpm; }
    int frameWidth() const { return m_frameWidth; }
    int frameHeight() const { return m_frameHeight; }
    void setFrameSize(int w, int h)
    {
        m_frameWidth = w;
        m_frameHeight = h;
    }

    double playheadSec() const { return m_playheadSec; }
    void setPlayheadSec(double s);

    /** Latest event end on the timeline (at least one frame). */
    double timelineEndSec() const;

    double pixelsPerSecond() const { return m_pps; }
    void setPixelsPerSecond(double pps);

    RulerTimeFormat rulerTimeFormat() const { return m_rulerTimeFormat; }
    void setRulerTimeFormat(RulerTimeFormat f) { m_rulerTimeFormat = f; }
    /** Format ruler tick / ruler corner label using current `rulerTimeFormat`. */
    QString formatRulerTime(double sec) const;

    LoopRegion &loopRegion() { return m_loopRegion; }
    const LoopRegion &loopRegion() const { return m_loopRegion; }
    bool hasLoopRegion() const { return m_loopRegion.active && m_loopRegion.endSec > m_loopRegion.startSec; }
    void setLoopRegion(double startSec, double endSec);
    void clearLoopRegion(double seedSec = 0.0);

    bool loopPlaybackEnabled() const { return m_loopPlayback; }
    void setLoopPlaybackEnabled(bool on) { m_loopPlayback = on; }

    bool snappingEnabled() const { return m_snappingEnabled; }
    void setSnappingEnabled(bool on) { m_snappingEnabled = on; }
    bool snapToGrid() const { return m_snapToGrid; }
    void setSnapToGrid(bool on) { m_snapToGrid = on; }
    bool snapToMarkers() const { return m_snapToMarkers; }
    void setSnapToMarkers(bool on) { m_snapToMarkers = on; }
    bool showEventMediaMarkers() const { return m_showEventMediaMarkers; }
    void setShowEventMediaMarkers(bool on) { m_showEventMediaMarkers = on; }
    bool snapToAllEvents() const { return m_snapToAllEvents; }
    void setSnapToAllEvents(bool on) { m_snapToAllEvents = on; }
    bool quantizeToFrames() const { return m_quantizeToFrames; }
    void setQuantizeToFrames(bool on) { m_quantizeToFrames = on; }
    TimelineGridSpacing gridSpacing() const { return m_gridSpacing; }
    void setGridSpacing(TimelineGridSpacing s) { m_gridSpacing = s; }

    TimelineMarker *findMarker(int markerId);
    const TimelineMarker *findMarker(int markerId) const;
    int addMarkerAt(double timeSec, const QString &label = QString());
    bool removeMarker(int markerId);
    void removeAllMarkers();
    void clearMarkerSelection();
    void selectMarker(int markerId, bool additive = false);
    void renumberMarkers();

    TrackEvent *findEvent(int eventId, int *outTrackIndex = nullptr);
    const TrackEvent *findEvent(int eventId, int *outTrackIndex = nullptr) const;
    bool moveEventToTrack(int eventId, int toTrackIndex);
    /** Append a new track of the given kind; returns its index. */
    int addTrack(TrackKind kind);
    bool removeTrackIfEmpty(int trackIndex);
    bool removeEvent(int eventId);
    /** Remove event; if grouped and grouping is honored, removes the whole group. */
    bool removeEventOrGroup(int eventId);
    bool splitEventAt(int eventId, double timeSec);
    bool trimEventStartTo(int eventId, double timeSec);
    bool trimEventEndTo(int eventId, double timeSec);
    void clearSelection();
    void selectEvent(int eventId, bool additive);
    QVector<int> selectedEventIds() const;

    /** Edit clipboard (Vegas-style event Cut/Copy/Paste). */
    bool hasEventClipboard() const { return !m_eventClipboard.empty(); }
    const EventClipboard &eventClipboard() const { return m_eventClipboard; }
    void setEventClipboard(const EventClipboard &clip) { m_eventClipboard = clip; }
    void copySelectedEvents();
    void cutSelectedEvents();
    int pasteEventsAt(double timeSec);
    bool deleteSelectedEvents();
    bool splitSelectedAt(double timeSec);
    bool trimSelectedStartTo(double timeSec);
    bool trimSelectedEndTo(double timeSec);
    void selectAllEvents();

    bool ignoreEventGrouping() const { return m_ignoreEventGrouping; }
    void setIgnoreEventGrouping(bool on) { m_ignoreEventGrouping = on; }

    bool anyTrackSoloed() const;
    /** True when the track is not muted and not silenced by another track's Solo. */
    bool isTrackAudible(int trackIndex) const;

    int nextEventId() const { return m_nextEventId; }
    int nextTrackId() const { return m_nextTrackId; }
    /** Peek without incrementing (for undo snapshots). */
    int nextGroupIdValue() const { return m_nextGroupId; }
    int nextMarkerId() const { return m_nextMarkerId; }
    int nextMarkerNumber() const { return m_nextMarkerNumber; }
    int nextAssignableFxId() const { return m_nextAssignableFxId; }
    int nextMixerBusId() const { return m_nextMixerBusId; }
    int nextMixerInputBusId() const { return m_nextMixerInputBusId; }
    void setIdCounters(int nextEventId, int nextTrackId, int nextGroupId, int nextMarkerId,
                       int nextMarkerNumber, int nextAssignableFxId, int nextMixerBusId,
                       int nextMixerInputBusId)
    {
        m_nextEventId = nextEventId;
        m_nextTrackId = nextTrackId;
        m_nextGroupId = nextGroupId;
        m_nextMarkerId = nextMarkerId;
        m_nextMarkerNumber = nextMarkerNumber;
        m_nextAssignableFxId = nextAssignableFxId;
        m_nextMixerBusId = nextMixerBusId;
        m_nextMixerInputBusId = nextMixerInputBusId;
    }

    int nextGroupId() { return m_nextGroupId++; }
    QVector<int> eventIdsInGroup(int groupId) const;
    /** Group currently selected events; returns new group id or 0. */
    int groupSelectedEvents();
    void ungroupEvent(int eventId);
    void clearGroup(int groupId);
    void selectGroup(int groupId);
    /** preferTrack: -1 auto, -2 force new Video+Audio tracks, >=0 target header track. */
    int addGroupedAvMedia(const QString &name, double startSec, double lengthSec,
                          double fadeInSec = 0.0, double fadeOutSec = 0.0, int preferTrack = -1,
                          const QString &mediaPath = {});

    /** Drop/import helper: video → A/V group; still → video track (5 s default); audio → audio track.
     *  @param lengthSec 0 = use Vegas-style default for the kind (still 5 s, else 8 s).
     *  @param preferTrack track index, -1 = first matching / create, -2 = always create new track(s). */
    int addMediaAt(const QString &name, const QString &kind, double startSec, double lengthSec = 0.0,
                   int preferTrack = -1, const QString &mediaPath = {});

    /** Resolve filesystem path for an event (explicit mediaPath or media-pool match by name). */
    QString mediaPathForEvent(const TrackEvent &ev) const;

    /**
     * Relink offline/missing media: update media pool + all events that used oldPath
     * (exact path or same file name).
     */
    int relinkMedia(const QString &oldPath, const QString &newPath);

    /** Paths currently marked missing in the media pool. */
    QStringList missingMediaPaths() const;

private:
    static QString guessKindFromPath(const QString &path);
    static QString resolveMediaPath(const QString &storedPath, const QString &vegPath);
    int ensureTrack(TrackKind kind);
    void assignPairedGroups(Track &video, Track &audio);
    void appendMixerStripRef(MixerStripKind kind, int id);
    void applyMixerChannelsFromVeg(const VegOpenResult &veg);
    /** Attach binary Track Motion (Position / Shadow / Glow KF) onto first video track. */
    void applyTrackMotionFromVeg(const VegOpenResult &veg);
    /** Attach binary Event Pan/Crop (POSK KF) onto first video event. */
    void applyPanCropFromVeg(const VegOpenResult &veg);
    /** Attach Color Grading OFX from .veg onto first video track FX chain. */
    void applyColorGradingFromVeg(const VegOpenResult &veg);
    /** Attach UTF-16 Audio Event FX names onto audio clip events (if chain empty). */
    void applyAudioEventFxFromVeg(const VegOpenResult &veg);
    /**
     * Assign TrackColors::palette by timeline index when displayColor is unset.
     * .veg binary color indices are not yet reverse-engineered; cycling matches Vegas defaults.
     */
    void applyDefaultTrackDisplayColors();

    QVector<Track> m_tracks;
    QVector<AssignableFxBus> m_assignableFx;
    QVector<MixerBus> m_mixerBuses;
    QVector<MixerInputBus> m_mixerInputBuses;
    QVector<MixerStripRef> m_mixerStripOrder;
    QVector<MediaItem> m_mediaPool;
    QVector<TimelineMarker> m_markers;
    LoopRegion m_loopRegion;
    EventClipboard m_eventClipboard;
    QString m_projectPath;
    double m_playheadSec = 0.0;
    double m_pps = 40.0;
    double m_frameRate = 29.97;
    quint32 m_sampleRate = 48000;
    double m_masterVolumeDb = 0.0;
    double m_tempoBpm = 120.0;
    int m_frameWidth = 1920;
    int m_frameHeight = 1080;
    int m_nextEventId = 1;
    int m_nextTrackId = 1;
    int m_nextGroupId = 1;
    int m_nextMarkerId = 1;
    int m_nextMarkerNumber = 1;
    int m_nextAssignableFxId = 1;
    int m_nextMixerBusId = 1;
    int m_nextMixerInputBusId = 1;
    bool m_ignoreEventGrouping = false;
    bool m_loopPlayback = true;
    bool m_snappingEnabled = true;
    bool m_snapToGrid = false;
    bool m_snapToMarkers = true;
    bool m_showEventMediaMarkers = true;
    bool m_snapToAllEvents = true;
    bool m_quantizeToFrames = true;
    TimelineGridSpacing m_gridSpacing = TimelineGridSpacing::RulerMarks;
    RulerTimeFormat m_rulerTimeFormat = RulerTimeFormat::TimeFrames;
};

} // namespace openvegas
