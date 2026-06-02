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

private:
    FetchResult fetchPath(const char *path, uint8_t *outBytes, size_t bufSize, size_t &outLen);
    FetchResult postPath(const char *path);
    FetchResult ensureWiFi(const DeviceNetworkConfig &cfg);
};

}  // namespace stopwatch
