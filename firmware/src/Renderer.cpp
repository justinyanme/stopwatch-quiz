// firmware/src/Renderer.cpp
#include "Renderer.h"
#include <algorithm>

namespace stopwatch {

void Renderer::begin() {
    sprite_.setColorDepth(16);
    sprite_.setPsram(true);
    sprite_.createSprite(M5.Display.width(), M5.Display.height());
    sprite_.fillSprite(0x000000);
}

void Renderer::clear(uint32_t color) {
    sprite_.fillSprite(color);
}

void Renderer::present() {
    sprite_.pushSprite(0, 0);
}

void Renderer::drawRing(int cx, int cy, int radius, int stroke,
                        uint32_t trackColor, uint32_t fillColor,
                        float fillFraction) {
    fillFraction = std::clamp(fillFraction, 0.0f, 1.0f);
    int innerR = radius - stroke;
    // Track (full ring).
    sprite_.fillArc(cx, cy, radius, innerR, 0, 360, trackColor);
    // Fill from 12 o'clock clockwise.
    if (fillFraction > 0.0f) {
        float endDeg = 360.0f * fillFraction;
        // LovyanGFX fillArc takes (startAngle, endAngle) in degrees with 0 = 3 o'clock.
        // Map "12 o'clock clockwise" → start at -90°, sweep forward.
        sprite_.fillArc(cx, cy, radius, innerR, -90, -90 + (int)endDeg, fillColor);
    }
}

}  // namespace stopwatch
