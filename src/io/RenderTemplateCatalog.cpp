#include "io/RenderTemplateCatalog.h"

#include <QSettings>

#include <algorithm>
#include <cmath>

namespace openvegas {
namespace {

RenderTemplate T(const QString &name, bool audioOnly, const QString &ext, const QString &info,
                 int sr = 0, int ch = 0, int br = 0, int bitDepth = 0, int w = 0, int h = 0,
                 double fps = 0.0, const QString &videoSummary = {}, double par = 1.0)
{
    RenderTemplate t;
    t.name = name;
    t.audioOnly = audioOnly;
    t.extension = ext;
    t.info = info;
    t.sampleRate = sr;
    t.channels = ch;
    t.bitrateKbps = br;
    t.bitDepth = bitDepth;
    t.width = w;
    t.height = h;
    t.fps = fps;
    t.videoSummary = videoSummary;
    t.pixelAspect = par;
    return t;
}

RenderFormat F(const QString &name, bool audioOnly, const QString &ext, const QString &about,
               const QVector<RenderTemplate> &tpl, const QString &aboutExtra = {})
{
    RenderFormat f;
    f.name = name;
    f.audioOnly = audioOnly;
    f.extension = ext;
    f.aboutTitle = about;
    f.aboutExtra = aboutExtra;
    f.templates = tpl;
    return f;
}

QString audioInfoLine(int br, int sr, int ch, int bits, const QString &codec,
                      const QString &extra = {})
{
    QString s = QStringLiteral("Audio: ");
    if (br > 0) {
        s += QStringLiteral("%1 Kbps; ").arg(br);
    }
    if (sr > 0) {
        s += QStringLiteral("%1 Hz; ").arg(sr);
    }
    if (bits > 0) {
        s += QStringLiteral("%1 Bit; ").arg(bits);
    }
    s += (ch <= 1) ? QStringLiteral("Mono") : QStringLiteral("Stereo");
    if (!codec.isEmpty()) {
        s += QStringLiteral("; %1").arg(codec);
    }
    if (!extra.isEmpty()) {
        s += QStringLiteral("\n%1").arg(extra);
    }
    return s;
}

QVector<RenderTemplate> aacTemplates()
{
    const QString e = QStringLiteral(".m4a");
    auto aac = [&](const QString &name, int sr, int ch, int br, const QString &profile) {
        return T(name, true, e, audioInfoLine(br, sr, ch, 16, profile), sr, ch, br, 16);
    };
    return {
        aac(QStringLiteral("44 100 Hz; 16 Bit; 256 Kbps, Stereo, AAC LC"), 44100, 2, 256,
            QStringLiteral("AAC LC")),
        aac(QStringLiteral("44 100 Hz; 16 Bit; 192 Kbps, Stereo, AAC LC"), 44100, 2, 192,
            QStringLiteral("AAC LC")),
        aac(QStringLiteral("44 100 Hz; 16 Bit; 128 Kbps, Stereo, AAC LC"), 44100, 2, 128,
            QStringLiteral("AAC LC")),
        aac(QStringLiteral("44 100 Hz; 16 Bit; 96 Kbps, Stereo, AAC LC"), 44100, 2, 96,
            QStringLiteral("AAC LC")),
        aac(QStringLiteral("32 000 Hz; 16 Bit; 128 Kbps, Stereo, AAC LC"), 32000, 2, 128,
            QStringLiteral("AAC LC")),
        aac(QStringLiteral("32 000 Hz; 16 Bit; 96 Kbps, Stereo, AAC LC"), 32000, 2, 96,
            QStringLiteral("AAC LC")),
        aac(QStringLiteral("22 050 Hz; 16 Bit; 64 Kbps, Stereo, AAC LC"), 22050, 2, 64,
            QStringLiteral("AAC LC")),
        aac(QStringLiteral("44 100 Hz; 16 Bit; 128 Kbps, Mono, AAC LC"), 44100, 1, 128,
            QStringLiteral("AAC LC")),
        aac(QStringLiteral("44 100 Hz; 16 Bit; 64 Kbps, Stereo, HE-AAC"), 44100, 2, 64,
            QStringLiteral("HE-AAC")),
        aac(QStringLiteral("44 100 Hz; 16 Bit; 48 Kbps, Stereo, HE-AAC"), 44100, 2, 48,
            QStringLiteral("HE-AAC")),
        aac(QStringLiteral("44 100 Hz; 16 Bit; 32 Kbps, Stereo, HE-AAC"), 44100, 2, 32,
            QStringLiteral("HE-AAC")),
    };
}

QVector<RenderTemplate> ac3Templates()
{
    const QString e = QStringLiteral(".ac3");
    return {
        T(QStringLiteral("Stereo DVD"), true, e,
          QStringLiteral(
              "Audio: 192 Kbps; 48 000 Hz; Stereo, Automatic Gain Control off.\n"
              "Use this setting for stereo DVD soundtracks.\n"
              "Audio: 192 Kbps; 48 000 Hz; 24 Bit; Stereo; AC3"),
          48000, 2, 192, 24),
        T(QStringLiteral("Surround DVD"), true, e,
          QStringLiteral(
              "Audio: 448 Kbps; 48 000 Hz; 5.1 Surround, Automatic Gain Control off.\n"
              "Use this setting for surround DVD soundtracks.\n"
              "Audio: 448 Kbps; 48 000 Hz; 24 Bit; 5.1; AC3"),
          48000, 6, 448, 24),
        T(QStringLiteral("48 000 Hz; 16 Bit; 448 Kbps, Stereo"), true, e,
          audioInfoLine(448, 48000, 2, 16, QStringLiteral("AC3")), 48000, 2, 448, 16),
        T(QStringLiteral("48 000 Hz; 16 Bit; 192 Kbps, Stereo"), true, e,
          audioInfoLine(192, 48000, 2, 16, QStringLiteral("AC3")), 48000, 2, 192, 16),
        T(QStringLiteral("Default Template"), true, e,
          audioInfoLine(192, 48000, 2, 16, QStringLiteral("AC3")), 48000, 2, 192, 16),
    };
}

QVector<RenderTemplate> aiffTemplates()
{
    const QString e = QStringLiteral(".aif");
    auto pcm = [&](int sr, int bits, int ch) {
        const QString chName = ch <= 1 ? QStringLiteral("Mono") : QStringLiteral("Stereo");
        const QString name =
            QStringLiteral("%1 Hz; %2 Bit; %3; PCM").arg(sr).arg(bits).arg(chName);
        // Insert thin spaces like Vegas "44 100"
        QString pretty = name;
        pretty.replace(QStringLiteral("44100"), QStringLiteral("44 100"));
        pretty.replace(QStringLiteral("48000"), QStringLiteral("48 000"));
        pretty.replace(QStringLiteral("22050"), QStringLiteral("22 050"));
        pretty.replace(QStringLiteral("96000"), QStringLiteral("96 000"));
        const int br = int(std::llround(double(sr) * bits * ch / 1000.0));
        return T(pretty, true, e, audioInfoLine(0, sr, ch, bits, QStringLiteral("PCM")), sr, ch, br,
                 bits);
    };
    return {
        T(QStringLiteral("Default Template"), true, e,
          audioInfoLine(0, 44100, 2, 16, QStringLiteral("PCM")), 44100, 2, 1411, 16),
        pcm(22050, 16, 1),
        pcm(44100, 16, 1),
        pcm(44100, 24, 1),
        pcm(44100, 16, 2),
        pcm(44100, 24, 2),
        pcm(48000, 16, 2),
        pcm(48000, 24, 2),
        pcm(96000, 24, 2),
    };
}

QVector<RenderTemplate> flacTemplates()
{
    const QString e = QStringLiteral(".flac");
    auto fl = [&](int sr, int bits, int ch) {
        const QString chName = ch <= 1 ? QStringLiteral("Mono") : QStringLiteral("Stereo");
        QString name = QStringLiteral("%1 Hz; %2 Bit; %3").arg(sr).arg(bits).arg(chName);
        name.replace(QStringLiteral("22050"), QStringLiteral("22 050"));
        name.replace(QStringLiteral("44100"), QStringLiteral("44 100"));
        name.replace(QStringLiteral("48000"), QStringLiteral("48 000"));
        name.replace(QStringLiteral("96000"), QStringLiteral("96 000"));
        // FLAC ~50% of PCM for estimate
        const int br = int(std::llround(double(sr) * bits * ch / 1000.0 * 0.5));
        return T(name, true, e, audioInfoLine(0, sr, ch, bits, QStringLiteral("FLAC")), sr, ch, br,
                 bits);
    };
    return {
        T(QStringLiteral("Default Template"), true, e,
          audioInfoLine(0, 44100, 2, 16, QStringLiteral("FLAC")), 44100, 2, 700, 16),
        fl(22050, 16, 1),
        fl(44100, 16, 1),
        fl(44100, 16, 2),
        fl(48000, 16, 2),
        fl(48000, 24, 2),
        fl(96000, 24, 2),
    };
}

QVector<RenderTemplate> proResTemplates()
{
    const QString e = QStringLiteral(".mov");
    // Approx ProRes Mbps: 422@1080p23.976 ~63, HQ higher, XQ higher; scale by res/fps
    auto add = [&](QVector<RenderTemplate> &out, const QString &flavor, int w, int h, double fps,
                   int mbps) {
        const QString name =
            QStringLiteral("ProRes %1 %2x%3-%4p")
                .arg(flavor)
                .arg(w)
                .arg(h)
                .arg(fps, 0, 'f', 3);
        const QString info =
            QStringLiteral("Audio: 48,000 Hz; 16 Bit (MAC); Stereo; PCM\n"
                           "Video: %1 fps; %2x%3 Progressive; YUV (10 bit)\n"
                           "Pixel Aspect Ratio: 1,000")
                .arg(fps, 0, 'f', 3)
                .arg(w)
                .arg(h);
        out.push_back(T(name, false, e, info, 48000, 2, mbps * 1000, 16, w, h, fps,
                        QStringLiteral("YUV (10 bit)"), 1.0));
    };

    QVector<RenderTemplate> out;
    const QList<double> rates = {23.976, 25.0, 29.97, 50.0, 59.94};
    for (const QString &flavor : {QStringLiteral("422"), QStringLiteral("422 HQ"),
                                  QStringLiteral("422 XQ")}) {
        const int base = flavor.contains(QLatin1String("XQ"))   ? 220
                         : flavor.contains(QLatin1String("HQ")) ? 110
                                                                : 63;
        for (double fps : rates) {
            add(out, flavor, 1920, 1080, fps, int(std::lround(base * (fps / 23.976))));
        }
        for (double fps : {23.976, 25.0, 29.97}) {
            add(out, flavor, 3840, 2160, fps, int(std::lround(base * 4.0 * (fps / 23.976))));
            add(out, flavor, 4096, 2160, fps, int(std::lround(base * 4.2 * (fps / 23.976))));
        }
        add(out, flavor, 1080, 1080, 29.97, int(std::lround(base * 0.6)));
    }
    out.push_back(T(QStringLiteral("Default Template"), false, e,
                    QStringLiteral("Audio: 48,000 Hz; 16 Bit (MAC); Stereo; PCM\n"
                                   "Video: 29.97 fps; 1920x1080 Progressive; YUV (10 bit)\n"
                                   "Pixel Aspect Ratio: 1,000"),
                    48000, 2, 63000, 16, 1920, 1080, 29.97, QStringLiteral("YUV (10 bit)")));
    return out;
}

QVector<RenderTemplate> mp4AvcTemplates()
{
    const QString e = QStringLiteral(".mp4");
    auto hd = [&](double fps, const QString &enc = {}) {
        const QString tag = enc.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(enc);
        const int br = 12000; // ~12 Mbps
        return T(QStringLiteral("Internet HD 1080p %1 fps%2").arg(fps, 0, 'f', 3).arg(tag), false, e,
                 QStringLiteral("Audio: AAC Stereo\nVideo: %1 fps; 1920x1080 Progressive; H.264")
                     .arg(fps, 0, 'f', 3),
                 48000, 2, br, 16, 1920, 1080, fps, QStringLiteral("H.264"));
    };
    QVector<RenderTemplate> out;
    for (double fps : {59.94, 50.0, 29.97, 25.0, 23.976}) {
        out.push_back(hd(fps, QStringLiteral("NVENC")));
    }
    for (double fps : {59.94, 50.0, 29.97, 25.0, 23.976}) {
        out.push_back(hd(fps));
    }
    out.push_back(T(QStringLiteral("Internet UHD 2160p 29.97 fps"), false, e,
                    QStringLiteral("Audio: AAC Stereo\nVideo: 29.97 fps; 3840x2160 Progressive; H.264"),
                    48000, 2, 45000, 16, 3840, 2160, 29.97));
    out.push_back(T(QStringLiteral("Default Template"), false, e,
                    QStringLiteral("Default AVC/AAC MP4 template"), 48000, 2, 12000, 16, 1920,
                    1080, 29.97));
    return out;
}

QVector<RenderTemplate> mp3Templates()
{
    const QString e = QStringLiteral(".mp3");
    auto mp3 = [&](const QString &name, int br, const QString &qualityNote) {
        return T(name, true, e,
                 audioInfoLine(br, 44100, 2, 16, QStringLiteral("MP3"), qualityNote), 44100, 2, br,
                 16);
    };
    return {
        mp3(QStringLiteral("Highest Quality VBR Stereo Audio"), 256,
            QStringLiteral("VBR · CD transparent quality")),
        mp3(QStringLiteral("320 Kbps, CD Transparent Audio"), 320,
            QStringLiteral("CD Transparent Audio")),
        mp3(QStringLiteral("256 Kbps, Near CD Transparent Audio"), 256,
            QStringLiteral("Near CD Transparent Audio")),
        mp3(QStringLiteral("192 Kbps, Near CD Quality Audio"), 192, QStringLiteral("Near CD Quality")),
        mp3(QStringLiteral("160 Kbps, Near CD Quality Audio"), 160, QStringLiteral("Near CD Quality")),
        mp3(QStringLiteral("128 Kbps, CD Quality Audio"), 128, QStringLiteral("CD Quality Audio")),
        mp3(QStringLiteral("112 Kbps, Near CD Quality Audio"), 112, QStringLiteral("Near CD Quality")),
        mp3(QStringLiteral("96 Kbps, Near CD Quality Audio"), 96, QStringLiteral("Near CD Quality Audio")),
        mp3(QStringLiteral("80 Kbps, FM Radio Quality Audio"), 80, QStringLiteral("FM Radio Quality")),
        mp3(QStringLiteral("64 Kbps, FM Radio Quality Audio"), 64, QStringLiteral("FM Radio Quality Audio")),
        mp3(QStringLiteral("56 Kbps, Voice Quality Audio"), 56, QStringLiteral("Voice Quality")),
        mp3(QStringLiteral("48 Kbps, Voice Quality Audio"), 48, QStringLiteral("Voice Quality")),
        mp3(QStringLiteral("40 Kbps, Voice Quality Audio"), 40, QStringLiteral("Voice Quality")),
        mp3(QStringLiteral("32 Kbps, Voice Quality Audio"), 32, QStringLiteral("Voice Quality")),
        T(QStringLiteral("Default Template"), true, e,
          audioInfoLine(192, 44100, 2, 16, QStringLiteral("MP3")), 44100, 2, 192, 16),
    };
}

QVector<RenderTemplate> mpeg1Templates()
{
    const QString e = QStringLiteral(".mpg");
    return {
        T(QStringLiteral("VCD NTSC"), false, e,
          QStringLiteral("Video: MPEG-1 · VCD NTSC 352×240\nAudio: MPEG Layer II"), 44100, 2, 1150,
          16, 352, 240, 29.97),
        T(QStringLiteral("VCD PAL"), false, e,
          QStringLiteral("Video: MPEG-1 · VCD PAL 352×288\nAudio: MPEG Layer II"), 44100, 2, 1150, 16,
          352, 288, 25.0),
        T(QStringLiteral("Default Template"), false, e, QStringLiteral("Default MPEG-1 template"),
          44100, 2, 1150, 16, 352, 240, 29.97),
    };
}

QVector<RenderTemplate> mpeg2Templates()
{
    const QString e = QStringLiteral(".mpg");
    return {
        T(QStringLiteral("Program Stream NTSC"), false, e,
          QStringLiteral("Video: MPEG-2 Program Stream NTSC\nAudio: MPEG Layer II"), 48000, 2, 8000,
          16, 720, 480, 29.97),
        T(QStringLiteral("Program Stream PAL"), false, e,
          QStringLiteral("Video: MPEG-2 Program Stream PAL\nAudio: MPEG Layer II"), 48000, 2, 8000, 16,
          720, 576, 25.0),
        T(QStringLiteral("DVD Architect NTSC video stream"), false, e,
          QStringLiteral("Video: MPEG-2 DVD NTSC 720×480\nAudio: AC-3 / MPEG"), 48000, 2, 8000, 16,
          720, 480, 29.97),
        T(QStringLiteral("DVD Architect PAL video stream"), false, e,
          QStringLiteral("Video: MPEG-2 DVD PAL 720×576\nAudio: AC-3 / MPEG"), 48000, 2, 8000, 16, 720,
          576, 25.0),
        T(QStringLiteral("HDV 720-25p"), false, e,
          QStringLiteral("Video: HDV 1280×720 25p\nAudio: MPEG Layer II"), 48000, 2, 19000, 16, 1280,
          720, 25.0),
        T(QStringLiteral("HDV 720-30p"), false, e,
          QStringLiteral("Video: HDV 1280×720 29.97p\nAudio: MPEG Layer II"), 48000, 2, 19000, 16,
          1280, 720, 29.97),
        T(QStringLiteral("HDV 1080-50i"), false, e,
          QStringLiteral("Video: HDV 1440×1080 50i\nAudio: MPEG Layer II"), 48000, 2, 25000, 16, 1440,
          1080, 25.0),
        T(QStringLiteral("HDV 1080-60i"), false, e,
          QStringLiteral("Video: HDV 1440×1080 60i\nAudio: MPEG Layer II"), 48000, 2, 25000, 16, 1440,
          1080, 29.97),
        T(QStringLiteral("Blu-ray 1920x1080-50i, 25 Mbps video stream"), false, e,
          QStringLiteral("Video: Blu-ray 1920×1080 50i · 25 Mbps\nAudio: AC-3 / LPCM"), 48000, 2,
          25000, 16, 1920, 1080, 25.0),
        T(QStringLiteral("Blu-ray 1920x1080-60i, 25 Mbps video stream"), false, e,
          QStringLiteral("Video: Blu-ray 1920×1080 60i · 25 Mbps\nAudio: AC-3 / LPCM"), 48000, 2,
          25000, 16, 1920, 1080, 29.97),
        T(QStringLiteral("Blu-ray 1920x1080-24p, 25 Mbps video stream"), false, e,
          QStringLiteral("Video: Blu-ray 1920×1080 24p · 25 Mbps\nAudio: AC-3 / LPCM"), 48000, 2,
          25000, 16, 1920, 1080, 23.976),
        T(QStringLiteral("Default Template"), false, e, QStringLiteral("Default MPEG-2 template"),
          48000, 2, 8000, 16, 720, 480, 29.97),
    };
}

QVector<RenderTemplate> imageSequenceTemplates()
{
    auto img = [](const QString &name, const QString &ext) {
        return T(name, false, ext, name + QStringLiteral(" image sequence"));
    };
    return {
        img(QStringLiteral("BMP"), QStringLiteral(".bmp")),
        img(QStringLiteral("JPEG"), QStringLiteral(".jpg")),
        img(QStringLiteral("PNG"), QStringLiteral(".png")),
        img(QStringLiteral("TIFF"), QStringLiteral(".tif")),
        img(QStringLiteral("WMPhoto"), QStringLiteral(".wdp")),
        img(QStringLiteral("DDS"), QStringLiteral(".dds")),
        img(QStringLiteral("HEIC"), QStringLiteral(".heic")),
        img(QStringLiteral("DPX"), QStringLiteral(".dpx")),
        T(QStringLiteral("Default Template"), false, QStringLiteral(".png"),
          QStringLiteral("Default image sequence (PNG)")),
    };
}

} // namespace

QVector<RenderFormat> RenderTemplateCatalog::formats()
{
    static const QVector<RenderFormat> kFormats = {
        F(QStringLiteral("AAC Audio"), true, QStringLiteral(".m4a"),
          QStringLiteral("AAC Audio File Format Plug-In"), aacTemplates()),
        F(QStringLiteral("AC-3"), true, QStringLiteral(".ac3"),
          QStringLiteral("AC-3 File Format Plug-In"), ac3Templates()),
        F(QStringLiteral("Apple ProRes"), false, QStringLiteral(".mov"),
          QStringLiteral("Compound File Format Plug-In"), proResTemplates(),
          QStringLiteral("ProRes templates are provided for interchange. "
                         "Encoding requires a future FFmpeg/ProRes pipeline.")),
        F(QStringLiteral("Audio Interchange File Format (AIFF)"), true, QStringLiteral(".aif"),
          QStringLiteral("Audio Interchange File Format (AIFF) File Format Plug-In"),
          aiffTemplates()),
        F(QStringLiteral("FLAC Audio"), true, QStringLiteral(".flac"),
          QStringLiteral("FLAC File Format Plug-In"), flacTemplates(),
          QStringLiteral("FLAC support uses the open-source libFLAC specification.")),
        F(QStringLiteral("Image Sequence"), false, QStringLiteral(".png"),
          QStringLiteral("Image Sequence File Format Plug-In"), imageSequenceTemplates()),
        F(QStringLiteral("AV1"), false, QStringLiteral(".mp4"),
          QStringLiteral("AV1 File Format Plug-In"),
          {T(QStringLiteral("Internet UHD 2160p 29.97 fps"), false, QStringLiteral(".mp4"),
            QStringLiteral("AV1 · 3840×2160 · 29.97 fps"), 48000, 2, 35000, 16, 3840, 2160, 29.97),
           T(QStringLiteral("Internet HD 1080p 29.97 fps"), false, QStringLiteral(".mp4"),
             QStringLiteral("AV1 · 1920×1080 · 29.97 fps"), 48000, 2, 10000, 16, 1920, 1080, 29.97),
           T(QStringLiteral("Default Template"), false, QStringLiteral(".mp4"),
             QStringLiteral("Default Template"), 48000, 2, 10000, 16, 1920, 1080, 29.97)}),
        F(QStringLiteral("AVC/AAC MP4"), false, QStringLiteral(".mp4"),
          QStringLiteral("AVC/AAC MP4 File Format Plug-In"), mp4AvcTemplates()),
        F(QStringLiteral("HEVC/AAC MP4"), false, QStringLiteral(".mp4"),
          QStringLiteral("HEVC/AAC MP4 File Format Plug-In"),
          {T(QStringLiteral("Internet UHD 2160p 29.97 fps (NVENC)"), false, QStringLiteral(".mp4"),
            QStringLiteral("HEVC · 3840×2160"), 48000, 2, 40000, 16, 3840, 2160, 29.97),
           T(QStringLiteral("Internet HD 1080p 59.94 fps (NVENC)"), false, QStringLiteral(".mp4"),
             QStringLiteral("HEVC · 1920×1080"), 48000, 2, 15000, 16, 1920, 1080, 59.94),
           T(QStringLiteral("Internet HD 1080p 29.97 fps (NVENC)"), false, QStringLiteral(".mp4"),
             QStringLiteral("HEVC · 1920×1080"), 48000, 2, 10000, 16, 1920, 1080, 29.97),
           T(QStringLiteral("Default Template"), false, QStringLiteral(".mp4"),
             QStringLiteral("Default Template"), 48000, 2, 10000, 16, 1920, 1080, 29.97)}),
        F(QStringLiteral("MainConcept MPEG-1"), false, QStringLiteral(".mpg"),
          QStringLiteral("MainConcept MPEG-1 File Format Plug-In"), mpeg1Templates()),
        F(QStringLiteral("MainConcept MPEG-2"), false, QStringLiteral(".mpg"),
          QStringLiteral("MainConcept MPEG-2 File Format Plug-In"), mpeg2Templates()),
        F(QStringLiteral("MP3 Audio"), true, QStringLiteral(".mp3"),
          QStringLiteral("MP3 Audio File Format Plug-In"), mp3Templates(),
          QStringLiteral("MPEG Layer-3 audio coding technology licensed from Fraunhofer IIS "
                         "and Thomson. http://www.iis.fhg.de/amm/")),
        F(QStringLiteral("OggVorbis"), true, QStringLiteral(".ogg"),
          QStringLiteral("Ogg Vorbis File Format Plug-In"),
          {T(QStringLiteral("96 Kbps, 44 100 Hz, Mono"), true, QStringLiteral(".ogg"),
            audioInfoLine(96, 44100, 1, 16, QStringLiteral("Vorbis")), 44100, 1, 96, 16),
           T(QStringLiteral("96 Kbps, 44 100 Hz, Stereo"), true, QStringLiteral(".ogg"),
             audioInfoLine(96, 44100, 2, 16, QStringLiteral("Vorbis")), 44100, 2, 96, 16),
           T(QStringLiteral("96 Kbps, 48 000 Hz, Stereo"), true, QStringLiteral(".ogg"),
             audioInfoLine(96, 48000, 2, 16, QStringLiteral("Vorbis")), 48000, 2, 96, 16),
           T(QStringLiteral("128 Kbps, 44 100 Hz, Stereo"), true, QStringLiteral(".ogg"),
             audioInfoLine(128, 44100, 2, 16, QStringLiteral("Vorbis")), 44100, 2, 128, 16),
           T(QStringLiteral("128 Kbps, 48 000 Hz, Stereo"), true, QStringLiteral(".ogg"),
             audioInfoLine(128, 48000, 2, 16, QStringLiteral("Vorbis")), 48000, 2, 128, 16),
           T(QStringLiteral("350 Kbps, 44 100 Hz, Stereo"), true, QStringLiteral(".ogg"),
             audioInfoLine(350, 44100, 2, 16, QStringLiteral("Vorbis")), 44100, 2, 350, 16),
           T(QStringLiteral("350 Kbps, 48 000 Hz, Stereo"), true, QStringLiteral(".ogg"),
             audioInfoLine(350, 48000, 2, 16, QStringLiteral("Vorbis")), 48000, 2, 350, 16),
           T(QStringLiteral("VBR, 44 100 Hz, Stereo, Quality 0,40"), true, QStringLiteral(".ogg"),
             QStringLiteral("Audio: VBR Vorbis · 44.1 kHz · Stereo · Quality 0.40"), 44100, 2, 160,
             16),
           T(QStringLiteral("Default Template"), true, QStringLiteral(".ogg"),
             audioInfoLine(160, 44100, 2, 16, QStringLiteral("Vorbis")), 44100, 2, 160, 16)},
          QStringLiteral("Ogg Vorbis — Copyright (C) xiph.org.")),
        F(QStringLiteral("AVC/MVC"), false, QStringLiteral(".mp4"),
          QStringLiteral("AVC/MVC File Format Plug-In"),
          {T(QStringLiteral("Internet 1920x1080-30p"), false, QStringLiteral(".mp4"),
            QStringLiteral("AVC · 1920×1080 29.97p"), 48000, 2, 12000, 16, 1920, 1080, 29.97),
           T(QStringLiteral("Internet 1280x720-30p"), false, QStringLiteral(".mp4"),
             QStringLiteral("AVC · 1280×720 29.97p"), 48000, 2, 8000, 16, 1280, 720, 29.97),
           T(QStringLiteral("Memory Stick QVGA - 512 Kbps"), false, QStringLiteral(".mp4"),
             QStringLiteral("AVC · QVGA · 512 Kbps"), 48000, 2, 512, 16, 320, 240, 29.97),
           T(QStringLiteral("Memory Stick PSP full screen"), false, QStringLiteral(".mp4"),
             QStringLiteral("AVC · PSP full screen"), 48000, 2, 2000, 16, 480, 272, 29.97),
           T(QStringLiteral("AVCHD 1440x1080-60i"), false, QStringLiteral(".mp4"),
             QStringLiteral("AVCHD · 1440×1080 60i"), 48000, 2, 18000, 16, 1440, 1080, 29.97),
           T(QStringLiteral("Blu-ray 1920x1080-60i, 10 Mbps video stream"), false,
             QStringLiteral(".mp4"), QStringLiteral("Blu-ray AVC · 1920×1080 60i · 10 Mbps"), 48000,
             2, 10000, 16, 1920, 1080, 29.97),
           T(QStringLiteral("Blu-ray 1920x1080-50i, 10 Mbps video stream"), false,
             QStringLiteral(".mp4"), QStringLiteral("Blu-ray AVC · 1920×1080 50i · 10 Mbps"), 48000,
             2, 10000, 16, 1920, 1080, 25.0),
           T(QStringLiteral("Blu-ray 1920x1080-24p, 16 Mbps video stream"), false,
             QStringLiteral(".mp4"), QStringLiteral("Blu-ray AVC · 1920×1080 24p · 16 Mbps"), 48000,
             2, 16000, 16, 1920, 1080, 23.976),
           T(QStringLiteral("Default Template"), false, QStringLiteral(".mp4"),
             QStringLiteral("Default Template"), 48000, 2, 12000, 16, 1920, 1080, 29.97)}),
        F(QStringLiteral("MXF"), false, QStringLiteral(".mxf"),
          QStringLiteral("MXF File Format Plug-In"),
          {T(QStringLiteral("NTSC DV"), false, QStringLiteral(".mxf"),
            QStringLiteral("MXF · NTSC DV"), 48000, 2, 25000, 16, 720, 480, 29.97),
           T(QStringLiteral("NTSC DV Widescreen"), false, QStringLiteral(".mxf"),
             QStringLiteral("MXF · NTSC DV Widescreen"), 48000, 2, 25000, 16, 720, 480, 29.97),
           T(QStringLiteral("PAL DV"), false, QStringLiteral(".mxf"),
             QStringLiteral("MXF · PAL DV"), 48000, 2, 25000, 16, 720, 576, 25.0),
           T(QStringLiteral("NTSC MPEG IMX 50"), false, QStringLiteral(".mxf"),
             QStringLiteral("MXF · NTSC MPEG IMX 50"), 48000, 2, 50000, 16, 720, 480, 29.97),
           T(QStringLiteral("HD422 1280x720-60p 50 Mbps"), false, QStringLiteral(".mxf"),
             QStringLiteral("MXF HD422 · 1280×720 60p · 50 Mbps"), 48000, 2, 50000, 16, 1280, 720,
             59.94),
           T(QStringLiteral("HD422 1920x1080-60i 50 Mbps"), false, QStringLiteral(".mxf"),
             QStringLiteral("MXF HD422 · 1920×1080 60i · 50 Mbps"), 48000, 2, 50000, 16, 1920, 1080,
             29.97),
           T(QStringLiteral("HD EX 1920x1080-60i"), false, QStringLiteral(".mxf"),
             QStringLiteral("MXF HD EX · 1920×1080 60i"), 48000, 2, 35000, 16, 1920, 1080, 29.97),
           T(QStringLiteral("Default Template"), false, QStringLiteral(".mxf"),
             QStringLiteral("Default Template"))}),
        F(QStringLiteral("MXF HDCAM SR"), false, QStringLiteral(".mxf"),
          QStringLiteral("MXF HDCAM SR File Format Plug-In"),
          {T(QStringLiteral("HDCAM SR 422 1080-24p"), false, QStringLiteral(".mxf"),
            QStringLiteral("HDCAM SR 422 · 1920×1080 24p"), 48000, 2, 440000, 16, 1920, 1080, 23.976),
           T(QStringLiteral("HDCAM SR 422 1080-25p"), false, QStringLiteral(".mxf"),
             QStringLiteral("HDCAM SR 422 · 1920×1080 25p"), 48000, 2, 440000, 16, 1920, 1080, 25.0),
           T(QStringLiteral("HDCAM SR 422 1080-50i"), false, QStringLiteral(".mxf"),
             QStringLiteral("HDCAM SR 422 · 1920×1080 50i"), 48000, 2, 440000, 16, 1920, 1080, 25.0),
           T(QStringLiteral("HDCAM SR 422 1080-60i"), false, QStringLiteral(".mxf"),
             QStringLiteral("HDCAM SR 422 · 1920×1080 60i"), 48000, 2, 440000, 16, 1920, 1080, 29.97),
           T(QStringLiteral("HDCAM SR 422 720-50p"), false, QStringLiteral(".mxf"),
             QStringLiteral("HDCAM SR 422 · 1280×720 50p"), 48000, 2, 440000, 16, 1280, 720, 50.0),
           T(QStringLiteral("HDCAM SR 422 720-60p"), false, QStringLiteral(".mxf"),
             QStringLiteral("HDCAM SR 422 · 1280×720 60p"), 48000, 2, 440000, 16, 1280, 720, 59.94),
           T(QStringLiteral("HDCAM SR Lite 422 1080-24p"), false, QStringLiteral(".mxf"),
             QStringLiteral("HDCAM SR Lite 422 · 1920×1080 24p"), 48000, 2, 220000, 16, 1920, 1080,
             23.976),
           T(QStringLiteral("Default Template"), false, QStringLiteral(".mxf"),
             QStringLiteral("Default Template"))}),
        F(QStringLiteral("Perfect Clarity Audio"), true, QStringLiteral(".pca"),
          QStringLiteral("Perfect Clarity Audio File Format Plug-In"),
          {T(QStringLiteral("Default Template"), true, QStringLiteral(".pca"),
            QStringLiteral("Perfect Clarity Audio · Default"), 44100, 2, 2000, 16),
           T(QStringLiteral("44 100 Hz; 16 Bit; Mono"), true, QStringLiteral(".pca"),
             audioInfoLine(0, 44100, 1, 16, QStringLiteral("PCA")), 44100, 1, 700, 16),
           T(QStringLiteral("44 100 Hz; 16 Bit; Stereo"), true, QStringLiteral(".pca"),
             audioInfoLine(0, 44100, 2, 16, QStringLiteral("PCA")), 44100, 2, 1400, 16)}),
        F(QStringLiteral("Wave64"), true, QStringLiteral(".w64"),
          QStringLiteral("Wave64 File Format Plug-In"),
          {T(QStringLiteral("44 100 Hz; 16 Bit; Mono, PCM"), true, QStringLiteral(".w64"),
            audioInfoLine(0, 44100, 1, 16, QStringLiteral("PCM")), 44100, 1, 705, 16),
           T(QStringLiteral("44 100 Hz; 16 Bit; Stereo, PCM"), true, QStringLiteral(".w64"),
             audioInfoLine(0, 44100, 2, 16, QStringLiteral("PCM")), 44100, 2, 1411, 16),
           T(QStringLiteral("44 100 Hz; 24 Bit; Stereo, PCM"), true, QStringLiteral(".w64"),
             audioInfoLine(0, 44100, 2, 24, QStringLiteral("PCM")), 44100, 2, 2116, 24),
           T(QStringLiteral("48 000 Hz; 16 Bit; Stereo, PCM"), true, QStringLiteral(".w64"),
             audioInfoLine(0, 48000, 2, 16, QStringLiteral("PCM")), 48000, 2, 1536, 16),
           T(QStringLiteral("48 000 Hz; 24 Bit; Stereo, PCM"), true, QStringLiteral(".w64"),
             audioInfoLine(0, 48000, 2, 24, QStringLiteral("PCM")), 48000, 2, 2304, 24),
           T(QStringLiteral("Default Template"), true, QStringLiteral(".w64"),
             audioInfoLine(0, 48000, 2, 16, QStringLiteral("PCM")), 48000, 2, 1536, 16)}),
        F(QStringLiteral("XAVC / XAVC S"), false, QStringLiteral(".mxf"),
          QStringLiteral("XAVC S File Format Plug-In"),
          {T(QStringLiteral("XAVC Intra 1920x1080-23.976p"), false, QStringLiteral(".mxf"),
            QStringLiteral("XAVC Intra · 1920×1080 23.976p"), 48000, 2, 100000, 16, 1920, 1080,
            23.976),
           T(QStringLiteral("XAVC Intra 1920x1080-29.97p"), false, QStringLiteral(".mxf"),
             QStringLiteral("XAVC Intra · 1920×1080 29.97p"), 48000, 2, 100000, 16, 1920, 1080, 29.97),
           T(QStringLiteral("XAVC Intra 3840x2160-23.976p"), false, QStringLiteral(".mxf"),
             QStringLiteral("XAVC Intra · 3840×2160 23.976p"), 48000, 2, 240000, 16, 3840, 2160,
             23.976),
           T(QStringLiteral("XAVC Long 1920x1080-29.97p"), false, QStringLiteral(".mxf"),
             QStringLiteral("XAVC Long · 1920×1080 29.97p"), 48000, 2, 50000, 16, 1920, 1080, 29.97),
           T(QStringLiteral("XAVC Long 3840x2160-29.97p"), false, QStringLiteral(".mxf"),
             QStringLiteral("XAVC Long · 3840×2160 29.97p"), 48000, 2, 100000, 16, 3840, 2160, 29.97),
           T(QStringLiteral("XAVC S Long 1920x1080-59.94p"), false, QStringLiteral(".mp4"),
             QStringLiteral("XAVC S Long · 1920×1080 59.94p"), 48000, 2, 50000, 16, 1920, 1080, 59.94),
           T(QStringLiteral("XAVC S Long 3840x2160-29.97p"), false, QStringLiteral(".mp4"),
             QStringLiteral("XAVC S Long · 3840×2160 29.97p"), 48000, 2, 100000, 16, 3840, 2160,
             29.97),
           T(QStringLiteral("Default Template"), false, QStringLiteral(".mxf"),
             QStringLiteral("Default Template"))}),
        F(QStringLiteral("Video for Windows"), false, QStringLiteral(".avi"),
          QStringLiteral("Audio/Video Interleaved File Format Plug-In"),
          {T(QStringLiteral("NTSC DV"), false, QStringLiteral(".avi"),
            QStringLiteral("AVI · NTSC DV 720×480"), 48000, 2, 25000, 16, 720, 480, 29.97),
           T(QStringLiteral("NTSC DV Widescreen"), false, QStringLiteral(".avi"),
             QStringLiteral("AVI · NTSC DV Widescreen"), 48000, 2, 25000, 16, 720, 480, 29.97),
           T(QStringLiteral("PAL DV"), false, QStringLiteral(".avi"),
             QStringLiteral("AVI · PAL DV 720×576"), 48000, 2, 25000, 16, 720, 576, 25.0),
           T(QStringLiteral("PAL DV Widescreen"), false, QStringLiteral(".avi"),
             QStringLiteral("AVI · PAL DV Widescreen"), 48000, 2, 25000, 16, 720, 576, 25.0),
           T(QStringLiteral("HD 1280x720-30p YUV"), false, QStringLiteral(".avi"),
             QStringLiteral("AVI · 1280×720 29.97p YUV"), 48000, 2, 50000, 16, 1280, 720, 29.97),
           T(QStringLiteral("HD 1280x720-25p YUV"), false, QStringLiteral(".avi"),
             QStringLiteral("AVI · 1280×720 25p YUV"), 48000, 2, 50000, 16, 1280, 720, 25.0),
           T(QStringLiteral("HD 1920x1080-30p YUV"), false, QStringLiteral(".avi"),
             QStringLiteral("AVI · 1920×1080 29.97p YUV"), 48000, 2, 100000, 16, 1920, 1080, 29.97),
           T(QStringLiteral("HD 1920x1080-25p YUV"), false, QStringLiteral(".avi"),
             QStringLiteral("AVI · 1920×1080 25p YUV"), 48000, 2, 100000, 16, 1920, 1080, 25.0),
           T(QStringLiteral("1080x1080-60p Xvid"), false, QStringLiteral(".avi"),
             QStringLiteral("AVI · 1080×1080 59.94p Xvid"), 48000, 2, 8000, 16, 1080, 1080, 59.94),
           T(QStringLiteral("Default Template"), false, QStringLiteral(".avi"),
             QStringLiteral("Default AVI template"), 48000, 2, 20000, 16, 720, 480, 29.97)}),
        F(QStringLiteral("Wave (Microsoft)"), true, QStringLiteral(".wav"),
          QStringLiteral("Wave (Microsoft) File Format Plug-In"),
          {T(QStringLiteral("Default Template"), true, QStringLiteral(".wav"),
            audioInfoLine(0, 44100, 2, 16, QStringLiteral("PCM")), 44100, 2, 1411, 16),
           T(QStringLiteral("44 100 Hz; 16 Bit; Mono, PCM"), true, QStringLiteral(".wav"),
             audioInfoLine(0, 44100, 1, 16, QStringLiteral("PCM")), 44100, 1, 705, 16),
           T(QStringLiteral("44 100 Hz; 16 Bit; Stereo, PCM"), true, QStringLiteral(".wav"),
             audioInfoLine(0, 44100, 2, 16, QStringLiteral("PCM")), 44100, 2, 1411, 16),
           T(QStringLiteral("44 100 Hz; 24 Bit; Stereo, PCM"), true, QStringLiteral(".wav"),
             audioInfoLine(0, 44100, 2, 24, QStringLiteral("PCM")), 44100, 2, 2116, 24),
           T(QStringLiteral("48 000 Hz; 16 Bit; Mono, PCM"), true, QStringLiteral(".wav"),
             audioInfoLine(0, 48000, 1, 16, QStringLiteral("PCM")), 48000, 1, 768, 16),
           T(QStringLiteral("48 000 Hz; 16 Bit; Stereo, PCM"), true, QStringLiteral(".wav"),
             audioInfoLine(0, 48000, 2, 16, QStringLiteral("PCM")), 48000, 2, 1536, 16),
           T(QStringLiteral("48 000 Hz; 24 Bit; Stereo, PCM"), true, QStringLiteral(".wav"),
             audioInfoLine(0, 48000, 2, 24, QStringLiteral("PCM")), 48000, 2, 2304, 24),
           T(QStringLiteral("96 000 Hz; 24 Bit; Stereo, PCM"), true, QStringLiteral(".wav"),
             audioInfoLine(0, 96000, 2, 24, QStringLiteral("PCM")), 96000, 2, 4608, 24)}),
        F(QStringLiteral("Windows Media Audio V11"), true, QStringLiteral(".wma"),
          QStringLiteral("Windows Media Technologies File Format Plug-In"),
          {T(QStringLiteral("64 Kbps Stereo Music"), true, QStringLiteral(".wma"),
            audioInfoLine(64, 44100, 2, 16, QStringLiteral("WMA")), 44100, 2, 64, 16),
           T(QStringLiteral("96 Kbps Stereo Music"), true, QStringLiteral(".wma"),
             audioInfoLine(96, 44100, 2, 16, QStringLiteral("WMA")), 44100, 2, 96, 16),
           T(QStringLiteral("128 Kbps CD-Quality Audio"), true, QStringLiteral(".wma"),
             audioInfoLine(128, 44100, 2, 16, QStringLiteral("WMA")), 44100, 2, 128, 16),
           T(QStringLiteral("128 Kbps CD-Transparency Audio"), true, QStringLiteral(".wma"),
             audioInfoLine(128, 44100, 2, 16, QStringLiteral("WMA")), 44100, 2, 128, 16),
           T(QStringLiteral("160 Kbps Stereo Music"), true, QStringLiteral(".wma"),
             audioInfoLine(160, 44100, 2, 16, QStringLiteral("WMA")), 44100, 2, 160, 16),
           T(QStringLiteral("192 Kbps Stereo Music"), true, QStringLiteral(".wma"),
             audioInfoLine(192, 44100, 2, 16, QStringLiteral("WMA")), 44100, 2, 192, 16),
           T(QStringLiteral("128 Kbps CD-Quality Audio, 24 Bit, Stereo"), true, QStringLiteral(".wma"),
             audioInfoLine(128, 44100, 2, 24, QStringLiteral("WMA")), 44100, 2, 128, 24),
           T(QStringLiteral("Default Template"), true, QStringLiteral(".wma"),
             audioInfoLine(192, 44100, 2, 16, QStringLiteral("WMA")), 44100, 2, 192, 16)}),
        F(QStringLiteral("Windows Media Video V11"), false, QStringLiteral(".wmv"),
          QStringLiteral("Windows Media Technologies File Format Plug-In"),
          {T(QStringLiteral("512 Kbps Video"), false, QStringLiteral(".wmv"),
            QStringLiteral("WMV · 512 Kbps"), 48000, 2, 512, 16, 640, 480, 29.97),
           T(QStringLiteral("1 Mbps Video"), false, QStringLiteral(".wmv"),
             QStringLiteral("WMV · 1 Mbps"), 48000, 2, 1000, 16, 720, 480, 29.97),
           T(QStringLiteral("3 Mbps Video"), false, QStringLiteral(".wmv"),
             QStringLiteral("WMV · 3 Mbps"), 48000, 2, 3000, 16, 1280, 720, 29.97),
           T(QStringLiteral("4.8 Mbps HD 720-24p Video"), false, QStringLiteral(".wmv"),
             QStringLiteral("WMV · 4.8 Mbps · 1280×720 24p"), 48000, 2, 4800, 16, 1280, 720, 23.976),
           T(QStringLiteral("4.8 Mbps HD 720-25p Video"), false, QStringLiteral(".wmv"),
             QStringLiteral("WMV · 4.8 Mbps · 1280×720 25p"), 48000, 2, 4800, 16, 1280, 720, 25.0),
           T(QStringLiteral("6 Mbps HD 720-30p Video"), false, QStringLiteral(".wmv"),
             QStringLiteral("WMV · 6 Mbps · 1280×720 30p"), 48000, 2, 6000, 16, 1280, 720, 29.97),
           T(QStringLiteral("6.4 Mbps HD 1080-24p Video"), false, QStringLiteral(".wmv"),
             QStringLiteral("WMV · 6.4 Mbps · 1920×1080 24p"), 48000, 2, 6400, 16, 1920, 1080,
             23.976),
           T(QStringLiteral("6.7 Mbps HD 1080-25p Video"), false, QStringLiteral(".wmv"),
             QStringLiteral("WMV · 6.7 Mbps · 1920×1080 25p"), 48000, 2, 6700, 16, 1920, 1080, 25.0),
           T(QStringLiteral("8 Mbps HD 1080-30p Video"), false, QStringLiteral(".wmv"),
             QStringLiteral("WMV · 8 Mbps · 1920×1080 30p"), 48000, 2, 8000, 16, 1920, 1080, 29.97),
           T(QStringLiteral("Default Template"), false, QStringLiteral(".wmv"),
             QStringLiteral("Default WMV template"), 48000, 2, 3000, 16, 1280, 720, 29.97)}),
        F(QStringLiteral("XDCAM EX"), false, QStringLiteral(".mp4"),
          QStringLiteral("XDCAM EX File Format Plug-In"),
          {T(QStringLiteral("SP 1440x1080-50i, 25 Mbps CBR"), false, QStringLiteral(".mp4"),
            QStringLiteral("XDCAM EX SP · 1440×1080 50i · 25 Mbps CBR"), 48000, 2, 25000, 16, 1440,
            1080, 25.0),
           T(QStringLiteral("SP 1440x1080-60i, 25 Mbps CBR"), false, QStringLiteral(".mp4"),
             QStringLiteral("XDCAM EX SP · 1440×1080 60i · 25 Mbps CBR"), 48000, 2, 25000, 16, 1440,
             1080, 29.97),
           T(QStringLiteral("HQ 1280x720-24p, 35 Mbps VBR"), false, QStringLiteral(".mp4"),
             QStringLiteral("XDCAM EX HQ · 1280×720 24p · 35 Mbps VBR"), 48000, 2, 35000, 16, 1280,
             720, 23.976),
           T(QStringLiteral("HQ 1280x720-25p, 35 Mbps VBR"), false, QStringLiteral(".mp4"),
             QStringLiteral("XDCAM EX HQ · 1280×720 25p · 35 Mbps VBR"), 48000, 2, 35000, 16, 1280,
             720, 25.0),
           T(QStringLiteral("HQ 1280x720-30p, 35 Mbps VBR"), false, QStringLiteral(".mp4"),
             QStringLiteral("XDCAM EX HQ · 1280×720 30p · 35 Mbps VBR"), 48000, 2, 35000, 16, 1280,
             720, 29.97),
           T(QStringLiteral("HQ 1280x720-50p, 35 Mbps VBR"), false, QStringLiteral(".mp4"),
             QStringLiteral("XDCAM EX HQ · 1280×720 50p · 35 Mbps VBR"), 48000, 2, 35000, 16, 1280,
             720, 50.0),
           T(QStringLiteral("HQ 1280x720-60p, 35 Mbps VBR"), false, QStringLiteral(".mp4"),
             QStringLiteral("XDCAM EX HQ · 1280×720 60p · 35 Mbps VBR"), 48000, 2, 35000, 16, 1280,
             720, 59.94),
           T(QStringLiteral("HQ 1920x1080-24p, 35 Mbps VBR"), false, QStringLiteral(".mp4"),
             QStringLiteral("XDCAM EX HQ · 1920×1080 24p · 35 Mbps VBR"), 48000, 2, 35000, 16, 1920,
             1080, 23.976),
           T(QStringLiteral("HQ 1920x1080-25p, 35 Mbps VBR"), false, QStringLiteral(".mp4"),
             QStringLiteral("XDCAM EX HQ · 1920×1080 25p · 35 Mbps VBR"), 48000, 2, 35000, 16, 1920,
             1080, 25.0),
           T(QStringLiteral("HQ 1920x1080-30p, 35 Mbps VBR"), false, QStringLiteral(".mp4"),
             QStringLiteral("XDCAM EX HQ · 1920×1080 30p · 35 Mbps VBR"), 48000, 2, 35000, 16, 1920,
             1080, 29.97),
           T(QStringLiteral("Default Template"), false, QStringLiteral(".mp4"),
             QStringLiteral("Default XDCAM EX template"), 48000, 2, 35000, 16, 1920, 1080, 29.97)}),
    };
    return kFormats;
}

QStringList RenderTemplateCatalog::formatNames()
{
    QStringList names;
    for (const RenderFormat &f : formats()) {
        names << f.name;
    }
    return names;
}

const RenderFormat *RenderTemplateCatalog::findFormat(const QString &name)
{
    static const QVector<RenderFormat> cache = formats();
    for (const RenderFormat &f : cache) {
        if (f.name.compare(name, Qt::CaseInsensitive) == 0) {
            return &f;
        }
    }
    return nullptr;
}

QString RenderTemplateCatalog::defaultExtensionFor(const QString &formatName)
{
    if (const RenderFormat *f = findFormat(formatName)) {
        return f->extension;
    }
    return QStringLiteral(".mp4");
}

QString RenderTemplateCatalog::favoriteKey(const QString &formatName, const QString &templateName)
{
    return formatName + QLatin1Char('\x1f') + templateName;
}

bool RenderTemplateCatalog::isFavorite(const QString &formatName, const QString &templateName)
{
    QSettings s;
    const QStringList favs = s.value(QStringLiteral("render/favorites")).toStringList();
    return favs.contains(favoriteKey(formatName, templateName));
}

void RenderTemplateCatalog::setFavorite(const QString &formatName, const QString &templateName,
                                        bool on)
{
    QSettings s;
    QStringList favs = s.value(QStringLiteral("render/favorites")).toStringList();
    const QString key = favoriteKey(formatName, templateName);
    if (on) {
        if (!favs.contains(key)) {
            favs.push_back(key);
        }
    } else {
        favs.removeAll(key);
    }
    s.setValue(QStringLiteral("render/favorites"), favs);
}

qint64 RenderTemplateCatalog::estimateBytes(const RenderTemplate &tpl, double durationSec)
{
    if (durationSec <= 0.0) {
        return 0;
    }
    if (tpl.bitrateKbps > 0) {
        return qint64(double(tpl.bitrateKbps) * 1000.0 / 8.0 * durationSec);
    }
    if (tpl.audioOnly && tpl.sampleRate > 0 && tpl.bitDepth > 0 && tpl.channels > 0) {
        return qint64(double(tpl.sampleRate) * tpl.bitDepth * tpl.channels / 8.0 * durationSec);
    }
    return 0;
}

} // namespace openvegas
