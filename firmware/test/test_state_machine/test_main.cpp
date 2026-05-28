// firmware/test/test_state_machine/test_main.cpp
#include <unity.h>
#include "../../src/App.h"

using namespace stopwatch;

void test_keyBShortCyclesForward(void) {
    App app;
    app.begin();
    TEST_ASSERT_EQUAL((int)ViewId::Overview, (int)app.currentView());
    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyBShort));
    TEST_ASSERT_EQUAL((int)ViewId::Codex, (int)app.currentView());
    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyBShort));
    TEST_ASSERT_EQUAL((int)ViewId::Claude, (int)app.currentView());
    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyBShort));
    TEST_ASSERT_EQUAL((int)ViewId::Gemini, (int)app.currentView());
    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyBShort));
    TEST_ASSERT_EQUAL((int)ViewId::Overview, (int)app.currentView());
}

void test_keyAShortCyclesBackward(void) {
    App app;
    app.begin();
    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyAShort));
    TEST_ASSERT_EQUAL((int)ViewId::Gemini, (int)app.currentView());
    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyAShort));
    TEST_ASSERT_EQUAL((int)ViewId::Claude, (int)app.currentView());
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

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_keyBShortCyclesForward);
    RUN_TEST(test_keyAShortCyclesBackward);
    RUN_TEST(test_longPressesSetFlags);
    return UNITY_END();
}
