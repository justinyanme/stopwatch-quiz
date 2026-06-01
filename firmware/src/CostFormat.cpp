#include "CostFormat.h"
#include <cstdio>
#include <cstring>

namespace stopwatch {

namespace {

void appendBounded(char *buf, size_t bufSize, const char *suffix) {
    if (bufSize == 0) return;

    size_t used = 0;
    while (used < bufSize && buf[used] != '\0') {
        ++used;
    }

    if (used == bufSize) {
        buf[bufSize - 1] = '\0';
        return;
    }

    snprintf(buf + used, bufSize - used, "%s", suffix);
}

}  // namespace

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
        if (shown > 0) appendBounded(buf, bufSize, " \xC2\xB7 ");  // " · "
        appendBounded(buf, bufSize, r.models[i]);
        ++shown;
    }
    int extra = (int)r.modelCount - shown;
    if (extra > 0) {
        char tail[8];
        snprintf(tail, sizeof(tail), " +%d", extra);
        appendBounded(buf, bufSize, tail);
    }
}

}  // namespace stopwatch
