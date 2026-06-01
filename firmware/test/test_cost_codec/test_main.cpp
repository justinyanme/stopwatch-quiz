#include <unity.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <fstream>
#include <cctype>
#include "../../src/CostCodec.h"

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

void test_costFixtureDecodes(void) {
    auto bytes = readHexFixture("codexbar-cost-two");
    TEST_ASSERT_EQUAL(182, bytes.size());

    CostSnapshot cs;
    auto rc = decodeCostSnapshot(bytes.data(), bytes.size(), cs);
    TEST_ASSERT_EQUAL((int)CostDecodeResult::Ok, (int)rc);
    TEST_ASSERT_EQUAL(2, cs.versionMajor);
    TEST_ASSERT_EQUAL(2, cs.recordCount);
    TEST_ASSERT_EQUAL(30, cs.historyDays);
    TEST_ASSERT_EQUAL(48, cs.historyUnitCents);

    const CostRecord *codex = cs.find(ProviderID::Codex);
    TEST_ASSERT_NOT_NULL(codex);
    TEST_ASSERT_TRUE(codex->todayCents.has_value());
    TEST_ASSERT_EQUAL(1200, codex->todayCents.value());
    TEST_ASSERT_EQUAL(30000, codex->monthCents.value());
    TEST_ASSERT_EQUAL(1000000, codex->todayTokens.value());
    TEST_ASSERT_EQUAL(100000000, codex->monthTokens.value());
    TEST_ASSERT_EQUAL(1, codex->modelCount);
    TEST_ASSERT_EQUAL_STRING("gpt-5.5", codex->models[0]);
    TEST_ASSERT_EQUAL_STRING("", codex->models[1]);
    TEST_ASSERT_EQUAL(250, codex->history[29]);

    const CostRecord *claude = cs.find(ProviderID::Claude);
    TEST_ASSERT_NOT_NULL(claude);
    TEST_ASSERT_EQUAL(3, claude->modelCount);
    TEST_ASSERT_EQUAL_STRING("opus-4-8", claude->models[0]);
    TEST_ASSERT_EQUAL_STRING("sonnet-4-6", claude->models[1]);
    TEST_ASSERT_EQUAL_STRING("haiku-4-5", claude->models[2]);
    TEST_ASSERT_EQUAL(125, claude->history[29]);

    TEST_ASSERT_NULL(cs.find(ProviderID::Gemini));
}

void test_unknownCentsBecomeNullopt(void) {
    std::vector<uint8_t> b(kCostHeaderSize + kCostRecordSize, 0);
    b[0] = 1; b[2] = 1; b[8] = 30; b[10] = 1;     // major, count, historyDays, unit
    b[kCostHeaderSize + 0] = (uint8_t)ProviderID::Codex;
    for (int i = 2; i < 2 + 16; ++i) b[kCostHeaderSize + i] = 0xFF;  // today/month cents+tokens
    CostSnapshot cs;
    TEST_ASSERT_EQUAL((int)CostDecodeResult::Ok, (int)decodeCostSnapshot(b.data(), b.size(), cs));
    const CostRecord *r = cs.find(ProviderID::Codex);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_FALSE(r->todayCents.has_value());
    TEST_ASSERT_FALSE(r->monthTokens.has_value());
}

void test_futureMajorRejected(void) {
    uint8_t b[kCostHeaderSize] = { 99, 0, 0, 0, 0, 0, 0, 0, 30, 0, 1, 0 };
    CostSnapshot cs;
    TEST_ASSERT_EQUAL((int)CostDecodeResult::MajorVersionTooNew, (int)decodeCostSnapshot(b, sizeof(b), cs));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_costFixtureDecodes);
    RUN_TEST(test_unknownCentsBecomeNullopt);
    RUN_TEST(test_futureMajorRejected);
    return UNITY_END();
}
