// firmware/src/SnapshotStore.cpp
#include "SnapshotStore.h"
#include <Preferences.h>

namespace stopwatch {

namespace {
Preferences prefs;
constexpr const char *kNs = "swq";
constexpr const char *kCarouselSettingsKey = "carousel";

struct StoredCarouselSettings {
    uint8_t version;
    uint8_t autoplayEnabled;
    uint8_t motionMode;
    uint8_t reserved;
    uint16_t intervalSeconds;
    uint16_t resumeSeconds;
};
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
    StoredCarouselSettings stored{};
    size_t sz = prefs.getBytesLength(kCarouselSettingsKey);
    if (sz != sizeof(stored)) return false;
    size_t read = prefs.getBytes(kCarouselSettingsKey, &stored, sizeof(stored));
    if (read != sizeof(stored) || stored.version != 1) return false;

    out.autoplayEnabled = stored.autoplayEnabled != 0;
    out.motionMode = (CarouselMotionMode)stored.motionMode;
    out.intervalSeconds = stored.intervalSeconds;
    out.resumeSeconds = stored.resumeSeconds;
    out.validate();
    return true;
}

void SnapshotStore::saveCarouselSettings(const CarouselSettings &settings) {
    if (!open_) return;
    CarouselSettings copy = settings;
    copy.validate();
    StoredCarouselSettings stored{};
    stored.version = 1;
    stored.autoplayEnabled = copy.autoplayEnabled ? 1 : 0;
    stored.motionMode = (uint8_t)copy.motionMode;
    stored.intervalSeconds = copy.intervalSeconds;
    stored.resumeSeconds = copy.resumeSeconds;
    prefs.putBytes(kCarouselSettingsKey, &stored, sizeof(stored));
}

}  // namespace stopwatch
