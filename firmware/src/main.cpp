// firmware/src/main.cpp
#include <Arduino.h>
#include <M5Unified.h>
#include "Renderer.h"
#include "Theme.h"
#include "Views/Overview.h"

stopwatch::Renderer g_renderer;
stopwatch::Snapshot g_snap;

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    g_renderer.begin();

    // Hard-coded sample snapshot until BLE is wired up.
    g_snap.versionMajor  = 1;
    g_snap.providerCount = 3;
    g_snap.providers[0]  = { stopwatch::ProviderID::Codex,  stopwatch::ProviderStatus::Ok,
                             72, 41, 1748467200, 1748538000, 1124, stopwatch::ProviderPlan::Plus };
    g_snap.providers[1]  = { stopwatch::ProviderID::Claude, stopwatch::ProviderStatus::Ok,
                             12, 37, 1748502000, 1748696400, std::nullopt, stopwatch::ProviderPlan::Pro };
    g_snap.providers[2]  = { stopwatch::ProviderID::Gemini, stopwatch::ProviderStatus::Ok,
                             8, std::nullopt, 1748476800, std::nullopt, std::nullopt, stopwatch::ProviderPlan::Free };

    stopwatch::views::drawOverview(g_renderer, g_snap);
    g_renderer.present();
}

void loop() {
    M5.update();
    delay(50);
}
