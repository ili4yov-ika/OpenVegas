#pragma once

#include "model/ProjectModel.h"

namespace openvegas {

/** Amplitude 0…1 for fade progress t∈[0,1] (fade-in semantics). Shared by UI and DSP. */
double fadeCurveAmplitude(FadeCurveType type, double t);

} // namespace openvegas
