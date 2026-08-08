#pragma once

#include "model/ProjectModel.h"

#include <QByteArray>
#include <QColor>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

namespace openvegas {

struct VegHeaderInfo {
    quint64 fileSize = 0;
    quint64 earlyBlobSize = 0;
    quint16 vegasVersion = 0;
    quint32 sampleRate = 0;
    double frameRate = 0.0;
    double tempoBpm = 0.0;
    int width = 0;
    int height = 0;
};

/** Timeline event recovered from binary rate/length blocks (Vegas ticks / 1e7). */
struct VegEventInfo {
    enum class Kind { Unknown, Video, Audio };

    Kind kind = Kind::Unknown;
    double startSec = 0.0;
    double lengthSec = 0.0;
    double playbackRate = 1.0;
    QString name;
    /** Byte offset of the kindCode field this record was matched at; -1 if not tracked. */
    int offset = -1;
};

/**
 * One `{Svfx:com.vegascreativesoftware:titlesandtext}` generator instance recovered from
 * the binary, with a best-effort timeline position (see VegReader::parseVideoTitlesText).
 * Field shapes mirror TitlesTextParams (src/video/TitlesTextApply.h) so ProjectModel can
 * convert 1:1; kept as its own struct so src/io stays independent of src/video.
 */
struct VegTitleTextInfo {
    QString text;
    QString fontFamily;
    double fontSize = 48.0;
    bool bold = false;
    bool italic = false;
    /** 0=Left, 1=Center, 2=Right — best-effort guess, see .cpp. */
    int alignment = 0;
    QColor textColor = QColor(255, 255, 255, 255);
    QString animationName = QStringLiteral("_None");
    double scale = 1.0;
    double locationX = 0.5;
    double locationY = 0.5;
    bool cropBackgroundToText = false;
    QColor backgroundColor = QColor(0, 0, 0, 0);
    double tracking = 0.0;
    double lineSpacing = 1.0;
    double outlineWidth = 0.0;
    QColor outlineColor = QColor(255, 255, 255, 255);
    bool shadowEnable = false;
    QColor shadowColor = QColor(0, 0, 0, 255);
    double shadowOffsetX = 0.2;
    double shadowOffsetY = 0.2;
    double shadowBlur = 0.4;

    double startSec = 0.0;
    double lengthSec = 10.0;
};

/** Ruler marker recovered from binary marker GUID blocks. */
struct VegMarkerInfo {
    double timeSec = 0.0;
    QString label;
    int number = 0;
};

struct VegOpenResult {
    VegHeaderInfo header;
    QStringList mediaPaths;
    QStringList eventLabels;
    QVector<VegEventInfo> events;
    QVector<VegMarkerInfo> markers;
    QStringList trackFxNames;
    /** Video/OFX-style event FX (`{Svfx:…}`, `OFX:…`). */
    QStringList eventFxNames;
    /**
     * Video Track FX chain (`{Svfx:…}`), e.g. Sepia + VelvetMatter Soft Contrast
     * (the latter has no `{Svfx:…}` string of its own — only `<Softlight>` XML —
     * so it's synthesized once the pairing is recognized). First video track.
     */
    QStringList videoTrackFxNames;
    /**
     * Audio Event FX chain recovered from UTF-16 (ordered):
     * e.g. "Fresh Air\\t(VST2, 64 Bit)", "OldPlug\\t(VST, 64 Bit)", "Auto-Key\\t(VST3, 64 Bit)".
     */
    QStringList audioEventFxNames;
    /** Mixing Console bus names recovered from UTF-16 (e.g. "Bus A"). */
    QStringList mixerBusNames;
    /** Mixing Console input bus names (e.g. "Input A"). */
    QStringList mixerInputBusNames;
    /**
     * Assignable FX buses from Mixing Console: label "FX 1" + plug-in display name "Chorus".
     * Parallel arrays; size matches.
     */
    QStringList mixerAssignableFxLabels;
    QStringList mixerAssignableFxPlugins;
    /** First video track Track Motion recovered from binary (may be empty). */
    TrackMotionState trackMotion;
    bool hasTrackMotion = false;
    /** First video event Pan/Crop (POSL/POSK) + optional Mask (MSKL/MSKK/ANCP). */
    EventPanCropState eventPanCrop;
    bool hasEventPanCrop = false;
    /**
     * Best-effort VST/OFX state blobs recovered near FX names (e.g. VST2 CcnK/FPCh/FxCk).
     * Keyed by display name (case-insensitive lower).
     */
    QMap<QString, QByteArray> fxStateChunks;
    /** First video track Color Grading (`{Svfx:com.vegascreativesoftware:colorgrading}`).
     * Params use ColorGradingEditor keys: lift|gamma|gain|offset.{r,g,b,y}, curve.rgb.
     */
    bool hasColorGrading = false;
    QVariantMap colorGradingParams;
    /** VEGAS Titles & Text generator instances recovered from the binary, in timeline order. */
    QVector<VegTitleTextInfo> titlesAndText;
    /**
     * Media basenames (lower) marked reversed via META:\\SubClip\\…[…][1] or
     * labels containing “(reversed)”.
     */
    QStringList reversedMediaBasenames;
    /** META SubClip start (sec) per reversed basename (lower). */
    QMap<QString, double> reversedSubclipStartSec;
    /** META SubClip length (sec) per reversed basename (lower). */
    QMap<QString, double> reversedSubclipLengthSec;
    QString projectPathHint;
    QStringList warnings;
    QString sourcePath;
    /** True when binary start/length/rate blocks were found. */
    bool hasTimelineTimings = false;
};

class VegReader {
public:
    static bool looksLikeVeg(const QByteArray &data);
    static VegOpenResult open(const QString &path, QString *error = nullptr);

private:
    static QStringList extractUtf16Strings(const QByteArray &data, int minChars = 4);
    static bool isMediaPath(const QString &s);
    static bool isProjectSelfPath(const QString &s);
    static bool isServicePath(const QString &s);
    static void parseHeader(const QByteArray &data, VegOpenResult *result);
    static void parseUtf16Metadata(const QByteArray &data, VegOpenResult *result);
    /** Rebuild `eventFxNames` from positioned `{Svfx:…}` + OFX XML roots (Glint, …). */
    static void recoverVideoEventFxNames(const QByteArray &data, VegOpenResult *result);
    /** Rebuild `videoTrackFxNames` — Sepia + Soft Contrast tail excluded from event FX. */
    static void recoverVideoTrackFxNames(const QByteArray &data, VegOpenResult *result);
    static void parseTimelineEvents(const QByteArray &data, VegOpenResult *result);
    static void parseMarkers(const QByteArray &data, VegOpenResult *result);
    static void parseTrackMotion(const QByteArray &data, VegOpenResult *result);
    static void parsePanCrop(const QByteArray &data, VegOpenResult *result);
    static void parseColorGrading(const QByteArray &data, VegOpenResult *result);
    static void parseVideoTitlesText(const QByteArray &data, VegOpenResult *result);
    static void parseFxStateChunks(const QByteArray &data, VegOpenResult *result);
    static void assignEventNames(VegOpenResult *result);
};

} // namespace openvegas
