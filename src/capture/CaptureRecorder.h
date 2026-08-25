#pragma once

#include "capture/CapturePlan.h"

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVector>

namespace openvegas {

/**
 * Runs a capture take: one ffmpeg per source, all started together, all stopped together.
 *
 * One process per source rather than one doing everything, because the plan already says
 * each source needs its own file — a take that muxed them together could not be pulled
 * back apart onto separate tracks. Separate processes also mean a camera that disconnects
 * mid-take loses its own recording and nothing else.
 *
 * Building the argument list is kept apart from running it, the same split that paid off
 * in device enumeration: the arguments are what actually break when a flag changes, and
 * they can be checked without a camera in the room.
 */
class CaptureRecorder : public QObject {
    Q_OBJECT
public:
    explicit CaptureRecorder(QObject *parent = nullptr);
    ~CaptureRecorder() override;

    /**
     * ffmpeg arguments that record `output` under `plan` into `filePath`.
     *
     * Empty when the source cannot be recorded on this platform, which the caller should
     * report rather than run.
     */
    static QStringList argumentsFor(const CapturePlan &plan, const CaptureOutput &output,
                                    const QString &filePath);

    /**
     * Start recording every output of `plan` into `folder`.
     *
     * Returns false and leaves nothing running when the plan does not validate or ffmpeg
     * is missing; a take that half-started would be worse than one that did not.
     */
    bool start(const CapturePlan &plan, const QString &folder, QString *error = nullptr);

    /**
     * Stop every recorder and wait for the files to be closed.
     *
     * ffmpeg is asked to finish rather than killed: a container needs its index written,
     * and a killed process leaves a file that may not play back.
     */
    void stop();

    bool isRecording() const { return !m_processes.isEmpty(); }

    /** Full paths of the files written by the take that just finished. */
    QStringList recordedFiles() const { return m_files; }

signals:
    /** A recorder stopped on its own — a device disconnected, or ffmpeg gave up. */
    void sourceFailed(const QString &sourceName, const QString &detail);
    /** Every recorder has finished and its file is closed. */
    void finished();

private:
    void wireProcess(QProcess *proc, const QString &sourceName);

    QVector<QProcess *> m_processes;
    QStringList m_files;
    QStringList m_names;
};

} // namespace openvegas
