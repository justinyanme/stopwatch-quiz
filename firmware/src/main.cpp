// firmware/src/main.cpp
#include <Arduino.h>
#include <M5Unified.h>
#include "Renderer.h"
#include "Theme.h"

stopwatch::Renderer g_renderer;

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    g_renderer.begin();
    g_renderer.clear(stopwatch::theme::kBackground);
    g_renderer.drawRing(stopwatch::theme::kCenterX,
                        stopwatch::theme::kCenterY,
                        stopwatch::theme::kRingOuterR,
                        stopwatch::theme::kRingStroke,
                        stopwatch::theme::kRingTrack,
                        stopwatch::theme::kCodex,
                        0.72f);
    g_renderer.present();
}

void loop() {
    M5.update();
    delay(50);
}
