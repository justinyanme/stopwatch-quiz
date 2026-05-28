#pragma once
#include "Protocol.h"
#include <cstddef>
#include <optional>

namespace stopwatch {

struct ProviderSlot {
    ProviderID     id;
    ProviderStatus status;
    std::optional<uint8_t> sessionPct;      // nullopt iff wire byte was 0xFF
    std::optional<uint8_t> weekPct;
    std::optional<uint32_t> sessionResetAt; // nullopt iff wire bytes were 0
    std::optional<uint32_t> weekResetAt;
    std::optional<uint16_t> creditsTimesTen; // nullopt iff 0xFFFF
    ProviderPlan plan;
};

struct Snapshot {
    uint8_t versionMajor   = 0;
    uint8_t versionMinor   = 0;
    uint8_t providerCount  = 0;
    uint8_t flags          = 0;
    uint32_t capturedAt    = 0;
    ProviderSlot providers[kProviderCount] = {};

    bool isStale()           const { return flags & kFlagStale; }
    bool isBridgeError()     const { return flags & kFlagBridgeError; }
    bool isProviderMissing() const { return flags & kFlagProviderMissing; }
};

enum class DecodeResult : uint8_t {
    Ok,
    TooShort,
    MajorVersionTooNew,    // versionMajor > kVersionMajor → render "update firmware"
    InvalidProviderCount,
};

DecodeResult decodeSnapshot(const uint8_t *bytes, size_t len, Snapshot &out);

}  // namespace stopwatch
