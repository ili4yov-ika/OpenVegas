#pragma once

#include "audio/AudioGraph.h"
#include "model/ProjectModel.h"
#include "plugins/AudioPluginHost.h"

#include <QObject>
#include <QMutex>

#include <atomic>
#include <memory>

struct ma_device;

namespace openvegas {

/**
 * Device I/O (miniaudio) + graph + transport clock.
 * During Play, engine is the time master; UI syncs via positionChanged.
 */
class AudioEngine : public QObject {
    Q_OBJECT
public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    void setProject(ProjectModel *model);
    void setPluginHost(AudioPluginHost *host);

    bool startDevice();
    void stopDevice();
    bool isDeviceRunning() const { return m_deviceRunning; }

    void play(double fromSec);
    void stop();
    void seek(double sec);
    bool isPlaying() const { return m_playing.load(); }

    double positionSec() const;
    int sampleRate() const { return m_sampleRate; }
    int blockSize() const { return m_blockSize; }

    /** Rebuild graph snapshot from project (call on UI thread). */
    void syncGraphFromProject();
    /** Soft update: faders/mute/solo/FX state without resetting DSP (no click). */
    void syncMixerLive();

    AudioGraph &graph() { return m_graph; }
    const AudioGraph &graph() const { return m_graph; }

    /** Offline render mixdown (faster-than-realtime). */
    bool renderToWav(const QString &path, double startSec, double lengthSec);

signals:
    void positionChanged(double sec);
    void playingChanged(bool playing);
    void underrun();

private:
    static void dataCallback(ma_device *device, void *output, const void *input,
                             unsigned int frameCount);
    void processBlock(float *interleavedStereo, unsigned int frameCount);
    void emitPositionSoon();

    ProjectModel *m_model = nullptr;
    AudioPluginHost *m_host = nullptr;
    AudioGraph m_graph;
    QMutex m_graphMutex;

    ma_device *m_device = nullptr;
    bool m_deviceRunning = false;
    int m_sampleRate = 48000;
    int m_blockSize = 512;

    std::atomic<bool> m_playing{false};
    std::atomic<double> m_positionSec{0.0};
    std::atomic<bool> m_positionDirty{false};
    /** Set on audio thread when play hits timeline end; drained on UI timer. */
    std::atomic<bool> m_endReached{false};

    std::vector<float> m_scratchL;
    std::vector<float> m_scratchR;
};

} // namespace openvegas
