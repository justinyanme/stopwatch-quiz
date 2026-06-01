#include <unity.h>
#include "../../src/OrientationController.h"

using namespace stopwatch;

static OrientationSample sample(float ax, float ay, float az = 0.0f) {
    OrientationSample s{ax, ay, az};
    return s;
}

static void assertOrientation(DisplayOrientation expected, DisplayOrientation actual) {
    TEST_ASSERT_EQUAL((int)expected, (int)actual);
}

void test_begins_at_initial_orientation(void) {
    OrientationController c;
    c.begin(100, DisplayOrientation::Deg0);

    assertOrientation(DisplayOrientation::Deg0, c.committed());
}

void test_debounces_new_cardinal_orientation(void) {
    OrientationController c;
    c.begin(1000, DisplayOrientation::Deg0);

    TEST_ASSERT_FALSE(c.tick(1000, sample(1.0f, 0.0f)));
    assertOrientation(DisplayOrientation::Deg0, c.committed());

    TEST_ASSERT_FALSE(c.tick(1299, sample(1.0f, 0.0f)));
    assertOrientation(DisplayOrientation::Deg0, c.committed());

    TEST_ASSERT_TRUE(c.tick(1300, sample(1.0f, 0.0f)));
    assertOrientation(DisplayOrientation::Deg180, c.committed());
}

void test_candidate_change_restarts_debounce(void) {
    OrientationController c;
    c.begin(2000, DisplayOrientation::Deg0);

    TEST_ASSERT_FALSE(c.tick(2000, sample(1.0f, 0.0f)));
    TEST_ASSERT_FALSE(c.tick(2150, sample(0.0f, 1.0f)));
    TEST_ASSERT_FALSE(c.tick(2449, sample(0.0f, 1.0f)));
    assertOrientation(DisplayOrientation::Deg0, c.committed());

    TEST_ASSERT_TRUE(c.tick(2450, sample(0.0f, 1.0f)));
    assertOrientation(DisplayOrientation::Deg270, c.committed());
}

void test_rejects_flat_or_invalid_samples(void) {
    OrientationController c;
    c.begin(0, DisplayOrientation::Deg0);

    TEST_ASSERT_FALSE(c.tick(0, sample(0.05f, -0.05f, 1.0f)));
    TEST_ASSERT_FALSE(c.tick(1000, sample(0.05f, -0.05f, 1.0f)));
    assertOrientation(DisplayOrientation::Deg0, c.committed());

    TEST_ASSERT_FALSE(c.tick(2000, sample(0.0f, 0.0f, 0.0f)));
    assertOrientation(DisplayOrientation::Deg0, c.committed());
}

void test_hysteresis_keeps_current_orientation_near_diagonal_boundary(void) {
    OrientationController c;
    c.begin(0, DisplayOrientation::Deg0);

    TEST_ASSERT_FALSE(c.tick(0, sample(0.66f, -0.70f)));
    TEST_ASSERT_FALSE(c.tick(1000, sample(0.66f, -0.70f)));
    assertOrientation(DisplayOrientation::Deg0, c.committed());
}

void test_commits_all_four_cardinal_orientations(void) {
    OrientationController c;
    c.begin(0, DisplayOrientation::Deg0);

    TEST_ASSERT_FALSE(c.tick(0, sample(1.0f, 0.0f)));
    TEST_ASSERT_TRUE(c.tick(300, sample(1.0f, 0.0f)));
    assertOrientation(DisplayOrientation::Deg180, c.committed());

    TEST_ASSERT_FALSE(c.tick(600, sample(0.0f, 1.0f)));
    TEST_ASSERT_TRUE(c.tick(900, sample(0.0f, 1.0f)));
    assertOrientation(DisplayOrientation::Deg270, c.committed());

    TEST_ASSERT_FALSE(c.tick(1200, sample(-1.0f, 0.0f)));
    TEST_ASSERT_TRUE(c.tick(1500, sample(-1.0f, 0.0f)));
    assertOrientation(DisplayOrientation::Deg0, c.committed());

    TEST_ASSERT_FALSE(c.tick(1800, sample(0.0f, -1.0f)));
    TEST_ASSERT_TRUE(c.tick(2100, sample(0.0f, -1.0f)));
    assertOrientation(DisplayOrientation::Deg90, c.committed());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_begins_at_initial_orientation);
    RUN_TEST(test_debounces_new_cardinal_orientation);
    RUN_TEST(test_candidate_change_restarts_debounce);
    RUN_TEST(test_rejects_flat_or_invalid_samples);
    RUN_TEST(test_hysteresis_keeps_current_orientation_near_diagonal_boundary);
    RUN_TEST(test_commits_all_four_cardinal_orientations);
    return UNITY_END();
}
