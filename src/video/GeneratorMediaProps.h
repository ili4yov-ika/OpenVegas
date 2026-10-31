#pragma once

#include <QColor>
#include <QString>

namespace openvegas {

enum class MediaFieldOrder { ProgressiveScan, UpperFieldFirst, LowerFieldFirst };
enum class MediaAlphaChannel { Undefined, None, Straight, Premultiplied, PremultipliedDirty };
enum class MediaRotation { Deg0, Deg90Clockwise, Deg180, Deg90CounterClockwise };

struct GeneratorMediaProps {
    QString tapeName;
    bool useCustomTimecode = false;
    double customTimecodeSec = 0.0;
    int frameWidth = 0;
    int frameHeight = 0;
    MediaFieldOrder fieldOrder = MediaFieldOrder::ProgressiveScan;
    double pixelAspect = 1.0;
    MediaAlphaChannel alphaChannel = MediaAlphaChannel::Premultiplied;
    QColor backgroundColor = QColor(0, 0, 0, 255);
    MediaRotation rotation = MediaRotation::Deg0;
};

} // namespace openvegas
