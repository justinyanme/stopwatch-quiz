# Upright Orientation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an off-by-default `UPRIGHT` setting that uses the StopWatch IMU to keep the UI upright at `0/90/180/270` degree cardinal rotations while awake.

**Architecture:** Add a pure native-testable `OrientationController` that converts accelerometer samples into debounced cardinal display orientation commits. Extend existing carousel settings rather than renaming the runtime type, then update persistence, the settings view, the renderer, and `main.cpp` IMU polling around that controller.

**Tech Stack:** PlatformIO, Arduino, M5Unified, LovyanGFX/M5Canvas, C++17, Unity native firmware tests, existing `App`, `Renderer`, `SnapshotStore`, and settings-screen patterns.

---

## File Structure

- Create `firmware/src/OrientationController.h`: display orientation enum, accelerometer sample type, and pure orientation controller API.
- Create `firmware/src/OrientationController.cpp`: accelerometer validation, cardinal candidate selection, hysteresis, and 300 ms debounce.
- Create `firmware/test/test_orientation_controller/test_main.cpp`: native unit tests for orientation commits and rejected samples.
- Modify `firmware/src/CarouselSettings.h`: add `uprightEnabled`, add `Upright` row, keep existing carousel fields.
- Modify `firmware/test/test_state_machine/test_main.cpp`: update settings defaults, cycling, reset, and row-order tests.
- Create `firmware/src/SettingsCodec.h`: pure byte codec API for settings persistence.
- Create `firmware/src/SettingsCodec.cpp`: backward-compatible decode of v1 carousel settings and v2 upright settings.
- Create `firmware/test/test_settings_codec/test_main.cpp`: native unit tests for settings persistence encoding/decoding.
- Modify `firmware/src/SnapshotStore.cpp`: delegate settings persistence to `SettingsCodec`.
- Modify `firmware/src/Views/CarouselSettings.cpp`: render grouped `SETTINGS` screen with `DISPLAY` and `CAROUSEL`.
- Modify `firmware/src/Renderer.h` and `firmware/src/Renderer.cpp`: own display orientation and apply `M5.Display.setRotation`.
- Modify `firmware/src/main.cpp`: initialize orientation runtime, poll `M5.Imu.getAccel`, apply commits, reset orientation on wake/off.
- Modify `README.md`: document the local settings panel and `UPRIGHT` behavior.

---

### Task 1: Orientation Controller Tests

**Files:**
- Create: `firmware/test/test_orientation_controller/test_main.cpp`
- Create in Task 2: `firmware/src/OrientationController.h`
- Create in Task 2: `firmware/src/OrientationController.cpp`

- [ ] **Step 1: Write the failing tests**

Create `firmware/test/test_orientation_controller/test_main.cpp`:

```cpp
#include <unity.h>
#include "../../src/OrientationController.h"

using namespace stopwatch;

static OrientationSample sample(float ax, float ay, float az = 0.0f) {
    OrientationSample s{ax, ay, az};
    return s;
}

static void assertOrientation(DisplayOrientation expected, DisplayOrientation actual) {
    TEST_ASSERT_EQUAL((int)expected, (int)actual);
}

void test_begins_at_initial_orientation(void) {
    OrientationController c;
    c.begin(100, DisplayOrientation::Deg0);

    assertOrientation(DisplayOrientation::Deg0, c.committed());
}

void test_debounces_new_cardinal_orientation(void) {
    OrientationController c;
    c.begin(1000, DisplayOrientation::Deg0);

    TEST_ASSERT_FALSE(c.tick(1000, sample(1.0f, 0.0f)));
    assertOrientation(DisplayOrientation::Deg0, c.committed());

    TEST_ASSERT_FALSE(c.tick(1299, sample(1.0f, 0.0f)));
    assertOrientation(DisplayOrientation::Deg0, c.committed());

    TEST_ASSERT_TRUE(c.tick(1300, sample(1.0f, 0.0f)));
    assertOrientation(DisplayOrientation::Deg90, c.committed());
}

void test_candidate_change_restarts_debounce(void) {
    OrientationController c;
    c.begin(2000, DisplayOrientation::Deg0);

    TEST_ASSERT_FALSE(c.tick(2000, sample(1.0f, 0.0f)));
    TEST_ASSERT_FALSE(c.tick(2150, sample(0.0f, 1.0f)));
    TEST_ASSERT_FALSE(c.tick(2449, sample(0.0f, 1.0f)));
    assertOrientation(DisplayOrientation::Deg0, c.committed());

    TEST_ASSERT_TRUE(c.tick(2450, sample(0.0f, 1.0f)));
    assertOrientation(DisplayOrientation::Deg180, c.committed());
}

void test_rejects_flat_or_invalid_samples(void) {
    OrientationController c;
    c.begin(0, DisplayOrientation::Deg0);

    TEST_ASSERT_FALSE(c.tick(0, sample(0.05f, -0.05f, 1.0f)));
    TEST_ASSERT_FALSE(c.tick(1000, sample(0.05f, -0.05f, 1.0f)));
    assertOrientation(DisplayOrientation::Deg0, c.committed());

    TEST_ASSERT_FALSE(c.tick(2000, sample(0.0f, 0.0f, 0.0f)));
    assertOrientation(DisplayOrientation::Deg0, c.committed());
}

void test_hysteresis_keeps_current_orientation_near_diagonal_boundary(void) {
    OrientationController c;
    c.begin(0, DisplayOrientation::Deg0);

    TEST_ASSERT_FALSE(c.tick(0, sample(0.66f, -0.70f)));
    TEST_ASSERT_FALSE(c.tick(1000, sample(0.66f, -0.70f)));
    assertOrientation(DisplayOrientation::Deg0, c.committed());
}

void test_commits_all_four_cardinal_orientations(void) {
    OrientationController c;
    c.begin(0, DisplayOrientation::Deg0);

    TEST_ASSERT_FALSE(c.tick(0, sample(1.0f, 0.0f)));
    TEST_ASSERT_TRUE(c.tick(300, sample(1.0f, 0.0f)));
    assertOrientation(DisplayOrientation::Deg90, c.committed());

    TEST_ASSERT_FALSE(c.tick(600, sample(0.0f, 1.0f)));
    TEST_ASSERT_TRUE(c.tick(900, sample(0.0f, 1.0f)));
    assertOrientation(DisplayOrientation::Deg180, c.committed());

    TEST_ASSERT_FALSE(c.tick(1200, sample(-1.0f, 0.0f)));
    TEST_ASSERT_TRUE(c.tick(1500, sample(-1.0f, 0.0f)));
    assertOrientation(DisplayOrientation::Deg270, c.committed());

    TEST_ASSERT_FALSE(c.tick(1800, sample(0.0f, -1.0f)));
    TEST_ASSERT_TRUE(c.tick(2100, sample(0.0f, -1.0f)));
    assertOrientation(DisplayOrientation::Deg0, c.committed());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_begins_at_initial_orientation);
    RUN_TEST(test_debounces_new_cardinal_orientation);
    RUN_TEST(test_candidate_change_restarts_debounce);
    RUN_TEST(test_rejects_flat_or_invalid_samples);
    RUN_TEST(test_hysteresis_keeps_current_orientation_near_diagonal_boundary);
    RUN_TEST(test_commits_all_four_cardinal_orientations);
    return UNITY_END();
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run:

```bash
cd firmware && pio test -e native -f test_orientation_controller
```

Expected: compile failure because `firmware/src/OrientationController.h` does not exist.

---

### Task 2: Orientation Controller Implementation

**Files:**
- Create: `firmware/src/OrientationController.h`
- Create: `firmware/src/OrientationController.cpp`
- Test: `firmware/test/test_orientation_controller/test_main.cpp`

- [ ] **Step 1: Add the controller header**

Create `firmware/src/OrientationController.h`:

```cpp
#pragma once
#include <cstdint>

namespace stopwatch {

enum class DisplayOrientation : uint8_t {
    Deg0 = 0,
    Deg90 = 1,
    Deg180 = 2,
    Deg270 = 3,
};

struct OrientationSample {
    float ax;
    float ay;
    float az;
};

class OrientationController {
public:
    static constexpr uint32_t kDebounceMs = 300;

    void begin(uint32_t nowMs, DisplayOrientation initial);
    void reset(uint32_t nowMs, DisplayOrientation orientation);
    bool tick(uint32_t nowMs, OrientationSample sample);
    DisplayOrientation committed() const { return committed_; }

private:
    DisplayOrientation committed_ = DisplayOrientation::Deg0;
    DisplayOrientation pending_ = DisplayOrientation::Deg0;
    uint32_t pendingSinceMs_ = 0;
};

}  // namespace stopwatch
```

- [ ] **Step 2: Add the controller implementation**

Create `firmware/src/OrientationController.cpp`:

```cpp
#include "OrientationController.h"
#include <cmath>

namespace stopwatch {
namespace {

constexpr float kMinTotalG = 0.50f;
constexpr float kMaxTotalG = 1.70f;
constexpr float kMinPlanarG = 0.35f;
constexpr float kAxisHysteresisMargin = 0.12f;

bool finiteSample(OrientationSample s) {
    return std::isfinite(s.ax) && std::isfinite(s.ay) && std::isfinite(s.az);
}

bool candidateFromSample(OrientationSample s,
                         DisplayOrientation fallback,
                         DisplayOrientation &out) {
    if (!finiteSample(s)) return false;

    float total = std::sqrt(s.ax * s.ax + s.ay * s.ay + s.az * s.az);
    if (total < kMinTotalG || total > kMaxTotalG) return false;

    float planar = std::sqrt(s.ax * s.ax + s.ay * s.ay);
    if (planar < kMinPlanarG) return false;

    float absX = std::fabs(s.ax);
    float absY = std::fabs(s.ay);

    if (absX > absY + kAxisHysteresisMargin) {
        out = s.ax >= 0.0f ? DisplayOrientation::Deg90 : DisplayOrientation::Deg270;
        return true;
    }

    if (absY > absX + kAxisHysteresisMargin) {
        out = s.ay >= 0.0f ? DisplayOrientation::Deg180 : DisplayOrientation::Deg0;
        return true;
    }

    out = fallback;
    return true;
}

}  // namespace

void OrientationController::begin(uint32_t nowMs, DisplayOrientation initial) {
    reset(nowMs, initial);
}

void OrientationController::reset(uint32_t nowMs, DisplayOrientation orientation) {
    committed_ = orientation;
    pending_ = orientation;
    pendingSinceMs_ = nowMs;
}

bool OrientationController::tick(uint32_t nowMs, OrientationSample sample) {
    DisplayOrientation candidate = committed_;
    if (!candidateFromSample(sample, committed_, candidate)) {
        pending_ = committed_;
        pendingSinceMs_ = nowMs;
        return false;
    }

    if (candidate == committed_) {
        pending_ = committed_;
        pendingSinceMs_ = nowMs;
        return false;
    }

    if (candidate != pending_) {
        pending_ = candidate;
        pendingSinceMs_ = nowMs;
        return false;
    }

    if ((uint32_t)(nowMs - pendingSinceMs_) < kDebounceMs) return false;

    committed_ = candidate;
    pending_ = candidate;
    pendingSinceMs_ = nowMs;
    return true;
}

}  // namespace stopwatch
```

- [ ] **Step 3: Run the orientation tests**

Run:

```bash
cd firmware && pio test -e native -f test_orientation_controller
```

Expected: all six tests pass.

- [ ] **Step 4: Commit**

```bash
git add firmware/src/OrientationController.h firmware/src/OrientationController.cpp firmware/test/test_orientation_controller/test_main.cpp
git commit -m "firmware: add upright orientation controller"
```

---

### Task 3: Settings Model And State-Machine Tests

**Files:**
- Modify: `firmware/src/CarouselSettings.h`
- Modify: `firmware/test/test_state_machine/test_main.cpp`

- [ ] **Step 1: Update state-machine tests first**

In `firmware/test/test_state_machine/test_main.cpp`, replace the existing settings tests from `test_carouselSettingsDefaultsAndValidation` through `test_carouselSettingsBlocksBalanceDetailEntry` with this block:

```cpp
void test_carouselSettingsDefaultsAndValidation(void) {
    CarouselSettings s = CarouselSettings::defaults();
    TEST_ASSERT_FALSE(s.uprightEnabled);
    TEST_ASSERT_TRUE(s.autoplayEnabled);
    TEST_ASSERT_EQUAL_UINT16(10, s.intervalSeconds);
    TEST_ASSERT_EQUAL((int)CarouselMotionMode::Iris, (int)s.motionMode);
    TEST_ASSERT_EQUAL_UINT16(20, s.resumeSeconds);

    CarouselSettings invalid;
    invalid.uprightEnabled = true;
    invalid.autoplayEnabled = true;
    invalid.intervalSeconds = 7;
    invalid.motionMode = (CarouselMotionMode)99;
    invalid.resumeSeconds = 11;
    invalid.validate();

    TEST_ASSERT_TRUE(invalid.uprightEnabled);
    TEST_ASSERT_EQUAL_UINT16(10, invalid.intervalSeconds);
    TEST_ASSERT_EQUAL((int)CarouselMotionMode::Iris, (int)invalid.motionMode);
    TEST_ASSERT_EQUAL_UINT16(20, invalid.resumeSeconds);
}

void test_carouselSettingsCyclesValues(void) {
    CarouselSettings s = CarouselSettings::defaults();

    s.cycle(CarouselSettingRow::Upright);
    TEST_ASSERT_TRUE(s.uprightEnabled);
    s.cycle(CarouselSettingRow::Upright);
    TEST_ASSERT_FALSE(s.uprightEnabled);

    s.cycle(CarouselSettingRow::Autoplay);
    TEST_ASSERT_FALSE(s.autoplayEnabled);
    s.cycle(CarouselSettingRow::Autoplay);
    TEST_ASSERT_TRUE(s.autoplayEnabled);

    s.cycle(CarouselSettingRow::Interval);
    TEST_ASSERT_EQUAL_UINT16(15, s.intervalSeconds);
    s.cycle(CarouselSettingRow::Interval);
    TEST_ASSERT_EQUAL_UINT16(30, s.intervalSeconds);
    s.cycle(CarouselSettingRow::Interval);
    TEST_ASSERT_EQUAL_UINT16(5, s.intervalSeconds);

    s.cycle(CarouselSettingRow::Motion);
    TEST_ASSERT_EQUAL((int)CarouselMotionMode::Fade, (int)s.motionMode);
    s.cycle(CarouselSettingRow::Motion);
    TEST_ASSERT_EQUAL((int)CarouselMotionMode::Instant, (int)s.motionMode);
    s.cycle(CarouselSettingRow::Motion);
    TEST_ASSERT_EQUAL((int)CarouselMotionMode::Iris, (int)s.motionMode);

    s.cycle(CarouselSettingRow::Resume);
    TEST_ASSERT_EQUAL_UINT16(30, s.resumeSeconds);
    s.cycle(CarouselSettingRow::Resume);
    TEST_ASSERT_EQUAL_UINT16(10, s.resumeSeconds);
}

void test_bothLongEntersAndExitsCarouselSettings(void) {
    App app; app.begin();
    CarouselSettings settings = CarouselSettings::defaults();

    TEST_ASSERT_FALSE(app.inCarouselSettings());
    bool changed = app.handleEvent(ButtonEvent::BothLong, settings);
    TEST_ASSERT_TRUE(changed);
    TEST_ASSERT_TRUE(app.inCarouselSettings());
    TEST_ASSERT_EQUAL((int)CarouselSettingRow::Upright, (int)app.carouselSettingRow());

    changed = app.handleEvent(ButtonEvent::BothLong, settings);
    TEST_ASSERT_TRUE(changed);
    TEST_ASSERT_FALSE(app.inCarouselSettings());
}

void test_carouselSettingsRowsAndValuesChange(void) {
    App app; app.begin();
    CarouselSettings settings = CarouselSettings::defaults();
    app.handleEvent(ButtonEvent::BothLong, settings);

    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyAShort, settings));
    TEST_ASSERT_TRUE(settings.uprightEnabled);

    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyBShort, settings));
    TEST_ASSERT_EQUAL((int)CarouselSettingRow::Autoplay, (int)app.carouselSettingRow());
    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyAShort, settings));
    TEST_ASSERT_FALSE(settings.autoplayEnabled);

    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyBShort, settings));
    TEST_ASSERT_EQUAL((int)CarouselSettingRow::Interval, (int)app.carouselSettingRow());
    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyAShort, settings));
    TEST_ASSERT_EQUAL_UINT16(15, settings.intervalSeconds);
}

void test_carouselSettingsResetDefaults(void) {
    App app; app.begin();
    CarouselSettings settings = CarouselSettings::defaults();
    app.handleEvent(ButtonEvent::BothLong, settings);
    app.handleEvent(ButtonEvent::KeyAShort, settings);
    TEST_ASSERT_TRUE(settings.uprightEnabled);

    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyALong, settings));
    TEST_ASSERT_FALSE(settings.uprightEnabled);
    TEST_ASSERT_TRUE(settings.autoplayEnabled);
    TEST_ASSERT_EQUAL_UINT16(10, settings.intervalSeconds);
    TEST_ASSERT_EQUAL((int)CarouselMotionMode::Iris, (int)settings.motionMode);
    TEST_ASSERT_EQUAL_UINT16(20, settings.resumeSeconds);
}

void test_carouselSettingsSleepStillWorks(void) {
    App app; app.begin();
    CarouselSettings settings = CarouselSettings::defaults();
    app.handleEvent(ButtonEvent::BothLong, settings);
    TEST_ASSERT_FALSE(app.wantsImmediateSleep());
    TEST_ASSERT_FALSE(app.handleEvent(ButtonEvent::KeyBLong, settings));
    TEST_ASSERT_TRUE(app.wantsImmediateSleep());
    TEST_ASSERT_TRUE(app.inCarouselSettings());
}

void test_carouselSettingsBlocksBalanceDetailEntry(void) {
    App app; app.begin();
    CarouselSettings settings = CarouselSettings::defaults();
    app.handleEvent(ButtonEvent::KeyAShort);     // Overview -> Balances
    TEST_ASSERT_EQUAL((int)ViewId::Balances, (int)app.currentView());

    app.handleEvent(ButtonEvent::BothLong, settings);
    TEST_ASSERT_TRUE(app.inCarouselSettings());
    app.enterBalanceDetail(1);

    TEST_ASSERT_FALSE(app.inBalanceDetail());
    TEST_ASSERT_TRUE(app.inCarouselSettings());
    TEST_ASSERT_EQUAL((int)ViewId::Balances, (int)app.currentView());
}
```

Keep the existing `RUN_TEST(...)` calls at the bottom; their function names do not change.

- [ ] **Step 2: Run the state-machine tests to verify failure**

Run:

```bash
cd firmware && pio test -e native -f test_state_machine
```

Expected: compile failure because `CarouselSettings` has no `uprightEnabled` field and `CarouselSettingRow::Upright` does not exist.

- [ ] **Step 3: Replace `CarouselSettings.h`**

Replace `firmware/src/CarouselSettings.h` with:

```cpp
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
```

- [ ] **Step 4: Update `App.cpp` settings entry row**

In `firmware/src/App.cpp`, change both assignments of `settingRow_ = CarouselSettingRow::Autoplay;` to:

```cpp
settingRow_ = CarouselSettingRow::Upright;
```

The two locations are in `App::begin()` and the `ButtonEvent::BothLong` branch of `App::handleEvent(ButtonEvent ev, CarouselSettings &settings)`.

- [ ] **Step 5: Run state-machine tests**

Run:

```bash
cd firmware && pio test -e native -f test_state_machine
```

Expected: all state-machine tests pass.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/CarouselSettings.h firmware/src/App.cpp firmware/test/test_state_machine/test_main.cpp
git commit -m "firmware: add upright setting state"
```

---

### Task 4: Settings Persistence Codec

**Files:**
- Create: `firmware/src/SettingsCodec.h`
- Create: `firmware/src/SettingsCodec.cpp`
- Create: `firmware/test/test_settings_codec/test_main.cpp`
- Modify: `firmware/src/SnapshotStore.cpp`

- [ ] **Step 1: Write settings-codec tests**

Create `firmware/test/test_settings_codec/test_main.cpp`:

```cpp
#include <unity.h>
#include "../../src/SettingsCodec.h"

using namespace stopwatch;

void test_roundTripsVersion2Settings(void) {
    CarouselSettings in = CarouselSettings::defaults();
    in.uprightEnabled = true;
    in.autoplayEnabled = false;
    in.intervalSeconds = 30;
    in.motionMode = CarouselMotionMode::Instant;
    in.resumeSeconds = 10;

    uint8_t bytes[kSettingsBytesSize];
    size_t len = 0;
    TEST_ASSERT_TRUE(encodeCarouselSettings(in, bytes, sizeof(bytes), len));
    TEST_ASSERT_EQUAL_UINT(kSettingsBytesSize, len);
    TEST_ASSERT_EQUAL_UINT8(2, bytes[0]);

    CarouselSettings out;
    TEST_ASSERT_TRUE(decodeCarouselSettings(bytes, len, out));
    TEST_ASSERT_TRUE(out.uprightEnabled);
    TEST_ASSERT_FALSE(out.autoplayEnabled);
    TEST_ASSERT_EQUAL_UINT16(30, out.intervalSeconds);
    TEST_ASSERT_EQUAL((int)CarouselMotionMode::Instant, (int)out.motionMode);
    TEST_ASSERT_EQUAL_UINT16(10, out.resumeSeconds);
}

void test_decodesVersion1WithUprightOff(void) {
    uint8_t bytes[kSettingsBytesSize] = {
        1, 1, (uint8_t)CarouselMotionMode::Fade, 0,
        15, 0,
        30, 0,
    };

    CarouselSettings out;
    TEST_ASSERT_TRUE(decodeCarouselSettings(bytes, sizeof(bytes), out));
    TEST_ASSERT_FALSE(out.uprightEnabled);
    TEST_ASSERT_TRUE(out.autoplayEnabled);
    TEST_ASSERT_EQUAL_UINT16(15, out.intervalSeconds);
    TEST_ASSERT_EQUAL((int)CarouselMotionMode::Fade, (int)out.motionMode);
    TEST_ASSERT_EQUAL_UINT16(30, out.resumeSeconds);
}

void test_rejectsWrongSizeAndUnknownVersion(void) {
    CarouselSettings out;
    uint8_t shortBytes[3] = {2, 0, 0};
    TEST_ASSERT_FALSE(decodeCarouselSettings(shortBytes, sizeof(shortBytes), out));

    uint8_t unknown[kSettingsBytesSize] = {99, 0, 0, 0, 10, 0, 20, 0};
    TEST_ASSERT_FALSE(decodeCarouselSettings(unknown, sizeof(unknown), out));
}

void test_decodeClampsInvalidStoredValues(void) {
    uint8_t bytes[kSettingsBytesSize] = {
        2, 0x03, 99, 0,
        7, 0,
        11, 0,
    };

    CarouselSettings out;
    TEST_ASSERT_TRUE(decodeCarouselSettings(bytes, sizeof(bytes), out));
    TEST_ASSERT_TRUE(out.uprightEnabled);
    TEST_ASSERT_TRUE(out.autoplayEnabled);
    TEST_ASSERT_EQUAL_UINT16(10, out.intervalSeconds);
    TEST_ASSERT_EQUAL((int)CarouselMotionMode::Iris, (int)out.motionMode);
    TEST_ASSERT_EQUAL_UINT16(20, out.resumeSeconds);
}

void test_encodeFailsWhenBufferTooSmall(void) {
    CarouselSettings in = CarouselSettings::defaults();
    uint8_t bytes[kSettingsBytesSize - 1];
    size_t len = 123;

    TEST_ASSERT_FALSE(encodeCarouselSettings(in, bytes, sizeof(bytes), len));
    TEST_ASSERT_EQUAL_UINT(0, len);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_roundTripsVersion2Settings);
    RUN_TEST(test_decodesVersion1WithUprightOff);
    RUN_TEST(test_rejectsWrongSizeAndUnknownVersion);
    RUN_TEST(test_decodeClampsInvalidStoredValues);
    RUN_TEST(test_encodeFailsWhenBufferTooSmall);
    return UNITY_END();
}
```

- [ ] **Step 2: Run codec tests to verify failure**

Run:

```bash
cd firmware && pio test -e native -f test_settings_codec
```

Expected: compile failure because `SettingsCodec.h` does not exist.

- [ ] **Step 3: Add codec header**

Create `firmware/src/SettingsCodec.h`:

```cpp
#pragma once
#include "CarouselSettings.h"
#include <cstddef>
#include <cstdint>

namespace stopwatch {

constexpr size_t kSettingsBytesSize = 8;

bool encodeCarouselSettings(const CarouselSettings &settings,
                            uint8_t *outBytes,
                            size_t capacity,
                            size_t &outLen);

bool decodeCarouselSettings(const uint8_t *bytes,
                            size_t len,
                            CarouselSettings &out);

}  // namespace stopwatch
```

- [ ] **Step 4: Add codec implementation**

Create `firmware/src/SettingsCodec.cpp`:

```cpp
#include "SettingsCodec.h"

namespace stopwatch {
namespace {

constexpr uint8_t kVersion1 = 1;
constexpr uint8_t kVersion2 = 2;
constexpr uint8_t kFlagAutoplay = 0x01;
constexpr uint8_t kFlagUpright = 0x02;

uint16_t readU16LE(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

void writeU16LE(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value & 0xFF);
    p[1] = (uint8_t)((value >> 8) & 0xFF);
}

}  // namespace

bool encodeCarouselSettings(const CarouselSettings &settings,
                            uint8_t *outBytes,
                            size_t capacity,
                            size_t &outLen) {
    outLen = 0;
    if (!outBytes || capacity < kSettingsBytesSize) return false;

    CarouselSettings copy = settings;
    copy.validate();

    uint8_t flags = 0;
    if (copy.autoplayEnabled) flags |= kFlagAutoplay;
    if (copy.uprightEnabled) flags |= kFlagUpright;

    outBytes[0] = kVersion2;
    outBytes[1] = flags;
    outBytes[2] = (uint8_t)copy.motionMode;
    outBytes[3] = 0;
    writeU16LE(outBytes + 4, copy.intervalSeconds);
    writeU16LE(outBytes + 6, copy.resumeSeconds);
    outLen = kSettingsBytesSize;
    return true;
}

bool decodeCarouselSettings(const uint8_t *bytes,
                            size_t len,
                            CarouselSettings &out) {
    if (!bytes || len != kSettingsBytesSize) return false;
    uint8_t version = bytes[0];
    if (version != kVersion1 && version != kVersion2) return false;

    CarouselSettings decoded = CarouselSettings::defaults();
    uint8_t flags = bytes[1];
    decoded.autoplayEnabled = (flags & kFlagAutoplay) != 0;
    decoded.uprightEnabled = version == kVersion2 && ((flags & kFlagUpright) != 0);
    decoded.motionMode = (CarouselMotionMode)bytes[2];
    decoded.intervalSeconds = readU16LE(bytes + 4);
    decoded.resumeSeconds = readU16LE(bytes + 6);
    decoded.validate();

    out = decoded;
    return true;
}

}  // namespace stopwatch
```

- [ ] **Step 5: Update `SnapshotStore.cpp` to use the codec**

In `firmware/src/SnapshotStore.cpp`, add this include:

```cpp
#include "SettingsCodec.h"
```

Remove the `StoredCarouselSettings` struct from the anonymous namespace. Keep `kCarouselSettingsKey`.

Replace `SnapshotStore::loadCarouselSettings` with:

```cpp
bool SnapshotStore::loadCarouselSettings(CarouselSettings &out) {
    if (!open_) return false;
    uint8_t bytes[kSettingsBytesSize];
    size_t sz = prefs.getBytesLength(kCarouselSettingsKey);
    if (sz != sizeof(bytes)) return false;
    size_t read = prefs.getBytes(kCarouselSettingsKey, bytes, sizeof(bytes));
    return decodeCarouselSettings(bytes, read, out);
}
```

Replace `SnapshotStore::saveCarouselSettings` with:

```cpp
void SnapshotStore::saveCarouselSettings(const CarouselSettings &settings) {
    if (!open_) return;
    uint8_t bytes[kSettingsBytesSize];
    size_t len = 0;
    if (!encodeCarouselSettings(settings, bytes, sizeof(bytes), len)) return;
    prefs.putBytes(kCarouselSettingsKey, bytes, len);
}
```

- [ ] **Step 6: Run codec and state-machine tests**

Run:

```bash
cd firmware && pio test -e native -f test_settings_codec -f test_state_machine
```

Expected: all tests pass.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/SettingsCodec.h firmware/src/SettingsCodec.cpp firmware/src/SnapshotStore.cpp firmware/test/test_settings_codec/test_main.cpp
git commit -m "firmware: persist upright setting"
```

---

### Task 5: Grouped Settings Screen

**Files:**
- Modify: `firmware/src/Views/CarouselSettings.cpp`

- [ ] **Step 1: Replace settings view rendering**

Replace `firmware/src/Views/CarouselSettings.cpp` with:

```cpp
#include "CarouselSettings.h"
#include "../Theme.h"
#include <cstddef>
#include <cstdio>

namespace stopwatch::views {

namespace {
constexpr uint32_t kSettingsBg = 0x05080C;
constexpr uint32_t kRowFill = 0x0C1218;
constexpr uint32_t kSelectedFill = 0x141D28;
constexpr int kRowX = 68;
constexpr int kRowW = 330;
constexpr int kRowH = 40;
constexpr int kRowR = 8;

const char *valueText(const CarouselSettings &settings, CarouselSettingRow row,
                      char *buf, size_t n) {
    switch (row) {
        case CarouselSettingRow::Upright:
            return settings.uprightEnabled ? "ON" : "OFF";
        case CarouselSettingRow::Autoplay:
            return settings.autoplayEnabled ? "ON" : "OFF";
        case CarouselSettingRow::Interval:
            snprintf(buf, n, "%us", (unsigned)settings.intervalSeconds);
            return buf;
        case CarouselSettingRow::Motion:
            return CarouselSettings::motionLabel(settings.motionMode);
        case CarouselSettingRow::Resume:
            snprintf(buf, n, "%us", (unsigned)settings.resumeSeconds);
            return buf;
    }
    return "?";
}

void drawGroup(M5Canvas &c, const char *label, int y) {
    c.setFont(theme::kFontMicro);
    c.setTextDatum(middle_left);
    c.setTextColor(theme::kTextMuted);
    c.drawString(label, kRowX + 4, y);
}

void drawRow(M5Canvas &c, const CarouselSettings &settings, CarouselSettingRow row,
             CarouselSettingRow selected, int y) {
    bool active = row == selected;
    c.fillRoundRect(kRowX, y - kRowH / 2, kRowW, kRowH, kRowR,
                    active ? kSelectedFill : kRowFill);
    if (active) {
        c.fillRoundRect(kRowX, y - kRowH / 2, 5, kRowH, 3, theme::kCodex);
    }

    c.setFont(theme::kFontBody);
    c.setTextDatum(middle_left);
    c.setTextColor(active ? theme::kTextPrimary : theme::kTextMuted);
    c.drawString(CarouselSettings::rowLabel(row), kRowX + 18, y);

    char buf[12];
    const char *value = valueText(settings, row, buf, sizeof(buf));
    c.setTextDatum(middle_right);
    c.setTextColor(active ? theme::kCodex : theme::kTextPrimary);
    c.drawString(value, kRowX + kRowW - 18, y);
}
}  // namespace

void drawCarouselSettings(Renderer &renderer, const CarouselSettings &settings,
                          CarouselSettingRow selected) {
    auto &c = renderer.canvas();
    renderer.clear(kSettingsBg);
    c.setTextDatum(middle_center);
    c.setFont(theme::kFontMicro);
    c.setTextColor(theme::kTextMuted);
    c.drawString("LOCAL", theme::kCenterX, 34);
    c.setFont(theme::kFontTitle);
    c.setTextColor(theme::kTextPrimary);
    c.drawString("SETTINGS", theme::kCenterX, 60);

    drawGroup(c, "DISPLAY", 96);
    drawRow(c, settings, CarouselSettingRow::Upright, selected, 124);

    drawGroup(c, "CAROUSEL", 166);
    drawRow(c, settings, CarouselSettingRow::Autoplay, selected, 194);
    drawRow(c, settings, CarouselSettingRow::Interval, selected, 240);
    drawRow(c, settings, CarouselSettingRow::Motion, selected, 286);
    drawRow(c, settings, CarouselSettingRow::Resume, selected, 332);

    c.setTextDatum(middle_center);
    c.setFont(theme::kFontMicro);
    c.setTextColor(theme::kTextMuted);
    c.drawString("A CHANGE  B NEXT", theme::kCenterX, 388);
    c.drawString("A+B HOLD SAVE", theme::kCenterX, 414);
}

}  // namespace stopwatch::views
```

- [ ] **Step 2: Build firmware to verify renderer compile**

Run:

```bash
cd firmware && pio run -e stopwatch
```

Expected: firmware build succeeds.

- [ ] **Step 3: Commit**

```bash
git add firmware/src/Views/CarouselSettings.cpp
git commit -m "firmware: group local settings screen"
```

---

### Task 6: Renderer Orientation And IMU Polling

**Files:**
- Modify: `firmware/src/Renderer.h`
- Modify: `firmware/src/Renderer.cpp`
- Modify: `firmware/src/main.cpp`

- [ ] **Step 1: Update `Renderer.h`**

In `firmware/src/Renderer.h`, add:

```cpp
#include "OrientationController.h"
```

Add these methods to `class Renderer` public section after `clear(...)`:

```cpp
    void setOrientation(DisplayOrientation orientation);
    DisplayOrientation orientation() const { return orientation_; }
```

Add this private member before `M5Canvas sprite_{&M5.Display};`:

```cpp
    DisplayOrientation orientation_ = DisplayOrientation::Deg0;
```

- [ ] **Step 2: Update `Renderer.cpp`**

In `Renderer::begin()`, set default display rotation before creating the sprite:

```cpp
void Renderer::begin() {
    M5.Display.setRotation((uint8_t)DisplayOrientation::Deg0);
    orientation_ = DisplayOrientation::Deg0;
    sprite_.setColorDepth(16);
    sprite_.setPsram(true);
    sprite_.createSprite(M5.Display.width(), M5.Display.height());
    sprite_.fillSprite(0x000000);
}
```

Add this method after `Renderer::clear(...)`:

```cpp
void Renderer::setOrientation(DisplayOrientation orientation) {
    if (orientation_ == orientation) return;
    orientation_ = orientation;
    M5.Display.setRotation((uint8_t)orientation_);
}
```

- [ ] **Step 3: Update `main.cpp` includes and globals**

In `firmware/src/main.cpp`, add this include next to the other local includes:

```cpp
#include "OrientationController.h"
```

Add these globals after `static bool g_loading = false;`:

```cpp
stopwatch::OrientationController g_orientation;
static uint32_t                  g_nextOrientationPollMs = 0;
static uint8_t                   g_orientationReadFailures = 0;
static bool                      g_orientationRuntimeDisabled = false;
static constexpr uint32_t        kOrientationPollMs = 50;
static constexpr uint8_t         kMaxOrientationReadFailures = 5;
```

- [ ] **Step 4: Add orientation runtime helpers in `main.cpp`**

Add these helpers after `renderTransitionFrame()`:

```cpp
static void resetOrientationRuntime(uint32_t now) {
    g_orientation.reset(now, stopwatch::DisplayOrientation::Deg0);
    g_renderer.setOrientation(stopwatch::DisplayOrientation::Deg0);
    g_nextOrientationPollMs = now;
    g_orientationReadFailures = 0;
    g_orientationRuntimeDisabled = false;
}

static void renderAfterOrientationCommit() {
    if (g_transition.isAnimating()) {
        renderTransitionFrame();
    } else {
        renderCurrent();
    }
}

static void pollOrientation(uint32_t now) {
    if (!g_carouselSettings.uprightEnabled || g_orientationRuntimeDisabled) return;
    if ((int32_t)(now - g_nextOrientationPollMs) < 0) return;
    g_nextOrientationPollMs = now + kOrientationPollMs;

    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    if (!M5.Imu.getAccel(&ax, &ay, &az)) {
        if (g_orientationReadFailures < 255) ++g_orientationReadFailures;
        if (g_orientationReadFailures >= kMaxOrientationReadFailures) {
            g_orientationRuntimeDisabled = true;
            Serial.println("[stopwatch-fw] IMU accel unavailable; upright disabled for this boot");
        }
        return;
    }

    g_orientationReadFailures = 0;
    stopwatch::OrientationSample sample{ax, ay, az};
    if (g_orientation.tick(now, sample)) {
        g_renderer.setOrientation(g_orientation.committed());
        renderAfterOrientationCommit();
    }
}
```

- [ ] **Step 5: Initialize orientation in `setup()`**

In `setup()`, immediately after `g_renderer.begin();`, add:

```cpp
    g_orientation.begin(millis(), stopwatch::DisplayOrientation::Deg0);
    g_renderer.setOrientation(stopwatch::DisplayOrientation::Deg0);
```

- [ ] **Step 6: Reset orientation on wake**

In `enterSleepAndRefreshOnWake()`, immediately after:

```cpp
    g_power.enterLightSleep();
```

add:

```cpp
    resetOrientationRuntime(millis());
```

- [ ] **Step 7: React when the setting changes**

In the button-event block in `loop()`, immediately after:

```cpp
        bool wasInSettings = g_app.inCarouselSettings();
```

add:

```cpp
        bool wasUprightEnabled = g_carouselSettings.uprightEnabled;
```

Immediately after:

```cpp
        bool changed = g_app.handleEvent(ev, g_carouselSettings);
```

add:

```cpp
        if (wasUprightEnabled != g_carouselSettings.uprightEnabled) {
            resetOrientationRuntime(millis());
        }
```

- [ ] **Step 8: Poll orientation once per loop**

In `loop()`, add this block after the button-event block and before `if (g_transition.isAnimating())`:

```cpp
    pollOrientation(millis());
```

- [ ] **Step 9: Run native tests and firmware build**

Run:

```bash
cd firmware && pio test -e native
cd firmware && pio run -e stopwatch
```

Expected: native tests pass and StopWatch firmware builds.

- [ ] **Step 10: Commit**

```bash
git add firmware/src/Renderer.h firmware/src/Renderer.cpp firmware/src/main.cpp
git commit -m "firmware: apply IMU upright orientation"
```

---

### Task 7: Touch Verification And README

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Verify touch-coordinate handling in source**

Confirm `M5Unified/src/utility/Touch_Class.cpp` calls `_gfx->convertRawXY(tp, count)` inside `Touch_Class::update`. This means `M5.Touch.getDetail()` should track the active display rotation after `M5.Display.setRotation(...)`.

Run:

```bash
rg -n "convertRawXY\\(tp, count\\)" firmware/.pio/libdeps/stopwatch/M5Unified/src/utility/Touch_Class.cpp
```

Expected output includes:

```text
_gfx->convertRawXY(tp, count);
```

- [ ] **Step 2: Update README settings documentation**

In `README.md`, replace the `## Carousel autoplay` settings controls subsection that starts with:

```markdown
Hold KEYA + KEYB to open carousel settings on the watch.
```

with:

```markdown
Hold KEYA + KEYB to open local settings on the watch. KEYB moves between rows, KEYA changes the selected value, KEYA-long resets defaults, and KEYA + KEYB long saves and exits.

Display setting:

- Upright: Off / On. When enabled, the watch uses its IMU to keep the UI upright at 0/90/180/270 degree rotations while awake. It is off by default and does not keep the display awake.

Carousel settings:
```

Keep the existing carousel settings bullets below that line:

```markdown
- Autoplay: On / Off
- Interval: 5s / 10s / 15s / 30s
- Motion: Iris / Fade / Instant
- Resume: 10s / 20s / 30s after input
```

- [ ] **Step 3: Run full repo tests**

Run:

```bash
make test
```

Expected: bridge and firmware native tests pass.

- [ ] **Step 4: Commit**

```bash
git add README.md
git commit -m "docs: document upright setting"
```

---

### Task 8: Hardware Verification And Axis Calibration

**Files:**
- Modify when hardware observation requires it: `firmware/src/OrientationController.cpp`

- [ ] **Step 1: Flash and monitor**

Run:

```bash
make flash
make monitor
```

Expected: firmware boots, renders cached or live data, and monitor stays connected.

- [ ] **Step 2: Enable upright mode**

On the StopWatch:

```text
Hold KEYA + KEYB -> SETTINGS
KEYA on UPRIGHT -> ON
Hold KEYA + KEYB -> save and exit
```

Expected: settings exits, and the current view remains readable.

- [ ] **Step 3: Verify the four cardinal directions**

Rotate the physical StopWatch slowly through `0`, `90`, `180`, and `270` degrees.

Expected:

- Text snaps upright after about 300 ms.
- The screen does not flicker at diagonal hand positions.
- Orientation changes do not reset idle sleep.

- [ ] **Step 4: Adjust axis mapping when observed directions are wrong**

If rotation commits are consistently 90/180/270 degrees away from the desired orientation, adjust only the mapping in `candidateFromSample(...)` in `firmware/src/OrientationController.cpp`.

Use one of these concrete mapping edits:

For a 90-degree clockwise offset, replace the four candidate assignments with:

```cpp
out = s.ax >= 0.0f ? DisplayOrientation::Deg180 : DisplayOrientation::Deg0;
```

and:

```cpp
out = s.ay >= 0.0f ? DisplayOrientation::Deg270 : DisplayOrientation::Deg90;
```

For a 180-degree offset, replace them with:

```cpp
out = s.ax >= 0.0f ? DisplayOrientation::Deg270 : DisplayOrientation::Deg90;
```

and:

```cpp
out = s.ay >= 0.0f ? DisplayOrientation::Deg0 : DisplayOrientation::Deg180;
```

For a 90-degree counter-clockwise offset, replace them with:

```cpp
out = s.ax >= 0.0f ? DisplayOrientation::Deg0 : DisplayOrientation::Deg180;
```

and:

```cpp
out = s.ay >= 0.0f ? DisplayOrientation::Deg90 : DisplayOrientation::Deg270;
```

After an axis-mapping edit, update `firmware/test/test_orientation_controller/test_main.cpp` expected orientations to match the chosen mapping and run:

```bash
cd firmware && pio test -e native -f test_orientation_controller
cd firmware && pio run -e stopwatch
```

Expected: orientation tests pass and firmware builds.

- [ ] **Step 5: Verify Balances touch**

Open the Balances view with `UPRIGHT ON`. Test drag scrolling and row tapping at all four cardinal orientations.

Expected: dragging down visually scrolls down, and tapping a visible row opens that row.

- [ ] **Step 6: Commit calibration changes**

If axis mapping changed, commit:

```bash
git add firmware/src/OrientationController.cpp firmware/test/test_orientation_controller/test_main.cpp
git commit -m "firmware: calibrate StopWatch orientation axes"
```

If no axis mapping changed, do not create a commit for this task.

---

## Final Verification

- [ ] Run native firmware tests:

```bash
cd firmware && pio test -e native
```

Expected: all native firmware tests pass.

- [ ] Build StopWatch firmware:

```bash
cd firmware && pio run -e stopwatch
```

Expected: firmware builds successfully.

- [ ] Run full repo tests:

```bash
make test
```

Expected: bridge and firmware tests pass.

- [ ] Confirm git history contains focused commits:

```bash
git log --oneline -8
```

Expected: separate commits for orientation controller, settings state, persistence, settings UI, runtime integration, documentation, and hardware calibration only when calibration changed.
