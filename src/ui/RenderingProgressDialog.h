#pragma once

#include "media/MediaEngine.h"

#include <QDialog>
#include <QElapsedTimer>

class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QToolButton;
class QWidget;

namespace openvegas {

/** Vegas-style File → Render As… progress window. */
class RenderingProgressDialog : public QDialog {
    Q_OBJECT
public:
    explicit RenderingProgressDialog(const QString &outputPath, QWidget *parent = nullptr);

    bool wasCanceled() const { return m_canceled; }
    bool showVideoInPreview() const;
    bool openFolderWhenDone() const;
    bool closeWhenDone() const;

    void beginRender(double lengthSec, qint64 estimatedBytes);
    void applyProgress(const MediaRenderProgress &p);
    void finishSuccess(const QString &message);
    void finishFailure(const QString &error);

signals:
    void cancelRequested();

protected:
    void reject() override;
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void toggleSection(QWidget *body, QToolButton *arrow);
    void refreshTitle(int percent);
    void refreshTiming();
    void setFinishedUi(bool ok);
    static QString formatBytes(qint64 bytes);
    static QString formatDuration(double sec);
    static QString formatClock(const QDateTime &dt);

    QString m_outputPath;
    bool m_canceled = false;
    bool m_finished = false;
    double m_lengthSec = 1.0;
    qint64 m_estimatedBytes = 0;
    qint64 m_renderedBytes = 0;
    int m_lastPercent = 0;
    double m_framesDone = 0;
    double m_framesTotal = 1;
    double m_recentFps = 0.0;
    QElapsedTimer m_elapsed;
    QElapsedTimer m_speedSample;
    int m_speedSampleFrames = 0;

    QLabel *m_pathLabel = nullptr;
    QProgressBar *m_bar = nullptr;
    QLabel *m_pctLabel = nullptr;
    QLabel *m_renderedLabel = nullptr;
    QLabel *m_remainingLabel = nullptr;
    QLabel *m_elapsedLabel = nullptr;

    QLabel *m_freeSpaceVal = nullptr;
    QLabel *m_durationVal = nullptr;
    QLabel *m_completionVal = nullptr;
    QLabel *m_avgSpeedVal = nullptr;
    QLabel *m_curSpeedVal = nullptr;
    QLabel *m_stageVal = nullptr;

    QWidget *m_estBody = nullptr;
    QWidget *m_perfBody = nullptr;
    QWidget *m_optBody = nullptr;
    QToolButton *m_estArrow = nullptr;
    QToolButton *m_perfArrow = nullptr;
    QToolButton *m_optArrow = nullptr;

    QCheckBox *m_showPreview = nullptr;
    QCheckBox *m_openFolder = nullptr;
    QCheckBox *m_closeDone = nullptr;

    QPushButton *m_openFolderBtn = nullptr;
    QPushButton *m_openBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
};

} // namespace openvegas
