#pragma once

#include <QKeySequence>
#include <QObject>

namespace openvegas {

/**
 * A key combination that works while the application is not in front.
 *
 * A recorder is used from inside whatever is being recorded, so the one command that must
 * always be reachable — start and stop — cannot be a menu item or a shortcut on a window
 * that is minimised to the tray. A normal `QShortcut` only fires when the application has
 * focus, which is exactly when a screen recorder does not.
 *
 * Windows registers these with the OS, so the combination is reserved system-wide and
 * another program can already hold it. Registration failing is ordinary, not exceptional,
 * and the caller should say so rather than pretend the key works.
 */
class GlobalHotkey : public QObject {
    Q_OBJECT
public:
    explicit GlobalHotkey(QObject *parent = nullptr);
    ~GlobalHotkey() override;

    /**
     * Claim `sequence` system-wide. Returns false when the platform has no such thing or
     * another program already holds the combination.
     *
     * Registering again replaces whatever was registered before.
     */
    bool setShortcut(const QKeySequence &sequence);

    /** Give the combination back. */
    void unregisterShortcut();

    bool isRegistered() const { return m_registered; }
    QKeySequence shortcut() const { return m_sequence; }

    /**
     * Split a key sequence into the modifier mask and virtual key Windows wants.
     *
     * Pure, so what a combination turns into can be checked without claiming keys from the
     * machine running the tests — and claiming them is exactly what this class does.
     *
     * @return false when the sequence is empty, has more than one chord, or names a key
     *         with no virtual-key equivalent.
     */
    static bool decode(const QKeySequence &sequence, unsigned *modifiersOut,
                       unsigned *virtualKeyOut);

signals:
    /** The combination was pressed, wherever the user happened to be. */
    void activated();

private:
    QKeySequence m_sequence;
    bool m_registered = false;
    int m_id = 0;
};

} // namespace openvegas
