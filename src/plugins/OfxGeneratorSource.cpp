#include "plugins/OfxGeneratorSource.h"

#include "plugins/OfxHost.h"
#include "plugins/OfxTransitionSource.h"
#include "video/GeneratorPluginHook.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>

namespace openvegas {

namespace {

QStringList g_preferredRoots;
QStringList g_binaries;
bool g_binariesScanned = false;

QMutex g_mutex;
/** effect key -> instance id; 0 means "asked and there is none", so it is asked once. */
QHash<QString, int> g_instances;

int instanceFor(const QString &effectKey)
{
    QMutexLocker lock(&g_mutex);
    const auto it = g_instances.constFind(effectKey);
    if (it != g_instances.constEnd()) {
        return it.value();
    }
    if (!g_binariesScanned) {
        g_binariesScanned = true;
        g_binaries = vegasOfxBundleBinaries(g_preferredRoots);
    }
    int id = 0;
    const QString effectId = QStringLiteral("com.vegascreativesoftware:") + effectKey;
    for (const QString &binary : g_binaries) {
        const QHash<QString, int> index = OfxHost::effectIndexMap(binary);
        if (!index.contains(effectId)) {
            continue; // the wrong VEGAS for it — keep looking
        }
        OfxPluginDesc desc;
        desc.path = binary;
        desc.effectId = effectId;
        desc.pluginIndex = index.value(effectId);
        desc.hasBinary = true;
        id = OfxHost::instance().createInstance(desc, nullptr);
        if (id > 0) {
            break;
        }
    }
    g_instances.insert(effectKey, id);
    return id;
}

bool renderThroughPlugin(const QString &effectKey, const QVariantMap &params, double timeSec,
                         const QSize &size, QImage *out)
{
    // Solid Color's 8-bit render writes blue first whatever pixel order the clip declares
    // (Test Pattern, in the same bundle, writes red first whatever it declares, so no one
    // buffer layout suits both); VEGAS runs these generators in 32-bit float. A solid fill is
    // exactly what our own renderer draws, so it is left to that.
    if (effectKey == QLatin1String("solidcolor")) {
        return false;
    }
    const int id = instanceFor(effectKey);
    if (id <= 0) {
        return false;
    }
    QImage frame(size, QImage::Format_ARGB32);
    frame.fill(Qt::transparent);
    // One render at a time per instance: the compositor may ask from several threads.
    static QMutex renderMutex;
    QMutexLocker lock(&renderMutex);
    if (!OfxHost::instance().processFrame(id, &frame, timeSec, params, nullptr)) {
        return false;
    }
    *out = frame.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    return true;
}

} // namespace

void OfxGeneratorSource::install(const QStringList &preferredRoots)
{
    g_preferredRoots = preferredRoots;
    setGeneratorPluginProvider(&renderThroughPlugin);
}

void OfxGeneratorSource::uninstall()
{
    setGeneratorPluginProvider(nullptr);
    QMutexLocker lock(&g_mutex);
    for (auto it = g_instances.constBegin(); it != g_instances.constEnd(); ++it) {
        if (it.value() > 0) {
            OfxHost::instance().destroyInstance(it.value());
        }
    }
    g_instances.clear();
    g_preferredRoots.clear();
    g_binaries.clear();
    g_binariesScanned = false;
}

} // namespace openvegas
