#pragma once

#include <QString>

namespace openvegas {

class ProjectModel;

/**
 * Single-file OpenVegas projects.
 *
 * The archive format is a folder (project.json + media_list.txt, optionally a Media copy),
 * which is right for handing a project to someone else and wrong for everyday saving: a
 * folder cannot be double-clicked, mailed, or kept in one place next to the footage. These
 * two put the same project in one file, through the same serializer
 * (ProjectInterchange::projectToJson), so no format can drift from the others.
 *
 *   .ovp   the project JSON on its own. Media is referenced where it lives, so the file
 *          is small and stays valid exactly as long as that media does.
 *   .ozp   a ZIP holding the same project.json and media_list.txt, and — when asked —
 *          the media itself under Media/, which makes it the portable one.
 *
 * The ZIP is written with the stored method rather than deflated. A project is a few tens
 * of kilobytes of JSON, so compression would buy little, and storing keeps the writer
 * small enough to read in one sitting and free of a compression dependency. Every ZIP
 * reader handles stored entries.
 */
namespace ProjectFile {

/** Extension without the dot, for file dialogs and for deciding which loader to use. */
inline QString singleFileSuffix()
{
    return QStringLiteral("ovp");
}
inline QString zipSuffix()
{
    return QStringLiteral("ozp");
}

/** Write the project as one `.ovp`. Media is referenced, never copied. */
bool saveOvp(const ProjectModel &model, const QString &path, QString *error = nullptr);
/** Read a `.ovp` back into `model`, as a fresh open. */
bool loadOvp(const QString &path, ProjectModel *model, QString *error = nullptr);

/**
 * Write the project as one `.ozp`. With `includeMedia` the referenced files are stored
 * inside it too, which is what makes the result portable; without, it is a zipped `.ovp`.
 */
bool saveOzp(const ProjectModel &model, const QString &path, bool includeMedia,
             QString *error = nullptr);
/**
 * Read a `.ozp`. Media stored inside it is unpacked next to the file, into
 * `<name>_media/`, because the model refers to media by path and a path has to exist.
 */
bool loadOzp(const QString &path, ProjectModel *model, QString *error = nullptr);

/** True when the file starts with a ZIP local-file header. */
bool looksLikeZip(const QString &path);

} // namespace ProjectFile

} // namespace openvegas
