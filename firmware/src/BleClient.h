// firmware/src/BleClient.h
#pragma once
#include <cstdint>
#include <cstddef>

namespace stopwatch {

class BleClient {
public:
    enum class FetchResult : uint8_t { Ok, NoPeripheral, ConnectFailed, ReadFailed };

    void begin();

    /// Scans for the service UUID, connects, writes scope to RefreshTrigger,
    /// reads UsageSnapshot. Blocks up to ~3s total. Tries up to twice on
    /// transient read failure per spec §9.2.
    /// On Ok, fills `outBytes` (capacity = bufSize) and sets `outLen`.
    FetchResult fetch(uint8_t scope, uint8_t *outBytes, size_t bufSize, size_t &outLen);

    /// Like fetch(), but writes the cost trigger scope and reads CostSnapshot.
    FetchResult fetchCost(uint8_t *outBytes, size_t bufSize, size_t &outLen);

    /// Like fetchCost(), but writes the balance trigger scope and reads BalanceSnapshot.
    FetchResult fetchBalances(uint8_t *outBytes, size_t bufSize, size_t &outLen);

    /// Like fetchBalances(), but writes the usage trigger scope and reads BalanceUsage.
    FetchResult fetchUsage(uint8_t *outBytes, size_t bufSize, size_t &outLen);

private:
    FetchResult fetchInto(const char *charUuid, uint8_t scope,
                          uint8_t *outBytes, size_t bufSize, size_t &outLen,
                          bool pollPastPendingUsage = false);
};

}  // namespace stopwatch
