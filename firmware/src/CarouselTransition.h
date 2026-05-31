#pragma once
#include <cstdint>
#include "Anim.h"
#include "CarouselSettings.h"

namespace stopwatch {

class CarouselTransition {
public:
    void start(uint32_t nowMs, CarouselMotionMode mode) {
        mode_ = mode;
        startMs_ = nowMs;
        nowMs_ = nowMs;
        switched_ = mode == CarouselMotionMode::Fade || mode == CarouselMotionMode::Instant;
        pendingSwitch_ = switched_;
        active_ = mode != CarouselMotionMode::Instant;
    }

    void tick(uint32_t nowMs) {
        nowMs_ = nowMs;
        if (!active_) return;
        if (mode_ == CarouselMotionMode::Iris &&
            !switched_ &&
            elapsed() >= motion::kIrisSwitchMs) {
            switched_ = true;
            pendingSwitch_ = true;
        }
        if (elapsed() >= durationMs()) active_ = false;
    }

    bool isAnimating() const { return active_; }
    bool hasSwitched() const { return switched_; }

    bool consumeSwitch() {
        bool out = pendingSwitch_;
        pendingSwitch_ = false;
        return out;
    }

    CarouselMotionMode mode() const { return mode_; }
    uint32_t elapsed() const { return (uint32_t)(nowMs_ - startMs_); }

    uint32_t durationMs() const {
        switch (mode_) {
            case CarouselMotionMode::Iris:    return motion::kIrisTransitionMs;
            case CarouselMotionMode::Fade:    return motion::kFadeMs;
            case CarouselMotionMode::Instant: return 0;
        }
        return 0;
    }

private:
    CarouselMotionMode mode_ = CarouselMotionMode::Iris;
    uint32_t startMs_ = 0;
    uint32_t nowMs_ = 0;
    bool active_ = false;
    bool switched_ = false;
    bool pendingSwitch_ = false;
};

}  // namespace stopwatch
