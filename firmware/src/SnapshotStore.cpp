// firmware/src/SnapshotStore.cpp
#include "SettingsCodec.h"
#include "SnapshotStore.h"
#include <Preferences.h>

namespace stopwatch {

namespace {
Preferences prefs;
constexpr const char *kNs = "swq";
constexpr const char *kCarouselSettingsKey = "carousel";
}

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

bool SnapshotStore::loadCarouselSettings(CarouselSettings &out) {
    if (!open_) return false;
    uint8_t bytes[kSettingsBytesSize];
    size_t sz = prefs.getBytesLength(kCarouselSettingsKey);
    if (sz != sizeof(bytes)) return false;
    size_t read = prefs.getBytes(kCarouselSettingsKey, bytes, sizeof(bytes));
    return decodeCarouselSettings(bytes, read, out);
}

void SnapshotStore::saveCarouselSettings(const CarouselSettings &settings) {
    if (!open_) return;
    uint8_t bytes[kSettingsBytesSize];
    size_t len = 0;
    if (!encodeCarouselSettings(settings, bytes, sizeof(bytes), len)) return;
    prefs.putBytes(kCarouselSettingsKey, bytes, len);
}

}  // namespace stopwatch
