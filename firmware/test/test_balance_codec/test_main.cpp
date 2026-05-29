#include <unity.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <fstream>
#include <cctype>
#include "../../src/BalanceCodec.h"

using namespace stopwatch;

static std::vector<uint8_t> readHexFixture(const char *name) {
    std::string path = std::string("../shared/fixtures/") + name + ".hex";
    std::ifstream f(path);
    if (!f) { char b[256]; snprintf(b, sizeof(b), "missing fixture: %s", path.c_str()); TEST_FAIL_MESSAGE(b); }
    std::string raw((std::istreambuf_iterator<char>(f)), {});
    std::string hex; for (char c : raw) if (!isspace((unsigned char)c)) hex.push_back(c);
    if (hex.size() % 2 != 0) TEST_FAIL_MESSAGE("hex fixture has odd length");
    std::vector<uint8_t> out;
    for (size_t i = 0; i < hex.size(); i += 2) { unsigned v = 0; sscanf(hex.c_str()+i, "%2x", &v); out.push_back((uint8_t)v); }
    return out;
}

void test_balancesFixtureDecodes(void) {
    auto bytes = readHexFixture("balances-two");
    TEST_ASSERT_EQUAL(80, bytes.size());

    BalanceSnapshot bs;
    auto rc = decodeBalanceSnapshot(bytes.data(), bytes.size(), bs);
    TEST_ASSERT_EQUAL((int)BalanceDecodeResult::Ok, (int)rc);
    TEST_ASSERT_EQUAL(1, bs.versionMajor);
    TEST_ASSERT_EQUAL(2, bs.recordCount);

    TEST_ASSERT_EQUAL((int)BalanceKind::OpenRouter, (int)bs.records[0].kind);
    TEST_ASSERT_EQUAL_STRING("USD", bs.records[0].currency);
    TEST_ASSERT_TRUE(bs.records[0].balanceMinor.has_value());
    TEST_ASSERT_EQUAL(4210, bs.records[0].balanceMinor.value());
    TEST_ASSERT_EQUAL(790, bs.records[0].usageMinor.value());
    TEST_ASSERT_EQUAL_STRING("OpenRouter", bs.records[0].name);
    TEST_ASSERT_FALSE(bs.records[0].low);

    TEST_ASSERT_EQUAL((int)BalanceKind::DeepSeek, (int)bs.records[1].kind);
    TEST_ASSERT_EQUAL_STRING("CNY", bs.records[1].currency);
    TEST_ASSERT_EQUAL(31850, bs.records[1].balanceMinor.value());
    TEST_ASSERT_FALSE(bs.records[1].usageMinor.has_value());   // 0xFFFFFFFF → nullopt
    TEST_ASSERT_EQUAL_STRING("DeepSeek", bs.records[1].name);
}

void test_unknownAndUnlimited(void) {
    std::vector<uint8_t> b(kBalanceHeaderSize + kBalanceRecordSize, 0);
    b[0] = 1; b[2] = 1;                                   // major, count
    for (int i = 8; i < 12; ++i) b[kBalanceHeaderSize + i] = 0xFF;   // balanceMinor = 0xFFFFFFFF
    BalanceSnapshot bs;
    TEST_ASSERT_EQUAL((int)BalanceDecodeResult::Ok, (int)decodeBalanceSnapshot(b.data(), b.size(), bs));
    TEST_ASSERT_FALSE(bs.records[0].balanceMinor.has_value());
    TEST_ASSERT_FALSE(bs.records[0].unlimited);

    b[kBalanceHeaderSize + 8] = 0xFE; b[kBalanceHeaderSize + 9] = 0xFF;
    b[kBalanceHeaderSize + 10] = 0xFF; b[kBalanceHeaderSize + 11] = 0xFF;   // 0xFFFFFFFE
    TEST_ASSERT_EQUAL((int)BalanceDecodeResult::Ok, (int)decodeBalanceSnapshot(b.data(), b.size(), bs));
    TEST_ASSERT_TRUE(bs.records[0].unlimited);
    TEST_ASSERT_FALSE(bs.records[0].balanceMinor.has_value());
}

void test_lowFlagAndFutureMajor(void) {
    std::vector<uint8_t> b(kBalanceHeaderSize + kBalanceRecordSize, 0);
    b[0] = 1; b[2] = 1; b[kBalanceHeaderSize + 2] = kBalanceRecordFlagLow;
    BalanceSnapshot bs;
    TEST_ASSERT_EQUAL((int)BalanceDecodeResult::Ok, (int)decodeBalanceSnapshot(b.data(), b.size(), bs));
    TEST_ASSERT_TRUE(bs.records[0].low);

    uint8_t f[kBalanceHeaderSize] = { 99, 0, 0, 0, 0, 0, 0, 0 };
    TEST_ASSERT_EQUAL((int)BalanceDecodeResult::MajorVersionTooNew,
                      (int)decodeBalanceSnapshot(f, sizeof(f), bs));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_balancesFixtureDecodes);
    RUN_TEST(test_unknownAndUnlimited);
    RUN_TEST(test_lowFlagAndFutureMajor);
    return UNITY_END();
}
