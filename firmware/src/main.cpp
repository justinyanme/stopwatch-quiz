// firmware/src/main.cpp
#include <Arduino.h>
#include <M5Unified.h>
#include "App.h"
#include "BleClient.h"
#include "Buttons.h"
#include "Power.h"
#include "Renderer.h"
#include "SnapshotCodec.h"
#include "SnapshotStore.h"
#include "Theme.h"
#include "Views/Overview.h"
#include "Views/Provider.h"

stopwatch::App         g_app;
stopwatch::BleClient   g_ble;
stopwatch::Renderer    g_renderer;
stopwatch::Power       g_power;
stopwatch::SnapshotStore g_store;
stopwatch::Snapshot    g_snap;

static void renderCurrent() {
    using namespace stopwatch;
    auto link = g_app.linkStatus();
    switch (g_app.currentView()) {
        case ViewId::Overview: views::drawOverview(g_renderer, g_snap, link); break;
        case ViewId::Codex:    views::drawProvider(g_renderer, g_snap, ProviderID::Codex,  link); break;
        case ViewId::Claude:   views::drawProvider(g_renderer, g_snap, ProviderID::Claude, link); break;
        case ViewId::Gemini:   views::drawProvider(g_renderer, g_snap, ProviderID::Gemini, link); break;
    }
    g_renderer.present();
}

// Paint a "Refreshing…" overlay on top of the current view before a blocking
// BLE fetch. Lets the user see something happened when they long-press KEYA.
static void renderRefreshingOverlay(const char *label) {
    using namespace stopwatch;
    auto link = g_app.linkStatus();
    switch (g_app.currentView()) {
        case ViewId::Overview: views::drawOverview(g_renderer, g_snap, link); break;
        case ViewId::Codex:    views::drawProvider(g_renderer, g_snap, ProviderID::Codex,  link); break;
        case ViewId::Claude:   views::drawProvider(g_renderer, g_snap, ProviderID::Claude, link); break;
        case ViewId::Gemini:   views::drawProvider(g_renderer, g_snap, ProviderID::Gemini, link); break;
    }
    auto &c = g_renderer.canvas();
    // Dim band across the center of the circle.
    c.fillRect(0, theme::kCenterY + theme::kRingOuterR / 2 - 14,
               M5.Display.width(), 28, 0x303030);
    c.setTextDatum(middle_center);
    c.setTextColor(theme::kTextPrimary);
    c.setFont(&fonts::Font2);
    c.drawString(label, theme::kCenterX, theme::kCenterY + theme::kRingOuterR / 2);
    g_renderer.present();
}

static bool fetchAndApply(uint8_t scope) {
    uint8_t buf[stopwatch::kSnapshotSize];
    size_t len = 0;
    auto rc = g_ble.fetch(scope, buf, sizeof(buf), len);
    switch (rc) {
        case stopwatch::BleClient::FetchResult::NoPeripheral:
            g_app.setLinkStatus(stopwatch::LinkStatus::NoBridge);  return false;
        case stopwatch::BleClient::FetchResult::ConnectFailed:
        case stopwatch::BleClient::FetchResult::ReadFailed:
            g_app.setLinkStatus(stopwatch::LinkStatus::LinkError); return false;
        case stopwatch::BleClient::FetchResult::Ok:
            break;
    }
    stopwatch::Snapshot snap;
    if (stopwatch::decodeSnapshot(buf, len, snap) != stopwatch::DecodeResult::Ok) {
        g_app.setLinkStatus(stopwatch::LinkStatus::LinkError);
        return false;
    }
    g_snap = snap;
    g_store.save(buf, len);
    g_app.setLinkStatus(stopwatch::LinkStatus::Connected);
    return true;
}

void setup() {
    delay(2000);  // Let USB-CDC enumerate before first Serial.print
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);   // never block waiting for the host CDC reader
    Serial.println("\n[stopwatch-fw] boot: setup() start");
    auto cfg = M5.config();
    // M5Unified's auto-detect doesn't recognize the StopWatch on our generic
    // ESP32-S3 board JSON; fall back to the StopWatch profile so display
    // power-on (via M5IOE1 I2C @ 0x4F) and pin map run correctly.
    cfg.fallback_board = m5::board_t::board_M5StopWatch;
    Serial.println("[stopwatch-fw] cfg ready, calling M5.begin");
    M5.begin(cfg);
    Serial.printf("[stopwatch-fw] M5.begin done; board=%d width=%d height=%d\n",
                  (int)M5.getBoard(), M5.Display.width(), M5.Display.height());
    Serial.println("[stopwatch-fw] starting renderer.begin (PSRAM sprite alloc)");
    g_renderer.begin();
    Serial.println("[stopwatch-fw] renderer ready; init store/app/power/ble");
    g_store.begin();
    g_app.begin();
    g_power.begin();
    g_ble.begin();
    Serial.println("[stopwatch-fw] all subsystems initialized");

    // Load last cached snapshot for instant first paint.
    uint8_t buf[stopwatch::kSnapshotSize];
    size_t len = 0;
    if (g_store.load(buf, sizeof(buf), len)) {
        stopwatch::decodeSnapshot(buf, len, g_snap);
        Serial.printf("[stopwatch-fw] loaded cached snapshot, %u bytes\n", (unsigned)len);
    } else {
        Serial.println("[stopwatch-fw] no cached snapshot");
    }
    renderCurrent();
    Serial.println("[stopwatch-fw] first render done; starting BLE fetch");

    // Initial fetch: show "Connecting…" overlay while the BLE round-trip blocks.
    renderRefreshingOverlay("Connecting\xE2\x80\xA6");
    bool ok = fetchAndApply(0x00);
    Serial.printf("[stopwatch-fw] BLE fetch returned %d (linkStatus=%d)\n",
                  (int)ok, (int)g_app.linkStatus());
    renderCurrent();
}

void loop() {
    using namespace stopwatch;
    auto ev = pollButtons();
    if (ev != ButtonEvent::None) {
        g_power.noteActivity();
        bool changed = g_app.handleEvent(ev);
        if (g_app.wantsRefresh()) {
            renderRefreshingOverlay("Refreshing\xE2\x80\xA6");
            fetchAndApply(0x00);
            g_app.clearRefreshRequest();
            changed = true;
        }
        if (g_app.wantsImmediateSleep()) {
            g_app.clearSleepRequest();
            g_power.enterLightSleep();
            renderCurrent();
        } else if (changed) {
            renderCurrent();
        }
    }
    if (g_power.shouldSleep()) {
        g_power.enterLightSleep();
        renderCurrent();
    }
    delay(20);
}
