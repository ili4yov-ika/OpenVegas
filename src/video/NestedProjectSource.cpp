#include "video/NestedProjectSource.h"

#include "io/VegReader.h"
#include "model/ProjectModel.h"
#include "video/NestedFrameHook.h"
#include "video/VideoCompositor.h"

#include <QDebug>
#include <QFileInfo>
#include <QMutexLocker>
#include <QSet>

namespace openvegas {

namespace {

/**
 * Projects being loaded on this thread, innermost last.
 *
 * A nested project can itself nest another, and nothing stops a project from referencing
 * itself — directly or around a cycle. Both are guarded here rather than by trusting the
 * files: following such a chain would recurse until the stack ran out.
 */
thread_local QStringList g_loading;
constexpr int kMaxNesting = 4;

QString canonical(const QString &path)
{
    const QFileInfo fi(path);
    const QString c = fi.canonicalFilePath();
    return c.isEmpty() ? fi.absoluteFilePath() : c;
}

} // namespace

NestedProjectSource &NestedProjectSource::instance()
{
    static NestedProjectSource source;
    return source;
}

bool NestedProjectSource::isProjectMedia(const QString &path)
{
    return path.endsWith(QLatin1String(".veg"), Qt::CaseInsensitive);
}

std::shared_ptr<const ProjectModel> NestedProjectSource::modelFor(const QString &vegPath)
{
    if (!isProjectMedia(vegPath)) {
        return {};
    }
    const QString key = canonical(vegPath);
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_models.constFind(key);
        if (it != m_models.cend()) {
            return *it;
        }
        if (m_failed.value(key, false)) {
            return {};
        }
    }

    if (g_loading.contains(key)) {
        qWarning().noquote() << "[nested-project] circular reference, not following:" << key;
        QMutexLocker lock(&m_mutex);
        m_failed.insert(key, true);
        return {};
    }
    if (g_loading.size() >= kMaxNesting) {
        qWarning().noquote() << "[nested-project] nesting deeper than" << kMaxNesting
                             << "levels, stopping at:" << key;
        QMutexLocker lock(&m_mutex);
        m_failed.insert(key, true);
        return {};
    }

    g_loading.push_back(key);
    QString error;
    const VegOpenResult veg = VegReader::open(vegPath, &error);
    auto model = std::make_shared<ProjectModel>();
    const bool ok = error.isEmpty() && model->applyVegImport(veg, vegPath);
    g_loading.removeLast();

    if (!ok) {
        qWarning().noquote() << "[nested-project] could not load" << vegPath << error;
        QMutexLocker lock(&m_mutex);
        m_failed.insert(key, true);
        return {};
    }

    std::shared_ptr<const ProjectModel> shared = model;
    QMutexLocker lock(&m_mutex);
    m_models.insert(key, shared);
    return shared;
}

QImage NestedProjectSource::frameAt(const QString &vegPath, double timeSec, const QSize &size)
{
    if (size.width() < 2 || size.height() < 2) {
        return {};
    }
    const auto model = modelFor(vegPath);
    if (!model) {
        return {};
    }
    // Same call the program monitor makes. `softRealtime` is on so a frame still being
    // decoded resolves to the nearest cached one instead of blocking a paint.
    return VideoCompositor::compose(*model, std::max(0.0, timeSec), size, /*softRealtime=*/true);
}

double NestedProjectSource::durationOf(const QString &vegPath)
{
    const auto model = modelFor(vegPath);
    if (!model) {
        return 0.0;
    }
    double end = 0.0;
    for (const Track &track : model->tracks()) {
        for (const TrackEvent &ev : track.events) {
            end = std::max(end, ev.startSec + ev.lengthSec);
        }
    }
    return end;
}

void NestedProjectSource::installAsFrameProvider()
{
    setNestedFrameProvider([](const QString &path, double timeSec, const QSize &size) {
        return NestedProjectSource::instance().frameAt(path, timeSec, size);
    });
}

void NestedProjectSource::invalidate(const QString &path)
{
    QMutexLocker lock(&m_mutex);
    if (path.isEmpty()) {
        m_models.clear();
        m_failed.clear();
        return;
    }
    const QString key = canonical(path);
    m_models.remove(key);
    m_failed.remove(key);
}

} // namespace openvegas
