// firmware/src/Renderer.h
#pragma once
#include <M5Unified.h>
#include "OrientationController.h"

namespace stopwatch {

/// Owns the full-screen sprite that all views draw to. Single pushSprite per frame.
class Renderer {
public:
    void begin();
    M5Canvas &canvas() { return sprite_; }
    void present();   // pushSprite to display
    void clear(uint32_t color = 0x000000);
    void setOrientation(DisplayOrientation orientation);
    DisplayOrientation orientation() const { return orientation_; }

    /// Draws an arc from -90° (top of circle) clockwise by `fillFraction` of 360°.
    /// fillFraction in [0.0, 1.0]. Track is drawn underneath.
    void drawRing(int cx, int cy, int radius, int stroke,
                  uint32_t trackColor, uint32_t fillColor,
                  float fillFraction);

    /// Draws a status chip (rounded fill + colored label) centered at (cx, cy).
    /// Label tier (Font4) so it stays readable even where it sits over a ring.
    /// No-op if label is null.
    void drawPill(int cx, int cy, const char *label, uint32_t color);

private:
    DisplayOrientation orientation_ = DisplayOrientation::Deg0;
    M5Canvas sprite_{&M5.Display};
};

}  // namespace stopwatch
