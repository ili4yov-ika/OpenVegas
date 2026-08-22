#include "plugins/SoundForgeHost.h"

#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <string>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace openvegas {
namespace {

QMutex g_mutex;
QVector<SoundForgeClass> g_cache;
bool g_cacheValid = false;

#ifdef Q_OS_WIN

/** Default ("") value of a registry key as a QString, or empty. */
QString defaultValue(HKEY key)
{
    DWORD type = 0;
    DWORD bytes = 0;
    if (::RegQueryValueExW(key, nullptr, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS
        || bytes == 0 || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return {};
    }
    QVector<wchar_t> buf(int(bytes / sizeof(wchar_t)) + 1, L'\0');
    if (::RegQueryValueExW(key, nullptr, nullptr, &type, reinterpret_cast<LPBYTE>(buf.data()),
                           &bytes)
        != ERROR_SUCCESS) {
        return {};
    }
    return QString::fromWCharArray(buf.constData()).trimmed();
}

/**
 * Scan HKCR\CLSID for classes served by a DLL under a "Shared Plug-Ins" directory.
 *
 * There is no shortcut: these classes are registered DirectShow-style (Merit + Pins) but
 * are **not** members of any DirectShow filter category — checked, and no category's
 * Instance list contains them — so they cannot be enumerated by category. Nor can the
 * CLSIDs be read out of the DLLs, which hold them as raw GUID constants and build the
 * registry path with a "CLSID\%s" format string at registration time. A single pass over
 * HKCR\CLSID is therefore the only way to recover the mapping, and it is cached.
 */
QVector<SoundForgeClass> scanRegistry()
{
    QVector<SoundForgeClass> out;
    HKEY clsidRoot = nullptr;
    if (::RegOpenKeyExW(HKEY_CLASSES_ROOT, L"CLSID", 0, KEY_READ | KEY_ENUMERATE_SUB_KEYS,
                        &clsidRoot)
        != ERROR_SUCCESS) {
        return out;
    }

    for (DWORD index = 0;; ++index) {
        wchar_t nameBuf[256] = {};
        DWORD nameLen = DWORD(std::size(nameBuf));
        const LSTATUS st =
            ::RegEnumKeyExW(clsidRoot, index, nameBuf, &nameLen, nullptr, nullptr, nullptr, nullptr);
        if (st == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (st != ERROR_SUCCESS) {
            continue; // a key we may not read; keep going rather than abandon the scan
        }

        HKEY inproc = nullptr;
        std::wstring sub(nameBuf, nameLen);
        sub += L"\\InprocServer32";
        if (::RegOpenKeyExW(clsidRoot, sub.c_str(), 0, KEY_READ, &inproc) != ERROR_SUCCESS) {
            continue;
        }
        const QString dllPath = defaultValue(inproc);
        ::RegCloseKey(inproc);
        if (!dllPath.contains(QLatin1String("Shared Plug-Ins"), Qt::CaseInsensitive)) {
            continue;
        }

        SoundForgeClass entry;
        entry.clsid = QString::fromWCharArray(nameBuf, int(nameLen));
        entry.dllPath = dllPath;
        entry.dllFileName = QFileInfo(dllPath).fileName();

        HKEY classKey = nullptr;
        if (::RegOpenKeyExW(clsidRoot, std::wstring(nameBuf, nameLen).c_str(), 0, KEY_READ,
                            &classKey)
            == ERROR_SUCCESS) {
            entry.name = defaultValue(classKey);
            ::RegCloseKey(classKey);
        }
        if (entry.name.isEmpty()) {
            entry.name = entry.clsid;
        }
        entry.isPropertyPage = entry.name.contains(QLatin1String("Property Page"),
                                                   Qt::CaseInsensitive);
        out.push_back(std::move(entry));
    }

    ::RegCloseKey(clsidRoot);
    return out;
}

#endif // Q_OS_WIN

} // namespace

QVector<SoundForgeClass> SoundForgeHost::discoverRegistered()
{
    QMutexLocker lock(&g_mutex);
    if (g_cacheValid) {
        return g_cache;
    }
#ifdef Q_OS_WIN
    g_cache = scanRegistry();
#else
    // COM registration is a Windows concept, and the packs ship x64 PE DLLs only.
    g_cache.clear();
#endif
    g_cacheValid = true;
    return g_cache;
}

QVector<SoundForgeClass> SoundForgeHost::discoverEffects()
{
    QVector<SoundForgeClass> out;
    for (const SoundForgeClass &c : discoverRegistered()) {
        if (!c.isPropertyPage) {
            out.push_back(c);
        }
    }
    std::sort(out.begin(), out.end(), [](const SoundForgeClass &a, const SoundForgeClass &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    return out;
}

bool SoundForgeHost::anyRegistered()
{
    return !discoverEffects().isEmpty();
}

QString SoundForgeHost::pluginId(const QString &clsid)
{
    if (clsid.isEmpty()) {
        return {};
    }
    return QStringLiteral("sfds:") + clsid;
}

QString SoundForgeHost::clsidFromPluginId(const QString &pluginId)
{
    static const QString prefix = QStringLiteral("sfds:");
    if (!pluginId.startsWith(prefix, Qt::CaseInsensitive)) {
        return {};
    }
    return pluginId.mid(prefix.size()).trimmed();
}

QVector<AudioPluginDesc> SoundForgeHost::pluginDescriptors()
{
    QVector<AudioPluginDesc> out;
    for (const SoundForgeClass &c : discoverEffects()) {
        AudioPluginDesc d;
        d.id = pluginId(c.clsid);
        d.name = c.name;
        d.vendor = QStringLiteral("VEGAS Creative Software");
        d.format = PluginFormat::DirectShow;
        d.category = QStringLiteral("VEGAS Shared");
        d.automatable = false; // parameters live behind the native property page
        d.trackOptimized = c.dllFileName.startsWith(QLatin1String("sftrkfx"), Qt::CaseInsensitive);
        d.path = c.dllPath;
        out.push_back(d);
    }
    return out;
}

void SoundForgeHost::invalidateCache()
{
    QMutexLocker lock(&g_mutex);
    g_cacheValid = false;
    g_cache.clear();
}

} // namespace openvegas
