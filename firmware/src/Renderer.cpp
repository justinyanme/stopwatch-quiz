// firmware/src/Renderer.cpp
#include "Renderer.h"
#include "Theme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

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
    // Track (full ring, no caps needed — it's a closed loop).
    sprite_.fillArc(cx, cy, radius, innerR, 0, 360, trackColor);
    if (fillFraction <= 0.0f) return;

    // Fill from 12 o'clock clockwise. LovyanGFX fillArc: 0° = 3 o'clock, +CW.
    float endDeg = 360.0f * fillFraction;
    sprite_.fillArc(cx, cy, radius, innerR, -90, -90 + (int)endDeg, fillColor);

    // Round end-caps: filled circles on the stroke centerline at each arc terminus.
    // Stroke 14 → cap radius 7. Skipped at fillFraction == 1.0 (caps would overlap meaninglessly).
    if (fillFraction >= 1.0f) return;
    float midR = radius - stroke / 2.0f;
    int capR = stroke / 2;
    // Start cap (12 o'clock).
    sprite_.fillCircle(cx, cy - (int)midR, capR, fillColor);
    // End cap.
    float endRad = (-90.0f + endDeg) * (float)M_PI / 180.0f;
    int ex = cx + (int)std::lround(midR * std::cos(endRad));
    int ey = cy + (int)std::lround(midR * std::sin(endRad));
    sprite_.fillCircle(ex, ey, capR, fillColor);
}

void Renderer::drawPill(int cx, int cy, const char *label, uint32_t color) {
    if (!label) return;
    sprite_.setFont(theme::kFontTitle);
    int textW = sprite_.textWidth(label);
    int h = sprite_.fontHeight() + 8;
    int w = textW + 28;
    // Tinted-dark chip: an opaque backdrop so the status reads even when it
    // lands over a ring, and a consistent shape across every view.
    sprite_.fillRoundRect(cx - w / 2, cy - h / 2, w, h, h / 2, theme::kRingTrack);
    sprite_.setTextDatum(middle_center);
    sprite_.setTextColor(color);
    sprite_.drawString(label, cx, cy);
}

}  // namespace stopwatch
