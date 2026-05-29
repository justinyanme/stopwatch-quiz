#include "TouchScroll.h"

namespace stopwatch {

void TouchScroll::setBounds(int contentHeight, int viewHeight) {
    contentH_ = contentHeight; viewH_ = viewHeight;
    max_ = contentHeight - viewHeight;
    if (max_ < 0) max_ = 0;
    clamp();
}

void TouchScroll::reset() { offset_ = 0; velocity_ = 0.0f; dragging_ = false; }

void TouchScroll::onPress(int y) {
    dragging_ = true; velocity_ = 0.0f; lastY_ = y; prevY_ = y;
}

void TouchScroll::onMove(int y) {
    if (!dragging_) return;
    offset_ += (lastY_ - y);
    prevY_ = lastY_;
    lastY_ = y;
    clamp();
}

void TouchScroll::onRelease() {
    if (!dragging_) return;
    velocity_ = (float)(prevY_ - lastY_);   // last delta = fling speed (px/tick)
    dragging_ = false;
}

void TouchScroll::tick(int dtMs) {
    if (dragging_ || velocity_ == 0.0f) return;
    offset_ += (int)velocity_;
    clamp();
    velocity_ *= 0.85f;                      // friction
    if (offset_ <= 0 || offset_ >= max_) velocity_ = 0.0f;   // stop at edges
    if (velocity_ > -1.0f && velocity_ < 1.0f) velocity_ = 0.0f;
}

void TouchScroll::clamp() {
    if (offset_ < 0) offset_ = 0;
    if (offset_ > max_) offset_ = max_;
}

}  // namespace stopwatch
