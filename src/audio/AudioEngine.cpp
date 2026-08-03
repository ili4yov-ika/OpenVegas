#include "audio/AudioEngine.h"

#include "audio/AudioUtil.h"

#include <QFile>
#include <QTimer>
#include <QtEndian>

#include <algorithm>
#include <cstring>

#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "miniaudio.h"

namespace openvegas {

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
{
    auto *tick = new QTimer(this);
    tick->setInterval(16);
    connect(tick, &QTimer::timeout, this, [this]() {
        if (m_endReached.exchange(false)) {
            emit positionChanged(positionSec());
            emit playingChanged(false);
            return;
        }
        if (m_positionDirty.exchange(false) || m_playing.load()) {
            emit positionChanged(positionSec());
        }
    });
    tick->start();
}

AudioEngine::~AudioEngine()
{
    stop();
    stopDevice();
}

void AudioEngine::setProject(ProjectModel *model)
{
    m_model = model;
    if (m_model) {
        m_sampleRate = int(m_model->sampleRate() > 0 ? m_model->sampleRate() : 48000);
    }
}

void AudioEngine::setPluginHost(AudioPluginHost *host)
{
    m_host = host;
}

bool AudioEngine::startDevice()
{
    if (m_deviceRunning) {
        return true;
    }
    m_device = new ma_device;
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = (ma_uint32)m_sampleRate;
    cfg.dataCallback = dataCallback;
    cfg.pUserData = this;
    cfg.periodSizeInFrames = (ma_uint32)m_blockSize;
    if (ma_device_init(nullptr, &cfg, m_device) != MA_SUCCESS) {
        delete m_device;
        m_device = nullptr;
        return false;
    }
    m_sampleRate = int(m_device->sampleRate);
    m_scratchL.assign(size_t(m_blockSize * 4), 0.f);
    m_scratchR.assign(size_t(m_blockSize * 4), 0.f);
    {
        QMutexLocker lock(&m_graphMutex);
        m_graph.prepare(m_sampleRate, m_blockSize);
    }
    if (ma_device_start(m_device) != MA_SUCCESS) {
        ma_device_uninit(m_device);
        delete m_device;
        m_device = nullptr;
        return false;
    }
    m_deviceRunning = true;
    return true;
}

void AudioEngine::stopDevice()
{
    if (!m_device) {
        return;
    }
    ma_device_stop(m_device);
    ma_device_uninit(m_device);
    delete m_device;
    m_device = nullptr;
    m_deviceRunning = false;
}

void AudioEngine::syncGraphFromProject()
{
    if (!m_model) {
        return;
    }
    QMutexLocker lock(&m_graphMutex);
    m_graph.rebuild(*m_model, m_host);
    m_graph.prepare(m_sampleRate, m_blockSize);
}

void AudioEngine::syncMixerLive()
{
    if (!m_model) {
        return;
    }
    QMutexLocker lock(&m_graphMutex);
    m_graph.applyLiveMixer(*m_model);
}

void AudioEngine::play(double fromSec)
{
    if (!m_deviceRunning) {
        startDevice();
    }
    syncGraphFromProject();
    m_positionSec.store(std::max(0.0, fromSec));
    {
        QMutexLocker lock(&m_graphMutex);
        m_graph.reset();
    }
    m_playing.store(true);
    m_endReached.store(false);
    emit playingChanged(true);
    emit positionChanged(positionSec());
}

void AudioEngine::stop()
{
    if (!m_playing.load()) {
        return;
    }
    m_playing.store(false);
    emit playingChanged(false);
}

void AudioEngine::seek(double sec)
{
    m_positionSec.store(std::max(0.0, sec));
    m_seekEpoch.fetch_add(1, std::memory_order_acq_rel);
    {
        QMutexLocker lock(&m_graphMutex);
        m_graph.reset();
    }
    m_positionDirty.store(true);
}

double AudioEngine::positionSec() const
{
    return m_positionSec.load();
}

void AudioEngine::dataCallback(ma_device *device, void *output, const void *input,
                               unsigned int frameCount)
{
    Q_UNUSED(input);
    auto *self = static_cast<AudioEngine *>(device->pUserData);
    if (!self || !output) {
        return;
    }
    self->processBlock(static_cast<float *>(output), frameCount);
}

void AudioEngine::processBlock(float *interleavedStereo, unsigned int frameCount)
{
    if (int(m_scratchL.size()) < int(frameCount)) {
        m_scratchL.resize(frameCount);
        m_scratchR.resize(frameCount);
    }
    std::fill(m_scratchL.begin(), m_scratchL.begin() + frameCount, 0.f);
    std::fill(m_scratchR.begin(), m_scratchR.begin() + frameCount, 0.f);

    if (m_playing.load()) {
        // Capture seek epoch before reading position so a concurrent seek() is not
        // overwritten by next = oldPos + block.
        const quint64 epoch = m_seekEpoch.load(std::memory_order_acquire);
        const double pos = m_positionSec.load(std::memory_order_acquire);
        {
            QMutexLocker lock(&m_graphMutex);
            m_graph.process(pos, m_scratchL.data(), m_scratchR.data(), int(frameCount));
        }

        double next = pos + double(frameCount) / double(m_sampleRate);
        bool stopAtEnd = false;
        if (m_model && m_model->loopPlaybackEnabled() && m_model->hasLoopRegion()) {
            const double a = m_model->loopRegion().startSec;
            const double b = m_model->loopRegion().endSec;
            if (b > a && next >= b) {
                next = a;
                QMutexLocker lock(&m_graphMutex);
                m_graph.reset();
            }
        } else if (m_model) {
            const double end = m_model->timelineEndSec();
            if (next >= end) {
                next = end;
                stopAtEnd = true;
            }
        }

        // Drop this advance if the UI seeked while we were mixing this block.
        if (m_seekEpoch.load(std::memory_order_acquire) == epoch) {
            m_positionSec.store(next, std::memory_order_release);
            if (stopAtEnd && m_playing.exchange(false)) {
                m_endReached.store(true);
            }
        }
    }

    for (unsigned int i = 0; i < frameCount; ++i) {
        interleavedStereo[i * 2 + 0] = m_scratchL[i];
        interleavedStereo[i * 2 + 1] = m_scratchR[i];
    }
}

bool AudioEngine::renderToWav(const QString &path, double startSec, double lengthSec)
{
    if (!m_model || lengthSec <= 0.0) {
        return false;
    }
    syncGraphFromProject();
    const int sr = m_sampleRate;
    const int frames = int(lengthSec * sr);
    QVector<float> interleaved(frames * 2);
    const int block = 1024;
    std::vector<float> L(static_cast<size_t>(block), 0.f);
    std::vector<float> R(static_cast<size_t>(block), 0.f);
    int done = 0;
    while (done < frames) {
        const int n = std::min(block, frames - done);
        const double t = startSec + double(done) / double(sr);
        {
            QMutexLocker lock(&m_graphMutex);
            m_graph.process(t, L.data(), R.data(), n);
        }
        for (int i = 0; i < n; ++i) {
            interleaved[(done + i) * 2 + 0] = L[size_t(i)];
            interleaved[(done + i) * 2 + 1] = R[size_t(i)];
        }
        done += n;
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    const quint32 dataBytes = quint32(frames * 2 * 4);
    const quint32 riffSize = 36 + dataBytes;
    f.write("RIFF", 4);
    quint32 le = riffSize;
    f.write(reinterpret_cast<const char *>(&le), 4);
    f.write("WAVEfmt ", 8);
    quint32 fmtSize = 16;
    f.write(reinterpret_cast<const char *>(&fmtSize), 4);
    quint16 audioFormat = 3; // IEEE float
    quint16 channels = 2;
    quint32 sampleRate = quint32(sr);
    quint16 bits = 32;
    quint16 blockAlign = channels * bits / 8;
    quint32 byteRate = sampleRate * blockAlign;
    f.write(reinterpret_cast<const char *>(&audioFormat), 2);
    f.write(reinterpret_cast<const char *>(&channels), 2);
    f.write(reinterpret_cast<const char *>(&sampleRate), 4);
    f.write(reinterpret_cast<const char *>(&byteRate), 4);
    f.write(reinterpret_cast<const char *>(&blockAlign), 2);
    f.write(reinterpret_cast<const char *>(&bits), 2);
    f.write("data", 4);
    f.write(reinterpret_cast<const char *>(&dataBytes), 4);
    f.write(reinterpret_cast<const char *>(interleaved.constData()), int(dataBytes));
    return true;
}

} // namespace openvegas
