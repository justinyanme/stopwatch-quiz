#pragma once
#include "DeviceConfig.h"
#include <cstddef>
#include <cstdint>

namespace stopwatch {

class NetworkClient {
public:
    enum class FetchResult : uint8_t {
        Ok,
        WiFiMissing,
        APIMissing,
        WiFiOffline,
        AuthFailed,
        RequestFailed,
        BadPayload,
    };

    void begin();
    FetchResult fetchSnapshot(uint8_t *outBytes, size_t bufSize, size_t &outLen);
    FetchResult fetchCost(uint8_t *outBytes, size_t bufSize, size_t &outLen);
    FetchResult fetchBalances(uint8_t *outBytes, size_t bufSize, size_t &outLen);
    FetchResult fetchUsage(uint8_t *outBytes, size_t bufSize, size_t &outLen);
    FetchResult refresh(uint8_t scope);
    FetchResult refreshAndFetchSnapshot(uint8_t scope, uint8_t *outBytes, size_t bufSize, size_t &outLen);
    FetchResult refreshAndFetchCost(uint8_t *outBytes, size_t bufSize, size_t &outLen);
    FetchResult refreshAndFetchBalances(uint8_t *outBytes, size_t bufSize, size_t &outLen);
    FetchResult refreshAndFetchUsage(uint8_t *outBytes, size_t bufSize, size_t &outLen);

#ifndef ARDUINO
    struct NativeTestHooks {
        FetchResult (*get)(const char *path, uint8_t *outBytes, size_t bufSize,
                           size_t &outLen, void *context);
        FetchResult (*post)(const char *path, void *context);
        void (*delayMs)(uint32_t ms, void *context);
        void *context;
    };

    static void setNativeTestHooks(const NativeTestHooks *hooks);
#endif

private:
    using Fetcher = FetchResult (NetworkClient::*)(uint8_t *, size_t, size_t &);

    FetchResult fetchPath(const char *path, uint8_t *outBytes, size_t bufSize, size_t &outLen);
    FetchResult postPath(const char *path);
    FetchResult ensureWiFi(const DeviceNetworkConfig &cfg);
    FetchResult refreshAndFetch(uint8_t scope, Fetcher fetcher,
                                uint8_t *outBytes, size_t bufSize, size_t &outLen);
};

}  // namespace stopwatch
