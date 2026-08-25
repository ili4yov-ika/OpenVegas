#include "capture/CapturePlan.h"

#include <QRegularExpression>

#include <algorithm>

namespace openvegas {

namespace {

/** A file name that survives every filesystem the take might be copied to. */
QString sanitise(const QString &name)
{
    static const QRegularExpression bad(QStringLiteral(R"([\\/:*?"<>|]+)"));
    QString out = name;
    out.replace(bad, QStringLiteral("_"));
    out = out.simplified();
    return out.isEmpty() ? QStringLiteral("source") : out;
}

QString kindTag(CaptureSource::Kind kind)
{
    switch (kind) {
    case CaptureSource::Kind::Screen:
        return QStringLiteral("screen");
    case CaptureSource::Kind::Window:
        return QStringLiteral("window");
    case CaptureSource::Kind::Camera:
        return QStringLiteral("camera");
    case CaptureSource::Kind::Audio:
        return QStringLiteral("audio");
    }
    return QStringLiteral("source");
}

} // namespace

int CapturePlan::resolvedReference() const
{
    if (referenceIndex >= 0 && referenceIndex < sources.size()
        && sources[referenceIndex].isVideo()) {
        return referenceIndex;
    }
    // No choice made: the largest video source is what the user means by "the take's
    // resolution" — recording a 4K screen into a take sized by a webcam would be a
    // surprising default.
    int best = -1;
    qint64 bestArea = -1;
    for (int i = 0; i < sources.size(); ++i) {
        if (!sources[i].isVideo()) {
            continue;
        }
        const qint64 area = qint64(sources[i].nativeSize.width())
                            * qint64(sources[i].nativeSize.height());
        if (area > bestArea) {
            bestArea = area;
            best = i;
        }
    }
    return best;
}

QSize CapturePlan::resolution() const
{
    if (forcedSize.isValid() && forcedSize.width() > 0 && forcedSize.height() > 0) {
        return forcedSize;
    }
    const int ref = resolvedReference();
    return ref >= 0 ? sources[ref].nativeSize : QSize();
}

double CapturePlan::frameRate() const
{
    const int ref = resolvedReference();
    return ref >= 0 ? sources[ref].frameRate : 0.0;
}

int CapturePlan::sampleRate() const
{
    int best = 0;
    for (const CaptureSource &s : sources) {
        if (s.isAudio()) {
            best = std::max(best, s.sampleRate);
        }
    }
    return best;
}

int CapturePlan::channels() const
{
    int best = 0;
    for (const CaptureSource &s : sources) {
        if (s.isAudio()) {
            best = std::max(best, s.channels);
        }
    }
    return best;
}

int CapturePlan::bitDepth() const
{
    int best = 0;
    for (const CaptureSource &s : sources) {
        if (s.isAudio()) {
            best = std::max(best, s.bitDepth);
        }
    }
    return best;
}

QVector<CaptureOutput> CapturePlan::outputs() const
{
    QVector<CaptureOutput> out;
    const QSize take = resolution();
    const double fps = frameRate();
    const int rate = sampleRate();
    const int chans = channels();
    const QString base = sanitise(takeName);

    // Names have to stay distinct even when two sources are called the same thing, which
    // two identical capture cards or two monitors regularly are.
    QVector<QString> used;
    for (int i = 0; i < sources.size(); ++i) {
        const CaptureSource &s = sources[i];
        CaptureOutput o;
        o.source = s;

        if (s.isVideo()) {
            // Under Native every track keeps whatever its source gave; otherwise the take
            // has one size and the others are fitted to it when recorded.
            o.size = (fit == CaptureFit::Native) ? s.nativeSize : take;
            o.frameRate = fps > 0.0 ? fps : s.frameRate;
        } else {
            o.sampleRate = rate > 0 ? rate : s.sampleRate;
            o.channels = chans > 0 ? chans : s.channels;
        }

        QString stem = QStringLiteral("%1_%2_%3")
                           .arg(base, kindTag(s.kind), sanitise(s.name));
        QString candidate = stem;
        int suffix = 2;
        while (used.contains(candidate)) {
            candidate = QStringLiteral("%1_%2").arg(stem).arg(suffix++);
        }
        used.push_back(candidate);
        o.fileName = candidate + (s.isVideo() ? QStringLiteral(".mkv") : QStringLiteral(".wav"));
        out.push_back(o);
    }
    return out;
}

QString CapturePlan::validate() const
{
    if (sources.isEmpty()) {
        return QObject::tr("Pick at least one source to record.");
    }
    bool anyVideo = false;
    for (const CaptureSource &s : sources) {
        if (s.isVideo()) {
            anyVideo = true;
            if (!s.nativeSize.isValid() || s.nativeSize.isEmpty()) {
                return QObject::tr("%1 did not report a size.").arg(s.name);
            }
        } else if (s.sampleRate <= 0) {
            return QObject::tr("%1 did not report a sample rate.").arg(s.name);
        }
    }
    if (anyVideo && !resolution().isValid()) {
        return QObject::tr("No video source to take the resolution from.");
    }
    if (forcedSize.isValid() && (forcedSize.width() % 2 || forcedSize.height() % 2)) {
        // Every codec worth writing to wants even dimensions; catching it here beats an
        // encoder failing halfway through a take.
        return QObject::tr("Recording size must be even on both sides.");
    }
    return QString();
}

} // namespace openvegas
