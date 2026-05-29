#pragma once
#include <cstdint>
#include <cstddef>

namespace stopwatch {

/// Maps a 3-letter currency code to a display symbol, or the code itself if
/// there's no known glyph (e.g. returns "$", "¥", or "CNY").
const char *currencySymbol(const char *code);

/// Formats minor units with the given decimal places: (4210, 2) → "42.10".
void formatBalanceMinor(uint32_t minor, uint8_t decimals, char *buf, size_t bufSize);

}  // namespace stopwatch
