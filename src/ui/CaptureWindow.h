#pragma once

#include "capture/CapturePlan.h"
#include "capture/CaptureRecorder.h"

#include <QDialog>
#include <QVector>

class QComboBox;
class QElapsedTimer;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class QTreeWidget;

namespace openvegas {

/**
 * OpenVegas Capture: pick what to record, see what will be recorded, record it.
 *
 * The window's job is to make the plan's three decisions visible before anything is
 * committed to — which resolution the take lands at, which audio format every file gets,
 * and how many files there will be. Those are worked out in CapturePlan; this shows them
 * and lets the reference and the fit be changed until they read right.
 *
 * Non-modal, because what is being recorded is usually the rest of the screen.
 */
class CaptureWindow : public QDialog {
    Q_OBJECT
public:
    explicit CaptureWindow(QWidget *parent = nullptr);

signals:
    /** A take finished; the files are ready to be brought in as tracks. */
    void takeRecorded(const QStringList &files);

protected:
    /** Closing the window ends the take: leaving ffmpeg running unseen would be worse. */
    void closeEvent(QCloseEvent *event) override;

private:
    void refreshSources();
    void rebuildPlan();
    void updateSummary();
    void chooseFolder();
    void toggleRecording();
    void setRecordingUi(bool recording);

    QTreeWidget *m_tree = nullptr;
    QComboBox *m_reference = nullptr;
    QComboBox *m_fit = nullptr;
    QComboBox *m_size = nullptr;
    QLineEdit *m_takeName = nullptr;
    QLineEdit *m_folder = nullptr;
    QLabel *m_summary = nullptr;
    QLabel *m_elapsed = nullptr;
    QPushButton *m_recordBtn = nullptr;
    QPushButton *m_rescanBtn = nullptr;

    QVector<CaptureSource> m_available;
    CapturePlan m_plan;
    CaptureRecorder m_recorder;
    QTimer *m_tick = nullptr;
    qint64 m_startedMs = 0;
};

} // namespace openvegas
