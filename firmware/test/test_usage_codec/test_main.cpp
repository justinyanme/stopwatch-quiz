#include <unity.h>
#include <vector>
#include <fstream>
#include <cctype>
#include <cstdio>
#include <cstring>
#include "../../src/UsageCodec.h"
using namespace stopwatch;

static std::vector<uint8_t> readHexFixture(const char *name) {
    std::string path = std::string("../shared/fixtures/") + name + ".hex";
    std::ifstream f(path);
    if (!f) { char b[256]; snprintf(b, sizeof(b), "missing fixture: %s", path.c_str()); TEST_FAIL_MESSAGE(b); }
    std::string raw((std::istreambuf_iterator<char>(f)), {});
    std::string hex;
    for (char c : raw) if (!isspace((unsigned char)c)) hex.push_back(c);
    if (hex.size() % 2 != 0) TEST_FAIL_MESSAGE("hex fixture has odd length");
    std::vector<uint8_t> out;
    for (size_t i = 0; i < hex.size(); i += 2) { unsigned v = 0; sscanf(hex.c_str()+i, "%2x", &v); out.push_back((uint8_t)v); }
    return out;
}

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

void test_sentinelsBecomeNullopt(void) {
    std::vector<uint8_t> b(kUsageHeaderSize + kUsageRecordSize, 0);
    b[0] = 1;                       // versionMajor
    b[2] = 1;                       // recordCount
    b[8] = 30;                      // historyDays
    uint8_t *r = b.data() + kUsageHeaderSize;
    r[0] = (uint8_t)BalanceKind::OpenRouter;
    r[1] = (uint8_t)BalanceStatus::Ok;
    r[2] = 'U'; r[3] = 'S'; r[4] = 'D';
    r[5] = 2;
    for (int i = 12; i < 36; ++i) r[i] = 0xFF;   // all six optional u32 fields = unknown
    UsageSnapshot u;
    TEST_ASSERT_EQUAL((int)UsageDecodeResult::Ok, (int)decodeUsageSnapshot(b.data(), b.size(), u));
    const UsageRecord *rec = u.find(BalanceKind::OpenRouter);
    TEST_ASSERT_NOT_NULL(rec);
    TEST_ASSERT_FALSE(rec->todayCostMinor.has_value());
    TEST_ASSERT_FALSE(rec->monthCostMinor.has_value());
    TEST_ASSERT_FALSE(rec->todayTokens.has_value());
    TEST_ASSERT_FALSE(rec->monthTokens.has_value());
    TEST_ASSERT_FALSE(rec->todayRequests.has_value());
    TEST_ASSERT_FALSE(rec->monthRequests.has_value());
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

void test_openRouterFixtureDecodes(void) {
    auto bytes = readHexFixture("usage-openrouter");
    TEST_ASSERT_EQUAL(12 + 96, (int)bytes.size());
    UsageSnapshot u;
    TEST_ASSERT_EQUAL((int)UsageDecodeResult::Ok, (int)decodeUsageSnapshot(bytes.data(), bytes.size(), u));
    const UsageRecord *r = u.find(BalanceKind::OpenRouter);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_STRING("USD", r->currency);
    TEST_ASSERT_EQUAL(10000, r->todayCostMinor.value());     // $100.00
    TEST_ASSERT_EQUAL(15000, r->monthCostMinor.value());     // $150.00
    TEST_ASSERT_EQUAL(1000000, r->todayTokens.value());
    TEST_ASSERT_EQUAL(1500000, r->monthTokens.value());
    TEST_ASSERT_EQUAL(1240, r->todayRequests.value());
    TEST_ASSERT_EQUAL(9000, r->monthRequests.value());
    // last day is the max in both arrays → scales to 255-ish; day28 is half.
    // cost: max=10000 minor, costUnit=ceil(10000/255)=40; day29=(10000+20)/40=250, day28=(5000+20)/40=125
    TEST_ASSERT_EQUAL(250, r->costHistory[29]);
    TEST_ASSERT_EQUAL(125, r->costHistory[28]);
    // tokens: max=1000000, tokenUnit=ceil(1000000/255)=3922; day29=(1000000+1961)/3922=255
    TEST_ASSERT_EQUAL(255, r->tokenHistory[29]);
    // reconstructed cost within one unit of true value:
    TEST_ASSERT_INT_WITHIN(r->costUnit, 10000, (int)r->costHistory[29] * (int)r->costUnit);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_decodesRecord);
    RUN_TEST(test_sentinelsBecomeNullopt);
    RUN_TEST(test_futureMajorRejected);
    RUN_TEST(test_tooShortRejected);
    RUN_TEST(test_recordCountOverMaxRejected);
    RUN_TEST(test_openRouterFixtureDecodes);
    return UNITY_END();
}
