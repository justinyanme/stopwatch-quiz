#pragma once
#include <cstdint>
#include <cstddef>
#include "CostCodec.h"

namespace stopwatch {

/// "$21.90" (twoDecimals) or "$415" (rounded whole dollars).
void formatDollars(uint32_t cents, char *buf, size_t bufSize, bool twoDecimals);

/// "391M" / "999k" / "500".
void humanizeTokens(uint32_t tokens, char *buf, size_t bufSize);

/// Joins a record's carried model names (token-ordered, up to kCostMaxModelSlots)
/// with " · " (U+00B7), appending " +N" when the day used more models than were
/// carried. Writes "" when no models. e.g. "opus-4-8 · sonnet-4-6 · haiku-4-5".
void costModelsLine(const CostRecord &r, char *buf, size_t bufSize);

}  // namespace stopwatch
