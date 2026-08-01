#pragma once

#include "model/ProjectModel.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
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
    static void parseTimelineEvents(const QByteArray &data, VegOpenResult *result);
    static void parseMarkers(const QByteArray &data, VegOpenResult *result);
    static void parseTrackMotion(const QByteArray &data, VegOpenResult *result);
    static void parsePanCrop(const QByteArray &data, VegOpenResult *result);
    static void assignEventNames(VegOpenResult *result);
};

} // namespace openvegas
