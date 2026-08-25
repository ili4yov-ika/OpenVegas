#include "video/TransitionPluginHook.h"

#include <QAtomicPointer>

namespace openvegas {

namespace {

QAtomicPointer<void> g_provider;

TransitionPluginFn provider()
{
    return reinterpret_cast<TransitionPluginFn>(g_provider.loadRelaxed());
}

} // namespace

void setTransitionPluginProvider(TransitionPluginFn fn)
{
    g_provider.storeRelaxed(reinterpret_cast<void *>(fn));
}

bool hasTransitionPluginProvider()
{
    return provider() != nullptr;
}

bool transitionPluginRender(const QString &groupKey, const QImage &from, const QImage &to,
                            double progress, const QVariantMap &params, QImage *out)
{
    TransitionPluginFn fn = provider();
    if (!fn || !out || groupKey.isEmpty()) {
        return false;
    }
    return fn(groupKey, from, to, progress, params, out);
}

} // namespace openvegas
