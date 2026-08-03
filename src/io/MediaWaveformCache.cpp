#include "MediaWaveformCache.h"

#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QRunnable>
#include <QThreadPool>
#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace openvegas {
namespace {

class WaveLoadJob : public QRunnable {
public:
    WaveLoadJob(MediaWaveformCache *cache, QString path)
        : m_cache(cache)
        , m_path(std::move(path))
    {
        setAutoDelete(true);
    }
    void run() override
    {
        if (m_cache) {
            m_cache->finishAsyncLoad(m_path);
        }
    }

private:
    MediaWaveformCache *m_cache = nullptr;
    QString m_path;
};

} // namespace

MediaWaveformCache &MediaWaveformCache::instance()
{
    static MediaWaveformCache cache;
    return cache;
}

MediaWaveformCache::MediaWaveformCache(QObject *parent)
    : QObject(parent)
{
}

void MediaWaveformCache::invalidate(const QString &path)
{
    QMutexLocker lock(&m_mutex);
    if (path.isEmpty()) {
        m_cache.clear();
        m_inflight.clear();
        return;
    }
    m_cache.remove(path);
    m_inflight.remove(path);
}

WaveformPeaks MediaWaveformCache::peaksFor(const QString &mediaPath)
{
    if (mediaPath.isEmpty()) {
        return {};
    }
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_cache.constFind(mediaPath);
        if (it != m_cache.cend() && it->ready) {
            return *it;
        }
        if (m_inflight.value(mediaPath, false)) {
            return it != m_cache.cend() ? *it : WaveformPeaks{};
        }
        m_inflight.insert(mediaPath, true);
    }
    requestAsync(mediaPath);
    return {};
}

WaveformPeaks MediaWaveformCache::peaksForBlocking(const QString &mediaPath)
{
    if (mediaPath.isEmpty()) {
        return {};
    }
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_cache.constFind(mediaPath);
        if (it != m_cache.cend() && it->ready) {
            return *it;
        }
    }
    WaveformPeaks peaks = loadSync(mediaPath);
    peaks.ready = true;
    {
        QMutexLocker lock(&m_mutex);
        m_cache.insert(mediaPath, peaks);
        m_inflight.remove(mediaPath);
    }
    return peaks;
}

int MediaWaveformCache::audioChannelCountHint(const QString &mediaPath)
{
    if (mediaPath.isEmpty()) {
        return 0;
    }
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_cache.constFind(mediaPath);
        if (it != m_cache.cend() && it->ready && it->channels > 0) {
            return it->channels;
        }
    }

    const QString sfk = findSfkBeside(mediaPath);
    if (!sfk.isEmpty()) {
        QFile f(sfk);
        if (f.open(QIODevice::ReadOnly) && f.size() >= 0x18) {
            const QByteArray hdr = f.read(64);
            if (hdr.size() >= 0x18 && hdr.left(4) == QByteArrayLiteral("SFPK")) {
                const auto *base = reinterpret_cast<const uchar *>(hdr.constData());
                const quint32 channels = qFromLittleEndian<quint32>(base + 0x14);
                if (channels >= 1 && channels <= 16) {
                    return static_cast<int>(channels);
                }
            }
        }
    }

    const QString ext = QFileInfo(mediaPath).suffix().toLower();
    if (ext == QLatin1String("wav") || ext == QLatin1String("bwf")) {
        QFile f(mediaPath);
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray data = f.read(256);
            if (data.size() >= 44 && data.left(4) == QByteArrayLiteral("RIFF")
                && data.mid(8, 4) == QByteArrayLiteral("WAVE")) {
                const auto *base = reinterpret_cast<const uchar *>(data.constData());
                int pos = 12;
                while (pos + 8 <= data.size()) {
                    const QByteArray id = data.mid(pos, 4);
                    const quint32 sz = qFromLittleEndian<quint32>(base + pos + 4);
                    pos += 8;
                    if (id == QByteArrayLiteral("fmt ") && pos + 4 <= data.size()) {
                        const quint16 channels = qFromLittleEndian<quint16>(base + pos + 2);
                        if (channels >= 1 && channels <= 16) {
                            return channels;
                        }
                        break;
                    }
                    pos += static_cast<int>(sz) + (sz & 1);
                }
            }
        }
    }
    return 0;
}

void MediaWaveformCache::requestAsync(const QString &path)
{
    QThreadPool::globalInstance()->start(new WaveLoadJob(this, path));
}

void MediaWaveformCache::finishAsyncLoad(const QString &path)
{
    WaveformPeaks peaks = loadSync(path);
    // Always mark ready so we do not re-queue forever on missing/unsupported media.
    peaks.ready = true;
    {
        QMutexLocker lock(&m_mutex);
        m_cache.insert(path, peaks);
        m_inflight.remove(path);
    }
    if (peaks.bins > 0 && peaks.channels > 0 && !peaks.minMax.isEmpty()) {
        QMetaObject::invokeMethod(
            this, [this, path]() { emit waveformReady(path); }, Qt::QueuedConnection);
    }
}

QString MediaWaveformCache::findSfkBeside(const QString &mediaPath) const
{
    const QFileInfo fi(mediaPath);
    if (!fi.exists()) {
        return {};
    }
    // name.wav → name.sfk ; name.mp4 → name.mp4.sfk (Vegas) or name.sfk
    const QStringList cands = {
        fi.absoluteFilePath() + QStringLiteral(".sfk"),
        fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".sfk"),
    };
    for (const QString &c : cands) {
        if (QFileInfo::exists(c)) {
            return c;
        }
    }
    return {};
}

WaveformPeaks MediaWaveformCache::loadSync(const QString &path) const
{
    const QString sfk = findSfkBeside(path);
    if (!sfk.isEmpty()) {
        WaveformPeaks p = loadSfk(sfk);
        if (p.isValid()) {
            return p;
        }
    }
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QLatin1String("wav") || ext == QLatin1String("bwf")) {
        return loadWavPcm(path);
    }
    return {};
}

WaveformPeaks MediaWaveformCache::loadSfk(const QString &sfkPath) const
{
    QFile f(sfkPath);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray data = f.readAll();
    if (data.size() < 64 || data.left(4) != QByteArrayLiteral("SFPK")) {
        return {};
    }
    const auto *base = reinterpret_cast<const uchar *>(data.constData());
    const quint32 ver = qFromLittleEndian<quint32>(base + 4);
    const quint32 hdr = qFromLittleEndian<quint32>(base + 8);
    const quint32 channels = qFromLittleEndian<quint32>(base + 0x14);
    const quint32 samplesPerBin = qFromLittleEndian<quint32>(base + 0x18);
    const quint32 sourceFrames = qFromLittleEndian<quint32>(base + 0x1C);
    Q_UNUSED(ver);
    if (hdr < 64 || channels == 0 || channels > 8 || samplesPerBin == 0 || sourceFrames == 0) {
        return {};
    }
    const int bins = static_cast<int>(sourceFrames / samplesPerBin);
    const int expect = bins * static_cast<int>(channels) * 2 * int(sizeof(qint16));
    if (bins <= 0 || data.size() < int(hdr) + expect) {
        return {};
    }

    WaveformPeaks out;
    out.channels = static_cast<int>(channels);
    out.bins = bins;
    // Prefer common rates by frame count heuristics
    double rate = 48000.0;
    if (sourceFrames > 100000) {
        const double d48 = double(sourceFrames) / 48000.0;
        const double d44 = double(sourceFrames) / 44100.0;
        const double d192 = double(sourceFrames) / 192000.0;
        // Pick rate that yields a "nice" duration (used only for mapping)
        if (std::abs(d192 - std::round(d192 * 1000.0) / 1000.0) < 0.002) {
            rate = 192000.0;
        } else if (std::abs(d44 - std::round(d44 * 100.0) / 100.0) < 0.01) {
            rate = 44100.0;
        } else {
            rate = 48000.0;
            Q_UNUSED(d48);
        }
    }
    out.durationSec = double(sourceFrames) / rate;
    out.minMax.resize(bins * out.channels * 2);
    const auto *body = reinterpret_cast<const qint16 *>(base + hdr);
    for (int i = 0; i < out.minMax.size(); ++i) {
        out.minMax[i] = qFromLittleEndian<qint16>(reinterpret_cast<const uchar *>(body + i));
    }
    out.ready = true;
    return out;
}

WaveformPeaks MediaWaveformCache::loadWavPcm(const QString &wavPath) const
{
    QFile f(wavPath);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray data = f.readAll();
    if (data.size() < 44 || data.left(4) != QByteArrayLiteral("RIFF")
        || data.mid(8, 4) != QByteArrayLiteral("WAVE")) {
        return {};
    }

    const auto *base = reinterpret_cast<const uchar *>(data.constData());
    int pos = 12;
    quint16 audioFormat = 0;
    quint16 channels = 0;
    quint32 sampleRate = 0;
    quint16 bitsPerSample = 0;
    int dataOff = -1;
    int dataSize = 0;

    while (pos + 8 <= data.size()) {
        const QByteArray id = data.mid(pos, 4);
        const quint32 sz = qFromLittleEndian<quint32>(base + pos + 4);
        pos += 8;
        if (id == QByteArrayLiteral("fmt ") && pos + 16 <= data.size()) {
            audioFormat = qFromLittleEndian<quint16>(base + pos);
            channels = qFromLittleEndian<quint16>(base + pos + 2);
            sampleRate = qFromLittleEndian<quint32>(base + pos + 4);
            bitsPerSample = qFromLittleEndian<quint16>(base + pos + 14);
        } else if (id == QByteArrayLiteral("data")) {
            dataOff = pos;
            dataSize = static_cast<int>(sz);
            break;
        }
        pos += static_cast<int>(sz) + (sz & 1);
    }

    if (dataOff < 0 || channels == 0 || channels > 8 || sampleRate == 0 || dataSize <= 0) {
        return {};
    }
    // PCM integer only (1 = PCM, 65534 = WAVE_FORMAT_EXTENSIBLE often still PCM)
    if (audioFormat != 1 && audioFormat != 65534) {
        return {};
    }
    if (bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32) {
        return {};
    }

    const int bytesPerSample = bitsPerSample / 8;
    const int frameBytes = bytesPerSample * channels;
    if (frameBytes <= 0) {
        return {};
    }
    const int totalFrames = dataSize / frameBytes;
    if (totalFrames <= 0) {
        return {};
    }

    constexpr int kTargetBins = 4096;
    const int bins = std::min(kTargetBins, totalFrames);
    const int spb = std::max(1, totalFrames / bins);

    WaveformPeaks out;
    out.channels = channels;
    out.bins = bins;
    out.durationSec = double(totalFrames) / double(sampleRate);
    out.minMax.fill(0, bins * channels * 2);

    const uchar *pcm = base + dataOff;
    for (int b = 0; b < bins; ++b) {
        const int f0 = b * spb;
        const int f1 = std::min(totalFrames, f0 + spb);
        for (int ch = 0; ch < channels; ++ch) {
            qint16 mn = 32767;
            qint16 mx = -32768;
            for (int fr = f0; fr < f1; ++fr) {
                const int off = fr * frameBytes + ch * bytesPerSample;
                qint32 sample = 0;
                if (bitsPerSample == 16) {
                    sample = qFromLittleEndian<qint16>(pcm + off);
                } else if (bitsPerSample == 24) {
                    const int v = pcm[off] | (pcm[off + 1] << 8) | (pcm[off + 2] << 16);
                    sample = (v & 0x800000) ? (v | ~0xFFFFFF) : v;
                    sample >>= 8;
                } else {
                    sample = qFromLittleEndian<qint32>(pcm + off) >> 16;
                }
                mn = static_cast<qint16>(std::min<qint32>(mn, sample));
                mx = static_cast<qint16>(std::max<qint32>(mx, sample));
            }
            const int idx = (b * channels + ch) * 2;
            out.minMax[idx] = mn;
            out.minMax[idx + 1] = mx;
        }
    }
    out.ready = true;
    return out;
}

} // namespace openvegas
