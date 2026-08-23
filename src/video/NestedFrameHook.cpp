#include "video/NestedFrameHook.h"

namespace openvegas {

namespace {
// Set once during start-up, read from decode threads afterwards.
NestedFrameFn g_provider = nullptr;
} // namespace

void setNestedFrameProvider(NestedFrameFn fn)
{
    g_provider = fn;
}

bool hasNestedFrameProvider()
{
    return g_provider != nullptr;
}

bool looksLikeProjectMedia(const QString &path)
{
    return path.endsWith(QLatin1String(".veg"), Qt::CaseInsensitive);
}

QImage nestedFrame(const QString &path, double timeSec, const QSize &size, bool exact)
{
    if (!g_provider || !looksLikeProjectMedia(path)) {
        return {};
    }
    return g_provider(path, timeSec, size, exact);
}

} // namespace openvegas
