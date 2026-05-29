// firmware/src/SnapshotStore.cpp
#include "SnapshotStore.h"
#include <Preferences.h>

namespace stopwatch {

namespace { Preferences prefs; constexpr const char *kNs = "swq"; }

void SnapshotStore::begin() {
    open_ = prefs.begin(kNs, false);
}

bool SnapshotStore::load(const char *key, uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    if (!open_) return false;
    size_t sz = prefs.getBytesLength(key);
    if (sz == 0 || sz > bufSize) { outLen = 0; return false; }
    outLen = prefs.getBytes(key, outBytes, sz);
    return outLen == sz;
}

void SnapshotStore::save(const char *key, const uint8_t *bytes, size_t len) {
    if (!open_) return;
    prefs.putBytes(key, bytes, len);
}

}  // namespace stopwatch
