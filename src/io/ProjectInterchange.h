#pragma once

#include "model/ProjectModel.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace openvegas {

struct InterchangeMediaRef {
    QString path;
    QString displayName;
    QString kind; // video | audio | still
};

struct InterchangeEvent {
    QString name;
    QString kind; // video | audio | still | caption
    double startSec = 0.0;
    double lengthSec = 8.0;
    QString sourcePath;
    int trackIndex = 0; // Vegas EDL Track field (relative within kind)
    double fadeInSec = 0.0;
    double fadeOutSec = 0.0;
    FadeCurveType fadeInCurve = FadeCurveType::Smooth;
    FadeCurveType fadeOutCurve = FadeCurveType::Smooth;
    /** Vegas EDL FirstChannel (0-based) / Channels; 0 channels = unspecified. */
    int firstChannel = 0;
    int channelCount = 0;
    /**
     * Vegas EDL SustainGain (0…1 linear shelf) when hasSustainGain.
     * Video → opacity; audio → amplitude → gainDb = 20·log10(g).
     */
    bool hasSustainGain = false;
    double sustainGain = 1.0;
    /** Vegas EDL StreamStart / StreamLength (ms→sec); PlayRate when present. */
    double mediaStartSec = 0.0;
    double mediaLengthSec = 0.0;
    double playRate = 1.0;
    /** Vegas EDL Looped column (default true when missing). */
    bool looped = true;
};

struct InterchangeResult {
    QVector<InterchangeMediaRef> media;
    QVector<InterchangeEvent> events;
    QStringList warnings;
    QString title;
};

/** Import/export helpers for File → Import / Export (Vegas-style interchange). */
class ProjectInterchange {
public:
    /** Pull media pool paths from another .veg without replacing the current project. */
    static InterchangeResult importMediaFromProject(const QString &vegPath, QString *error = nullptr);

    /** CMX3600-ish EDL text; auto-routes Vegas semicolon CSV (EDL Text File) to importVegasCsvEdl. */
    static InterchangeResult importEdl(const QString &path, double frameRate, QString *error = nullptr);
    static bool exportEdl(const ProjectModel &model, const QString &path, QString *error = nullptr);

    /** Final Cut Pro 7 / Resolve XML and FCPXML — media paths + simple timeline when present. */
    static InterchangeResult importFinalCutXml(const QString &path, QString *error = nullptr);
    static bool exportFinalCutXml(const ProjectModel &model, const QString &path, QString *error = nullptr);
    static bool exportFcpxml(const ProjectModel &model, const QString &path, QString *error = nullptr);

    /** Best-effort path scrape from Premiere .prproj (zip/XML or binary strings). */
    static InterchangeResult importPremiereProject(const QString &path, QString *error = nullptr);

    /** Broadcast Wave / WAV media files. */
    static InterchangeResult importBroadcastWave(const QStringList &paths);

    /**
     * VEGAS Pro “EDL Text File” CSV (semicolon, times in milliseconds).
     * Used as sidecar next to .veg: edl-text-file/<name>.txt
     */
    static InterchangeResult importVegasCsvEdl(const QString &path, QString *error = nullptr);
    static bool exportVegasCsvEdl(const ProjectModel &model, const QString &path, QString *error = nullptr);

    /** Map Vegas CurveIn/CurveOut integer codes → FadeCurveType. */
    static FadeCurveType fadeCurveFromVegasCode(int code);
    /** Inverse of fadeCurveFromVegasCode (Vegas EDL CurveIn/Out). */
    static int fadeCurveToVegasCode(FadeCurveType type);

    /** Closed captions (.srt / .vtt / .scc) → timeline markers. */
    static InterchangeResult importClosedCaptions(const QString &path, QString *error = nullptr);

    /**
     * OpenVegas project archive folder:
     *   <name>/project.json + media_list.txt (+ optional Media/ copies when copyMedia).
     * v2 round-trips fxChain (Titles & Text, Media Generator, OFX/VST state — opaque,
     * preserved byte-for-byte), pan/crop position keyframes, and markers; see
     * MARKDOWN/PROJECT_ARCHIVE_FORMAT.md for exact coverage and known gaps.
     */
    static bool exportProjectArchive(const ProjectModel &model, const QString &dirPath, bool copyMedia,
                                     QString *error = nullptr);
    /** Inverse of exportProjectArchive() — populates *model in place (like a fresh open). */
    static bool importProjectArchive(const QString &dirPath, ProjectModel *model,
                                     QString *error = nullptr);
    /**
     * The project as JSON — the one serializer every OpenVegas format goes through, so a
     * single file and an archive folder can never describe the project differently.
     * `dirPath`/`copyMedia` matter only to the folder form.
     */
    static QJsonObject projectToJson(const ProjectModel &model,
                                     QStringList *mediaLinesOut = nullptr,
                                     const QString &dirPath = QString(),
                                     bool copyMedia = false);

    /** Inverse of projectToJson(); `baseDir` resolves archived media, empty when there is none. */
    static bool projectFromJson(const QJsonObject &root, const QString &baseDir,
                                ProjectModel *model, QString *error = nullptr);

    /** True if dirPath/project.json exists and declares format "OpenVegasArchive". */
    static bool isProjectArchive(const QString &dirPath);

    static QString guessKind(const QString &pathOrName);
    static QString formatTimecode(double sec, double fps, bool dropFrame = false);
    static bool parseTimecode(const QString &tc, double fps, double *outSec);
};

} // namespace openvegas
