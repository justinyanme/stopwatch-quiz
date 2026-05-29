#include "CostCodec.h"
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

CostDecodeResult decodeCostSnapshot(const uint8_t *bytes, size_t len, CostSnapshot &out) {
    if (len < kCostHeaderSize) return CostDecodeResult::TooShort;

    uint8_t major = bytes[0];
    uint8_t count = bytes[2];
    if (major > kCostVersionMajor) return CostDecodeResult::MajorVersionTooNew;
    if (count > kCostMaxRecords)   return CostDecodeResult::InvalidRecordCount;
    if (len < (size_t)(kCostHeaderSize + count * kCostRecordSize)) return CostDecodeResult::TooShort;

    out.versionMajor     = major;
    out.versionMinor     = bytes[1];
    out.recordCount      = count;
    out.flags            = bytes[3];
    out.capturedAt       = readU32(bytes + 4);
    out.historyDays      = bytes[8];
    out.historyUnitCents = readU16(bytes + 10);

    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t *r = bytes + kCostHeaderSize + i * kCostRecordSize;
        CostRecord &rec = out.records[i];
        rec.id          = (ProviderID)r[0];
        rec.todayCents  = optU32(r + 2);
        rec.monthCents  = optU32(r + 6);
        rec.todayTokens = optU32(r + 10);
        rec.monthTokens = optU32(r + 14);
        memcpy(rec.topModel, r + 18, 12);
        rec.topModel[12] = '\0';
        memcpy(rec.history, r + 30, kCostHistoryDays);
    }
    return CostDecodeResult::Ok;
}

}  // namespace stopwatch
