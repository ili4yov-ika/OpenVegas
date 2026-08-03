#pragma once

#include "audio/BuiltinDsp.h"
#include "model/ProjectModel.h"
#include "plugins/AudioPluginHost.h"

#include <QVector>

#include <atomic>
#include <memory>
#include <vector>

namespace openvegas {

struct AudioGraphClip {
    int eventId = 0;
    int trackId = 0;
    QString mediaPath;
    double startSec = 0.0;
    double lengthSec = 0.0;
    double inPointSec = 0.0;
    double mediaLengthSec = 0.0;
    bool looped = true;
    bool reversed = false;
    double gainDb = 0.0;
    double fadeInSec = 0.0;
    double fadeOutSec = 0.0;
    FadeCurveType fadeInCurve = FadeCurveType::Smooth;
    FadeCurveType fadeOutCurve = FadeCurveType::Smooth;
    int firstChannel = 0;
    int channelCount = 0;
    QVector<FxSlot> fxChain;
    QVector<AutomationLane> automationLanes;
};

struct AudioGraphTrack {
    int trackId = 0;
    bool audible = true;
    double volumeDb = 0.0;
    float pan = 0.f;
    int busId = -1; // −1 = Master
    QVector<FxSlot> fxChain;
    QVector<AutomationLane> automationLanes;
    QVector<AudioGraphClip> clips;
};

struct AudioGraphBus {
    int busId = 0;
    bool muted = false;
    bool solo = false;
    double volumeDb = 0.0;
    float pan = 0.f;
    QVector<FxSlot> fxChain;
};

struct AudioMeterLevels {
    std::atomic<float> peakL{0.f};
    std::atomic<float> peakR{0.f};
    std::atomic<float> rmsL{0.f};
    std::atomic<float> rmsR{0.f};
};

/**
 * Realtime-safe mix graph snapshot. Rebuild on the UI thread; process() on audio thread.
 * Routing: Track → (optional MixerBus) → Master.
 */
class AudioGraph {
public:
    void rebuild(const ProjectModel &model, AudioPluginHost *host);
    /** Update fader/pan/mute/solo/master without resetting DSP state (no click). */
    void applyLiveMixer(const ProjectModel &model);
    void prepare(double sampleRate, int blockSize);
    void reset();

    void process(double startSec, float *left, float *right, int frames);

    AudioMeterLevels &masterMeter() { return m_masterMeter; }
    const AudioMeterLevels &masterMeter() const { return m_masterMeter; }
    QVector<AudioMeterLevels *> trackMeters();
    QVector<AudioMeterLevels *> busMeters();

    int sampleRate() const { return m_sampleRate; }
    int trackCount() const { return m_tracks.size(); }
    int busCount() const { return m_buses.size(); }

private:
    void processClip(const AudioGraphClip &clip, double startSec, float *left, float *right,
                     int frames);
    void processFxChain(QVector<FxSlot> &chain, std::vector<BuiltinDspState> &states, float *L,
                        float *R, int frames, const QVector<AutomationLane> *lanes,
                        double timeSec);
    float automationGain(const QVector<AutomationLane> &lanes, const QString &targetId,
                         double timeSec, float fallback) const;
    int busIndexForId(int busId) const;

    int m_sampleRate = 48000;
    int m_blockSize = 512;
    AudioPluginHost *m_host = nullptr;
    QVector<AudioGraphTrack> m_tracks;
    QVector<AudioGraphBus> m_buses;
    QVector<FxSlot> m_masterFx;
    std::vector<std::vector<BuiltinDspState>> m_trackFxStates;
    std::vector<std::vector<std::vector<BuiltinDspState>>> m_clipFxStates;
    std::vector<std::vector<BuiltinDspState>> m_busFxStates;
    std::vector<BuiltinDspState> m_masterFxStates;
    std::vector<std::unique_ptr<AudioMeterLevels>> m_trackMeters;
    std::vector<std::unique_ptr<AudioMeterLevels>> m_busMeters;
    AudioMeterLevels m_masterMeter;
    std::vector<float> m_tmpL;
    std::vector<float> m_tmpR;
    std::vector<float> m_trackL;
    std::vector<float> m_trackR;
    std::vector<float> m_accL;
    std::vector<float> m_accR;
    /** Interleaved bus buffers: busIndex * block + frame */
    std::vector<float> m_busL;
    std::vector<float> m_busR;
    float m_seekFade = 1.f;
    double m_masterGainDb = 0.0;
};

} // namespace openvegas
