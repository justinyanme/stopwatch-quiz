// firmware/src/SnapshotStore.h
#pragma once
#include "Protocol.h"
#include <cstdint>
#include <cstddef>

namespace stopwatch {

/// Persists the most-recent raw snapshot bytes to NVS so the watch can render
/// last-known data on a cold boot before the bridge responds.
class SnapshotStore {
public:
    void begin();
    bool load(uint8_t *outBytes, size_t bufSize, size_t &outLen);
    void save(const uint8_t *bytes, size_t len);

private:
    bool open_ = false;
};

}  // namespace stopwatch
