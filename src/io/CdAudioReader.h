#pragma once

#include <QString>
#include <QVector>
#include <functional>

namespace openvegas {

struct CdTrackInfo {
    int number = 0;
    bool isAudio = true;
    quint32 startLba = 0;
    quint32 endLba = 0; // exclusive
    double startSec = 0.0;
    double lengthSec = 0.0;
};

struct CdDriveInfo {
    QString rootPath; // e.g. "E:\\"
    QString displayName;
};

/** Windows CDDA: TOC + raw sector rip to WAV. Stub elsewhere. */
class CdAudioReader {
public:
    static QVector<CdDriveInfo> listOpticalDrives();
    static bool readToc(const QString &rootPath, QVector<CdTrackInfo> *tracks, QString *error = nullptr);
    /**
     * Extract [startLba, endLba) as 44.1 kHz stereo 16-bit WAV.
     * @param optimization 0=None (small chunks+delay), 1=Partial, 2=Full
     * @param speedFactor 0=Max, else approximate Nx throttle
     * @param progress return false to cancel; percent 0…100
     */
    static bool extractToWav(const QString &rootPath, const CdTrackInfo &track, const QString &wavPath,
                             int optimization = 2, int speedFactor = 0,
                             const std::function<bool(int percent)> &progress = {},
                             QString *error = nullptr);
    static bool eject(const QString &rootPath);
    /** Best-effort MCI preview of a track (Windows). */
    static bool playTrack(const QString &rootPath, int trackNumber, QString *error = nullptr);
    static void stopPlayback();

    static constexpr int kFramesPerSec = 75;
    static constexpr int kBytesPerSector = 2352;
};

} // namespace openvegas
