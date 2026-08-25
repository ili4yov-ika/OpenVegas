#pragma once

#include "capture/CaptureSource.h"

#include <QStringList>

namespace openvegas {

/**
 * How a source is opened — the `-f …  -i …` half of an ffmpeg command line.
 *
 * Kept in one place because two things open the same devices: the recorder, and the
 * preview that shows what a source looks like before anything is recorded. Two copies
 * would drift, and the way they would drift is nasty — a preview that opened a window by
 * its caption while the take opened it by handle would show one window and record
 * another, and nothing would say so until the take was watched.
 *
 * @param frameRate rate to ask the device for; 0 leaves it to the device.
 * @return empty when the source cannot be opened on this platform, which the caller must
 *         report rather than run.
 */
QStringList captureInputArguments(const CaptureSource &source, double frameRate);

} // namespace openvegas
