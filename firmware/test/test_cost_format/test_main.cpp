#include <unity.h>
#include <cstring>
#include "../../src/CostFormat.h"

using namespace stopwatch;

void test_formatDollars(void) {
    char buf[16];
    formatDollars(2190, buf, sizeof(buf), true);   TEST_ASSERT_EQUAL_STRING("$21.90", buf);
    formatDollars(1200, buf, sizeof(buf), true);    TEST_ASSERT_EQUAL_STRING("$12.00", buf);
    formatDollars(41502, buf, sizeof(buf), false);  TEST_ASSERT_EQUAL_STRING("$415", buf);
    formatDollars(30000, buf, sizeof(buf), false);  TEST_ASSERT_EQUAL_STRING("$300", buf);
}

void test_humanizeTokens(void) {
    char buf[16];
    humanizeTokens(391120777, buf, sizeof(buf)); TEST_ASSERT_EQUAL_STRING("391M", buf);
    humanizeTokens(34356111, buf, sizeof(buf));  TEST_ASSERT_EQUAL_STRING("34M", buf);
    humanizeTokens(1000000, buf, sizeof(buf));   TEST_ASSERT_EQUAL_STRING("1M", buf);
    humanizeTokens(999999, buf, sizeof(buf));    TEST_ASSERT_EQUAL_STRING("999k", buf);
    humanizeTokens(500, buf, sizeof(buf));       TEST_ASSERT_EQUAL_STRING("500", buf);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_formatDollars);
    RUN_TEST(test_humanizeTokens);
    return UNITY_END();
}
