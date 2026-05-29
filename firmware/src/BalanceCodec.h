#pragma once
#include "Protocol.h"
#include <cstdint>
#include <cstddef>
#include <optional>

namespace stopwatch {

struct BalanceRecord {
    BalanceKind   kind   = BalanceKind::Generic;
    BalanceStatus status = BalanceStatus::Ok;
    bool          low    = false;
    char          currency[4] = {0};        // up to 3 chars + null
    uint8_t       decimals = 2;
    std::optional<uint32_t> balanceMinor;    // nullopt iff 0xFFFFFFFF (unknown)
    bool          unlimited = false;         // true iff 0xFFFFFFFE
    std::optional<uint32_t> usageMinor;
    uint32_t      updatedAt = 0;
    char          name[17] = {0};            // 16 wire bytes + null
};

struct BalanceSnapshot {
    uint8_t  versionMajor = 0;
    uint8_t  versionMinor = 0;
    uint8_t  recordCount  = 0;
    uint8_t  flags        = 0;
    uint32_t capturedAt   = 0;
    BalanceRecord records[kBalanceMaxRecords] = {};

    bool isStale()       const { return flags & kBalanceFlagStale; }
    bool isBridgeError() const { return flags & kBalanceFlagBridgeError; }
};

enum class BalanceDecodeResult : uint8_t { Ok, TooShort, MajorVersionTooNew, InvalidRecordCount };

BalanceDecodeResult decodeBalanceSnapshot(const uint8_t *bytes, size_t len, BalanceSnapshot &out);

}  // namespace stopwatch
