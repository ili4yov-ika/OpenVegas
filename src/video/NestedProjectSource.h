#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QSize>
#include <QString>

#include <memory>

namespace openvegas {

class ProjectModel;

/**
 * Frames for a VEGAS project used as a clip.
 *
 * A `.veg` can be dropped on a timeline as media — a nested project. The "media" is then
 * not a media file at all, so ffmpeg cannot open it and such a clip showed no filmstrip
 * and a black preview. Its audio comes from the `.veg.sfap0` mixdown VEGAS writes beside
 * it (see AudioDecodeCache), but there is no equivalent sidecar for picture: the frames
 * have to be composed.
 *
 * That is what this does — it loads the nested project into its own ProjectModel once and
 * asks VideoCompositor for a frame, exactly as the program monitor does for the open
 * project. No new rendering path, just the existing one pointed at a second model.
 *
 * A project may reference another project, so loads are depth-limited and a file already
 * being loaded is refused rather than followed: a project that references itself would
 * otherwise recurse until the stack ran out.
 */
class NestedProjectSource {
public:
    static NestedProjectSource &instance();

    /** True when this path is a VEGAS project rather than a media file. */
    static bool isProjectMedia(const QString &path);

    /**
     * Composited frame of the nested project at `timeSec`.
     *
     * Null while the nested project's own media is still decoding — the caller should
     * ask again, the same contract VideoCompositor already has. Null too when the file
     * is not a project, cannot be loaded, or the nesting is circular.
     *
     * With `exact` the nested timeline is composed from the source frames at this very
     * time and nothing else; without it, frames already decoded near it will do.
     */
    QImage frameAt(const QString &vegPath, double timeSec, const QSize &size,
                   bool exact = false);

    /** Timeline length of the nested project in seconds; 0 when unknown. */
    double durationOf(const QString &vegPath);

    /** Drop cached models (one path, or all). */
    void invalidate(const QString &path = {});

    /**
     * Register with NestedFrameHook so the filmstrip and preview caches can reach this
     * without depending on project loading. Call once at start-up.
     */
    static void installAsFrameProvider();

private:
    NestedProjectSource() = default;

    /** Loaded model for a path, loading it if needed; null when it cannot be used. */
    std::shared_ptr<const ProjectModel> modelFor(const QString &vegPath);

    QMutex m_mutex;
    QHash<QString, std::shared_ptr<const ProjectModel>> m_models;
    /** Paths that failed once — retrying them on every repaint would be pointless. */
    QHash<QString, bool> m_failed;
};

} // namespace openvegas
