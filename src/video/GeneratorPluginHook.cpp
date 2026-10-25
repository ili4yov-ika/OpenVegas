#include "video/GeneratorPluginHook.h"

#include <QAtomicPointer>

namespace openvegas {

namespace {

QAtomicPointer<void> g_provider;

GeneratorPluginFn provider()
{
    return reinterpret_cast<GeneratorPluginFn>(g_provider.loadRelaxed());
}

} // namespace

void setGeneratorPluginProvider(GeneratorPluginFn fn)
{
    g_provider.storeRelaxed(reinterpret_cast<void *>(fn));
}

bool hasGeneratorPluginProvider()
{
    return provider() != nullptr;
}

bool generatorPluginRender(const QString &effectKey, const QVariantMap &params, double timeSec,
                           const QSize &size, QImage *out)
{
    GeneratorPluginFn fn = provider();
    if (!fn || !out || effectKey.isEmpty() || size.isEmpty()) {
        return false;
    }
    return fn(effectKey, params, timeSec, size, out);
}

} // namespace openvegas
