// firmware/test/test_state_machine/test_main.cpp
#include <unity.h>
#include "../../src/App.h"

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

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_keyBShortCyclesForward);
    RUN_TEST(test_keyAShortCyclesBackward);
    RUN_TEST(test_longPressesSetFlags);
    RUN_TEST(test_wakeFromSleepRequestsRefresh);
    RUN_TEST(test_linkStatusDefaultsToNoBridgeAndMutates);
    RUN_TEST(test_isSpendView);
    RUN_TEST(test_balancesInCarousel);
    return UNITY_END();
}
