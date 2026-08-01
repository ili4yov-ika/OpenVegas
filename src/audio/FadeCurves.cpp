#include "audio/FadeCurves.h"

#include <algorithm>
#include <cmath>

namespace openvegas {

double fadeCurveAmplitude(FadeCurveType type, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    switch (type) {
    case FadeCurveType::Fast:
        return 1.0 - (1.0 - t) * (1.0 - t);
    case FadeCurveType::Linear:
        return t;
    case FadeCurveType::Slow:
        return t * t;
    case FadeCurveType::Smooth:
        return t * t * (3.0 - 2.0 * t);
    case FadeCurveType::Sharp:
        return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    }
    return t;
}

} // namespace openvegas
