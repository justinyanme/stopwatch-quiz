#pragma once
#include <cmath>

// Easing curves for UI motion. Pure functions of normalized time t in [0,1],
// returning eased progress in [0,1]. Shared by entrance/transition animations.
namespace ease {

// Exponential ease-out: fast start, long gentle settle. The confident,
// decisive curve, no overshoot or bounce. t is clamped to [0,1]. Normalized so
// outExpo(1) == 1 exactly (the raw 1-2^-10t lands at 0.999, leaving a one-frame
// snap at the end); the 1/(1-2^-10) factor stretches it back onto [0,1].
inline float outExpo(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    constexpr float kNorm = 1.0f / (1.0f - 0.0009765625f);  // 1/(1 - 2^-10)
    return (1.0f - powf(2.0f, -10.0f * t)) * kNorm;
}

} // namespace ease
