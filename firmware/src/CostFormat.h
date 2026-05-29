#pragma once
#include <cstdint>
#include <cstddef>

namespace stopwatch {

/// "$21.90" (twoDecimals) or "$415" (rounded whole dollars).
void formatDollars(uint32_t cents, char *buf, size_t bufSize, bool twoDecimals);

/// "391M" / "999k" / "500".
void humanizeTokens(uint32_t tokens, char *buf, size_t bufSize);

}  // namespace stopwatch
