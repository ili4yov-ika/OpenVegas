#include "audio/AudioDecodeCache.h"
#include "io/MediaFilmstripCache.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryFile>
#include <QThreadPool>
#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace openvegas {
namespace {

QString findFfmpeg()
{
    return MediaFilmstripCache::findFfmpeg();
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
    } else if (const QString sidecar = sfap0Beside(mediaPath); !sidecar.isEmpty()) {
        // A VEGAS project dropped on the timeline as a clip. ffmpeg cannot open a .veg,
        // so without this the whole track is silent even though its waveform draws.
        buf = loadSfap0(sidecar);
    }
    if (!buf || !buf->ready) {
        buf = decodeViaFfmpeg(mediaPath, targetSampleRate > 0 ? targetSampleRate : 48000);
    }
    if (buf && buf->ready && targetSampleRate > 0 && buf->sampleRate != targetSampleRate) {
        buf = resample(buf, targetSampleRate);
    }
    return buf;
}

/**
 * Audio for a VEGAS project used as media.
 *
 * A `.veg` can be dropped on a timeline as a clip — a nested project — and then the
 * "media" is not a media file at all: neither ffmpeg nor a WAV reader can open it, so a
 * project built that way came back completely silent while its waveforms drew fine (those
 * come from the `.sfk` peak file beside it).
 *
 * VEGAS solves this by mixing the nested project down once into a sidecar named
 * `<project>.veg.sfap0` and playing that. The container is Sony's RIFF variant — the same
 * one `.veg` itself uses — where every chunk id is 16 bytes rather than the usual four,
 * which is why a stock WAV parser cannot read it. Inside it is ordinary interleaved PCM,
 * 32-bit float in the file measured here.
 *
 * See MARKDOWN/VEG_SFAP0_FORMAT.md for the byte-level breakdown.
 */
std::shared_ptr<DecodedAudioBuffer> AudioDecodeCache::loadSfap0(const QString &path)
{
    auto buf = std::make_shared<DecodedAudioBuffer>();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return buf;
    }
    const QByteArray head = f.read(4096);
    if (head.size() < 0x80 || head.left(4) != QByteArrayLiteral("riff")) {
        return buf;
    }

    // Chunk header: a 16-byte id (four readable characters plus a 12-byte GUID tail),
    // then an 8-byte size — and that size **counts the header too**. That is the detail a
    // normal RIFF reader gets wrong here: "fmt " declares 40, of which only 16 are the
    // WAVEFORMATEX, and 0x28 + 40 lands exactly on the "data" chunk.
    constexpr int kChunkHeader = 24; // 16-byte id + 8-byte size
    const uchar *base = reinterpret_cast<const uchar *>(head.constData());
    int pos = kChunkHeader + 16; // past the riff header and the "wave" form id
    int fmtAt = -1;
    int dataAt = -1;
    qint64 dataBytes = 0;
    while (pos + kChunkHeader <= head.size()) {
        const QByteArray id = head.mid(pos, 4);
        const qint64 size = qFromLittleEndian<qint64>(base + pos + 16);
        if (size <= kChunkHeader || size > (1LL << 40)) {
            break;
        }
        const int payload = pos + kChunkHeader;
        if (id == QByteArrayLiteral("fmt ")) {
            fmtAt = payload;
        } else if (id == QByteArrayLiteral("data")) {
            dataAt = payload;
            dataBytes = size - kChunkHeader;
            break;
        }
        pos += int(size);
    }
    if (fmtAt < 0 || dataAt < 0 || fmtAt + 16 > head.size()) {
        return buf;
    }

    const quint16 formatTag = qFromLittleEndian<quint16>(base + fmtAt);
    const quint16 channels = qFromLittleEndian<quint16>(base + fmtAt + 2);
    const quint32 rate = qFromLittleEndian<quint32>(base + fmtAt + 4);
    const quint16 blockAlign = qFromLittleEndian<quint16>(base + fmtAt + 12);
    const quint16 bits = qFromLittleEndian<quint16>(base + fmtAt + 14);
    if (channels < 1 || channels > 8 || rate < 8000 || rate > 384000 || blockAlign == 0) {
        return buf;
    }
    const bool isFloat = formatTag == 3;
    if (!isFloat && !(formatTag == 1 && (bits == 16 || bits == 24 || bits == 32))) {
        return buf; // an encoding this reader does not claim to handle
    }

    if (!f.seek(dataAt)) {
        return buf;
    }
    const qint64 available = qMin<qint64>(dataBytes, f.size() - dataAt);
    const qint64 frames = available / blockAlign;
    if (frames <= 0) {
        return buf;
    }
    buf->sampleRate = int(rate);
    buf->channels = int(channels);
    buf->samples.resize(int(frames * channels));

    // Stream it: this file is a full-quality mixdown, 232 MB for ten minutes in the
    // sample, so reading it whole before converting would double that in memory.
    constexpr qint64 kChunkFrames = 1 << 15;
    QByteArray raw;
    qint64 done = 0;
    while (done < frames) {
        const qint64 want = qMin(kChunkFrames, frames - done);
        raw = f.read(want * blockAlign);
        const qint64 got = raw.size() / blockAlign;
        if (got <= 0) {
            break;
        }
        const uchar *p = reinterpret_cast<const uchar *>(raw.constData());
        float *out = buf->samples.data() + done * channels;
        for (qint64 i = 0; i < got * channels; ++i) {
            if (isFloat) {
                out[i] = qFromLittleEndian<float>(p + i * 4);
            } else if (bits == 16) {
                out[i] = float(qFromLittleEndian<qint16>(p + i * 2)) / 32768.0f;
            } else if (bits == 24) {
                const qint32 v = (qint32(p[i * 3]) << 8) | (qint32(p[i * 3 + 1]) << 16)
                                 | (qint32(p[i * 3 + 2]) << 24);
                out[i] = float(v >> 8) / 8388608.0f;
            } else {
                out[i] = float(qFromLittleEndian<qint32>(p + i * 4)) / 2147483648.0f;
            }
        }
        done += got;
    }
    if (done < frames) {
        buf->samples.resize(int(done * channels));
    }
    buf->ready = !buf->samples.isEmpty();
    return buf;
}

/** `<project>.veg` → its `<project>.veg.sfap0` mixdown, when VEGAS has written one. */
QString AudioDecodeCache::sfap0Beside(const QString &mediaPath)
{
    if (!mediaPath.endsWith(QLatin1String(".veg"), Qt::CaseInsensitive)) {
        return {};
    }
    const QString candidate = mediaPath + QStringLiteral(".sfap0");
    return QFileInfo::exists(candidate) ? candidate : QString();
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
