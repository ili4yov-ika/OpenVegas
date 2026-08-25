#include "ui/GlobalHotkey.h"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QHash>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace openvegas {

namespace {

#ifdef Q_OS_WIN

/**
 * One filter for every hotkey in the process.
 *
 * WM_HOTKEY arrives on the thread that registered the key, not at a window, so it has to
 * be picked out of the native event stream. Installing a filter per hotkey would mean
 * every filter seeing every message; one filter with a table of ids is both cheaper and
 * easier to reason about.
 */
class HotkeyFilter : public QAbstractNativeEventFilter {
public:
    static HotkeyFilter &instance()
    {
        static HotkeyFilter filter;
        return filter;
    }

    void add(int id, GlobalHotkey *owner)
    {
        if (!m_installed) {
            QCoreApplication::instance()->installNativeEventFilter(this);
            m_installed = true;
        }
        m_owners.insert(id, owner);
    }

    void remove(int id) { m_owners.remove(id); }

    int nextId() { return ++m_lastId; }

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *) override
    {
        if (eventType != "windows_generic_MSG") {
            return false;
        }
        auto *msg = static_cast<MSG *>(message);
        if (!msg || msg->message != WM_HOTKEY) {
            return false;
        }
        GlobalHotkey *owner = m_owners.value(int(msg->wParam), nullptr);
        if (!owner) {
            return false;
        }
        emit owner->activated();
        // Not swallowed: this is a system-wide key and other filters are entitled to see
        // it. Nothing else in this process listens for WM_HOTKEY anyway.
        return false;
    }

private:
    QHash<int, GlobalHotkey *> m_owners;
    bool m_installed = false;
    // Above the range Windows reserves for hotkeys registered by DLLs.
    int m_lastId = 0xB000;
};

#endif // Q_OS_WIN

} // namespace

GlobalHotkey::GlobalHotkey(QObject *parent)
    : QObject(parent)
{
}

GlobalHotkey::~GlobalHotkey()
{
    unregisterShortcut();
}

bool GlobalHotkey::decode(const QKeySequence &sequence, unsigned *modifiersOut,
                          unsigned *virtualKeyOut)
{
    if (sequence.isEmpty() || sequence.count() != 1) {
        return false; // a system-wide key is one chord, not a sequence of them
    }
    const QKeyCombination combo = sequence[0];
    const int key = combo.key();
    if (key == 0) {
        return false;
    }

#ifdef Q_OS_WIN
    unsigned mods = 0;
    const Qt::KeyboardModifiers qtMods = combo.keyboardModifiers();
    if (qtMods & Qt::AltModifier) {
        mods |= MOD_ALT;
    }
    if (qtMods & Qt::ControlModifier) {
        mods |= MOD_CONTROL;
    }
    if (qtMods & Qt::ShiftModifier) {
        mods |= MOD_SHIFT;
    }
    if (qtMods & Qt::MetaModifier) {
        mods |= MOD_WIN;
    }
    // Without this a hotkey held down repeats as fast as the keyboard does, and a
    // start/stop toggle would flicker on and off for as long as the key is pressed.
    mods |= MOD_NOREPEAT;

    unsigned vk = 0;
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        vk = unsigned(VK_F1 + (key - Qt::Key_F1));
    } else if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9)) {
        // Letters and digits share their ASCII values with the virtual-key codes.
        vk = unsigned(key);
    } else {
        switch (key) {
        case Qt::Key_Space: vk = VK_SPACE; break;
        case Qt::Key_Escape: vk = VK_ESCAPE; break;
        case Qt::Key_Print: vk = VK_SNAPSHOT; break;
        case Qt::Key_Pause: vk = VK_PAUSE; break;
        case Qt::Key_Insert: vk = VK_INSERT; break;
        case Qt::Key_Delete: vk = VK_DELETE; break;
        case Qt::Key_Home: vk = VK_HOME; break;
        case Qt::Key_End: vk = VK_END; break;
        default: return false;
        }
    }
    if (modifiersOut) {
        *modifiersOut = mods;
    }
    if (virtualKeyOut) {
        *virtualKeyOut = vk;
    }
    return true;
#else
    Q_UNUSED(modifiersOut);
    Q_UNUSED(virtualKeyOut);
    return false; // no system-wide keys wired up off Windows yet
#endif
}

bool GlobalHotkey::setShortcut(const QKeySequence &sequence)
{
    unregisterShortcut();
    m_sequence = sequence;

    unsigned mods = 0;
    unsigned vk = 0;
    if (!decode(sequence, &mods, &vk)) {
        return false;
    }

#ifdef Q_OS_WIN
    if (!QCoreApplication::instance()) {
        return false;
    }
    m_id = HotkeyFilter::instance().nextId();
    // A null window handle sends WM_HOTKEY to this thread's message queue, which is where
    // the native event filter is watching.
    if (!RegisterHotKey(nullptr, m_id, mods, vk)) {
        m_id = 0;
        return false; // another program already holds it; the caller should say so
    }
    HotkeyFilter::instance().add(m_id, this);
    m_registered = true;
    return true;
#else
    return false;
#endif
}

void GlobalHotkey::unregisterShortcut()
{
#ifdef Q_OS_WIN
    if (m_registered && m_id != 0) {
        UnregisterHotKey(nullptr, m_id);
        HotkeyFilter::instance().remove(m_id);
    }
#endif
    m_registered = false;
    m_id = 0;
}

} // namespace openvegas
