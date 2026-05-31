#include <unity.h>
#include "../../src/Anim.h"
using namespace stopwatch;

// ── Rings: outer (index 0) leads; inner rings are delayed by the stagger. ────
void test_ringOuterLeadsInner(void) {
    uint32_t t = motion::kRingStaggerMs;                       // ring 1 just begins here
    TEST_ASSERT_TRUE(motion::ringFill(t, 0) > motion::ringFill(t, 1));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::ringFill(t, 1));     // inner not started yet
    TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::ringFill(0, 2));     // before anything moves
}

void test_ringClampsAndDuration(void) {
    TEST_ASSERT_EQUAL_FLOAT(1.0f, motion::ringFill(motion::kRingSweepMs, 0));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, motion::ringFill(100000, 2));        // long after → settled
    TEST_ASSERT_EQUAL_UINT32(500, motion::ringEntranceMs(1));
    TEST_ASSERT_EQUAL_UINT32(700, motion::ringEntranceMs(3));         // 500 + 2*100
    TEST_ASSERT_EQUAL_UINT32(0,   motion::ringEntranceMs(0));
}

// ── Bars: left bar (index 0) leads the last; each clamps 0→1, monotonic. ──────
void test_barLeftLeadsRight(void) {
    uint32_t t = 60;
    TEST_ASSERT_TRUE(motion::barRise(t, 0, 30) > motion::barRise(t, 29, 30));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::barRise(0, 5, 30));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, motion::barRise(100000, 29, 30));   // settled
}

void test_barMonotonic(void) {
    float prev = -1.0f;
    for (uint32_t e = 0; e <= motion::kBarStaggerSpanMs + motion::kBarRiseMs; e += 15) {
        float s = motion::barRise(e, 10, 30);
        TEST_ASSERT_TRUE(s >= prev);
        TEST_ASSERT_TRUE(s >= 0.0f && s <= 1.0f);
        prev = s;
    }
}

// Single-bar charts must not divide by zero and should track the timeline.
void test_barSingle(void) {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::barRise(0, 0, 1));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, motion::barRise(motion::kBarRiseMs, 0, 1));
}

// ── Count-up: 0 at start, 1 at/after the window, monotonic between. ──────────
void test_countUp(void) {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::countUp(0));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, motion::countUp(motion::kCountUpMs));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, motion::countUp(100000));
    TEST_ASSERT_TRUE(motion::countUp(120) > 0.0f && motion::countUp(120) < 1.0f);
}

// ── Entrance clock: animates until the duration elapses, then settles. ───────
void test_entranceLifecycle(void) {
    Entrance e;
    TEST_ASSERT_FALSE(e.isAnimating());                       // fresh → idle
    e.start(1000, motion::kSpendEntranceMs);
    TEST_ASSERT_TRUE(e.isAnimating());
    TEST_ASSERT_EQUAL_UINT32(0, e.elapsed());                 // frame 0
    e.tick(1000 + motion::kSpendEntranceMs - 1);
    TEST_ASSERT_TRUE(e.isAnimating());                        // one tick before the end
    e.tick(1000 + motion::kSpendEntranceMs);
    TEST_ASSERT_FALSE(e.isAnimating());                       // settled
}

// Idle / settled clock reports kSettled, so every shape reads its final value.
void test_idleReadsSettled(void) {
    Entrance e;
    TEST_ASSERT_EQUAL_UINT32(Entrance::kSettled, e.elapsed());
    TEST_ASSERT_EQUAL_FLOAT(1.0f, motion::ringFill(e.elapsed(), 0));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, motion::barRise(e.elapsed(), 29, 30));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, motion::countUp(e.elapsed()));
    e.start(0, 0);                                            // zero-duration → never animates
    TEST_ASSERT_FALSE(e.isAnimating());
}

void test_irisHelpersClampAndMove(void) {
    TEST_ASSERT_EQUAL_FLOAT(1.0f, motion::irisCover(0));
    TEST_ASSERT_TRUE(motion::irisCover(80) > 0.0f);
    TEST_ASSERT_TRUE(motion::irisCover(80) < 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::irisCover(motion::kIrisCloseMs));

    TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::irisReveal(0));
    TEST_ASSERT_TRUE(motion::irisReveal(100) > 0.0f);
    TEST_ASSERT_TRUE(motion::irisReveal(100) < 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, motion::irisReveal(motion::kIrisOpenMs));
}

void test_haloAndFadeClamp(void) {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::irisHalo(0));
    TEST_ASSERT_TRUE(motion::irisHalo(motion::kIrisHaloStartMs + 10) > 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::irisHalo(100000));

    TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::fadeReveal(0));
    TEST_ASSERT_TRUE(motion::fadeReveal(80) > 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, motion::fadeReveal(motion::kFadeMs));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_ringOuterLeadsInner);
    RUN_TEST(test_ringClampsAndDuration);
    RUN_TEST(test_barLeftLeadsRight);
    RUN_TEST(test_barMonotonic);
    RUN_TEST(test_barSingle);
    RUN_TEST(test_countUp);
    RUN_TEST(test_entranceLifecycle);
    RUN_TEST(test_idleReadsSettled);
    RUN_TEST(test_irisHelpersClampAndMove);
    RUN_TEST(test_haloAndFadeClamp);
    return UNITY_END();
}
