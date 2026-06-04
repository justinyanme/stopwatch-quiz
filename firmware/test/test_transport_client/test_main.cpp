#include <unity.h>
#include "../../src/TransportClient.h"
#include "../../src/Protocol.h"
#include <cstdio>
#include <cstring>
#include <vector>

using namespace stopwatch;

namespace {

struct HookState {
    std::vector<uint8_t> postedScopes;
    int snapshotGets = 0;
    int costGets = 0;
    int balanceGets = 0;
    int usageGets = 0;
    int delays = 0;
};

void writeU32(uint8_t *bytes, uint32_t value) {
    bytes[4] = (uint8_t)(value & 0xFF);
    bytes[5] = (uint8_t)((value >> 8) & 0xFF);
    bytes[6] = (uint8_t)((value >> 16) & 0xFF);
    bytes[7] = (uint8_t)((value >> 24) & 0xFF);
}

uint32_t readU32(const uint8_t *bytes) {
    return (uint32_t)bytes[4] |
           ((uint32_t)bytes[5] << 8) |
           ((uint32_t)bytes[6] << 16) |
           ((uint32_t)bytes[7] << 24);
}

NetworkClient::FetchResult postHook(const char *path, void *context) {
    auto *state = static_cast<HookState *>(context);
    unsigned scope = 999;
    if (std::sscanf(path, "/v1/refresh?scope=%u", &scope) != 1 || scope > 255) {
        return NetworkClient::FetchResult::RequestFailed;
    }
    state->postedScopes.push_back((uint8_t)scope);
    return NetworkClient::FetchResult::Ok;
}

void delayHook(uint32_t, void *context) {
    static_cast<HookState *>(context)->delays++;
}

NetworkClient::FetchResult getHook(const char *path, uint8_t *outBytes, size_t bufSize,
                                   size_t &outLen, void *context) {
    auto *state = static_cast<HookState *>(context);
    size_t len = 0;
    uint8_t major = 1;
    int *counter = nullptr;

    if (std::strcmp(path, "/v1/snapshot") == 0) {
        len = kSnapshotSize;
        counter = &state->snapshotGets;
    } else if (std::strcmp(path, "/v1/cost") == 0) {
        len = kCostHeaderSize;
        major = kCostVersionMajor;
        counter = &state->costGets;
    } else if (std::strcmp(path, "/v1/balances") == 0) {
        len = kBalanceHeaderSize;
        major = kBalanceVersionMajor;
        counter = &state->balanceGets;
    } else if (std::strcmp(path, "/v1/balance-usage") == 0) {
        len = kUsageHeaderSize;
        major = kUsageVersionMajor;
        counter = &state->usageGets;
    } else {
        return NetworkClient::FetchResult::RequestFailed;
    }

    if (bufSize < len) return NetworkClient::FetchResult::BadPayload;
    std::memset(outBytes, 0, len);
    outBytes[0] = major;
    outBytes[1] = 0;
    writeU32(outBytes, *counter < 2 ? 10 : 20);
    outLen = len;
    (*counter)++;
    return NetworkClient::FetchResult::Ok;
}

NetworkClient::NativeTestHooks hooksFor(HookState &state) {
    NetworkClient::NativeTestHooks hooks;
    hooks.get = getHook;
    hooks.post = postHook;
    hooks.delayMs = delayHook;
    hooks.context = &state;
    return hooks;
}

CarouselSettings wifiSettings() {
    CarouselSettings settings = CarouselSettings::defaults();
    settings.transportMode = TransportMode::WiFi;
    return settings;
}

}  // namespace

void test_wifiSnapshotRefreshesScopeBeforeReturningFrame(void) {
    HookState state;
    auto hooks = hooksFor(state);
    NetworkClient::setNativeTestHooks(&hooks);

    TransportClient transport;
    uint8_t out[kSnapshotSize] = {};
    size_t len = 0;
    auto rc = transport.fetchSnapshot(wifiSettings(), 0x02, out, sizeof(out), len);

    NetworkClient::setNativeTestHooks(nullptr);
    TEST_ASSERT_EQUAL((int)NetworkClient::FetchResult::Ok, (int)rc);
    TEST_ASSERT_EQUAL_UINT(kSnapshotSize, len);
    TEST_ASSERT_EQUAL_UINT32(20, readU32(out));
    TEST_ASSERT_EQUAL_UINT(1, state.postedScopes.size());
    TEST_ASSERT_EQUAL_UINT8(0x02, state.postedScopes[0]);
    TEST_ASSERT_GREATER_OR_EQUAL(3, state.snapshotGets);
    TEST_ASSERT_GREATER_OR_EQUAL(2, state.delays);
}

void test_wifiSpecializedFetchesUseMatchingRefreshScopes(void) {
    HookState state;
    auto hooks = hooksFor(state);
    NetworkClient::setNativeTestHooks(&hooks);

    TransportClient transport;
    auto settings = wifiSettings();
    uint8_t out[kBalanceSnapshotMaxSize] = {};
    size_t len = 0;

    TEST_ASSERT_EQUAL((int)NetworkClient::FetchResult::Ok,
                      (int)transport.fetchCost(settings, out, sizeof(out), len));
    TEST_ASSERT_EQUAL_UINT32(20, readU32(out));

    TEST_ASSERT_EQUAL((int)NetworkClient::FetchResult::Ok,
                      (int)transport.fetchBalances(settings, out, sizeof(out), len));
    TEST_ASSERT_EQUAL_UINT32(20, readU32(out));

    TEST_ASSERT_EQUAL((int)NetworkClient::FetchResult::Ok,
                      (int)transport.fetchUsage(settings, out, sizeof(out), len));
    TEST_ASSERT_EQUAL_UINT32(20, readU32(out));

    NetworkClient::setNativeTestHooks(nullptr);
    TEST_ASSERT_EQUAL_UINT(3, state.postedScopes.size());
    TEST_ASSERT_EQUAL_UINT8(kTriggerScopeCost, state.postedScopes[0]);
    TEST_ASSERT_EQUAL_UINT8(kTriggerScopeBalances, state.postedScopes[1]);
    TEST_ASSERT_EQUAL_UINT8(kTriggerScopeUsage, state.postedScopes[2]);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_wifiSnapshotRefreshesScopeBeforeReturningFrame);
    RUN_TEST(test_wifiSpecializedFetchesUseMatchingRefreshScopes);
    return UNITY_END();
}
