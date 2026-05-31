// firmware/src/SnapshotStore.h
#pragma once
#include "CarouselSettings.h"
#include "Protocol.h"
#include <cstdint>
#include <cstddef>

namespace stopwatch {

/// Persists raw snapshot bytes to NVS (one namespace, multiple keys) so the watch
/// can render last-known data on a cold boot before the bridge responds.
class SnapshotStore {
public:
    void begin();
    bool load(const char *key, uint8_t *outBytes, size_t bufSize, size_t &outLen);
    void save(const char *key, const uint8_t *bytes, size_t len);
    bool loadCarouselSettings(CarouselSettings &out);
    void saveCarouselSettings(const CarouselSettings &settings);

private:
    bool open_ = false;
};

}  // namespace stopwatch
