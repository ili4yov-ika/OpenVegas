#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

class QMimeData;

namespace openvegas {

/** Shared helpers for media DnD (Project Media, Explorer, OS file manager). */
class MediaMime {
public:
    static QString mimeType();

    static bool isMediaFile(const QString &path);
    static QString guessKind(const QString &pathOrName);

    /**
     * Expand folders (one level) and keep only media files.
     * Non-media files are skipped.
     */
    static QStringList expandToMediaFiles(const QStringList &paths);

    /** Build MIME for timeline / bin (openvegas + text + urls). */
    static QMimeData *fromLocalPaths(const QStringList &paths);

    /**
     * Build MIME for a path-less synthetic drag source (e.g. a Media Generator preset).
     * extra carries kind-specific payload — e.g. a Titles & Text animation key.
     */
    static QMimeData *fromSynthetic(const QString &kind, const QString &name,
                                    const QString &extra = QString());

    /**
     * Parse drag payload into parallel lists (filtered to media when possible).
     * extras carries the synthetic per-entry payload (e.g. animation key) when present.
     */
    static void parse(const QMimeData *md, QStringList *names, QStringList *kinds, QStringList *paths,
                      QVector<double> *lengths, QStringList *extras = nullptr);

    static bool hasMediaPayload(const QMimeData *md);
};

} // namespace openvegas
