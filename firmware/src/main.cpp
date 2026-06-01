// firmware/src/main.cpp
#include <Arduino.h>
#include <M5Unified.h>
#include "soc/rtc_cntl_reg.h"
#include "App.h"
#include "BleClient.h"
#include "Buttons.h"
#include "CarouselController.h"
#include "CarouselSettings.h"
#include "CarouselTransition.h"
#include "OrientationController.h"
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
#include "Anim.h"
#include "Views/Balances.h"
#include "Views/CarouselSettings.h"
#include "Views/ProviderUsage.h"
#include "UsageCodec.h"

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
static int  g_balPressY = 0;     // finger Y at touch-down, for tap-vs-drag detection
static bool g_balMoved  = false; // set once the finger moves beyond the tap threshold
static bool g_touchActive = false;
stopwatch::Entrance        g_entrance;
static bool                g_entranceExtendsWake = false;
stopwatch::UsageSnapshot   g_usage;
bool                       g_usageLoaded = false;
stopwatch::CarouselSettings   g_carouselSettings;
stopwatch::CarouselController g_carousel;
stopwatch::CarouselTransition g_transition;
static bool                   g_loading = false;
stopwatch::OrientationController g_orientation;
static uint32_t                  g_nextOrientationPollMs = 0;
static uint8_t                   g_orientationReadFailures = 0;
static bool                      g_orientationRuntimeDisabled = false;
static constexpr uint32_t        kOrientationPollMs = 50;
static constexpr uint8_t         kMaxOrientationReadFailures = 5;

struct LoadingScope {
    LoadingScope() { g_loading = true; }
    ~LoadingScope() { g_loading = false; }
};

static void drawCurrentView() {
    using namespace stopwatch;
    auto link = g_app.linkStatus();
    if (g_app.inCarouselSettings()) {
        views::drawCarouselSettings(g_renderer, g_carouselSettings, g_app.carouselSettingRow());
        return;
    }
    switch (g_app.currentView()) {
        case ViewId::Overview:   views::drawOverview(g_renderer, g_snap, link, g_entrance); break;
        case ViewId::TotalSpend: views::drawTotalSpend(g_renderer, g_cost, link, g_entrance); break;
        case ViewId::Codex:      views::drawProvider(g_renderer, g_snap, ProviderID::Codex,  link, g_entrance); break;
        case ViewId::CodexCost:  views::drawProviderCost(g_renderer, g_cost, ProviderID::Codex, link, g_entrance); break;
        case ViewId::Claude:     views::drawProvider(g_renderer, g_snap, ProviderID::Claude, link, g_entrance); break;
        case ViewId::ClaudeCost: views::drawProviderCost(g_renderer, g_cost, ProviderID::Claude, link, g_entrance); break;
        case ViewId::Gemini:     views::drawProvider(g_renderer, g_snap, ProviderID::Gemini, link, g_entrance); break;
        case ViewId::Balances: {
            if (g_app.inBalanceDetail() &&
                g_app.balanceDetailIndex() < g_balance.recordCount) {
                const auto &bal = g_balance.records[g_app.balanceDetailIndex()];
                views::drawProviderUsage(g_renderer, bal, g_usage, g_app.usageMetric(),
                                         g_app.linkStatus(), g_entrance);
            } else {
                int contentH = views::drawBalances(g_renderer, g_balance, link, g_balScroll.offset());
                g_balScroll.setBounds(contentH, views::balancesViewportHeight());
            }
            break;
        }
    }
}

static void renderCurrent() {
    drawCurrentView();
    g_renderer.present();
}

static uint32_t accentForView(stopwatch::ViewId v) {
    using namespace stopwatch;
    switch (v) {
        case ViewId::Codex:
        case ViewId::CodexCost:  return theme::kCodex;
        case ViewId::Claude:
        case ViewId::ClaudeCost: return theme::kClaude;
        case ViewId::Gemini:     return theme::kGemini;
        case ViewId::Balances:   return theme::kCodex;
        case ViewId::Overview:
        case ViewId::TotalSpend: return theme::kTextMuted;
    }
    return theme::kTextMuted;
}

static void drawIrisMask(float visibleFraction, uint32_t accent, float halo) {
    using namespace stopwatch;
    auto &c = g_renderer.canvas();
    int maxR = theme::kRingOuterR + 42;
    int visibleR = (int)(visibleFraction * (float)maxR + 0.5f);
    if (visibleR < maxR) {
        c.fillArc(theme::kCenterX, theme::kCenterY, maxR, visibleR, 0, 360, theme::kBackground);
    }
    if (halo > 0.0f) {
        int haloR = visibleR + 10 + (int)(halo * 12.0f);
        c.drawCircle(theme::kCenterX, theme::kCenterY, haloR, accent);
        c.drawCircle(theme::kCenterX, theme::kCenterY, haloR + 1, accent);
    }
}

static void drawFadeMask(float revealFraction) {
    if (revealFraction >= 1.0f) return;
    if (revealFraction <= 0.0f) {
        g_renderer.canvas().fillRect(0, 0, M5.Display.width(), M5.Display.height(),
                                     stopwatch::theme::kBackground);
        return;
    }
    int coveredColumns = 4 - (int)(revealFraction * 4.0f + 0.5f);
    if (coveredColumns <= 0) return;
    if (coveredColumns > 4) coveredColumns = 4;
    auto &c = g_renderer.canvas();
    for (int x = 0; x < M5.Display.width(); x += 4) {
        c.fillRect(x, 0, coveredColumns, M5.Display.height(), stopwatch::theme::kBackground);
    }
}

static void renderTransitionFrame() {
    using namespace stopwatch;
    drawCurrentView();
    if (g_transition.mode() == CarouselMotionMode::Iris) {
        uint32_t e = g_transition.elapsed();
        float visible = 0.0f;
        if (!g_transition.hasSwitched()) {
            visible = motion::irisCover(e);
        } else {
            uint32_t local = e > motion::kIrisSwitchMs ? e - motion::kIrisSwitchMs : 0;
            visible = motion::irisReveal(local);
        }
        drawIrisMask(visible, accentForView(g_app.currentView()), motion::irisHalo(e));
    } else if (g_transition.mode() == CarouselMotionMode::Fade) {
        drawFadeMask(motion::fadeReveal(g_transition.elapsed()));
    }
    g_renderer.present();
}

static void resetOrientationRuntime(uint32_t now) {
    g_orientation.reset(now, stopwatch::DisplayOrientation::Deg0);
    g_renderer.setOrientation(stopwatch::DisplayOrientation::Deg0);
    g_nextOrientationPollMs = now;
    g_orientationReadFailures = 0;
    g_orientationRuntimeDisabled = false;
}

static void renderAfterOrientationCommit() {
    if (g_transition.isAnimating()) {
        renderTransitionFrame();
    } else {
        renderCurrent();
    }
}

static void pollOrientation(uint32_t now) {
    if (!g_carouselSettings.uprightEnabled || g_orientationRuntimeDisabled) return;
    if ((int32_t)(now - g_nextOrientationPollMs) < 0) return;
    g_nextOrientationPollMs = now + kOrientationPollMs;

    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    if (!M5.Imu.getAccel(&ax, &ay, &az)) {
        if (g_orientationReadFailures < 255) ++g_orientationReadFailures;
        if (g_orientationReadFailures >= kMaxOrientationReadFailures) {
            g_orientationRuntimeDisabled = true;
            Serial.println("[stopwatch-fw] IMU accel unavailable; upright disabled for this boot");
        }
        return;
    }

    g_orientationReadFailures = 0;
    stopwatch::OrientationSample sample{ax, ay, az};
    if (g_orientation.tick(now, sample)) {
        g_renderer.setOrientation(g_orientation.committed());
        renderAfterOrientationCommit();
    }
}

// Entrance length per view: ring views charge up outer→inner; spend views grow
// their chart and count the hero number up; viewless screens (Balances) get none.
static uint32_t entranceDurationForView(stopwatch::ViewId v) {
    using namespace stopwatch;
    switch (v) {
        case ViewId::Overview:   return motion::ringEntranceMs(3);   // Codex, Claude, Gemini
        case ViewId::Codex:
        case ViewId::Claude:
        case ViewId::Gemini:     return motion::ringEntranceMs(2);   // session + week
        case ViewId::TotalSpend:
        case ViewId::CodexCost:
        case ViewId::ClaudeCost: return motion::kSpendEntranceMs;
        case ViewId::Balances:   return 0;
    }
    return 0;
}

// Enter the current view: start the entrance if it animates (the first frame
// draws empty/zeroed), otherwise just paint it. Always renders one frame now;
// loop() drives the remaining frames while g_entrance is animating.
static void startViewAnim() {
    uint32_t dur = entranceDurationForView(g_app.currentView());
    g_entranceExtendsWake = true;
    if (dur > 0) g_entrance.start(millis(), dur);
    else         g_entrance.cancel();
    renderCurrent();
}

static void renderRefreshingOverlay(const char *label) {
    using namespace stopwatch;
    g_loading = true;
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
    LoadingScope loading;
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
    LoadingScope loading;
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
    LoadingScope loading;
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

static bool fetchUsageAndApply() {
    LoadingScope loading;
    uint8_t buf[stopwatch::kUsageSnapshotMaxSize];
    size_t len = 0;
    if (g_ble.fetchUsage(buf, sizeof(buf), len) != stopwatch::BleClient::FetchResult::Ok) return false;
    stopwatch::UsageSnapshot us;
    if (stopwatch::decodeUsageSnapshot(buf, len, us) != stopwatch::UsageDecodeResult::Ok) return false;
    if (!us.shouldCache()) return false;
    g_usage = us;
    g_store.save("usage", buf, len);
    g_usageLoaded = true;
    return true;
}

// On first entry to a balance detail this wake-session, pull usage once.
static void ensureUsageLoaded() {
    if (g_usageLoaded) return;
    renderRefreshingOverlay("Loading usage\xE2\x80\xA6");
    fetchUsageAndApply();
}

// On first entry to the Balances screen this wake-session, pull balances once.
static void ensureBalanceLoaded() {
    using namespace stopwatch;
    if (!isBalanceView(g_app.currentView()) || g_balanceLoaded) return;
    renderRefreshingOverlay("Loading balances\xE2\x80\xA6");
    fetchBalancesAndApply();
}

static void completeAutoplayAdvance(bool animateEntrance) {
    using namespace stopwatch;
    g_app.handleEvent(ButtonEvent::KeyBShort);
    if (!isBalanceView(g_app.currentView())) g_balScroll.reset();
    ensureCostLoaded();
    ensureBalanceLoaded();
    if (animateEntrance) {
        uint32_t dur = entranceDurationForView(g_app.currentView());
        g_entranceExtendsWake = false;
        if (dur > 0) g_entrance.start(millis(), dur);
        else         g_entrance.cancel();
    } else {
        g_entranceExtendsWake = false;
        g_entrance.cancel();
    }
    if (!animateEntrance) renderCurrent();
    g_carousel.recordAdvance(millis());
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
    g_loading = false;
    return true;
}

static void enterSleepAndRefreshOnWake() {
    g_transition.cancel();
    g_power.enterLightSleep();
    resetOrientationRuntime(millis());
    g_app.noteWakeFromSleep();
    g_costLoaded = false;
    g_balanceLoaded = false;
    g_usageLoaded = false;
    g_balScroll.reset();
    applyRefreshRequest("Refreshing\xE2\x80\xA6");
    ensureCostLoaded();
    g_carousel.recordWake(millis());
    startViewAnim();   // play the entrance on wake
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
    g_orientation.begin(millis(), stopwatch::DisplayOrientation::Deg0);
    g_renderer.setOrientation(stopwatch::DisplayOrientation::Deg0);
    Serial.println("[stopwatch-fw] renderer ready; init store/app/power/ble");
    g_store.begin();
    if (!g_store.loadCarouselSettings(g_carouselSettings)) {
        g_carouselSettings = stopwatch::CarouselSettings::defaults();
    }
    g_carousel.begin(millis(), g_carouselSettings);
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
    uint8_t ubuf[stopwatch::kUsageSnapshotMaxSize];
    size_t ulen = 0;
    if (g_store.load("usage", ubuf, sizeof(ubuf), ulen)) {
        stopwatch::decodeUsageSnapshot(ubuf, ulen, g_usage);
    }
    renderCurrent();
    Serial.println("[stopwatch-fw] first render done; starting BLE fetch");

    // Initial fetch: show "Connecting…" overlay while the BLE round-trip blocks.
    renderRefreshingOverlay("Connecting\xE2\x80\xA6");
    bool ok = fetchAndApply(0x00);
    Serial.printf("[stopwatch-fw] BLE fetch returned %d (linkStatus=%d)\n",
                  (int)ok, (int)g_app.linkStatus());
    startViewAnim();   // first paint plays the entrance
}

void loop() {
    using namespace stopwatch;
    M5.update();
    auto ev = pollButtons();
    if (ev != ButtonEvent::None) {
        g_power.noteActivity();
        g_transition.cancel();
        bool wasInSettings = g_app.inCarouselSettings();
        bool wasUprightEnabled = g_carouselSettings.uprightEnabled;
        bool changed = g_app.handleEvent(ev, g_carouselSettings);
        if (wasUprightEnabled != g_carouselSettings.uprightEnabled) {
            resetOrientationRuntime(millis());
        }
        if (wasInSettings && !g_app.inCarouselSettings()) {
            g_store.saveCarouselSettings(g_carouselSettings);
        }
        g_carousel.noteUserActivity(millis());
        if (applyRefreshRequest("Refreshing\xE2\x80\xA6")) changed = true;
        if (g_app.wantsImmediateSleep()) {
            g_app.clearSleepRequest();
            enterSleepAndRefreshOnWake();
        } else if (changed && g_app.inCarouselSettings()) {
            renderCurrent();
        } else if (changed) {
            g_carousel.recordManualViewChange(millis());
            if (!isBalanceView(g_app.currentView())) g_balScroll.reset();
            ensureCostLoaded();
            ensureBalanceLoaded();
            if (g_app.inBalanceDetail()) {
                ensureUsageLoaded();
                g_entranceExtendsWake = true;
                g_entrance.start(millis(), stopwatch::motion::kSpendEntranceMs);
                renderCurrent();
            } else {
                startViewAnim();
            }
        }
    }

    pollOrientation(millis());

    if (g_transition.isAnimating()) {
        g_transition.tick(millis());
        if (g_transition.consumeSwitch()) {
            completeAutoplayAdvance(true);
        }
        renderTransitionFrame();
    }

    // View entrance: keep redrawing each frame until it settles.
    if (!g_transition.isAnimating() && g_entrance.isAnimating()) {
        g_entrance.tick(millis());
        renderCurrent();
        if (g_entranceExtendsWake) g_power.noteActivity();   // user/wake entrances hold off sleep
    }

    // Touch: only meaningful on the Balances screen; drives the scroll model.
    g_touchActive = false;
    if (isBalanceView(g_app.currentView()) && !g_app.inCarouselSettings()) {
        auto t = M5.Touch.getDetail();
        g_touchActive = t.isPressed();
        if (g_app.inBalanceDetail()) {
            // In detail: ignore touch; only buttons act. Entrance animates via the block below.
        } else {
            if (t.isPressed()) {
                g_power.noteActivity();
                g_carousel.noteUserActivity(millis());
                if (t.wasPressed()) {
                    g_balScroll.onPress(t.y);
                    g_balPressY = t.y;
                    g_balMoved  = false;
                } else {
                    g_balScroll.onMove(t.y);
                    if (abs(t.y - g_balPressY) > 8) g_balMoved = true;
                }
                renderCurrent();
            } else if (t.wasReleased()) {
                g_balScroll.onRelease();
                // A tap (finger didn't move beyond threshold) selects the row under the finger.
                if (!g_balMoved) {
                    int idx = views::balanceRowAtY(t.y, g_balScroll.offset(), g_balance.recordCount);
                    if (idx >= 0) {
                        g_carousel.noteUserActivity(millis());
                        g_app.enterBalanceDetail(idx);
                        ensureUsageLoaded();
                        g_entranceExtendsWake = true;
                        g_entrance.start(millis(), stopwatch::motion::kSpendEntranceMs);
                        renderCurrent();
                    }
                }
            }
            if (!g_balScroll.isResting()) {
                g_balScroll.tick(20);
                renderCurrent();
            }
        }
    }

    bool sleepPending = g_power.shouldSleep();
    stopwatch::CarouselContext cctx;
    cctx.inSettings = g_app.inCarouselSettings();
    cctx.inBalanceDetail = g_app.inBalanceDetail();
    cctx.touchActive = g_touchActive || (isBalanceView(g_app.currentView()) && !g_balScroll.isResting());
    cctx.loading = g_loading;
    cctx.transitionActive = g_transition.isAnimating();
    cctx.sleepPending = sleepPending;
    if (g_carousel.shouldAdvance(millis(), g_carouselSettings, cctx)) {
        g_transition.start(millis(), g_carouselSettings.motionMode);
        if (g_carouselSettings.motionMode == CarouselMotionMode::Instant) {
            completeAutoplayAdvance(false);
        }
    }

    if (sleepPending) enterSleepAndRefreshOnWake();
    // While an entrance plays, drop the 50 Hz idle throttle so frames run as
    // fast as the panel can push — the millis()-based clock keeps the timing
    // correct, the extra frames just make the motion smooth. delay(2) still
    // yields to the RTOS. Idle stays at 20 ms to keep the device cool/asleep.
    delay((g_transition.isAnimating() || g_entrance.isAnimating()) ? 2 : 20);
}
