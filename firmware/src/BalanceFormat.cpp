#include "BalanceFormat.h"
#include <cstdio>
#include <cstring>

namespace stopwatch {

const char *currencySymbol(const char *code) {
    if (strcmp(code, "USD") == 0) return "$";
    if (strcmp(code, "CNY") == 0 || strcmp(code, "JPY") == 0) return "\xC2\xA5";  // ¥
    if (strcmp(code, "GBP") == 0) return "\xC2\xA3";                              // £
    return code;   // unknown → show the raw code (e.g. "EUR")
}

void formatBalanceMinor(uint32_t minor, uint8_t decimals, char *buf, size_t bufSize) {
    if (decimals == 0) { snprintf(buf, bufSize, "%u", minor); return; }
    uint32_t scale = 1; for (uint8_t i = 0; i < decimals; ++i) scale *= 10;
    uint32_t whole = minor / scale;
    uint32_t frac  = minor % scale;
    snprintf(buf, bufSize, "%u.%0*u", whole, (int)decimals, frac);
}

}  // namespace stopwatch
