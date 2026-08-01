#include "io/CdAudioReader.h"

#include <QFile>
#include <QDir>
#include <QStorageInfo>
#include <QThread>
#include <cstring>
#include <algorithm>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#include <mmsystem.h>
#endif

namespace openvegas {

namespace {

#ifdef Q_OS_WIN

#ifndef FILE_DEVICE_CD_ROM
#define FILE_DEVICE_CD_ROM 0x00000002
#endif
#ifndef IOCTL_CDROM_BASE
#define IOCTL_CDROM_BASE FILE_DEVICE_CD_ROM
#endif
// MinGW / older SDKs may lack full ntddcdrm.h — define what we need.
#ifndef IOCTL_CDROM_READ_TOC
#define IOCTL_CDROM_READ_TOC CTL_CODE(IOCTL_CDROM_BASE, 0x0000, METHOD_BUFFERED, FILE_READ_ACCESS)
#endif
#ifndef IOCTL_CDROM_RAW_READ
#define IOCTL_CDROM_RAW_READ CTL_CODE(IOCTL_CDROM_BASE, 0x000F, METHOD_OUT_DIRECT, FILE_READ_ACCESS)
#endif

#pragma pack(push, 1)
struct OvTrackData {
    UCHAR Reserved;
    UCHAR ControlAdr;
    UCHAR TrackNumber;
    UCHAR Reserved1;
    UCHAR Address[4];
};

struct OvCdromToc {
    UCHAR Length[2];
    UCHAR FirstTrack;
    UCHAR LastTrack;
    OvTrackData TrackData[100];
};

enum OvTrackModeType : ULONG {
    OvYellowMode2 = 0,
    OvXAForm2 = 1,
    OvCDDA = 2
};

struct OvRawReadInfo {
    LARGE_INTEGER DiskOffset;
    ULONG SectorCount;
    OvTrackModeType TrackMode;
};
#pragma pack(pop)

quint32 msfToLba(UCHAR min, UCHAR sec, UCHAR frame)
{
    return (quint32(min) * 60u + quint32(sec)) * 75u + quint32(frame) - 150u;
}

quint32 tocAddressToLba(const UCHAR address[4])
{
    // TOC uses MSF in bytes 1..3 when Absolute Address format.
    return msfToLba(address[1], address[2], address[3]);
}

QString devicePathForRoot(const QString &rootPath)
{
    const QString letter = rootPath.trimmed().left(1).toUpper();
    if (letter.isEmpty()) {
        return {};
    }
    return QStringLiteral("\\\\.\\%1:").arg(letter);
}

HANDLE openCdHandle(const QString &rootPath, QString *error)
{
    const QString path = devicePathForRoot(rootPath);
    if (path.isEmpty()) {
        if (error) {
            *error = QObject::tr("Invalid drive path.");
        }
        return INVALID_HANDLE_VALUE;
    }
    HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE && error) {
        *error = QObject::tr("Cannot open drive %1 (error %2).")
                     .arg(path)
                     .arg(GetLastError());
    }
    return h;
}

QString queryProductName(HANDLE h)
{
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;
    BYTE buf[1024]{};
    DWORD got = 0;
    if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buf, sizeof(buf),
                         &got, nullptr)
        || got < sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
        return {};
    }
    auto *desc = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR *>(buf);
    QString vendor;
    QString product;
    QString revision;
    if (desc->VendorIdOffset) {
        vendor = QString::fromLatin1(reinterpret_cast<const char *>(buf + desc->VendorIdOffset)).trimmed();
    }
    if (desc->ProductIdOffset) {
        product =
            QString::fromLatin1(reinterpret_cast<const char *>(buf + desc->ProductIdOffset)).trimmed();
    }
    if (desc->ProductRevisionOffset) {
        revision = QString::fromLatin1(reinterpret_cast<const char *>(buf + desc->ProductRevisionOffset))
                       .trimmed();
    }
    QStringList parts;
    if (!vendor.isEmpty()) {
        parts << vendor;
    }
    if (!product.isEmpty()) {
        parts << product;
    }
    if (!revision.isEmpty()) {
        parts << revision;
    }
    return parts.join(QLatin1Char(' '));
}

bool writeWavHeader(QFile &f, quint32 dataBytes)
{
    const quint32 sampleRate = 44100;
    const quint16 channels = 2;
    const quint16 bits = 16;
    const quint32 byteRate = sampleRate * channels * (bits / 8);
    const quint16 blockAlign = channels * (bits / 8);
    const quint32 riffSize = 36 + dataBytes;

    auto w32 = [&](quint32 v) {
        char b[4] = {char(v & 0xff), char((v >> 8) & 0xff), char((v >> 16) & 0xff),
                     char((v >> 24) & 0xff)};
        return f.write(b, 4) == 4;
    };
    auto w16 = [&](quint16 v) {
        char b[2] = {char(v & 0xff), char((v >> 8) & 0xff)};
        return f.write(b, 2) == 2;
    };

    return f.write("RIFF", 4) == 4 && w32(riffSize) && f.write("WAVE", 4) == 4 && f.write("fmt ", 4) == 4
           && w32(16) && w16(1) && w16(channels) && w32(sampleRate) && w32(byteRate) && w16(blockAlign)
           && w16(bits) && f.write("data", 4) == 4 && w32(dataBytes);
}

#endif // Q_OS_WIN

} // namespace

QVector<CdDriveInfo> CdAudioReader::listOpticalDrives()
{
    QVector<CdDriveInfo> out;
#ifdef Q_OS_WIN
    const DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if ((mask & (1u << i)) == 0) {
            continue;
        }
        const wchar_t root[] = {wchar_t(L'A' + i), L':', L'\\', L'\0'};
        if (GetDriveTypeW(root) != DRIVE_CDROM) {
            continue;
        }
        CdDriveInfo d;
        d.rootPath = QString::fromWCharArray(root);
        QString product;
        QString err;
        HANDLE h = openCdHandle(d.rootPath, &err);
        if (h != INVALID_HANDLE_VALUE) {
            product = queryProductName(h);
            CloseHandle(h);
        }
        const QString letter = d.rootPath.left(2);
        if (product.isEmpty()) {
            QStorageInfo info(d.rootPath);
            product = info.name().trimmed();
        }
        if (product.isEmpty()) {
            product = QObject::tr("Optical Drive");
        }
        d.displayName = QStringLiteral("[%1] %2").arg(letter, product);
        out.push_back(d);
    }
#else
    for (const QStorageInfo &s : QStorageInfo::mountedVolumes()) {
        if (!s.isValid()) {
            continue;
        }
        const QString root = s.rootPath();
        if (root.contains(QLatin1String("cdrom"), Qt::CaseInsensitive)
            || root.contains(QLatin1String("dvd"), Qt::CaseInsensitive)) {
            CdDriveInfo d;
            d.rootPath = root;
            d.displayName = QStringLiteral("[%1] %2").arg(root, s.name());
            out.push_back(d);
        }
    }
#endif
    return out;
}

bool CdAudioReader::readToc(const QString &rootPath, QVector<CdTrackInfo> *tracks, QString *error)
{
    if (!tracks) {
        return false;
    }
    tracks->clear();
#ifdef Q_OS_WIN
    HANDLE h = openCdHandle(rootPath, error);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }

    OvCdromToc toc{};
    DWORD got = 0;
    const BOOL ok =
        DeviceIoControl(h, IOCTL_CDROM_READ_TOC, nullptr, 0, &toc, sizeof(toc), &got, nullptr);
    CloseHandle(h);
    if (!ok) {
        if (error) {
            *error = QObject::tr("No audio CD found or TOC could not be read (error %1).")
                         .arg(GetLastError());
        }
        return false;
    }

    const int first = toc.FirstTrack;
    const int last = toc.LastTrack;
    if (first < 1 || last < first || last > 99) {
        if (error) {
            *error = QObject::tr("Invalid CD table of contents.");
        }
        return false;
    }

    // Collect track entries + lead-out (0xAA).
    QVector<OvTrackData> entries;
    for (int i = 0; i < 100; ++i) {
        const OvTrackData &td = toc.TrackData[i];
        if (td.TrackNumber == 0) {
            break;
        }
        entries.push_back(td);
        if (td.TrackNumber == 0xAA) {
            break;
        }
    }
    if (entries.size() < 2) {
        if (error) {
            *error = QObject::tr("CD has no usable tracks.");
        }
        return false;
    }

    for (int i = 0; i + 1 < entries.size(); ++i) {
        const OvTrackData &cur = entries[i];
        if (cur.TrackNumber == 0xAA) {
            break;
        }
        const OvTrackData &next = entries[i + 1];
        CdTrackInfo t;
        t.number = cur.TrackNumber;
        t.isAudio = ((cur.ControlAdr & 0x04) == 0); // bit 2 of Control = data track
        t.startLba = tocAddressToLba(cur.Address);
        t.endLba = tocAddressToLba(next.Address);
        if (t.endLba <= t.startLba) {
            continue;
        }
        t.startSec = double(t.startLba) / double(kFramesPerSec);
        t.lengthSec = double(t.endLba - t.startLba) / double(kFramesPerSec);
        tracks->push_back(t);
    }
    return !tracks->isEmpty();
#else
    Q_UNUSED(rootPath);
    if (error) {
        *error = QObject::tr("CD extraction is only supported on Windows.");
    }
    return false;
#endif
}

bool CdAudioReader::extractToWav(const QString &rootPath, const CdTrackInfo &track,
                                 const QString &wavPath, int optimization, int speedFactor,
                                 const std::function<bool(int percent)> &progress, QString *error)
{
#ifdef Q_OS_WIN
    if (!track.isAudio || track.endLba <= track.startLba) {
        if (error) {
            *error = QObject::tr("Track %1 is not an audio track.").arg(track.number);
        }
        return false;
    }

    HANDLE h = openCdHandle(rootPath, error);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }

    const quint32 totalSectors = track.endLba - track.startLba;

    QFile out(wavPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        CloseHandle(h);
        if (error) {
            *error = QObject::tr("Cannot create %1").arg(wavPath);
        }
        return false;
    }
    // Placeholder header; rewrite after successful write.
    if (!writeWavHeader(out, 0)) {
        CloseHandle(h);
        out.close();
        out.remove();
        if (error) {
            *error = QObject::tr("Failed to write WAV header.");
        }
        return false;
    }

    // Chunk size: Full=26 (~1/3 s), Partial=13, None=5 sectors.
    ULONG chunk = 26;
    int delayMs = 0;
    if (optimization <= 0) {
        chunk = 5;
        delayMs = 8;
    } else if (optimization == 1) {
        chunk = 13;
        delayMs = 2;
    }
    if (speedFactor > 0) {
        // Approximate throttle: smaller chunks + longer delay at lower Nx.
        chunk = std::max<ULONG>(3, chunk / std::max(1, speedFactor / 4 + 1));
        delayMs = std::max(delayMs, 40 / std::max(1, speedFactor));
    }

    QByteArray buffer(int(chunk) * kBytesPerSector, Qt::Uninitialized);
    quint32 done = 0;
    bool cancelled = false;
    quint64 writtenPcm = 0;

    while (done < totalSectors) {
        const ULONG n = std::min<ULONG>(chunk, totalSectors - done);
        OvRawReadInfo info{};
        // DiskOffset is LBA * 2048 for CDROM_RAW_READ (Microsoft docs).
        info.DiskOffset.QuadPart = LONGLONG(track.startLba + done) * 2048LL;
        info.SectorCount = n;
        info.TrackMode = OvCDDA;

        DWORD got = 0;
        const BOOL ok = DeviceIoControl(h, IOCTL_CDROM_RAW_READ, &info, sizeof(info), buffer.data(),
                                        DWORD(n * kBytesPerSector), &got, nullptr);
        if (!ok || got == 0) {
            CloseHandle(h);
            out.close();
            out.remove();
            if (error) {
                *error = QObject::tr("Read failed at sector %1 (error %2).")
                             .arg(track.startLba + done)
                             .arg(GetLastError());
            }
            return false;
        }
        if (out.write(buffer.constData(), qint64(got)) != qint64(got)) {
            CloseHandle(h);
            out.close();
            out.remove();
            if (error) {
                *error = QObject::tr("Write failed for %1").arg(wavPath);
            }
            return false;
        }
        writtenPcm += got;
        done += n;

        if (progress) {
            const int pct = int((quint64(done) * 100ull) / quint64(totalSectors));
            if (!progress(std::min(100, pct))) {
                cancelled = true;
                break;
            }
        }
        if (delayMs > 0) {
            QThread::msleep(DWORD(delayMs));
        }
    }

    CloseHandle(h);

    if (cancelled) {
        out.close();
        out.remove();
        if (error) {
            *error = QObject::tr("Extraction cancelled.");
        }
        return false;
    }

    // Fix WAV sizes.
    out.seek(0);
    if (!writeWavHeader(out, quint32(std::min<quint64>(writtenPcm, 0xffffffffull)))) {
        out.close();
        out.remove();
        if (error) {
            *error = QObject::tr("Failed to finalize WAV header.");
        }
        return false;
    }
    out.close();
    if (progress) {
        progress(100);
    }
    return true;
#else
    Q_UNUSED(rootPath);
    Q_UNUSED(track);
    Q_UNUSED(wavPath);
    Q_UNUSED(optimization);
    Q_UNUSED(speedFactor);
    Q_UNUSED(progress);
    if (error) {
        *error = QObject::tr("CD extraction is only supported on Windows.");
    }
    return false;
#endif
}

bool CdAudioReader::eject(const QString &rootPath)
{
#ifdef Q_OS_WIN
    QString err;
    HANDLE h = openCdHandle(rootPath, &err);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD got = 0;
    // Unlock then eject.
    PREVENT_MEDIA_REMOVAL pmr{};
    pmr.PreventMediaRemoval = FALSE;
    DeviceIoControl(h, IOCTL_STORAGE_MEDIA_REMOVAL, &pmr, sizeof(pmr), nullptr, 0, &got, nullptr);
    const BOOL ok = DeviceIoControl(h, IOCTL_STORAGE_EJECT_MEDIA, nullptr, 0, nullptr, 0, &got, nullptr);
    CloseHandle(h);
    return ok == TRUE;
#else
    Q_UNUSED(rootPath);
    return false;
#endif
}

bool CdAudioReader::playTrack(const QString &rootPath, int trackNumber, QString *error)
{
#ifdef Q_OS_WIN
    stopPlayback();
    const QString letter = rootPath.left(2);
    // MCI CD audio
    QString openCmd = QStringLiteral("open %1 type cdaudio alias ovcd shareable").arg(letter);
    MCIERROR e = mciSendStringW(reinterpret_cast<LPCWSTR>(openCmd.utf16()), nullptr, 0, nullptr);
    if (e != 0) {
        // Retry without shareable
        openCmd = QStringLiteral("open %1 type cdaudio alias ovcd").arg(letter);
        e = mciSendStringW(reinterpret_cast<LPCWSTR>(openCmd.utf16()), nullptr, 0, nullptr);
    }
    if (e != 0) {
        if (error) {
            *error = QObject::tr("Cannot open CD for playback (MCI %1).").arg(e);
        }
        return false;
    }
    mciSendStringW(L"set ovcd time format tmsf", nullptr, 0, nullptr);
    const QString playCmd =
        QStringLiteral("play ovcd from %1 to %2").arg(trackNumber).arg(trackNumber + 1);
    e = mciSendStringW(reinterpret_cast<LPCWSTR>(playCmd.utf16()), nullptr, 0, nullptr);
    if (e != 0) {
        // Some drives want only "from"
        const QString playFrom = QStringLiteral("play ovcd from %1").arg(trackNumber);
        e = mciSendStringW(reinterpret_cast<LPCWSTR>(playFrom.utf16()), nullptr, 0, nullptr);
    }
    if (e != 0) {
        stopPlayback();
        if (error) {
            *error = QObject::tr("Cannot play track %1 (MCI %2).").arg(trackNumber).arg(e);
        }
        return false;
    }
    return true;
#else
    Q_UNUSED(rootPath);
    Q_UNUSED(trackNumber);
    if (error) {
        *error = QObject::tr("CD playback is only supported on Windows.");
    }
    return false;
#endif
}

void CdAudioReader::stopPlayback()
{
#ifdef Q_OS_WIN
    mciSendStringW(L"stop ovcd", nullptr, 0, nullptr);
    mciSendStringW(L"close ovcd", nullptr, 0, nullptr);
#endif
}

} // namespace openvegas
