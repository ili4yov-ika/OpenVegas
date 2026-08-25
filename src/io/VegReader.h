#pragma once

#include "io/VegOfxParams.h"
#include "model/ProjectModel.h"

#include <QByteArray>
#include <QColor>
#include <QHash>
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
/**
 * One transition instance recovered from the binary (see VegReader::parseTransitions
 * for the byte layout and how it was derived). Kept as its own plain struct so src/io
 * stays independent of src/video's TransitionInstance, which ProjectModel converts to.
 */
/**
 * Which legacy VEGAS transition a record belongs to.
 *
 * Each is identified by the 16-byte plug-in GUID the record carries. The GUID → plug-in
 * mapping was established by locating those GUIDs inside VEGAS's own binaries and reading
 * the class names beside them, not by guessing from preset names:
 *
 *   vfx1.dll        3D Blinds, 3D Cascade, 3D Shuffle, Flash   (CDXBlinds/CDXCascade/…)
 *   vidpcore.dll    Gradient Wipe                              (VEGAS Gradient Wipe)
 *   sftrans1.dll    Venetian Blinds                            (VEGAS Venetian Blinds)
 *   SfPagePeel.dll  Portals                                    (DXTSfPortal)
 *
 * See MARKDOWN/VEG_TRANSITIONS_REVERSE.md and decompile-src/ for the raw evidence.
 */
enum class VegTransitionKind {
    Unknown = 0,
    Blinds3D,
    Cascade3D,
    /** "VEGAS 3D Fly In/Out" — presets Default / Spin Away / Tumble In. */
    FlyInOut3D,
    /** "VEGAS Shuffle", shown as 3D Shuffle — presets Bright Light / Low Light. */
    Shuffle3D,
    GradientWipe,
    VenetianBlinds,
    Portals,
    /**
     * A transition stored the *other* way VEGAS has: keyed by an identifier string such
     * as "{Svfx:com.vegascreativesoftware:iris}" rather than by a 16-byte GUID. These
     * are the majority — Iris, Linear Wipe, Push, Dissolve and the rest — and they carry
     * their parameters as named values, so `ofxPluginId` and `ofxParams` hold what the
     * fixed fields above cannot.
     */
    Ofx
};

struct VegTransitionInfo {
    /** Which plug-in this record belongs to. */
    VegTransitionKind kind = VegTransitionKind::Blinds3D;
    /**
     * Plug-in display name as VEGAS shows it ("3D Blinds", "Venetian Blinds", …).
     * For `kind == Ofx` this is the identifier's tail ("iris") — the catalog turns that
     * into the label the timeline draws.
     */
    QString pluginName;
    /** Vegas preset name as stored ("Simple", "Slot Machine", …). */
    QString presetName;
    int divisions = 8;
    int extraSpins = 0;
    double stagger = 0.0;
    double specularLight = 1.0;
    /** 0 = Left to Right, 1 = Right to Left, 2 = Top to Bottom, 3 = Bottom to Top. */
    int direction = 0;
    /** True when the transition sits on the event's fade-OUT rather than its fade-in.
     *  A crossfade is stored like a fade-in (on the incoming clip). */
    bool fadeOut = false;
    /** Start time of the event this transition was nested in; < 0 when unresolved. */
    double eventStartSec = -1.0;
    /** Byte offset of the plug-in GUID this record was matched at. */
    int offset = -1;
    /** Offset of the owning event's timing record; internal to the owner search. */
    int eventOffsetForCompare = -1;

    // --- Venetian Blinds ---------------------------------------------------------
    /** Number of blinds. The preset names state it outright — "Seven Horizontal
     *  Blinds" stores 7.0 — which is what confirmed this field. */
    double blindCount = 5.0;
    /** Blind angle in degrees; 0 = vertical, 90 = horizontal (again matching names). */
    double blindAngleDeg = 0.0;
    /** Small edge softness, 0…1 (0.01 for "Fifteen Tilted", 0.07 for "Sloped"). */
    double blindFeather = 0.0;

    // --- 3D Shuffle --------------------------------------------------------------
    // Its dialog has exactly one control, "Specular light", and the stored double is
    // 1.0 for the "Bright Light" preset and 0.2 for "Low Light" — which is what the
    // preset names describe. Reuses `specularLight` above rather than a second field.

    /**
     * True when the record was recognised but its parameters were deliberately not
     * decoded — Gradient Wipe, 3D Fly In/Out and Portals carry layouts that the sample
     * project does not pin down, so their numbers are left at defaults rather than
     * invented. The preset name is still recovered.
     */
    bool paramsUndecoded = false;

    // --- OFX-stored transitions --------------------------------------------------
    /** "{Svfx:com.vegascreativesoftware:iris}" when kind == Ofx; empty otherwise. */
    QString ofxPluginId;
    /**
     * Named parameters as the file stores them ("Angle" → 90.0, "Shape" → 4).
     * A point or a colour arrives as a QVariantList of doubles.
     */
    QVariantMap ofxParams;
};

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

/**
 * One serialized state of a *legacy* (pre-OFX) VEGAS video plug-in.
 *
 * Effects like Glint ("Мерцание") and Soft Contrast are not OFX plug-ins in VEGAS Pro 22
 * — no installed `.ofx` binary registers them — so their entire state lives in the
 * project as plain XML (`<Glint>`, `<Softlight>`) rather than as an OFX parameter blob.
 * Recovering only the effect's *name* from that XML and then showing invented defaults
 * is what made OpenVegas look like it had substituted its own stand-in plug-in.
 *
 * Values are already scaled to the units VEGAS's own dialog shows (percentages, degrees),
 * so they can go straight into an FxSlot's parameter map.
 */
struct VegLegacyFxKeyframe {
    double timeSec = 0.0;
    QVariantMap params;
};

struct VegLegacyFxState {
    /** Preset the effect was set to ("Sparkle", "Soft Moderate Contrast"), when recovered. */
    QString presetName;
    /** The plug-in's current values — the blob VEGAS writes ahead of the keyframe list. */
    QVariantMap baseParams;
    /** Animation, in timeline order. Empty when the effect isn't animated. */
    QVector<VegLegacyFxKeyframe> keyframes;
};

/** Ruler marker recovered from binary marker GUID blocks. */
struct VegMarkerInfo {
    double timeSec = 0.0;
    QString label;
    int number = 0;
};

/**
 * One strip of the Vegas mixing console, as the project stores it.
 *
 * Identified by counting: the chunk appears exactly twice in a project with the default
 * mixer and eight times in one with extra busses, where the names inside read Preview,
 * Master, Bus A, Bus B and Input A…D — which is what the console shows for that project.
 */
struct VegMixerBus {
    /** 2 Master, 3 a bus, 4 an assignable FX bus, 7 Preview, 11 an input bus. */
    quint32 kind = 0;
    QString name;
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
    /** Mixing console busses, in the order the project stores them. */
    QVector<VegMixerBus> mixerBuses;
    /**
     * Decoded OFX parameters, keyed by the identifier exactly as `eventFxNames` holds it.
     *
     * Without these an effect out of a project reached its plug-in with an empty parameter
     * map, so the plug-in used its own defaults — zero radius for Chroma Blur — and
     * rendered as very nearly nothing. The chain looked right and did nothing.
     */
    QHash<QString, VegOfxEffect> ofxParams;
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
    /**
     * Real parameter values + animation of legacy (non-OFX) VEGAS video plug-ins,
     * keyed by short display name, lower-case ("glint", "soft contrast").
     */
    QMap<QString, VegLegacyFxState> legacyFxStates;
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
    QVector<VegTransitionInfo> transitions;
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
    /** Recover transition instances (plug-in GUID + preset + parameters + placement). */
    static void parseTransitions(const QByteArray &data, VegOpenResult *result);
    /** Transitions keyed by a "{Svfx:…}" identifier rather than a GUID. */
    static void parseOfxTransitions(const QByteArray &data, VegOpenResult *result);
    /** Mixing console busses from the `{220D2BE4-…}` list. */
    static void parseMixerBuses(const QByteArray &data, VegOpenResult *result);
    static void parseFxStateChunks(const QByteArray &data, VegOpenResult *result);
    /** Recover `<Glint>` / `<Softlight>` XML state — values, keyframe times, preset name. */
    static void parseLegacyVideoFxStates(const QByteArray &data, VegOpenResult *result);
    static void assignEventNames(VegOpenResult *result);
};

} // namespace openvegas
