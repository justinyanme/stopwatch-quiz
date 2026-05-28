#include "SnapshotCodec.h"

namespace stopwatch {

namespace {
uint16_t readU16(const uint8_t *b) { return (uint16_t)b[0] | ((uint16_t)b[1] << 8); }
uint32_t readU32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
}  // namespace

DecodeResult decodeSnapshot(const uint8_t *bytes, size_t len, Snapshot &out) {
    if (len < kHeaderSize) return DecodeResult::TooShort;
    out.versionMajor  = bytes[0];
    out.versionMinor  = bytes[1];
    out.providerCount = bytes[2];
    out.flags         = bytes[3];
    out.capturedAt    = readU32(bytes + 4);

    if (out.versionMajor > kVersionMajor) return DecodeResult::MajorVersionTooNew;
    if (out.providerCount > kProviderCount) return DecodeResult::InvalidProviderCount;
    if (len < (size_t)(kHeaderSize + out.providerCount * kPerProviderSize)) {
        return DecodeResult::TooShort;
    }

    for (uint8_t i = 0; i < out.providerCount; ++i) {
        const uint8_t *p = bytes + kHeaderSize + i * kPerProviderSize;
        auto &slot = out.providers[i];
        slot.id     = (ProviderID)p[0];
        slot.status = (ProviderStatus)p[1];
        slot.sessionPct      = (p[2] == 0xFF) ? std::nullopt : std::optional<uint8_t>(p[2]);
        slot.weekPct         = (p[3] == 0xFF) ? std::nullopt : std::optional<uint8_t>(p[3]);
        uint32_t sr = readU32(p + 4);
        uint32_t wr = readU32(p + 8);
        slot.sessionResetAt  = (sr == 0) ? std::nullopt : std::optional<uint32_t>(sr);
        slot.weekResetAt     = (wr == 0) ? std::nullopt : std::optional<uint32_t>(wr);
        uint16_t cr = readU16(p + 12);
        slot.creditsTimesTen = (cr == 0xFFFF) ? std::nullopt : std::optional<uint16_t>(cr);
        slot.plan            = (ProviderPlan)p[14];
    }
    return DecodeResult::Ok;
}

}  // namespace stopwatch
