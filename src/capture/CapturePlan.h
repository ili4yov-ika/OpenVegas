#pragma once

#include "capture/CaptureSource.h"

#include <QSize>
#include <QString>
#include <QVector>

namespace openvegas {

/**
 * What a capture take will actually record, worked out before anything is opened.
 *
 * Three questions have to be settled before recording starts, and all three are decided
 * here rather than inside whichever backend does the grabbing:
 *
 *   * **Which resolution.** One video source is the reference — normally the monitor or
 *     capture card the user picked — and the take is that size. The others are fitted to
 *     it, or left at their own size, depending on `fit`. The reference can also be
 *     overridden outright, for recording a 4K screen into a 1080p take.
 *   * **Which audio format.** A container generally cannot hold two audio streams at
 *     different rates and depths, so the take takes the *best* of what the chosen sources
 *     offer — the highest sample rate, the most channels, the deepest samples — and the
 *     poorer sources are brought up to it. Going the other way would throw away quality
 *     that was there for the taking.
 *   * **Which files.** One per source, because each has to land on its own track when the
 *     take is brought into a project. A single muxed file could not be pulled apart again.
 *
 * All of it is arithmetic over the source descriptions, so it is settled and tested
 * without a capture device present.
 */
class CapturePlan {
public:
    /** Sources chosen for the take, in the order the user ticked them. */
    QVector<CaptureSource> sources;

    /**
     * Index into `sources` of the video source the take's resolution follows; -1 picks
     * the largest video source, which is what a user who has not chosen means.
     */
    int referenceIndex = -1;

    /** Overrides the reference's own size when valid — "record this 4K screen at 1080p". */
    QSize forcedSize;

    CaptureFit fit = CaptureFit::Letterbox;

    /** Base name for the take's files; each output appends its own source name. */
    QString takeName = QStringLiteral("Take");

    /** The take's video size, after reference choice and any override. */
    QSize resolution() const;

    /** Frame rate of the reference source, or 0 when the take has no video. */
    double frameRate() const;

    /** Sample rate every audio output is recorded at — the best on offer. */
    int sampleRate() const;
    /** Channel count every audio output is recorded at — the most on offer. */
    int channels() const;
    /** Bit depth every audio output is recorded at — the deepest on offer. */
    int bitDepth() const;

    /** One output per chosen source, sized and named. */
    QVector<CaptureOutput> outputs() const;

    /** Empty when the plan can be recorded; otherwise why it cannot. */
    QString validate() const;

private:
    /** Index of the source the resolution follows, resolved from `referenceIndex`. */
    int resolvedReference() const;
};

} // namespace openvegas
