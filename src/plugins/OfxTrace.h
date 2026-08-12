#pragma once

#include <QString>

namespace openvegas {
namespace ofx {

/**
 * Host-callback tracing for the OFX host.
 *
 * Off unless the environment variable `OPENVEGAS_OFX_TRACE` is set. Value is either
 * a truthy flag (`1`, `on`, `yes`, `true`) — log goes to
 * `<temp>/openvegas-ofx-trace.log` — or an explicit file path.
 *
 * Third-party OFX binaries answer with bare status codes and no diagnostics, so the
 * only way to tell *which* host feature a plug-in is missing is to record every
 * callback it makes. That is a permanent, cross-platform facility rather than a
 * throwaway debug patch: the same trace is what a user has to attach to a bug report
 * about a plug-in that refuses to load.
 */
class Trace {
public:
    /** Cached env lookup — cheap enough to guard every callback. */
    static bool enabled();

    /** Append one line (a timestamp and newline are added). No-op when disabled. */
    static void write(const QString &line);

    /** Resolved log file path (empty when disabled). */
    static QString filePath();

    /** Truncate the log and write a header. Used by diagnostics and tests. */
    static void restart(const QString &header);
};

} // namespace ofx
} // namespace openvegas

/** Guarded trace: the QString is only built when tracing is on. */
#define OPENVEGAS_OFX_TRACE(expr)                                                                  \
    do {                                                                                           \
        if (::openvegas::ofx::Trace::enabled()) {                                                  \
            ::openvegas::ofx::Trace::write(expr);                                                  \
        }                                                                                          \
    } while (false)
