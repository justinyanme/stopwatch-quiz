#include <unity.h>
#include "../../src/CarouselController.h"

using namespace stopwatch;

void test_autoplayAdvancesAfterInterval(void) {
    CarouselController c;
    CarouselSettings s = CarouselSettings::defaults();
    c.begin(1000, s);

    CarouselContext ctx;
    TEST_ASSERT_FALSE(c.shouldAdvance(1000 + 9999, s, ctx));
    TEST_ASSERT_TRUE(c.shouldAdvance(1000 + 10000, s, ctx));
    c.recordAdvance(1000 + 10000);
    TEST_ASSERT_FALSE(c.shouldAdvance(1000 + 19999, s, ctx));
    TEST_ASSERT_TRUE(c.shouldAdvance(1000 + 20000, s, ctx));
}

void test_autoplayDisabledDoesNotAdvance(void) {
    CarouselController c;
    CarouselSettings s = CarouselSettings::defaults();
    s.autoplayEnabled = false;
    c.begin(0, s);
    CarouselContext ctx;
    TEST_ASSERT_FALSE(c.shouldAdvance(60000, s, ctx));
}

void test_userActivityDelaysResume(void) {
    CarouselController c;
    CarouselSettings s = CarouselSettings::defaults();
    c.begin(0, s);
    CarouselContext ctx;
    c.noteUserActivity(9000);

    TEST_ASSERT_FALSE(c.shouldAdvance(28000, s, ctx));
    TEST_ASSERT_TRUE(c.shouldAdvance(29000, s, ctx));
}

void test_pauseContextsBlockAdvance(void) {
    CarouselController c;
    CarouselSettings s = CarouselSettings::defaults();
    c.begin(0, s);

    CarouselContext ctx;
    ctx.inSettings = true;
    TEST_ASSERT_FALSE(c.shouldAdvance(60000, s, ctx));
    ctx = CarouselContext{};
    ctx.inBalanceDetail = true;
    TEST_ASSERT_FALSE(c.shouldAdvance(60000, s, ctx));
    ctx = CarouselContext{};
    ctx.touchActive = true;
    TEST_ASSERT_FALSE(c.shouldAdvance(60000, s, ctx));
    ctx = CarouselContext{};
    ctx.loading = true;
    TEST_ASSERT_FALSE(c.shouldAdvance(60000, s, ctx));
    ctx = CarouselContext{};
    ctx.transitionActive = true;
    TEST_ASSERT_FALSE(c.shouldAdvance(60000, s, ctx));
    ctx = CarouselContext{};
    ctx.sleepPending = true;
    TEST_ASSERT_FALSE(c.shouldAdvance(60000, s, ctx));
}

void test_manualAdvanceResetsSchedule(void) {
    CarouselController c;
    CarouselSettings s = CarouselSettings::defaults();
    c.begin(0, s);
    CarouselContext ctx;
    c.recordManualViewChange(5000);

    TEST_ASSERT_FALSE(c.shouldAdvance(14999, s, ctx));
    TEST_ASSERT_TRUE(c.shouldAdvance(25000, s, ctx));
}

void test_wakeActivityResetsResumeAndIntervalSchedule(void) {
    CarouselController c;
    CarouselSettings s = CarouselSettings::defaults();
    s.intervalSeconds = 30;
    s.resumeSeconds = 10;
    c.begin(0, s);
    CarouselContext ctx;

    c.recordWake(60000);

    TEST_ASSERT_FALSE(c.shouldAdvance(69999, s, ctx));
    TEST_ASSERT_FALSE(c.shouldAdvance(70000, s, ctx));
    TEST_ASSERT_TRUE(c.shouldAdvance(90000, s, ctx));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_autoplayAdvancesAfterInterval);
    RUN_TEST(test_autoplayDisabledDoesNotAdvance);
    RUN_TEST(test_userActivityDelaysResume);
    RUN_TEST(test_pauseContextsBlockAdvance);
    RUN_TEST(test_manualAdvanceResetsSchedule);
    RUN_TEST(test_wakeActivityResetsResumeAndIntervalSchedule);
    return UNITY_END();
}
