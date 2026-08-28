#pragma once

#include "capture/CaptureLivePreview.h"
#include "capture/CapturePlan.h"
#include "capture/CaptureRecorder.h"
#include "ui/CaptureTrayIcon.h"
#include "ui/GlobalHotkey.h"

#include <QDialog>
#include <QHash>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class QVBoxLayout;

namespace openvegas {

class CaptureSourceCard;
class CaptureSourceList;
class CaptureWindowTree;

/**
 * OpenVegas Capture: pick what to record, watch it, record it.
 *
 * Two columns. On the left, what the take is made of — the video sources in the order they
 * will be used, each showing what is on it right now, and under them the audio inputs,
 * each in the colour of the video source it belongs to. On the right, everything open on
 * the desktop, grouped by monitor; a window is recorded by dragging it across into the
 * left column, which is the only gesture in the window that adds a source.
 *
 * The pictures are live because that is the question being asked — which of these two
 * monitors is the one I mean — and a still from a moment ago does not answer it. Windows
 * refresh more slowly than monitors and cameras: there are more of them, and confirming a
 * window is the right window does not need a smooth picture.
 *
 * Non-modal, because what is being recorded is usually the rest of the screen.
 */
class CaptureWindow : public QDialog {
    Q_OBJECT
public:
    explicit CaptureWindow(QWidget *parent = nullptr);
    ~CaptureWindow() override;

signals:
    /** A take finished; the files are ready to be brought in as tracks. */
    void takeRecorded(const QStringList &files);

protected:
    /** Closing the window ends the take: leaving ffmpeg running unseen would be worse. */
    void closeEvent(QCloseEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void rescan();
    void rebuildVideoCards();
    void rebuildAudioRows();
    void rebuildPlan();
    void updateSummary();
    void chooseFolder();
    void toggleRecording();
    void setRecordingUi(bool recording);

    void startLivePreviews();
    void stopLivePreviews();
    void showWindowPreview(const CaptureSource &window);

    void moveVideoSource(int fromIndex, int toIndex);
    void addWindowSource(const QString &windowId);
    void selectVideoSource(int index);

    /** The colour this row of the picker is drawn in; paired audio inherits it. */
    QColor accentFor(int videoIndex) const;
    /** The audio input that belongs to `video`, or empty when nothing obviously does. */
    QString pairedAudioName(const CaptureSource &video) const;

    // Left column
    CaptureSourceList *m_videoList = nullptr;
    QVector<CaptureSourceCard *> m_cards;
    QWidget *m_audioBox = nullptr;
    QVBoxLayout *m_audioLayout = nullptr;
    QVector<QCheckBox *> m_audioChecks;

    // Right column
    CaptureWindowTree *m_windowTree = nullptr;
    QLabel *m_windowPreview = nullptr;

    // Settings
    QComboBox *m_reference = nullptr;
    QComboBox *m_fit = nullptr;
    QComboBox *m_size = nullptr;
    QLineEdit *m_takeName = nullptr;
    QLineEdit *m_folder = nullptr;

    QLabel *m_summary = nullptr;
    QLabel *m_elapsed = nullptr;
    QPushButton *m_recordBtn = nullptr;
    QPushButton *m_rescanBtn = nullptr;

    CaptureTrayIcon *m_tray = nullptr;

    /**
     * One row of the video column: the source, and whether it is going into the take.
     *
     * The tick lives in the row rather than in a list beside it. Reordering has to carry it
     * along, and a parallel array is one `move()` away from putting someone else's tick on
     * a source they did not choose — which is not a crash, it is a take with the wrong
     * thing in it, discovered afterwards.
     */
    struct PickedSource {
        CaptureSource source;
        bool checked = false;
    };

    /** Video sources in the order the picker shows them; the take follows this order. */
    QVector<PickedSource> m_video;
    /** Audio inputs, in the order they were found. */
    QVector<CaptureSource> m_audio;
    /** Which of `m_audio` are ticked. */
    QVector<bool> m_audioChecked;

    /** One live stream per card, in step with `m_cards`. */
    QVector<CaptureLivePreview *> m_livePreviews;
    /** The stream behind the right-hand panel's picture. */
    CaptureLivePreview *m_windowLive = nullptr;

    CapturePlan m_plan;
    CaptureRecorder m_recorder;
    GlobalHotkey m_hotkey;
    QTimer *m_tick = nullptr;
    qint64 m_startedMs = 0;
    int m_selected = -1;
};

} // namespace openvegas
