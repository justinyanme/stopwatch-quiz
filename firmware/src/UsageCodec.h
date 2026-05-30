#pragma once
#include "Protocol.h"
#include <cstdint>
#include <cstddef>
#include <optional>

namespace stopwatch {

struct UsageRecord {
    BalanceKind kind = BalanceKind::Generic;
    BalanceStatus status = BalanceStatus::Ok;
    char     currency[4] = {0};            // 3 chars + null
    uint8_t  decimals = 2;
    std::optional<uint32_t> todayCostMinor;  // nullopt iff 0xFFFFFFFF
    std::optional<uint32_t> monthCostMinor;
    std::optional<uint32_t> todayTokens;
    std::optional<uint32_t> monthTokens;
    std::optional<uint32_t> todayRequests;
    std::optional<uint32_t> monthRequests;
    uint16_t costUnit  = 1;                 // costHistory[i] * costUnit = minor units
    uint32_t tokenUnit = 1;                 // tokenHistory[i] * tokenUnit = tokens
    uint8_t  costHistory[kUsageHistoryDays]  = {0};
    uint8_t  tokenHistory[kUsageHistoryDays] = {0};

    bool isSuccessful() const { return status == BalanceStatus::Ok; }
};

struct UsageSnapshot {
    uint8_t  versionMajor = 0;
    uint8_t  versionMinor = 0;
    uint8_t  recordCount  = 0;
    uint8_t  flags        = 0;
    uint32_t capturedAt   = 0;
    uint8_t  historyDays  = 0;
    UsageRecord records[kUsageMaxRecords] = {};

    bool isStale()       const { return flags & kUsageFlagStale; }
    bool isBridgeError() const { return flags & kUsageFlagBridgeError; }
    bool isUnavailable() const { return flags & kUsageFlagUnavailable; }
    bool isFresh()       const { return !isStale() && !isBridgeError() && !isUnavailable(); }

    bool isPendingEmpty() const {
        return recordCount == 0 && capturedAt == 0 && isStale() && isUnavailable();
    }

    bool shouldCache() const {
        return !isPendingEmpty();
    }

    const UsageRecord *find(BalanceKind k) const {
        for (uint8_t i = 0; i < recordCount; ++i)
            if (records[i].kind == k) return &records[i];
        return nullptr;
    }

    bool hasFreshSuccessfulData(BalanceKind k) const {
        if (!isFresh()) return false;
        const UsageRecord *record = find(k);
        return record && record->isSuccessful();
    }
};

enum class UsageDecodeResult : uint8_t { Ok, TooShort, MajorVersionTooNew, InvalidRecordCount };

UsageDecodeResult decodeUsageSnapshot(const uint8_t *bytes, size_t len, UsageSnapshot &out);

}  // namespace stopwatch
