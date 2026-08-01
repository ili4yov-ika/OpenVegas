#pragma once

#include <algorithm>
#include <cmath>

namespace openvegas {

inline float dbToLinear(double db)
{
    if (db <= -120.0) {
        return 0.f;
    }
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

inline double linearToDb(float lin)
{
    const float a = std::abs(lin);
    if (a < 1e-10f) {
        return -120.0;
    }
    return 20.0 * std::log10(double(a));
}

/** Map mixer fader 0…100 → dB (−60…+12), Vegas-like. */
inline double faderPosToDb(int pos0to100)
{
    const double t = std::clamp(pos0to100, 0, 100) / 100.0;
    if (t <= 0.0) {
        return -120.0;
    }
    // 0 → −∞, ~0.7 → 0 dB, 1 → +12
    if (t < 0.7) {
        return -60.0 + (t / 0.7) * 60.0;
    }
    return ((t - 0.7) / 0.3) * 12.0;
}

inline int dbToFaderPos(double db)
{
    if (db <= -119.0) {
        return 0;
    }
    if (db <= 0.0) {
        return int(std::lround(std::clamp(db + 60.0, 0.0, 60.0) / 60.0 * 70.0));
    }
    return int(std::lround(70.0 + std::clamp(db, 0.0, 12.0) / 12.0 * 30.0));
}

/** Constant-power pan: pan −1…+1 → L/R gains. */
inline void panGains(float pan, float &gainL, float &gainR)
{
    const float p = std::clamp(pan, -1.f, 1.f);
    constexpr float kPi = 3.14159265358979323846f;
    const float angle = (p + 1.f) * 0.25f * kPi; // 0…π/2
    gainL = std::cos(angle);
    gainR = std::sin(angle);
}

} // namespace openvegas
