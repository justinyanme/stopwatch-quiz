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
};

}  // namespace stopwatch
