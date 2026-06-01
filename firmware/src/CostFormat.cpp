#include "CostFormat.h"
#include <cstdio>
#include <cstring>

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

void costModelsLine(const CostRecord &r, char *buf, size_t bufSize) {
    if (bufSize == 0) return;
    buf[0] = '\0';
    int shown = 0;
    for (int i = 0; i < kCostMaxModelSlots; ++i) {
        if (r.models[i][0] == '\0') break;
        if (shown > 0) strlcat(buf, " \xC2\xB7 ", bufSize);  // " · "
        strlcat(buf, r.models[i], bufSize);
        ++shown;
    }
    int extra = (int)r.modelCount - shown;
    if (extra > 0) {
        char tail[8];
        snprintf(tail, sizeof(tail), " +%d", extra);
        strlcat(buf, tail, bufSize);
    }
}

}  // namespace stopwatch
