#include "UsageCodec.h"
#include <cstring>

namespace stopwatch {

namespace {
uint16_t readU16(const uint8_t *b) { return (uint16_t)b[0] | ((uint16_t)b[1] << 8); }
uint32_t readU32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
std::optional<uint32_t> optU32(const uint8_t *b) {
    uint32_t v = readU32(b);
    return (v == 0xFFFFFFFFu) ? std::nullopt : std::optional<uint32_t>(v);
}
}  // namespace

UsageDecodeResult decodeUsageSnapshot(const uint8_t *bytes, size_t len, UsageSnapshot &out) {
    if (len < kUsageHeaderSize) return UsageDecodeResult::TooShort;

    uint8_t major = bytes[0];
    uint8_t count = bytes[2];
    if (major > kUsageVersionMajor) return UsageDecodeResult::MajorVersionTooNew;
    if (count > kUsageMaxRecords)   return UsageDecodeResult::InvalidRecordCount;
    if (len < (size_t)(kUsageHeaderSize + count * kUsageRecordSize)) return UsageDecodeResult::TooShort;

    out.versionMajor = major;
    out.versionMinor = bytes[1];
    out.recordCount  = count;
    out.flags        = bytes[3];
    out.capturedAt   = readU32(bytes + 4);
    out.historyDays  = bytes[8];

    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t *r = bytes + kUsageHeaderSize + i * kUsageRecordSize;
        UsageRecord &rec = out.records[i];
        rec.kind   = (BalanceKind)r[0];
        rec.status = (BalanceStatus)r[1];
        memcpy(rec.currency, r + 2, 3); rec.currency[3] = '\0';
        rec.decimals = r[5];
        rec.costUnit  = readU16(r + 6);
        rec.tokenUnit = readU32(r + 8);
        rec.todayCostMinor = optU32(r + 12);
        rec.monthCostMinor = optU32(r + 16);
        rec.todayTokens    = optU32(r + 20);
        rec.monthTokens    = optU32(r + 24);
        rec.todayRequests  = optU32(r + 28);
        rec.monthRequests  = optU32(r + 32);
        memcpy(rec.costHistory,  r + 36, kUsageHistoryDays);
        memcpy(rec.tokenHistory, r + 66, kUsageHistoryDays);
    }
    return UsageDecodeResult::Ok;
}

}  // namespace stopwatch
