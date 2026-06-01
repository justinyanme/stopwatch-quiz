#pragma once
#include "CarouselSettings.h"
#include <cstddef>
#include <cstdint>

namespace stopwatch {

constexpr size_t kSettingsBytesSize = 8;

bool encodeCarouselSettings(const CarouselSettings &settings,
                            uint8_t *outBytes,
                            size_t capacity,
                            size_t &outLen);

bool decodeCarouselSettings(const uint8_t *bytes,
                            size_t len,
                            CarouselSettings &out);

}  // namespace stopwatch
