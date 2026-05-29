#include "BalanceCodec.h"
#include <cstring>

namespace stopwatch {

namespace {
uint32_t readU32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
}  // namespace

BalanceDecodeResult decodeBalanceSnapshot(const uint8_t *bytes, size_t len, BalanceSnapshot &out) {
    if (len < kBalanceHeaderSize) return BalanceDecodeResult::TooShort;

    uint8_t major = bytes[0];
    uint8_t count = bytes[2];
    if (major > kBalanceVersionMajor) return BalanceDecodeResult::MajorVersionTooNew;
    if (count > kBalanceMaxRecords)   return BalanceDecodeResult::InvalidRecordCount;
    if (len < (size_t)(kBalanceHeaderSize + count * kBalanceRecordSize)) return BalanceDecodeResult::TooShort;

    out.versionMajor = major;
    out.versionMinor = bytes[1];
    out.recordCount  = count;
    out.flags        = bytes[3];
    out.capturedAt   = readU32(bytes + 4);

    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t *r = bytes + kBalanceHeaderSize + i * kBalanceRecordSize;
        BalanceRecord &rec = out.records[i];
        rec.kind   = (BalanceKind)r[0];
        rec.status = (BalanceStatus)r[1];
        rec.low    = (r[2] & kBalanceRecordFlagLow) != 0;
        memcpy(rec.currency, r + 3, 3); rec.currency[3] = '\0';
        rec.decimals = r[6];
        uint32_t bal = readU32(r + 8);
        rec.unlimited    = (bal == 0xFFFFFFFEu);
        rec.balanceMinor = (bal == 0xFFFFFFFFu || bal == 0xFFFFFFFEu)
                           ? std::nullopt : std::optional<uint32_t>(bal);
        uint32_t use = readU32(r + 12);
        rec.usageMinor = (use == 0xFFFFFFFFu) ? std::nullopt : std::optional<uint32_t>(use);
        rec.updatedAt  = readU32(r + 16);
        memcpy(rec.name, r + 20, 16); rec.name[16] = '\0';
    }
    return BalanceDecodeResult::Ok;
}

}  // namespace stopwatch
