#include "audio/AudioDecodeCache.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QThreadPool>
#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace openvegas {
namespace {

QString findFfmpeg()
{
    static QString cached;
    if (!cached.isEmpty()) {
        return cached;
    }
    cached = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (!cached.isEmpty()) {
        return cached;
    }
    const QStringList candidates = {
        QStringLiteral("C:/ffmpeg/bin/ffmpeg.exe"),
        QStringLiteral("C:/ProgramData/chocolatey/bin/ffmpeg.exe"),
    };
    for (const QString &c : candidates) {
        if (QFileInfo::exists(c)) {
            cached = c;
            return cached;
        }
    }
    return {};
}

class DecodeJob : public QRunnable {
public:
    DecodeJob(AudioDecodeCache *cache, QString path, int sr)
        : m_cache(cache)
        , m_path(std::move(path))
        , m_sr(sr)
    {
        setAutoDelete(true);
    }
    void run() override
    {
        if (m_cache) {
            (void)m_cache->get(m_path, m_sr);
        }
    }

private:
    AudioDecodeCache *m_cache = nullptr;
    QString m_path;
    int m_sr = 48000;
};

float readSample(const uchar *p, int bytesPerSample)
{
    if (bytesPerSample == 2) {
        return float(qint16(qFromLittleEndian<qint16>(p))) / 32768.f;
    }
    if (bytesPerSample == 3) {
        const qint32 v = qint32(p[0]) | (qint32(p[1]) << 8) | (qint32(qint8(p[2])) << 16);
        return float(v) / 8388608.f;
    }
    if (bytesPerSample == 4) {
        return float(qint32(qFromLittleEndian<qint32>(p))) / 2147483648.f;
    }
    return 0.f;
}

} // namespace

AudioDecodeCache &AudioDecodeCache::instance()
{
    static AudioDecodeCache cache;
    return cache;
}

AudioDecodeCache::AudioDecodeCache(QObject *parent)
    : QObject(parent)
{
}

void AudioDecodeCache::invalidate(const QString &mediaPath)
{
    QMutexLocker lock(&m_mutex);
    if (mediaPath.isEmpty()) {
        m_cache.clear();
        m_inflight.clear();
        return;
    }
    m_cache.remove(mediaPath);
    m_inflight.remove(mediaPath);
}

std::shared_ptr<const DecodedAudioBuffer> AudioDecodeCache::peek(const QString &mediaPath) const
{
    if (mediaPath.isEmpty()) {
        return {};
    }
    QMutexLocker lock(&m_mutex);
    const auto it = m_cache.constFind(mediaPath);
    if (it != m_cache.cend() && it.value() && it.value()->ready) {
        return it.value();
    }
    return {};
}

void AudioDecodeCache::put(const QString &mediaPath, std::shared_ptr<DecodedAudioBuffer> buffer)
{
    if (mediaPath.isEmpty() || !buffer) {
        return;
    }
    QMutexLocker lock(&m_mutex);
    m_cache.insert(mediaPath, std::move(buffer));
    m_inflight.remove(mediaPath);
}

void AudioDecodeCache::requestAsync(const QString &mediaPath, int targetSampleRate)
{
    if (mediaPath.isEmpty()) {
        return;
    }
    {
        QMutexLocker lock(&m_mutex);
        if (m_cache.contains(mediaPath) && m_cache.value(mediaPath)
            && m_cache.value(mediaPath)->ready) {
            return;
        }
        if (m_inflight.value(mediaPath, false)) {
            return;
        }
        m_inflight.insert(mediaPath, true);
    }
    QThreadPool::globalInstance()->start(new DecodeJob(this, mediaPath, targetSampleRate));
}

std::shared_ptr<const DecodedAudioBuffer> AudioDecodeCache::get(const QString &mediaPath,
                                                               int targetSampleRate)
{
    if (mediaPath.isEmpty()) {
        return {};
    }
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_cache.constFind(mediaPath);
        if (it != m_cache.cend() && it.value() && it.value()->ready
            && (targetSampleRate <= 0 || it.value()->sampleRate == targetSampleRate)) {
            return it.value();
        }
    }

    auto buf = decodeFile(mediaPath, targetSampleRate);
    {
        QMutexLocker lock(&m_mutex);
        if (buf && buf->ready) {
            m_cache.insert(mediaPath, buf);
        }
        m_inflight.remove(mediaPath);
    }
    if (buf && buf->ready) {
        emit decoded(mediaPath);
    }
    return buf;
}

std::shared_ptr<DecodedAudioBuffer> AudioDecodeCache::decodeFile(const QString &mediaPath,
                                                                 int targetSampleRate)
{
    const QString lower = mediaPath.toLower();
    std::shared_ptr<DecodedAudioBuffer> buf;
    if (lower.endsWith(QLatin1String(".wav"))) {
        buf = loadWav(mediaPath);
    }
    if (!buf || !buf->ready) {
        buf = decodeViaFfmpeg(mediaPath, targetSampleRate > 0 ? targetSampleRate : 48000);
    }
    if (buf && buf->ready && targetSampleRate > 0 && buf->sampleRate != targetSampleRate) {
        buf = resample(buf, targetSampleRate);
    }
    return buf;
}

std::shared_ptr<DecodedAudioBuffer> AudioDecodeCache::loadWav(const QString &path)
{
    QFile f(path);
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
    if (audioFormat != 1 && audioFormat != 65534 && audioFormat != 3) {
        return {};
    }
    if (bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32) {
        return {};
    }

    const int bytesPerSample = bitsPerSample / 8;
    const int frameBytes = bytesPerSample * int(channels);
    if (frameBytes <= 0 || dataOff + dataSize > data.size()) {
        return {};
    }
    const int frames = dataSize / frameBytes;
    auto out = std::make_shared<DecodedAudioBuffer>();
    out->sampleRate = int(sampleRate);
    out->channels = int(channels);
    out->samples.resize(frames * int(channels));

    const uchar *src = base + dataOff;
    if (audioFormat == 3 && bitsPerSample == 32) {
        for (int i = 0; i < frames * int(channels); ++i) {
            float v = 0.f;
            memcpy(&v, src + i * 4, 4);
            out->samples[i] = v;
        }
    } else {
        for (int i = 0; i < frames; ++i) {
            for (int c = 0; c < int(channels); ++c) {
                out->samples[i * int(channels) + c] =
                    readSample(src + i * frameBytes + c * bytesPerSample, bytesPerSample);
            }
        }
    }
    out->ready = true;
    return out;
}

std::shared_ptr<DecodedAudioBuffer> AudioDecodeCache::decodeViaFfmpeg(const QString &path,
                                                                     int targetSr)
{
    const QString ffmpeg = findFfmpeg();
    if (ffmpeg.isEmpty() || !QFileInfo::exists(path)) {
        return {};
    }

    QTemporaryFile tmp(QDir::temp().filePath(QStringLiteral("ov-audio-XXXXXX.wav")));
    tmp.setAutoRemove(true);
    if (!tmp.open()) {
        return {};
    }
    const QString outPath = tmp.fileName();
    tmp.close();

    QProcess proc;
    const QStringList args = {
        QStringLiteral("-y"),
        QStringLiteral("-i"),
        path,
        QStringLiteral("-vn"),
        QStringLiteral("-ac"),
        QStringLiteral("2"),
        QStringLiteral("-ar"),
        QString::number(targetSr > 0 ? targetSr : 48000),
        QStringLiteral("-c:a"),
        QStringLiteral("pcm_f32le"),
        outPath,
    };
    proc.start(ffmpeg, args);
    if (!proc.waitForFinished(120000) || proc.exitStatus() != QProcess::NormalExit
        || proc.exitCode() != 0) {
        return {};
    }
    return loadWav(outPath);
}

std::shared_ptr<DecodedAudioBuffer> AudioDecodeCache::resample(
    const std::shared_ptr<DecodedAudioBuffer> &src, int targetSr)
{
    if (!src || !src->ready || targetSr <= 0 || src->sampleRate == targetSr) {
        return src;
    }
    const int ch = src->channels;
    const qint64 inFrames = src->frameCount();
    if (ch <= 0 || inFrames <= 0) {
        return src;
    }
    const double ratio = double(targetSr) / double(src->sampleRate);
    const qint64 outFrames = std::max<qint64>(1, qint64(std::llround(double(inFrames) * ratio)));
    auto out = std::make_shared<DecodedAudioBuffer>();
    out->sampleRate = targetSr;
    out->channels = ch;
    out->samples.resize(int(outFrames * ch));
    for (qint64 i = 0; i < outFrames; ++i) {
        const double srcPos = double(i) / ratio;
        const qint64 i0 = qint64(srcPos);
        const qint64 i1 = std::min(i0 + 1, inFrames - 1);
        const float frac = float(srcPos - double(i0));
        for (int c = 0; c < ch; ++c) {
            const float a = src->samples[int(i0 * ch + c)];
            const float b = src->samples[int(i1 * ch + c)];
            out->samples[int(i * ch + c)] = a + (b - a) * frac;
        }
    }
    out->ready = true;
    return out;
}

} // namespace openvegas
