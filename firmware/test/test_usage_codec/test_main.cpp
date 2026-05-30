#include <unity.h>
#include <vector>
#include "../../src/UsageCodec.h"
using namespace stopwatch;

// Builds one 12+96 byte snapshot with a single OpenRouter record by hand.
static std::vector<uint8_t> buildOne() {
    std::vector<uint8_t> b(kUsageHeaderSize + kUsageRecordSize, 0);
    b[0] = 1;                       // versionMajor
    b[2] = 1;                       // recordCount
    b[8] = 30;                      // historyDays
    uint8_t *r = b.data() + kUsageHeaderSize;
    r[0] = (uint8_t)BalanceKind::OpenRouter;
    r[1] = (uint8_t)BalanceStatus::Ok;
    r[2] = 'U'; r[3] = 'S'; r[4] = 'D';
    r[5] = 2;                       // decimals
    r[6] = 50; r[7] = 0;            // costUnit = 50
    r[8] = 100; r[9]=0; r[10]=0; r[11]=0;   // tokenUnit = 100
    r[12] = 0x90; r[13]=0x01;       // todayCostMinor = 400
    r[20] = 0xFF; r[21]=0xFF; r[22]=0xFF; r[23]=0xFF;  // todayTokens unknown
    r[36 + 29] = 200;               // costHistory[29] = 200
    r[66 + 29] = 123;               // tokenHistory[29] = 123
    return b;
}

void test_decodesRecord(void) {
    auto b = buildOne();
    UsageSnapshot u;
    TEST_ASSERT_EQUAL((int)UsageDecodeResult::Ok, (int)decodeUsageSnapshot(b.data(), b.size(), u));
    TEST_ASSERT_EQUAL(1, u.versionMajor);
    TEST_ASSERT_EQUAL(1, u.recordCount);
    TEST_ASSERT_EQUAL(30, u.historyDays);
    const UsageRecord *r = u.find(BalanceKind::OpenRouter);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_STRING("USD", r->currency);
    TEST_ASSERT_EQUAL(2, r->decimals);
    TEST_ASSERT_EQUAL(50, r->costUnit);
    TEST_ASSERT_EQUAL(100, r->tokenUnit);
    TEST_ASSERT_TRUE(r->todayCostMinor.has_value());
    TEST_ASSERT_EQUAL(400, r->todayCostMinor.value());
    TEST_ASSERT_FALSE(r->todayTokens.has_value());     // 0xFFFFFFFF sentinel
    TEST_ASSERT_EQUAL(200, r->costHistory[29]);
    TEST_ASSERT_EQUAL(123, r->tokenHistory[29]);
}

void test_futureMajorRejected(void) {
    uint8_t b[kUsageHeaderSize] = {99,0,0,0,0,0,0,0,30,0,0,0};
    UsageSnapshot u;
    TEST_ASSERT_EQUAL((int)UsageDecodeResult::MajorVersionTooNew,
                      (int)decodeUsageSnapshot(b, sizeof(b), u));
}

void test_tooShortRejected(void) {
    uint8_t b[4] = {1,0,1,0};
    UsageSnapshot u;
    TEST_ASSERT_EQUAL((int)UsageDecodeResult::TooShort,
                      (int)decodeUsageSnapshot(b, sizeof(b), u));
}

void test_recordCountOverMaxRejected(void) {
    uint8_t b[kUsageHeaderSize] = {1,0,(uint8_t)(kUsageMaxRecords+1),0,0,0,0,0,30,0,0,0};
    UsageSnapshot u;
    TEST_ASSERT_EQUAL((int)UsageDecodeResult::InvalidRecordCount,
                      (int)decodeUsageSnapshot(b, sizeof(b), u));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_decodesRecord);
    RUN_TEST(test_futureMajorRejected);
    RUN_TEST(test_tooShortRejected);
    RUN_TEST(test_recordCountOverMaxRejected);
    return UNITY_END();
}
