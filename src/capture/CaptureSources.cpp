#include "capture/CaptureSources.h"

#include "io/FfmpegLocator.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QRect>
#include <QRegularExpression>
#include <QScreen>

#include <cmath>
#include <iterator>

#ifdef Q_OS_WIN
#include <windows.h>
// After windows.h, which dwmapi.h needs.
#include <dwmapi.h>
#endif

namespace openvegas {

#ifdef Q_OS_WIN

namespace {

/** Facts the OS holds about one window, in the shape the rules want them. */
WindowFacts factsFor(HWND hwnd)
{
    WindowFacts f;

    wchar_t title[512] = {0};
    GetWindowTextW(hwnd, title, int(std::size(title)));
    f.title = QString::fromWCharArray(title).trimmed();

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != 0) {
        HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (proc) {
            wchar_t path[MAX_PATH] = {0};
            DWORD len = DWORD(std::size(path));
            if (QueryFullProcessImageNameW(proc, 0, path, &len)) {
                f.exeName = QFileInfo(QString::fromWCharArray(path, int(len))).fileName();
            }
            CloseHandle(proc);
        }
    }

    f.visible = IsWindowVisible(hwnd);
    f.minimized = IsIconic(hwnd);

    // A UWP app that is "running" but not on screen is cloaked rather than hidden: it
    // passes IsWindowVisible and captures as a black rectangle.
    DWORD cloaked = 0;
    f.cloaked = SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))
                && cloaked != 0;

    const auto styles = DWORD(GetWindowLongPtrW(hwnd, GWL_STYLE));
    const auto exStyles = DWORD(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    f.toolWindow = (exStyles & WS_EX_TOOLWINDOW) != 0;
    f.child = (styles & WS_CHILD) != 0;

    RECT client = {0, 0, 0, 0};
    GetClientRect(hwnd, &client);
    f.clientSize = QSize(int(client.right - client.left), int(client.bottom - client.top));

    // Where it sits on the virtual desktop. Not needed to record a window — gdigrab takes
    // it by handle — but it is how the picker files a window under the monitor it is on,
    // which is how people remember where a window is on a multi-monitor desktop.
    RECT frame = {0, 0, 0, 0};
    if (GetWindowRect(hwnd, &frame)) {
        f.origin = QPoint(int(frame.left), int(frame.top));
    }
    return f;
}

BOOL CALLBACK collectWindow(HWND hwnd, LPARAM param)
{
    auto *out = reinterpret_cast<QVector<CaptureSource> *>(param);
    const WindowFacts f = factsFor(hwnd);
    if (!CaptureSources::shouldOfferWindow(f)) {
        return TRUE;
    }
    CaptureSource s;
    s.kind = CaptureSource::Kind::Window;
    // The handle, not the title: titles change while a take is running (a document is
    // saved, a tab switches) and two windows can share one. gdigrab takes either.
    s.id = QString::number(reinterpret_cast<quintptr>(hwnd));
    s.name = f.exeName.isEmpty() ? f.title
                                 : QStringLiteral("[%1]: %2").arg(f.exeName, f.title);
    s.nativeSize = f.clientSize;
    s.origin = f.origin;
    s.frameRate = 30.0;
    out->push_back(s);
    return TRUE;
}

BOOL CALLBACK collectMonitor(HMONITOR handle, HDC, LPRECT rect, LPARAM param)
{
    auto *out = reinterpret_cast<QVector<CaptureSource> *>(param);
    if (!rect) {
        return TRUE;
    }
    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    const bool haveInfo = GetMonitorInfoW(handle, &info);

    CaptureSource s;
    s.kind = CaptureSource::Kind::Screen;
    s.id = QString::number(out->size());
    s.name = QObject::tr("Display %1").arg(out->size() + 1);
    if (haveInfo && (info.dwFlags & MONITORINFOF_PRIMARY)) {
        s.name = QObject::tr("Display %1 (primary)").arg(out->size() + 1);
    }
    // These rectangles are already the physical pixels a grabber is told to cut out, so
    // nothing has to be scaled and monitors at different DPI settings need no special
    // case — which is exactly what Qt's logical geometry could not give.
    s.origin = QPoint(int(rect->left), int(rect->top));
    s.nativeSize = QSize(int(rect->right - rect->left), int(rect->bottom - rect->top));
    s.frameRate = 60.0;
    out->push_back(s);
    return TRUE;
}

/** Refresh rate of the monitor at `origin`, from Qt, which knows it and Win32 does not. */
double refreshRateAt(const QPoint &origin)
{
    if (!QGuiApplication::instance()) {
        return 60.0; // no GUI application (a test binary): Qt has no screens to ask
    }
    for (QScreen *sc : QGuiApplication::screens()) {
        if (!sc) {
            continue;
        }
        const double dpr = sc->devicePixelRatio();
        const QPoint physical(int(std::lround(sc->geometry().x() * dpr)),
                              int(std::lround(sc->geometry().y() * dpr)));
        if (physical == origin && sc->refreshRate() > 1.0) {
            return sc->refreshRate();
        }
    }
    return 60.0;
}

} // namespace

#endif // Q_OS_WIN

QVector<CaptureSource> CaptureSources::windows()
{
#ifdef Q_OS_WIN
    QVector<CaptureSource> out;
    EnumWindows(collectWindow, reinterpret_cast<LPARAM>(&out));
    return out;
#else
    // No window grabber is wired up off Windows; saying so with an empty list is better
    // than offering windows that cannot be recorded.
    return {};
#endif
}

QVector<CaptureSource> CaptureSources::screens()
{
#ifdef Q_OS_WIN
    // Win32 hands back the physical rectangles directly, which is what the grabber wants.
    // Qt only has logical geometry, and converting it needs each monitor's own scaling —
    // exact for one display, guesswork the moment two are scaled differently.
    //
    // Physical is the right space because ffmpeg's manifest declares `PerMonitorV2`, so it
    // sees the same unvirtualised desktop this process does. Were it DPI-unaware, Windows
    // would hand it the scaled desktop instead and every offset here would be off by the
    // scaling factor — invisible on a 100% display and half a screen out on a 200% one.
    QVector<CaptureSource> fromOs;
    EnumDisplayMonitors(nullptr, nullptr, collectMonitor, reinterpret_cast<LPARAM>(&fromOs));
    for (CaptureSource &s : fromOs) {
        s.frameRate = refreshRateAt(s.origin); // Win32 does not report it; Qt does
    }
    if (!fromOs.isEmpty()) {
        return fromOs;
    }
    // Nothing enumerated at all: fall through to Qt rather than offer no screens.
#endif
    QVector<CaptureSource> out;
    const QList<QScreen *> list = QGuiApplication::screens();
    for (int i = 0; i < list.size(); ++i) {
        QScreen *sc = list[i];
        if (!sc) {
            continue;
        }
        CaptureSource s;
        s.kind = CaptureSource::Kind::Screen;
        // The index is what a grabber is told to open; the name is for the user.
        s.id = QString::number(i);
        s.name = sc->name().isEmpty() ? QObject::tr("Display %1").arg(i + 1)
                                      : sc->name();
        // Device pixels, not logical ones: a scaled display records at its real size, and
        // recording a 4K monitor at its 1080p logical size would quietly lose half of it.
        const QRect logical = sc->geometry();
        const double dpr = sc->devicePixelRatio();
        s.nativeSize = QSize(int(std::lround(logical.width() * dpr)),
                             int(std::lround(logical.height() * dpr)));
        s.origin = QPoint(int(std::lround(logical.x() * dpr)),
                          int(std::lround(logical.y() * dpr)));
        s.frameRate = sc->refreshRate() > 1.0 ? sc->refreshRate() : 60.0;
        out.push_back(s);
    }
    return out;
}

QVector<CaptureSource> CaptureSources::parseDshowListing(const QString &stderrText)
{
    QVector<CaptureSource> out;

    // ffmpeg prints the listing on stderr, one device per line, with its alternative name
    // on the line after. The prefix in brackets has changed between versions — older ones
    // say `[dshow @ ...]`, 8.x says `[in#0 @ ...]` — so any bracketed prefix is accepted
    // and the device's kind is taken from the `(video)` / `(audio)` suffix:
    //
    //   [in#0 @ 000001d...] "Camera (NVIDIA Broadcast)" (video)
    //   [in#0 @ 000001d...]   Alternative name "@device_sw_{860BB310-...}"
    //
    // Older versions printed no suffix and grouped devices under a heading instead, so
    // that is kept as a fallback. Matching `[dshow` alone, which is what the documented
    // format suggests, finds nothing at all on a current ffmpeg.
    //
    // A custom delimiter: the pattern itself contains `)"`, in `"(.+)"`, which would end a
    // plain raw string in the middle of the expression.
    static const QRegularExpression nameLine(
        QStringLiteral(R"RX(^\s*\[[^\]]*\]\s+"(.+)"(?:\s+\((video|audio)\))?\s*$)RX"));
    static const QRegularExpression header(
        QStringLiteral(R"RX(DirectShow\s+(video|audio)\s+devices)RX"),
        QRegularExpression::CaseInsensitiveOption);

    QString section;
    const QStringList lines = stderrText.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        const QRegularExpressionMatch head = header.match(line);
        if (head.hasMatch()) {
            section = head.captured(1).toLower();
            continue;
        }
        const QRegularExpressionMatch m = nameLine.match(raw);
        if (!m.hasMatch()) {
            continue;
        }
        const QString name = m.captured(1);
        if (name.startsWith(QStringLiteral("@device"))) {
            continue; // the alternative-name line, not a device of its own
        }
        // The per-line kind wins when ffmpeg gives it; otherwise the section heading does.
        const QString kindText = m.captured(2).isEmpty() ? section : m.captured(2).toLower();
        if (kindText.isEmpty()) {
            continue;
        }

        CaptureSource s;
        s.name = name;
        s.id = name; // what -i "video=<name>" wants
        if (kindText == QLatin1String("audio")) {
            s.kind = CaptureSource::Kind::Audio;
            // ffmpeg does not report a device's formats in the listing, so these are the
            // defaults a capture starts from; the picker lets them be raised, and the
            // plan takes the best across everything chosen.
            s.sampleRate = 48000;
            s.channels = 2;
            s.bitDepth = 16;
        } else {
            s.kind = CaptureSource::Kind::Camera;
            s.nativeSize = QSize(1920, 1080);
            s.frameRate = 30.0;
        }
        out.push_back(s);
    }
    return out;
}

QVector<CaptureSource> CaptureSources::devices()
{
    const QString ffmpeg = FfmpegLocator::find();
    if (ffmpeg.isEmpty()) {
        return {}; // no ffmpeg is not an error: screen capture still works
    }
#ifdef Q_OS_WIN
    QProcess proc;
    proc.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-list_devices"),
                        QStringLiteral("true"), QStringLiteral("-f"),
                        QStringLiteral("dshow"), QStringLiteral("-i"),
                        QStringLiteral("dummy")});
    if (!proc.waitForFinished(8000)) {
        proc.kill();
        return {};
    }
    // Listing devices always "fails" — ffmpeg has nothing to open — so the exit code says
    // nothing and the output is what matters.
    // UTF-8, not the local codepage: ffmpeg writes device names as UTF-8 on Windows too,
    // and decoding them as CP1251 turned every non-ASCII microphone into mojibake —
    // "Микрофон (Realtek(R) Audio)" came out as "РњРёРєСЂРѕС„РѕРЅ", which is also the name
    // that would then be handed back to ffmpeg to open.
    return parseDshowListing(QString::fromUtf8(proc.readAllStandardError()));
#else
    return {};
#endif
}

bool CaptureSources::shouldOfferWindow(const WindowFacts &facts)
{
    // OBS's list, arrived at over years of bug reports rather than from first principles
    // (libobs/util/windows/window-helpers.c). Worth taking whole: every one of these
    // exclusions is a window that looks capturable and is not.
    if (!facts.visible || facts.minimized || facts.cloaked) {
        return false;
    }
    if (facts.toolWindow || facts.child) {
        return false;
    }
    if (facts.clientSize.width() <= 0 || facts.clientSize.height() <= 0) {
        return false;
    }
    // Windows' own invisible plumbing: these have real windows with real titles and
    // nothing behind them.
    static const QStringList internal = {
        QStringLiteral("applicationframehost.exe"), QStringLiteral("shellexperiencehost.exe"),
        QStringLiteral("systemsettings.exe"),       QStringLiteral("winstore.app.exe"),
        QStringLiteral("searchui.exe"),             QStringLiteral("lockapp.exe"),
        QStringLiteral("searchapp.exe"),            QStringLiteral("video.ui.exe"),
        QStringLiteral("peopleexperiencehost.exe"), QStringLiteral("textinputhost.exe"),
    };
    const QString exe = facts.exeName.toLower();
    if (internal.contains(exe) || exe.startsWith(QStringLiteral("windowsinternal"))) {
        // ApplicationFrameHost owns the frame of every UWP app; the app's own window is a
        // child of it. OBS digs that child out and captures it with Windows Graphics
        // Capture. GDI cannot: those windows are composited, and a GDI grab of one comes
        // back black. Better not to offer what would record nothing.
        return false;
    }
    // The desktop itself is an explorer.exe window with no title; the taskbar is another.
    if (exe == QLatin1String("explorer.exe") && facts.title.isEmpty()) {
        return false;
    }
    // Nothing to show the user, and nothing to tell them apart by.
    return !facts.title.isEmpty();
}

QVector<CaptureSource> CaptureSources::all()
{
    QVector<CaptureSource> out = screens();
    out += windows();
    out += devices();
    return out;
}

} // namespace openvegas
