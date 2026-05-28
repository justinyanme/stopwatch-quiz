// firmware/src/Renderer.h
#pragma once
#include <M5Unified.h>

namespace stopwatch {

/// Owns the full-screen sprite that all views draw to. Single pushSprite per frame.
class Renderer {
public:
    void begin();
    M5Canvas &canvas() { return sprite_; }
    void present();   // pushSprite to display
    void clear(uint32_t color = 0x000000);

    /// Draws an arc from -90° (top of circle) clockwise by `fillFraction` of 360°.
    /// fillFraction in [0.0, 1.0]. Track is drawn underneath.
    void drawRing(int cx, int cy, int radius, int stroke,
                  uint32_t trackColor, uint32_t fillColor,
                  float fillFraction);

    /// Draws a small "● label" pill at (cx, baselineY). No-op if label is null.
    void drawPill(int cx, int baselineY, const char *label, uint32_t color);

private:
    M5Canvas sprite_{&M5.Display};
};

}  // namespace stopwatch
