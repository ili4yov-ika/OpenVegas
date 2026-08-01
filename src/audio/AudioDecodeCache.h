#pragma once

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>
#include <memory>

namespace openvegas {

struct DecodedAudioBuffer {
    int sampleRate = 0;
    int channels = 0;
    /** Interleaved float32 PCM (−1…1). */
    QVector<float> samples;
    bool ready = false;

    qint64 frameCount() const
    {
        if (channels <= 0) {
            return 0;
        }
        return samples.size() / channels;
    }
};

/**
 * Decode cache for playback: WAV PCM in-process; other formats via ffmpeg CLI → temp WAV
 * (linked libav optional later via OPENVGAS_FFMPEG).
 */
class AudioDecodeCache : public QObject {
    Q_OBJECT
public:
    static AudioDecodeCache &instance();

    /** Synchronous get; may decode on caller thread (prefer requestAsync for UI). */
    std::shared_ptr<const DecodedAudioBuffer> get(const QString &mediaPath, int targetSampleRate);

    /** Realtime-safe: returns cached buffer only (never decodes). */
    std::shared_ptr<const DecodedAudioBuffer> peek(const QString &mediaPath) const;

    /** Insert a ready buffer (tests / pre-seed). */
    void put(const QString &mediaPath, std::shared_ptr<DecodedAudioBuffer> buffer);

    void requestAsync(const QString &mediaPath, int targetSampleRate);
    void invalidate(const QString &mediaPath = {});

signals:
    void decoded(const QString &mediaPath);

private:
    explicit AudioDecodeCache(QObject *parent = nullptr);
    std::shared_ptr<DecodedAudioBuffer> decodeFile(const QString &mediaPath, int targetSampleRate);
    static std::shared_ptr<DecodedAudioBuffer> loadWav(const QString &path);
    static std::shared_ptr<DecodedAudioBuffer> decodeViaFfmpeg(const QString &path, int targetSr);
    static std::shared_ptr<DecodedAudioBuffer> resample(
        const std::shared_ptr<DecodedAudioBuffer> &src, int targetSr);

    mutable QMutex m_mutex;
    QHash<QString, std::shared_ptr<DecodedAudioBuffer>> m_cache;
    QHash<QString, bool> m_inflight;
};

} // namespace openvegas
