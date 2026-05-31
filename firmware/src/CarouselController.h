#pragma once
#include <cstdint>
#include "CarouselSettings.h"

namespace stopwatch {

struct CarouselContext {
    bool inSettings = false;
    bool inBalanceDetail = false;
    bool touchActive = false;
    bool loading = false;
    bool transitionActive = false;
};

class CarouselController {
public:
    void begin(uint32_t nowMs, const CarouselSettings &) {
        lastAdvanceMs_ = nowMs;
        lastUserActivityMs_ = nowMs;
        hasUserActivity_ = false;
    }

    void noteUserActivity(uint32_t nowMs) {
        lastUserActivityMs_ = nowMs;
        hasUserActivity_ = true;
    }

    void recordAdvance(uint32_t nowMs) {
        lastAdvanceMs_ = nowMs;
    }

    void recordManualViewChange(uint32_t nowMs) {
        lastAdvanceMs_ = nowMs;
        noteUserActivity(nowMs);
    }

    bool shouldAdvance(uint32_t nowMs, const CarouselSettings &settings,
                       const CarouselContext &ctx) const {
        if (!settings.autoplayEnabled) return false;
        if (ctx.inSettings || ctx.inBalanceDetail || ctx.touchActive ||
            ctx.loading || ctx.transitionActive) {
            return false;
        }
        if (hasUserActivity_ &&
            (uint32_t)(nowMs - lastUserActivityMs_) <
                (uint32_t)settings.resumeSeconds * 1000u) {
            return false;
        }
        return (uint32_t)(nowMs - lastAdvanceMs_) >=
               (uint32_t)settings.intervalSeconds * 1000u;
    }

private:
    uint32_t lastAdvanceMs_ = 0;
    uint32_t lastUserActivityMs_ = 0;
    bool hasUserActivity_ = false;
};

}  // namespace stopwatch
