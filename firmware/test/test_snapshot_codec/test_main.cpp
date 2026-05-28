#include <unity.h>
#include <cstdio>
#include <vector>
#include <fstream>
#include <cctype>
#include "../../src/SnapshotCodec.h"

using namespace stopwatch;

static std::vector<uint8_t> readHexFixture(const char *name) {
    std::string path = std::string("../shared/fixtures/") + name + ".hex";
    std::ifstream f(path);
    if (!f) {
        char buf[256];
        snprintf(buf, sizeof(buf), "missing fixture: %s", path.c_str());
        TEST_FAIL_MESSAGE(buf);
    }
    std::string raw((std::istreambuf_iterator<char>(f)), {});
    // Strip whitespace.
    std::string hex;
    hex.reserve(raw.size());
    for (char c : raw) {
        if (!isspace((unsigned char)c)) hex.push_back(c);
    }
    if (hex.size() % 2 != 0) {
        TEST_FAIL_MESSAGE("hex fixture has odd length");
    }
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        unsigned v = 0;
        sscanf(hex.c_str() + i, "%2x", &v);
        out.push_back((uint8_t)v);
    }
    return out;
}

void test_threeProvidersFixtureDecodes(void) {
    auto bytes = readHexFixture("codexbar-three-providers");
    TEST_ASSERT_EQUAL(kSnapshotSize, bytes.size());

    Snapshot snap;
    auto rc = decodeSnapshot(bytes.data(), bytes.size(), snap);
    TEST_ASSERT_EQUAL((int)DecodeResult::Ok, (int)rc);
    TEST_ASSERT_EQUAL(1, snap.versionMajor);
    TEST_ASSERT_EQUAL(0, snap.versionMinor);
    TEST_ASSERT_EQUAL(3, snap.providerCount);
    TEST_ASSERT_EQUAL(0, snap.flags);

    TEST_ASSERT_EQUAL((int)ProviderID::Codex, (int)snap.providers[0].id);
    TEST_ASSERT_TRUE(snap.providers[0].sessionPct.has_value());
    TEST_ASSERT_EQUAL(72, snap.providers[0].sessionPct.value());
    TEST_ASSERT_EQUAL(41, snap.providers[0].weekPct.value());
    TEST_ASSERT_TRUE(snap.providers[0].creditsTimesTen.has_value());
    TEST_ASSERT_EQUAL(1124, snap.providers[0].creditsTimesTen.value());
    TEST_ASSERT_EQUAL((int)ProviderPlan::Plus, (int)snap.providers[0].plan);

    // Claude
    TEST_ASSERT_EQUAL((int)ProviderID::Claude, (int)snap.providers[1].id);
    TEST_ASSERT_EQUAL(12, snap.providers[1].sessionPct.value());
    TEST_ASSERT_EQUAL(37, snap.providers[1].weekPct.value());
    TEST_ASSERT_FALSE(snap.providers[1].creditsTimesTen.has_value());
    TEST_ASSERT_EQUAL((int)ProviderPlan::Pro, (int)snap.providers[1].plan);

    TEST_ASSERT_FALSE(snap.providers[2].weekPct.has_value());
    TEST_ASSERT_FALSE(snap.providers[2].weekResetAt.has_value());
}

void test_errorFixtureDecodes(void) {
    auto bytes = readHexFixture("codexbar-error");
    Snapshot snap;
    auto rc = decodeSnapshot(bytes.data(), bytes.size(), snap);
    TEST_ASSERT_EQUAL((int)DecodeResult::Ok, (int)rc);
    TEST_ASSERT_EQUAL(0, snap.providerCount);
    TEST_ASSERT_TRUE(snap.isStale());
    TEST_ASSERT_TRUE(snap.isBridgeError());
}

void test_futureMajorIsRejected(void) {
    uint8_t bytes[kHeaderSize] = { 99 /*major*/, 0, 0, 0, 0, 0, 0, 0 };
    Snapshot snap;
    auto rc = decodeSnapshot(bytes, sizeof(bytes), snap);
    TEST_ASSERT_EQUAL((int)DecodeResult::MajorVersionTooNew, (int)rc);
}

void test_tooShortIsRejected(void) {
    uint8_t bytes[3] = { 1, 0, 3 };
    Snapshot snap;
    auto rc = decodeSnapshot(bytes, sizeof(bytes), snap);
    TEST_ASSERT_EQUAL((int)DecodeResult::TooShort, (int)rc);
}

void test_providerCountTooLargeIsRejected(void) {
    uint8_t bytes[kHeaderSize] = { 1, 0, 4 /*count > kProviderCount*/, 0, 0, 0, 0, 0 };
    Snapshot snap;
    auto rc = decodeSnapshot(bytes, sizeof(bytes), snap);
    TEST_ASSERT_EQUAL((int)DecodeResult::InvalidProviderCount, (int)rc);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_threeProvidersFixtureDecodes);
    RUN_TEST(test_errorFixtureDecodes);
    RUN_TEST(test_futureMajorIsRejected);
    RUN_TEST(test_tooShortIsRejected);
    RUN_TEST(test_providerCountTooLargeIsRejected);
    return UNITY_END();
}
