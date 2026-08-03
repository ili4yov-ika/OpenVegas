#include "io/FFmpegStreamDecoder.h"

#include "io/MediaFilmstripCache.h"

#include <QMutex>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef OPENVGAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#endif

namespace openvegas {
namespace {

QMutex &encoderProbeMutex()
{
    static QMutex m;
    return m;
}

QSet<QString> &encoderCache()
{
    static QSet<QString> s;
    return s;
}

bool &encoderCacheReady()
{
    static bool r = false;
    return r;
}

void ensureEncoderCache()
{
    QMutexLocker lock(&encoderProbeMutex());
    if (encoderCacheReady()) {
        return;
    }
    encoderCacheReady() = true;
    const QString ffmpeg = MediaFilmstripCache::findFfmpeg();
    if (ffmpeg.isEmpty()) {
        return;
    }
    QProcess proc;
    proc.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-encoders")});
    if (!proc.waitForFinished(15000) || proc.exitStatus() != QProcess::NormalExit) {
        return;
    }
    const QString text = QString::fromUtf8(proc.readAllStandardOutput());
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        // " V..... h264_nvenc           NVIDIA NVENC H.264 encoder"
        if (line.size() < 10) {
            continue;
        }
        // Skip header lines
        if (!line.contains(QLatin1Char(' '))) {
            continue;
        }
        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 2 && parts[0].size() >= 6) {
            encoderCache().insert(parts[1]);
        }
    }
}

QImage rgb24ToArgb(const QByteArray &rgb, int w, int h)
{
    if (w < 1 || h < 1 || rgb.size() < w * h * 3) {
        return {};
    }
    QImage img(w, h, QImage::Format_RGB888);
    const char *src = rgb.constData();
    for (int y = 0; y < h; ++y) {
        std::memcpy(img.scanLine(y), src + y * w * 3, size_t(w * 3));
    }
    return img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QStringList hwAccelPrefix()
{
    if (!FFmpegStreamDecoder::hwAccelEnabled()) {
        return {};
    }
    return {QStringLiteral("-hwaccel"), QStringLiteral("auto")};
}

int decodeSequenceCli(const QString &path, double startSec, double fps, int count,
                      const QSize &outSize, QVector<QImage> *out, bool useHw)
{
    if (!out || count < 1 || outSize.width() < 2 || outSize.height() < 2) {
        return 0;
    }
    out->clear();
    out->resize(count);

    const QString ffmpeg = MediaFilmstripCache::findFfmpeg();
    if (ffmpeg.isEmpty()) {
        return 0;
    }

    const double useFps = std::clamp(fps, 1.0, 120.0);
    const double dur = double(count) / useFps + (1.0 / useFps) * 0.5;
    const int w = outSize.width();
    const int h = outSize.height();
    const int frameBytes = w * h * 3;

    QStringList args;
    args << QStringLiteral("-hide_banner") << QStringLiteral("-loglevel") << QStringLiteral("error");
    if (useHw) {
        args << hwAccelPrefix();
    }
    args << QStringLiteral("-ss") << QString::number(std::max(0.0, startSec), 'f', 3)
         << QStringLiteral("-i") << path << QStringLiteral("-t")
         << QString::number(dur, 'f', 3) << QStringLiteral("-an") << QStringLiteral("-vf")
         << QStringLiteral("fps=%1,scale=%2:%3:force_original_aspect_ratio=increase,crop=%2:%3")
                .arg(useFps, 0, 'f', 4)
                .arg(w)
                .arg(h)
         << QStringLiteral("-f") << QStringLiteral("rawvideo") << QStringLiteral("-pix_fmt")
         << QStringLiteral("rgb24") << QStringLiteral("-");

    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start(ffmpeg, args);
    if (!proc.waitForStarted(10000)) {
        return 0;
    }

    QByteArray buf;
    buf.reserve(frameBytes);
    int got = 0;
    const int timeoutMs = std::min(120000, 15000 + count * 400);

    while (got < count) {
        if (proc.bytesAvailable() < 1) {
            if (!proc.waitForReadyRead(timeoutMs)) {
                if (proc.state() == QProcess::NotRunning) {
                    break;
                }
                // Drain whatever is left
                if (proc.bytesAvailable() < 1) {
                    break;
                }
            }
        }
        buf.append(proc.read(frameBytes - buf.size()));
        while (buf.size() >= frameBytes && got < count) {
            (*out)[got] = rgb24ToArgb(buf.left(frameBytes), w, h);
            buf.remove(0, frameBytes);
            if (!(*out)[got].isNull()) {
                ++got;
            } else {
                // count null as consumed slot
                ++got;
            }
        }
        if (proc.state() == QProcess::NotRunning && proc.bytesAvailable() < 1
            && buf.size() < frameBytes) {
            break;
        }
    }

    if (proc.state() != QProcess::NotRunning) {
        proc.closeReadChannel(QProcess::StandardOutput);
        proc.kill();
        proc.waitForFinished(3000);
    } else {
        proc.waitForFinished(3000);
    }

    // If hwaccel produced nothing, caller may retry without.
    int nonNull = 0;
    for (const QImage &im : *out) {
        if (!im.isNull()) {
            ++nonNull;
        }
    }
    return nonNull;
}

#ifdef OPENVGAS_FFMPEG
int decodeSequenceLibav(const QString &path, double startSec, double fps, int count,
                        const QSize &outSize, QVector<QImage> *out)
{
    if (!out || count < 1) {
        return 0;
    }
    out->clear();
    out->resize(count);

    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0) {
        return 0;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return 0;
    }
    const int vStream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vStream < 0) {
        avformat_close_input(&fmt);
        return 0;
    }
    AVStream *st = fmt->streams[vStream];
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt);
        return 0;
    }
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    if (!ctx || avcodec_parameters_to_context(ctx, st->codecpar) < 0
        || avcodec_open2(ctx, codec, nullptr) < 0) {
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return 0;
    }

    const int w = outSize.width();
    const int h = outSize.height();
    SwsContext *sws =
        sws_getContext(ctx->width, ctx->height, ctx->pix_fmt, w, h, AV_PIX_FMT_RGB24, SWS_BILINEAR,
                       nullptr, nullptr, nullptr);
    if (!sws) {
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return 0;
    }

    const double tb = av_q2d(st->time_base);
    const int64_t seekTs = int64_t(std::max(0.0, startSec) / tb);
    av_seek_frame(fmt, vStream, seekTs, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(ctx);

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    QByteArray rgb(w * h * 3, Qt::Uninitialized);
    uint8_t *dstSlice[4] = {reinterpret_cast<uint8_t *>(rgb.data()), nullptr, nullptr, nullptr};
    int dstStride[4] = {w * 3, 0, 0, 0};

    const double useFps = std::clamp(fps, 1.0, 120.0);
    const double step = 1.0 / useFps;
    int got = 0;
    double nextT = startSec;

    while (got < count && av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index != vStream) {
            av_packet_unref(pkt);
            continue;
        }
        if (avcodec_send_packet(ctx, pkt) < 0) {
            av_packet_unref(pkt);
            continue;
        }
        av_packet_unref(pkt);
        while (got < count && avcodec_receive_frame(ctx, frame) == 0) {
            double pts = startSec;
            if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                pts = frame->best_effort_timestamp * tb;
            } else if (frame->pts != AV_NOPTS_VALUE) {
                pts = frame->pts * tb;
            }
            if (pts + step * 0.5 < nextT) {
                continue; // skip until near target bucket
            }
            sws_scale(sws, frame->data, frame->linesize, 0, ctx->height, dstSlice, dstStride);
            (*out)[got] = rgb24ToArgb(rgb, w, h);
            ++got;
            nextT = startSec + got * step;
        }
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    sws_freeContext(sws);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);

    int nonNull = 0;
    for (const QImage &im : *out) {
        if (!im.isNull()) {
            ++nonNull;
        }
    }
    return nonNull;
}
#endif

} // namespace

bool FFmpegStreamDecoder::linkedAvailable()
{
#ifdef OPENVGAS_FFMPEG
    return true;
#else
    return false;
#endif
}

bool FFmpegStreamDecoder::hwAccelEnabled()
{
    const QByteArray env = qgetenv("OPENVGAS_HWACCEL");
    if (env == "0" || env == "off" || env == "false") {
        return false;
    }
    if (env == "1" || env == "on" || env == "auto" || env == "true") {
        return true;
    }
    QSettings s(QStringLiteral("OpenVegas"), QStringLiteral("OpenVegas"));
    return s.value(QStringLiteral("media/hwAccel"), true).toBool();
}

int FFmpegStreamDecoder::decodeSequence(const QString &path, double startSec, double fps, int count,
                                        const QSize &outSize, QVector<QImage> *out)
{
    if (!out || count < 1 || path.isEmpty()) {
        return 0;
    }

#ifdef OPENVGAS_FFMPEG
    const int linked = decodeSequenceLibav(path, startSec, fps, count, outSize, out);
    if (linked > 0) {
        return linked;
    }
#endif

    const bool wantHw = hwAccelEnabled();
    int n = decodeSequenceCli(path, startSec, fps, count, outSize, out, wantHw);
    if (n == 0 && wantHw) {
        n = decodeSequenceCli(path, startSec, fps, count, outSize, out, false);
    }
    return n;
}

QImage FFmpegStreamDecoder::decodeFrame(const QString &path, double timeSec, const QSize &outSize)
{
    QVector<QImage> frames;
    // Decode a tiny continuous window (2 frames) so ffmpeg does sequential read after one seek.
    const double fps = 30.0;
    if (decodeSequence(path, timeSec, fps, 2, outSize, &frames) > 0 && !frames[0].isNull()) {
        return frames[0];
    }
    if (frames.size() > 1 && !frames[1].isNull()) {
        return frames[1];
    }
    return {};
}

bool FFmpegStreamDecoder::encoderAvailable(const QString &encoderName)
{
    if (encoderName.isEmpty()) {
        return false;
    }
    ensureEncoderCache();
    QMutexLocker lock(&encoderProbeMutex());
    return encoderCache().contains(encoderName);
}

QStringList FFmpegStreamDecoder::videoEncodeCodecArgs(int crf)
{
    crf = std::clamp(crf, 0, 51);
    // Prefer HW encoders when ffmpeg reports them (Phase 6); soft fallback to libx264.
    if (encoderAvailable(QStringLiteral("h264_nvenc"))) {
        return {QStringLiteral("-c:v"), QStringLiteral("h264_nvenc"), QStringLiteral("-preset"),
                QStringLiteral("p4"),   QStringLiteral("-cq"),        QString::number(crf)};
    }
    if (encoderAvailable(QStringLiteral("h264_qsv"))) {
        return {QStringLiteral("-c:v"), QStringLiteral("h264_qsv"), QStringLiteral("-global_quality"),
                QString::number(crf)};
    }
    if (encoderAvailable(QStringLiteral("h264_amf"))) {
        return {QStringLiteral("-c:v"), QStringLiteral("h264_amf"), QStringLiteral("-rc"),
                QStringLiteral("cqp"),  QStringLiteral("-qp_i"),    QString::number(crf)};
    }
    return {QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-pix_fmt"),
            QStringLiteral("yuv420p"), QStringLiteral("-crf"), QString::number(crf)};
}

} // namespace openvegas
