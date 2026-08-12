#include "plugins/OfxTrace.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QProcessEnvironment>

namespace openvegas {
namespace ofx {
namespace {

QMutex g_mutex;

struct TraceConfig {
    bool enabled = false;
    QString path;
};

TraceConfig resolveConfig()
{
    TraceConfig cfg;
    const QString raw =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("OPENVEGAS_OFX_TRACE"));
    if (raw.isEmpty()) {
        return cfg;
    }
    const QString v = raw.trimmed();
    const QString lower = v.toLower();
    if (lower == QStringLiteral("0") || lower == QStringLiteral("off")
        || lower == QStringLiteral("no") || lower == QStringLiteral("false")) {
        return cfg;
    }
    cfg.enabled = true;
    const bool looksLikeFlag = lower == QStringLiteral("1") || lower == QStringLiteral("on")
                               || lower == QStringLiteral("yes") || lower == QStringLiteral("true");
    cfg.path = looksLikeFlag
                   ? QDir::temp().filePath(QStringLiteral("openvegas-ofx-trace.log"))
                   : v;
    return cfg;
}

const TraceConfig &config()
{
    static const TraceConfig cfg = resolveConfig();
    return cfg;
}

} // namespace

bool Trace::enabled()
{
    return config().enabled;
}

QString Trace::filePath()
{
    return config().enabled ? config().path : QString();
}

void Trace::write(const QString &line)
{
    const TraceConfig &cfg = config();
    if (!cfg.enabled) {
        return;
    }
    QMutexLocker lock(&g_mutex);
    QFile f(cfg.path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz"));
    f.write(QStringLiteral("%1 %2\n").arg(stamp, line).toUtf8());
}

void Trace::restart(const QString &header)
{
    const TraceConfig &cfg = config();
    if (!cfg.enabled) {
        return;
    }
    {
        QMutexLocker lock(&g_mutex);
        QFile f(cfg.path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            f.write(QByteArrayLiteral("=== OpenVegas OFX trace ===\n"));
        }
    }
    if (!header.isEmpty()) {
        write(header);
    }
}

} // namespace ofx
} // namespace openvegas
