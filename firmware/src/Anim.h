// firmware/src/Anim.h
#pragma once
#include <cstdint>
#include "Ease.h"

namespace stopwatch {

/// One entrance-animation clock per view. It only tracks time; the pure
/// `motion::` shape functions below turn elapsed-ms into per-element progress.
/// No M5 deps, so it's unit-testable in `native` (like TouchScroll).
class Entrance {
public:
    /// A large elapsed value reported once settled, so every motion shape reads
    /// its final (clamped-to-1) value with no per-call "is it animating?" branch.
    static constexpr uint32_t kSettled = 0x3FFFFFFF;

    /// Begin an entrance lasting `durationMs` at `nowMs`. durationMs 0 → idle.
    void start(uint32_t nowMs, uint32_t durationMs) {
        startMs_    = nowMs;
        nowMs_      = nowMs;
        durationMs_ = durationMs;
        active_     = durationMs > 0;
    }

    /// Advance the frame clock; settles once the whole entrance has elapsed, so
    /// the final rendered frame shows true values.
    void tick(uint32_t nowMs) {
        nowMs_ = nowMs;
        if (active_ && (uint32_t)(nowMs - startMs_) >= durationMs_) active_ = false;
    }

    bool isAnimating() const { return active_; }
    void cancel() { active_ = false; }

    /// Ms since start while animating; `kSettled` once idle.
    uint32_t elapsed() const { return active_ ? (uint32_t)(nowMs_ - startMs_) : kSettled; }

private:
    uint32_t startMs_    = 0;
    uint32_t nowMs_      = 0;
    uint32_t durationMs_ = 0;
    bool     active_     = false;
};

/// Entrance motion shapes. Each is a pure function of elapsed ms returning a
/// 0..1 scale; multiply a target (ring fraction, bar height, hero value) by it.
namespace motion {

// ── Rings: outer ring (index 0) leads, inner rings cascade behind. ───────────
constexpr uint32_t kRingSweepMs   = 500;   // each ring's own sweep
constexpr uint32_t kRingStaggerMs = 100;   // gap between rings (outer leads)

constexpr uint32_t ringEntranceMs(int ringCount) {
    return ringCount <= 0 ? 0u : kRingSweepMs + (uint32_t)(ringCount - 1) * kRingStaggerMs;
}

inline float ringFill(uint32_t elapsedMs, int ringIndex) {
    int32_t local = (int32_t)elapsedMs - ringIndex * (int32_t)kRingStaggerMs;
    if (local <= 0)                     return 0.0f;
    if (local >= (int32_t)kRingSweepMs) return 1.0f;
    return ease::outExpo((float)local / (float)kRingSweepMs);
}

// ── Spend charts: bars rise left→right; hero money counts up. ─────────────────
constexpr uint32_t kBarStaggerSpanMs = 200;  // spread of bar start times across width
constexpr uint32_t kBarRiseMs        = 260;  // each bar's own rise
constexpr uint32_t kCountUpMs        = 480;  // hero money count-up
constexpr uint32_t kSpendEntranceMs  = 520;  // outlasts the slowest element

/// Height scale for bar `barIndex` of `barCount`, left bars leading.
inline float barRise(uint32_t elapsedMs, int barIndex, int barCount) {
    float frac = barCount > 1 ? (float)barIndex / (float)(barCount - 1) : 0.0f;
    int32_t local = (int32_t)elapsedMs - (int32_t)(frac * (float)kBarStaggerSpanMs);
    if (local <= 0)                   return 0.0f;
    if (local >= (int32_t)kBarRiseMs) return 1.0f;
    return ease::outExpo((float)local / (float)kBarRiseMs);
}

/// Eased fraction for the hero number to count up by.
inline float countUp(uint32_t elapsedMs) {
    if (elapsedMs >= kCountUpMs) return 1.0f;
    return ease::outExpo((float)elapsedMs / (float)kCountUpMs);
}

}  // namespace motion
}  // namespace stopwatch
