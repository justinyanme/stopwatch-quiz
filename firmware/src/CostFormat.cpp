#include "CostFormat.h"
#include <cstdio>

namespace stopwatch {

void formatDollars(uint32_t cents, char *buf, size_t bufSize, bool twoDecimals) {
    if (twoDecimals) {
        snprintf(buf, bufSize, "$%u.%02u", cents / 100, cents % 100);
    } else {
        snprintf(buf, bufSize, "$%u", (cents + 50) / 100);  // round to nearest dollar
    }
}

void humanizeTokens(uint32_t tokens, char *buf, size_t bufSize) {
    if (tokens >= 1000000u)      snprintf(buf, bufSize, "%uM", tokens / 1000000u);
    else if (tokens >= 1000u)    snprintf(buf, bufSize, "%uk", tokens / 1000u);
    else                         snprintf(buf, bufSize, "%u", tokens);
}

}  // namespace stopwatch
