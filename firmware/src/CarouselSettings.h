#pragma once
#include <cstdint>

namespace stopwatch {

enum class CarouselMotionMode : uint8_t { Iris = 0, Fade = 1, Instant = 2 };
enum class CarouselSettingRow : uint8_t {
    Upright = 0,
    Autoplay = 1,
    Interval = 2,
    Motion = 3,
    Resume = 4,
};

struct CarouselSettings {
    bool uprightEnabled = false;
    bool autoplayEnabled = true;
    uint16_t intervalSeconds = 10;
    CarouselMotionMode motionMode = CarouselMotionMode::Iris;
    uint16_t resumeSeconds = 20;

    static constexpr uint8_t kRowCount = 5;

    static CarouselSettings defaults() { return CarouselSettings{}; }

    void validate() {
        if (!isAllowedInterval(intervalSeconds)) intervalSeconds = 10;
        if (!isAllowedResume(resumeSeconds)) resumeSeconds = 20;
        if (motionMode != CarouselMotionMode::Iris &&
            motionMode != CarouselMotionMode::Fade &&
            motionMode != CarouselMotionMode::Instant) {
            motionMode = CarouselMotionMode::Iris;
        }
    }

    void resetDefaults() { *this = defaults(); }

    void cycle(CarouselSettingRow row) {
        switch (row) {
            case CarouselSettingRow::Upright:
                uprightEnabled = !uprightEnabled;
                break;
            case CarouselSettingRow::Autoplay:
                autoplayEnabled = !autoplayEnabled;
                break;
            case CarouselSettingRow::Interval:
                intervalSeconds = nextInterval(intervalSeconds);
                break;
            case CarouselSettingRow::Motion:
                motionMode = nextMotion(motionMode);
                break;
            case CarouselSettingRow::Resume:
                resumeSeconds = nextResume(resumeSeconds);
                break;
        }
    }

    static const char *rowLabel(CarouselSettingRow row) {
        switch (row) {
            case CarouselSettingRow::Upright:  return "UPRIGHT";
            case CarouselSettingRow::Autoplay: return "AUTOPLAY";
            case CarouselSettingRow::Interval: return "INTERVAL";
            case CarouselSettingRow::Motion:   return "MOTION";
            case CarouselSettingRow::Resume:   return "RESUME";
        }
        return "?";
    }

    static const char *motionLabel(CarouselMotionMode mode) {
        switch (mode) {
            case CarouselMotionMode::Iris:    return "IRIS";
            case CarouselMotionMode::Fade:    return "FADE";
            case CarouselMotionMode::Instant: return "INSTANT";
        }
        return "IRIS";
    }

private:
    static bool isAllowedInterval(uint16_t v) {
        return v == 5 || v == 10 || v == 15 || v == 30;
    }

    static bool isAllowedResume(uint16_t v) {
        return v == 10 || v == 20 || v == 30;
    }

    static uint16_t nextInterval(uint16_t v) {
        switch (v) {
            case 5:  return 10;
            case 10: return 15;
            case 15: return 30;
            default: return 5;
        }
    }

    static uint16_t nextResume(uint16_t v) {
        switch (v) {
            case 10: return 20;
            case 20: return 30;
            default: return 10;
        }
    }

    static CarouselMotionMode nextMotion(CarouselMotionMode mode) {
        switch (mode) {
            case CarouselMotionMode::Iris:    return CarouselMotionMode::Fade;
            case CarouselMotionMode::Fade:    return CarouselMotionMode::Instant;
            case CarouselMotionMode::Instant: return CarouselMotionMode::Iris;
        }
        return CarouselMotionMode::Iris;
    }
};

inline CarouselSettingRow nextSettingRow(CarouselSettingRow row) {
    switch (row) {
        case CarouselSettingRow::Upright:  return CarouselSettingRow::Autoplay;
        case CarouselSettingRow::Autoplay: return CarouselSettingRow::Interval;
        case CarouselSettingRow::Interval: return CarouselSettingRow::Motion;
        case CarouselSettingRow::Motion:   return CarouselSettingRow::Resume;
        case CarouselSettingRow::Resume:   return CarouselSettingRow::Upright;
    }
    return CarouselSettingRow::Upright;
}

}  // namespace stopwatch
