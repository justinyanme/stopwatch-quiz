#include <unity.h>
#include "../../src/TouchScroll.h"
using namespace stopwatch;

void test_clampsAtBothEnds(void) {
    TouchScroll s; s.setBounds(/*content*/ 300, /*view*/ 100);   // max = 200
    TEST_ASSERT_EQUAL(200, s.maxOffset());
    s.onPress(100); s.onMove(40); s.onRelease();   // dragged up 60px → offset 60
    TEST_ASSERT_EQUAL(60, s.offset());
    s.onPress(1000); s.onMove(0); s.onRelease();   // finger far UP → offset clamps to max
    TEST_ASSERT_EQUAL(200, s.offset());
    s.onPress(0); s.onMove(1000); s.onRelease();   // finger far DOWN → offset clamps to 0
    TEST_ASSERT_EQUAL(0, s.offset());
}

void test_noScrollWhenContentFits(void) {
    TouchScroll s; s.setBounds(80, 100);           // content < view → max 0
    s.onPress(50); s.onMove(0); s.onRelease();
    TEST_ASSERT_EQUAL(0, s.offset());
}

void test_momentumDecaysToRest(void) {
    TouchScroll s; s.setBounds(1000, 100);
    s.onPress(200); s.onMove(160); s.onMove(120); s.onRelease();   // fling up
    TEST_ASSERT_FALSE(s.isResting());
    int guard = 0;
    while (!s.isResting() && guard++ < 1000) s.tick(16);
    TEST_ASSERT_TRUE(s.isResting());
    TEST_ASSERT_TRUE(s.offset() >= 0 && s.offset() <= s.maxOffset());
}

void test_tapDoesNotScroll(void) {
    TouchScroll s; s.setBounds(1000, 100);
    s.onPress(120); s.onRelease();                 // press + release, no move
    s.tick(16);
    TEST_ASSERT_EQUAL(0, s.offset());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_clampsAtBothEnds);
    RUN_TEST(test_noScrollWhenContentFits);
    RUN_TEST(test_momentumDecaysToRest);
    RUN_TEST(test_tapDoesNotScroll);
    return UNITY_END();
}
