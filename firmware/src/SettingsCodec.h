#pragma once
#include "CarouselSettings.h"
#include <cstddef>
#include <cstdint>

namespace stopwatch {

constexpr size_t kSettingsV2BytesSize = 8;
constexpr size_t kSettingsV3BytesSize = 10;
constexpr size_t kSettingsMaxBytesSize = kSettingsV3BytesSize;
constexpr size_t kSettingsBytesSize = kSettingsMaxBytesSize;

bool encodeCarouselSettings(const CarouselSettings &settings,
                            uint8_t *outBytes,
                            size_t capacity,
                            size_t &outLen);

bool decodeCarouselSettings(const uint8_t *bytes,
                            size_t len,
                            CarouselSettings &out);

}  // namespace stopwatch
