// firmware/test/test_state_machine/test_main.cpp
#include <unity.h>
#include "../../src/App.h"
#include "../../src/CarouselSettings.h"

using namespace stopwatch;

void test_keyBShortCyclesForward(void) {
    App app; app.begin();
    ViewId order[] = { ViewId::Overview, ViewId::TotalSpend, ViewId::Codex, ViewId::CodexCost,
                       ViewId::Claude, ViewId::ClaudeCost, ViewId::Gemini, ViewId::Balances, ViewId::Overview };
    for (int i = 0; i < 8; ++i) {
        TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyBShort));
        TEST_ASSERT_EQUAL((int)order[i + 1], (int)app.currentView());
    }
}

void test_keyAShortCyclesBackward(void) {
    App app; app.begin();
    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyAShort));
    TEST_ASSERT_EQUAL((int)ViewId::Balances, (int)app.currentView());
    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyAShort));
    TEST_ASSERT_EQUAL((int)ViewId::Gemini, (int)app.currentView());
}

void test_isSpendView(void) {
    TEST_ASSERT_TRUE(isSpendView(ViewId::TotalSpend));
    TEST_ASSERT_TRUE(isSpendView(ViewId::CodexCost));
    TEST_ASSERT_TRUE(isSpendView(ViewId::ClaudeCost));
    TEST_ASSERT_FALSE(isSpendView(ViewId::Overview));
    TEST_ASSERT_FALSE(isSpendView(ViewId::Codex));
    TEST_ASSERT_FALSE(isSpendView(ViewId::Gemini));
}

void test_balancesInCarousel(void) {
    using namespace stopwatch;
    TEST_ASSERT_EQUAL((int)ViewId::Balances, (int)nextView(ViewId::Gemini));
    TEST_ASSERT_EQUAL((int)ViewId::Overview, (int)nextView(ViewId::Balances));
    TEST_ASSERT_EQUAL((int)ViewId::Balances, (int)prevView(ViewId::Overview));
    TEST_ASSERT_EQUAL((int)ViewId::Gemini,   (int)prevView(ViewId::Balances));
    TEST_ASSERT_TRUE(isBalanceView(ViewId::Balances));
    TEST_ASSERT_FALSE(isBalanceView(ViewId::Gemini));
}

void test_longPressesSetFlags(void) {
    App app;
    app.begin();
    TEST_ASSERT_FALSE(app.wantsRefresh());
    app.handleEvent(ButtonEvent::KeyALong);
    TEST_ASSERT_TRUE(app.wantsRefresh());
    app.clearRefreshRequest();
    TEST_ASSERT_FALSE(app.wantsRefresh());

    app.handleEvent(ButtonEvent::KeyBLong);
    TEST_ASSERT_TRUE(app.wantsImmediateSleep());
}

void test_wakeFromSleepRequestsRefresh(void) {
    App app;
    app.begin();
    TEST_ASSERT_FALSE(app.wantsRefresh());
    app.noteWakeFromSleep();
    TEST_ASSERT_TRUE(app.wantsRefresh());
}

void test_linkStatusDefaultsToNoBridgeAndMutates(void) {
    App app;
    app.begin();
    TEST_ASSERT_EQUAL((int)LinkStatus::NoBridge, (int)app.linkStatus());
    app.setLinkStatus(LinkStatus::Connected);
    TEST_ASSERT_EQUAL((int)LinkStatus::Connected, (int)app.linkStatus());
}

void test_balanceDetailEnterExit(void) {
    App app; app.begin();
    app.handleEvent(ButtonEvent::KeyAShort);     // Overview → Balances (prevView)
    TEST_ASSERT_EQUAL((int)ViewId::Balances, (int)app.currentView());
    TEST_ASSERT_FALSE(app.inBalanceDetail());

    app.enterBalanceDetail(2);                   // tap row index 2
    TEST_ASSERT_TRUE(app.inBalanceDetail());
    TEST_ASSERT_EQUAL(2, app.balanceDetailIndex());

    bool changed = app.handleEvent(ButtonEvent::KeyAShort);   // A = back out of detail
    TEST_ASSERT_TRUE(changed);
    TEST_ASSERT_FALSE(app.inBalanceDetail());
    TEST_ASSERT_EQUAL((int)ViewId::Balances, (int)app.currentView());  // carousel unchanged
}

void test_balanceDetailToggle(void) {
    App app; app.begin();
    app.handleEvent(ButtonEvent::KeyAShort);     // → Balances
    app.enterBalanceDetail(0);
    TEST_ASSERT_EQUAL((int)UsageMetric::Cost, (int)app.usageMetric());
    bool changed = app.handleEvent(ButtonEvent::KeyBShort);   // B = toggle in detail
    TEST_ASSERT_TRUE(changed);
    TEST_ASSERT_EQUAL((int)UsageMetric::Tokens, (int)app.usageMetric());
    app.handleEvent(ButtonEvent::KeyBShort);                  // toggle back
    TEST_ASSERT_EQUAL((int)UsageMetric::Cost, (int)app.usageMetric());
}

void test_carouselUnaffectedWhenNotInDetail(void) {
    App app; app.begin();
    app.handleEvent(ButtonEvent::KeyBShort);     // Overview → TotalSpend (normal carousel)
    TEST_ASSERT_EQUAL((int)ViewId::TotalSpend, (int)app.currentView());
    TEST_ASSERT_FALSE(app.inBalanceDetail());
}

void test_carouselSettingsDefaultsAndValidation(void) {
    CarouselSettings s = CarouselSettings::defaults();
    TEST_ASSERT_TRUE(s.autoplayEnabled);
    TEST_ASSERT_EQUAL_UINT16(10, s.intervalSeconds);
    TEST_ASSERT_EQUAL((int)CarouselMotionMode::Iris, (int)s.motionMode);
    TEST_ASSERT_EQUAL_UINT16(20, s.resumeSeconds);

    CarouselSettings invalid;
    invalid.autoplayEnabled = true;
    invalid.intervalSeconds = 7;
    invalid.motionMode = (CarouselMotionMode)99;
    invalid.resumeSeconds = 11;
    invalid.validate();

    TEST_ASSERT_EQUAL_UINT16(10, invalid.intervalSeconds);
    TEST_ASSERT_EQUAL((int)CarouselMotionMode::Iris, (int)invalid.motionMode);
    TEST_ASSERT_EQUAL_UINT16(20, invalid.resumeSeconds);
}

void test_carouselSettingsCyclesValues(void) {
    CarouselSettings s = CarouselSettings::defaults();

    s.cycle(CarouselSettingRow::Autoplay);
    TEST_ASSERT_FALSE(s.autoplayEnabled);
    s.cycle(CarouselSettingRow::Autoplay);
    TEST_ASSERT_TRUE(s.autoplayEnabled);

    s.cycle(CarouselSettingRow::Interval);
    TEST_ASSERT_EQUAL_UINT16(15, s.intervalSeconds);
    s.cycle(CarouselSettingRow::Interval);
    TEST_ASSERT_EQUAL_UINT16(30, s.intervalSeconds);
    s.cycle(CarouselSettingRow::Interval);
    TEST_ASSERT_EQUAL_UINT16(5, s.intervalSeconds);

    s.cycle(CarouselSettingRow::Motion);
    TEST_ASSERT_EQUAL((int)CarouselMotionMode::Fade, (int)s.motionMode);
    s.cycle(CarouselSettingRow::Motion);
    TEST_ASSERT_EQUAL((int)CarouselMotionMode::Instant, (int)s.motionMode);
    s.cycle(CarouselSettingRow::Motion);
    TEST_ASSERT_EQUAL((int)CarouselMotionMode::Iris, (int)s.motionMode);

    s.cycle(CarouselSettingRow::Resume);
    TEST_ASSERT_EQUAL_UINT16(30, s.resumeSeconds);
    s.cycle(CarouselSettingRow::Resume);
    TEST_ASSERT_EQUAL_UINT16(10, s.resumeSeconds);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_keyBShortCyclesForward);
    RUN_TEST(test_keyAShortCyclesBackward);
    RUN_TEST(test_longPressesSetFlags);
    RUN_TEST(test_wakeFromSleepRequestsRefresh);
    RUN_TEST(test_linkStatusDefaultsToNoBridgeAndMutates);
    RUN_TEST(test_isSpendView);
    RUN_TEST(test_balancesInCarousel);
    RUN_TEST(test_balanceDetailEnterExit);
    RUN_TEST(test_balanceDetailToggle);
    RUN_TEST(test_carouselUnaffectedWhenNotInDetail);
    RUN_TEST(test_carouselSettingsDefaultsAndValidation);
    RUN_TEST(test_carouselSettingsCyclesValues);
    return UNITY_END();
}
