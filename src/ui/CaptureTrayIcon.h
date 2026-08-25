#pragma once

#include <QImage>
#include <QObject>
#include <QString>

class QAction;
class QMenu;
class QSystemTrayIcon;

namespace openvegas {

/**
 * Tray presence for OpenVegas Capture: what is recording, and how to stop it.
 *
 * A recorder is used with its own window hidden — that is the point of recording the
 * screen — so the tray is the only place the state is visible while a take runs. It is also
 * the only way to stop one without bringing the window back over whatever is being
 * captured, which would put the window into the recording.
 *
 * The icon carries the state itself rather than relying on a tooltip nobody hovers: a red
 * dot appears low and right of the glyph while recording, the way OBS marks it.
 */
class CaptureTrayIcon : public QObject {
    Q_OBJECT
public:
    explicit CaptureTrayIcon(QObject *parent = nullptr);
    ~CaptureTrayIcon() override;

    /** False where the desktop has no tray; the caller should carry on without one. */
    static bool isAvailable();

    void setRecording(bool recording);
    bool isRecording() const { return m_recording; }

    /** Second line of the tooltip — the take's elapsed time, or empty. */
    void setStatusText(const QString &text);

    void show();
    void hide();

    /**
     * The tray image at `size`, with the recording dot when `recording`.
     *
     * Drawn rather than scaled from one bitmap because a tray icon is asked for at whatever
     * size the desktop uses, and a 16-pixel dot scaled down from 48 is a smudge. Pure, so
     * what the two states actually look like can be checked without a desktop.
     */
    static QImage image(int size, bool recording);

signals:
    /** The window should be brought back up. */
    void showWindowRequested();
    /** Start if idle, stop if recording — the same thing the window's button does. */
    void toggleRecordingRequested();

private:
    void refreshIcon();

    QSystemTrayIcon *m_tray = nullptr;
    /** Owned here: setContextMenu() does not take it, and a QMenu has no QObject parent. */
    QMenu *m_menu = nullptr;
    QAction *m_recordAction = nullptr;
    bool m_recording = false;
    QString m_status;
};

} // namespace openvegas
