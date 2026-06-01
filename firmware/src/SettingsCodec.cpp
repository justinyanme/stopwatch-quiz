#include "SettingsCodec.h"

namespace stopwatch {
namespace {

constexpr uint8_t kVersion1 = 1;
constexpr uint8_t kVersion2 = 2;
constexpr uint8_t kFlagAutoplay = 0x01;
constexpr uint8_t kFlagUpright = 0x02;

uint16_t readU16LE(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

void writeU16LE(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value & 0xFF);
    p[1] = (uint8_t)((value >> 8) & 0xFF);
}

}  // namespace

bool encodeCarouselSettings(const CarouselSettings &settings,
                            uint8_t *outBytes,
                            size_t capacity,
                            size_t &outLen) {
    outLen = 0;
    if (!outBytes || capacity < kSettingsBytesSize) return false;

    CarouselSettings copy = settings;
    copy.validate();

    uint8_t flags = 0;
    if (copy.autoplayEnabled) flags |= kFlagAutoplay;
    if (copy.uprightEnabled) flags |= kFlagUpright;

    outBytes[0] = kVersion2;
    outBytes[1] = flags;
    outBytes[2] = (uint8_t)copy.motionMode;
    outBytes[3] = 0;
    writeU16LE(outBytes + 4, copy.intervalSeconds);
    writeU16LE(outBytes + 6, copy.resumeSeconds);
    outLen = kSettingsBytesSize;
    return true;
}

bool decodeCarouselSettings(const uint8_t *bytes,
                            size_t len,
                            CarouselSettings &out) {
    if (!bytes || len != kSettingsBytesSize) return false;
    uint8_t version = bytes[0];
    if (version != kVersion1 && version != kVersion2) return false;

    CarouselSettings decoded = CarouselSettings::defaults();
    uint8_t flags = bytes[1];
    decoded.autoplayEnabled = (flags & kFlagAutoplay) != 0;
    decoded.uprightEnabled = version == kVersion2 && ((flags & kFlagUpright) != 0);
    decoded.motionMode = (CarouselMotionMode)bytes[2];
    decoded.intervalSeconds = readU16LE(bytes + 4);
    decoded.resumeSeconds = readU16LE(bytes + 6);
    decoded.validate();

    out = decoded;
    return true;
}

}  // namespace stopwatch
