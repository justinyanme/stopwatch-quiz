#include <unity.h>
#include <cstring>
#include "../../src/BalanceFormat.h"
using namespace stopwatch;

void test_symbols(void) {
    TEST_ASSERT_EQUAL_STRING("$", currencySymbol("USD"));
    TEST_ASSERT_EQUAL_STRING("\xC2\xA5", currencySymbol("CNY"));  // ¥ (U+00A5 UTF-8)
    TEST_ASSERT_EQUAL_STRING("\xC2\xA5", currencySymbol("JPY"));
    TEST_ASSERT_EQUAL_STRING("EUR", currencySymbol("EUR"));        // no glyph mapping → code
    TEST_ASSERT_EQUAL_STRING("", currencySymbol(""));
}

void test_formatMinor(void) {
    char buf[16];
    formatBalanceMinor(4210, 2, buf, sizeof(buf)); TEST_ASSERT_EQUAL_STRING("42.10", buf);
    formatBalanceMinor(31850, 2, buf, sizeof(buf)); TEST_ASSERT_EQUAL_STRING("318.50", buf);
    formatBalanceMinor(800, 2, buf, sizeof(buf));  TEST_ASSERT_EQUAL_STRING("8.00", buf);
    formatBalanceMinor(5, 0, buf, sizeof(buf));    TEST_ASSERT_EQUAL_STRING("5", buf);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_symbols);
    RUN_TEST(test_formatMinor);
    return UNITY_END();
}
