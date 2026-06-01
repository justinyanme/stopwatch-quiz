#include "OrientationController.h"
#include <cmath>

namespace stopwatch {
namespace {

constexpr float kMinTotalG = 0.50f;
constexpr float kMaxTotalG = 1.70f;
constexpr float kMinPlanarG = 0.35f;
constexpr float kAxisHysteresisMargin = 0.12f;

bool finiteSample(OrientationSample s) {
    return std::isfinite(s.ax) && std::isfinite(s.ay) && std::isfinite(s.az);
}

bool candidateFromSample(OrientationSample s,
                         DisplayOrientation fallback,
                         DisplayOrientation &out) {
    if (!finiteSample(s)) return false;

    float total = std::sqrt(s.ax * s.ax + s.ay * s.ay + s.az * s.az);
    if (total < kMinTotalG || total > kMaxTotalG) return false;

    float planar = std::sqrt(s.ax * s.ax + s.ay * s.ay);
    if (planar < kMinPlanarG) return false;

    float absX = std::fabs(s.ax);
    float absY = std::fabs(s.ay);

    if (absX > absY + kAxisHysteresisMargin) {
        out = s.ax >= 0.0f ? DisplayOrientation::Deg90 : DisplayOrientation::Deg270;
        return true;
    }

    if (absY > absX + kAxisHysteresisMargin) {
        out = s.ay >= 0.0f ? DisplayOrientation::Deg180 : DisplayOrientation::Deg0;
        return true;
    }

    out = fallback;
    return true;
}

}  // namespace

void OrientationController::begin(uint32_t nowMs, DisplayOrientation initial) {
    reset(nowMs, initial);
}

void OrientationController::reset(uint32_t nowMs, DisplayOrientation orientation) {
    committed_ = orientation;
    pending_ = orientation;
    pendingSinceMs_ = nowMs;
}

bool OrientationController::tick(uint32_t nowMs, OrientationSample sample) {
    DisplayOrientation candidate = committed_;
    if (!candidateFromSample(sample, committed_, candidate)) {
        pending_ = committed_;
        pendingSinceMs_ = nowMs;
        return false;
    }

    if (candidate == committed_) {
        pending_ = committed_;
        pendingSinceMs_ = nowMs;
        return false;
    }

    if (candidate != pending_) {
        pending_ = candidate;
        pendingSinceMs_ = nowMs;
        return false;
    }

    if ((uint32_t)(nowMs - pendingSinceMs_) < kDebounceMs) return false;

    committed_ = candidate;
    pending_ = candidate;
    pendingSinceMs_ = nowMs;
    return true;
}

}  // namespace stopwatch
