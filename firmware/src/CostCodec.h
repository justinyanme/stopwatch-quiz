#pragma once
#include "Protocol.h"
#include <cstdint>
#include <cstddef>
#include <optional>

namespace stopwatch {

struct CostRecord {
    ProviderID id;
    std::optional<uint32_t> todayCents;   // nullopt iff 0xFFFFFFFF
    std::optional<uint32_t> monthCents;
    std::optional<uint32_t> todayTokens;
    std::optional<uint32_t> monthTokens;
    char topModel[13] = {0};              // 12 wire bytes + null terminator
    uint8_t history[kCostHistoryDays] = {0};
};

struct CostSnapshot {
    uint8_t  versionMajor    = 0;
    uint8_t  versionMinor    = 0;
    uint8_t  recordCount     = 0;
    uint8_t  flags           = 0;
    uint32_t capturedAt      = 0;
    uint8_t  historyDays     = 0;
    uint16_t historyUnitCents = 0;
    CostRecord records[kCostMaxRecords] = {};

    bool isStale()       const { return flags & kCostFlagStale; }
    bool isBridgeError() const { return flags & kCostFlagBridgeError; }
    bool isUnavailable() const { return flags & kCostFlagUnavailable; }

    const CostRecord *find(ProviderID pid) const {
        for (uint8_t i = 0; i < recordCount; ++i)
            if (records[i].id == pid) return &records[i];
        return nullptr;
    }
};

enum class CostDecodeResult : uint8_t { Ok, TooShort, MajorVersionTooNew, InvalidRecordCount };

CostDecodeResult decodeCostSnapshot(const uint8_t *bytes, size_t len, CostSnapshot &out);

}  // namespace stopwatch
