#pragma once
#include <cstdint>

namespace stopwatch {

/// Pure vertical scroll model: drag tracking + clamped offset + decaying
/// momentum. No hardware/M5 dependencies so it's unit-testable in `native`.
/// Units are pixels; +offset scrolls content up (reveals lower rows).
class TouchScroll {
public:
    void setBounds(int contentHeight, int viewHeight);
    void reset();                       // offset = 0, velocity = 0

    void onPress(int y);
    void onMove(int y);                 // drag: offset follows finger
    void onRelease();                   // captures fling velocity from recent moves
    void tick(int dtMs);                // applies + decays momentum
    bool isResting() const { return !dragging_ && velocity_ == 0.0f; }

    int  offset() const { return offset_; }
    int  maxOffset() const { return max_ > 0 ? max_ : 0; }

private:
    void clamp();
    int   contentH_ = 0, viewH_ = 0, max_ = 0;
    int   offset_ = 0;
    bool  dragging_ = false;
    int   lastY_ = 0, prevY_ = 0;
    float velocity_ = 0.0f;             // px per tick
};

}  // namespace stopwatch
