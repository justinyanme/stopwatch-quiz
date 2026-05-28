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
    auto cfg = M5.config();
    M5.begin(cfg);
    g_renderer.begin();
    g_store.begin();
    g_app.begin();
    g_power.begin();
    g_ble.begin();

    // Load last cached snapshot for instant first paint.
    uint8_t buf[stopwatch::kSnapshotSize];
    size_t len = 0;
    if (g_store.load(buf, sizeof(buf), len)) {
        stopwatch::decodeSnapshot(buf, len, g_snap);
    }
    renderCurrent();

    // Fire a refresh in the background to get fresh data.
    fetchAndApply(0x00);
    renderCurrent();
}

void loop() {
    using namespace stopwatch;
    auto ev = pollButtons();
    if (ev != ButtonEvent::None) {
        g_power.noteActivity();
        bool changed = g_app.handleEvent(ev);
        if (g_app.wantsRefresh()) {
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
