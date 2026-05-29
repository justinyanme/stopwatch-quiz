// firmware/src/main.cpp
#include <Arduino.h>
#include <M5Unified.h>
#include "soc/rtc_cntl_reg.h"
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
#include "Views/Spend.h"
#include "CostCodec.h"
#include "BalanceCodec.h"
#include "BalanceFormat.h"
#include "TouchScroll.h"
#include "Views/Balances.h"

stopwatch::App         g_app;
stopwatch::BleClient   g_ble;
stopwatch::Renderer    g_renderer;
stopwatch::Power       g_power;
stopwatch::SnapshotStore g_store;
stopwatch::Snapshot    g_snap;
stopwatch::CostSnapshot g_cost;
bool                    g_costLoaded = false;
stopwatch::BalanceSnapshot g_balance;
bool                       g_balanceLoaded = false;
stopwatch::TouchScroll     g_balScroll;

static void drawCurrentView() {
    using namespace stopwatch;
    auto link = g_app.linkStatus();
    switch (g_app.currentView()) {
        case ViewId::Overview:   views::drawOverview(g_renderer, g_snap, link); break;
        case ViewId::TotalSpend: views::drawTotalSpend(g_renderer, g_cost, link); break;
        case ViewId::Codex:      views::drawProvider(g_renderer, g_snap, ProviderID::Codex,  link); break;
        case ViewId::CodexCost:  views::drawProviderCost(g_renderer, g_cost, ProviderID::Codex, link); break;
        case ViewId::Claude:     views::drawProvider(g_renderer, g_snap, ProviderID::Claude, link); break;
        case ViewId::ClaudeCost: views::drawProviderCost(g_renderer, g_cost, ProviderID::Claude, link); break;
        case ViewId::Gemini:     views::drawProvider(g_renderer, g_snap, ProviderID::Gemini, link); break;
        case ViewId::Balances: {
            int contentH = views::drawBalances(g_renderer, g_balance, link, g_balScroll.offset());
            g_balScroll.setBounds(contentH, views::balancesViewportHeight());
            break;
        }
    }
}

static void renderCurrent() {
    drawCurrentView();
    g_renderer.present();
}

static void renderRefreshingOverlay(const char *label) {
    using namespace stopwatch;
    drawCurrentView();
    auto &c = g_renderer.canvas();
    c.fillRect(0, theme::kCenterY + theme::kRingOuterR / 2 - 18,
               M5.Display.width(), 36, 0x303030);
    c.setTextDatum(middle_center);
    c.setTextColor(theme::kTextPrimary);
    c.setFont(theme::kFontTitle);
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
    g_store.save("snap", buf, len);
    g_app.setLinkStatus(stopwatch::LinkStatus::Connected);
    return true;
}

static bool fetchCostAndApply() {
    uint8_t buf[stopwatch::kCostSnapshotMaxSize];
    size_t len = 0;
    if (g_ble.fetchCost(buf, sizeof(buf), len) != stopwatch::BleClient::FetchResult::Ok) {
        return false;
    }
    stopwatch::CostSnapshot cs;
    if (stopwatch::decodeCostSnapshot(buf, len, cs) != stopwatch::CostDecodeResult::Ok) {
        return false;
    }
    g_cost = cs;
    g_store.save("cost", buf, len);
    g_costLoaded = true;
    return true;
}

// On first entry to any spend screen this wake-session, pull cost once.
static void ensureCostLoaded() {
    using namespace stopwatch;
    if (!isSpendView(g_app.currentView()) || g_costLoaded) return;
    renderRefreshingOverlay("Loading cost\xE2\x80\xA6");
    fetchCostAndApply();
}

static bool fetchBalancesAndApply() {
    uint8_t buf[stopwatch::kBalanceSnapshotMaxSize];
    size_t len = 0;
    if (g_ble.fetchBalances(buf, sizeof(buf), len) != stopwatch::BleClient::FetchResult::Ok) return false;
    stopwatch::BalanceSnapshot bs;
    if (stopwatch::decodeBalanceSnapshot(buf, len, bs) != stopwatch::BalanceDecodeResult::Ok) return false;
    g_balance = bs;
    g_store.save("bal", buf, len);
    g_balanceLoaded = true;
    return true;
}

// On first entry to the Balances screen this wake-session, pull balances once.
static void ensureBalanceLoaded() {
    using namespace stopwatch;
    if (!isBalanceView(g_app.currentView()) || g_balanceLoaded) return;
    renderRefreshingOverlay("Loading balances\xE2\x80\xA6");
    fetchBalancesAndApply();
}

static bool applyRefreshRequest(const char *label) {
    if (!g_app.wantsRefresh()) return false;
    renderRefreshingOverlay(label);
    if (stopwatch::isBalanceView(g_app.currentView())) {
        fetchBalancesAndApply();
    } else {
        fetchAndApply(0x00);
    }
    g_app.clearRefreshRequest();
    return true;
}

static void enterSleepAndRefreshOnWake() {
    g_power.enterLightSleep();
    g_app.noteWakeFromSleep();
    g_costLoaded = false;
    g_balanceLoaded = false;
    g_balScroll.reset();
    applyRefreshRequest("Refreshing\xE2\x80\xA6");
    ensureCostLoaded();
    renderCurrent();
}

// USB-CDC RX handler: looks for the magic flash trigger. When matched, reboots
// into the ROM download bootloader so `pio run -t upload` can flash without a
// manual BOOT-button long-press. Streaming match across chunks of available().
static void onUsbCdcRx(void *, esp_event_base_t, int32_t, void *) {
    static constexpr const char kMagic[] = "STOPWATCH-DL\n";
    static constexpr int kMagicLen = sizeof(kMagic) - 1;
    static int matched = 0;
    while (Serial.available() > 0) {
        int b = Serial.read();
        if (b < 0) break;
        if (b == kMagic[matched]) {
            ++matched;
            if (matched == kMagicLen) {
                Serial.println("[stopwatch-fw] flash trigger received; entering download mode");
                Serial.flush();
                REG_WRITE(RTC_CNTL_OPTION1_REG, 0x1);   // force next reset into ROM dl
                esp_restart();
            }
        } else {
            // Reset, but accept this byte as a potential new start.
            matched = (b == kMagic[0]) ? 1 : 0;
        }
    }
}

void setup() {
    delay(2000);  // Let USB-CDC enumerate before first Serial.print
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);   // never block waiting for the host CDC reader
    Serial.onEvent(ARDUINO_HW_CDC_RX_EVENT, onUsbCdcRx);
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

    // Load last cached snapshots for instant first paint.
    uint8_t buf[stopwatch::kSnapshotSize];
    size_t len = 0;
    if (g_store.load("snap", buf, sizeof(buf), len)) {
        stopwatch::decodeSnapshot(buf, len, g_snap);
        Serial.printf("[stopwatch-fw] loaded cached snapshot, %u bytes\n", (unsigned)len);
    } else {
        Serial.println("[stopwatch-fw] no cached snapshot");
    }
    uint8_t cbuf[stopwatch::kCostSnapshotMaxSize];
    size_t clen = 0;
    if (g_store.load("cost", cbuf, sizeof(cbuf), clen)) {
        stopwatch::decodeCostSnapshot(cbuf, clen, g_cost);  // shown until first fresh fetch
    }
    uint8_t bbuf[stopwatch::kBalanceSnapshotMaxSize];
    size_t blen = 0;
    if (g_store.load("bal", bbuf, sizeof(bbuf), blen)) {
        stopwatch::decodeBalanceSnapshot(bbuf, blen, g_balance);
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
        if (applyRefreshRequest("Refreshing\xE2\x80\xA6")) changed = true;
        if (g_app.wantsImmediateSleep()) {
            g_app.clearSleepRequest();
            enterSleepAndRefreshOnWake();
        } else if (changed) {
            if (!isBalanceView(g_app.currentView())) g_balScroll.reset();
            ensureCostLoaded();
            ensureBalanceLoaded();
            renderCurrent();
        }
    }

    // Touch: only meaningful on the Balances screen; drives the scroll model.
    if (isBalanceView(g_app.currentView())) {
        M5.update();
        auto t = M5.Touch.getDetail();
        if (t.isPressed()) {
            g_power.noteActivity();
            if (t.wasPressed()) g_balScroll.onPress(t.y);
            else                g_balScroll.onMove(t.y);
            renderCurrent();
        } else if (t.wasReleased()) {
            g_balScroll.onRelease();
        }
        if (!g_balScroll.isResting()) {
            g_balScroll.tick(20);
            renderCurrent();
        }
    }

    if (g_power.shouldSleep()) enterSleepAndRefreshOnWake();
    delay(20);
}
