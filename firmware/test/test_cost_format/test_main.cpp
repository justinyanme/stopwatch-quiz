#include <unity.h>
#include <cstring>
#include "../../src/CostFormat.h"
#include "../../src/CostCodec.h"

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

void test_costModelsLine(void) {
    char buf[64];

    CostRecord r{};
    r.modelCount = 3;
    strcpy(r.models[0], "opus-4-8");
    strcpy(r.models[1], "sonnet-4-6");
    strcpy(r.models[2], "haiku-4-5");
    costModelsLine(r, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("opus-4-8 \xC2\xB7 sonnet-4-6 \xC2\xB7 haiku-4-5", buf);

    CostRecord more{};
    more.modelCount = 5;                 // used more than the 3 carried → "+2"
    strcpy(more.models[0], "opus-4-8");
    strcpy(more.models[1], "sonnet-4-6");
    strcpy(more.models[2], "haiku-4-5");
    costModelsLine(more, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("opus-4-8 \xC2\xB7 sonnet-4-6 \xC2\xB7 haiku-4-5 +2", buf);

    CostRecord one{};
    one.modelCount = 1;
    strcpy(one.models[0], "gpt-5.5");
    costModelsLine(one, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("gpt-5.5", buf);

    CostRecord none{};
    costModelsLine(none, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_costModelsLineTruncatesWithinBuffer(void) {
    CostRecord r{};
    r.modelCount = 3;
    strcpy(r.models[0], "opus-4-8");
    strcpy(r.models[1], "sonnet-4-6");
    strcpy(r.models[2], "haiku-4-5");

    char tiny[5];
    costModelsLine(r, tiny, sizeof(tiny));
    TEST_ASSERT_EQUAL_STRING("opus", tiny);

    char one[1];
    one[0] = 'x';
    costModelsLine(r, one, sizeof(one));
    TEST_ASSERT_EQUAL_STRING("", one);

    char untouched = 'x';
    costModelsLine(r, &untouched, 0);
    TEST_ASSERT_EQUAL('x', untouched);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_formatDollars);
    RUN_TEST(test_humanizeTokens);
    RUN_TEST(test_costModelsLine);
    RUN_TEST(test_costModelsLineTruncatesWithinBuffer);
    return UNITY_END();
}
