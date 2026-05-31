#include <unity.h>
#include "../../src/CarouselTransition.h"

using namespace stopwatch;

void test_irisTransitionSwitchesOnce(void) {
    CarouselTransition t;
    t.start(1000, CarouselMotionMode::Iris);
    TEST_ASSERT_TRUE(t.isAnimating());
    TEST_ASSERT_FALSE(t.hasSwitched());

    t.tick(1000 + motion::kIrisSwitchMs - 1);
    TEST_ASSERT_FALSE(t.hasSwitched());

    t.tick(1000 + motion::kIrisSwitchMs);
    TEST_ASSERT_TRUE(t.hasSwitched());
    TEST_ASSERT_TRUE(t.consumeSwitch());
    TEST_ASSERT_FALSE(t.consumeSwitch());

    t.tick(1000 + motion::kIrisTransitionMs);
    TEST_ASSERT_FALSE(t.isAnimating());
    TEST_ASSERT_TRUE(t.hasSwitched());
}

void test_instantTransitionSwitchesImmediately(void) {
    CarouselTransition t;
    t.start(2000, CarouselMotionMode::Instant);
    TEST_ASSERT_FALSE(t.isAnimating());
    TEST_ASSERT_TRUE(t.hasSwitched());
    TEST_ASSERT_TRUE(t.consumeSwitch());
}

void test_fadeTransitionDuration(void) {
    CarouselTransition t;
    t.start(0, CarouselMotionMode::Fade);
    TEST_ASSERT_TRUE(t.isAnimating());
    TEST_ASSERT_TRUE(t.hasSwitched());
    TEST_ASSERT_TRUE(t.consumeSwitch());
    t.tick(motion::kFadeMs - 1);
    TEST_ASSERT_TRUE(t.isAnimating());
    t.tick(motion::kFadeMs);
    TEST_ASSERT_FALSE(t.isAnimating());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_irisTransitionSwitchesOnce);
    RUN_TEST(test_instantTransitionSwitchesImmediately);
    RUN_TEST(test_fadeTransitionDuration);
    return UNITY_END();
}
