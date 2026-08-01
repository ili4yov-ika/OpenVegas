#pragma once

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QVector>

namespace openvegas {

/** Peak bins for timeline audio waveforms (from .sfk or WAV PCM). */
struct WaveformPeaks {
    int channels = 0;
    int bins = 0;
    double durationSec = 0.0;
    /** Interleaved per bin: ch0min, ch0max, ch1min, ch1max, ... (qint16). */
    QVector<qint16> minMax;
    bool ready = false;

    bool isValid() const { return ready && bins > 0 && channels > 0 && !minMax.isEmpty(); }
};

class MediaWaveformCache : public QObject {
    Q_OBJECT
public:
    static MediaWaveformCache &instance();

    /** Sync lookup; may return empty and queue async load. */
    WaveformPeaks peaksFor(const QString &mediaPath);

    /**
     * Fast channel-count hint for timeline placement (SFK header or WAV fmt).
     * Returns 0 if unknown.
     */
    int audioChannelCountHint(const QString &mediaPath);

    void invalidate(const QString &path = {});

    void finishAsyncLoad(const QString &path);

signals:
    void waveformReady(const QString &path);

private:
    explicit MediaWaveformCache(QObject *parent = nullptr);

    WaveformPeaks loadSync(const QString &path) const;
    WaveformPeaks loadSfk(const QString &sfkPath) const;
    WaveformPeaks loadWavPcm(const QString &wavPath) const;
    QString findSfkBeside(const QString &mediaPath) const;
    void requestAsync(const QString &path);

    QMutex m_mutex;
    QHash<QString, WaveformPeaks> m_cache;
    QHash<QString, bool> m_inflight;
};

} // namespace openvegas
