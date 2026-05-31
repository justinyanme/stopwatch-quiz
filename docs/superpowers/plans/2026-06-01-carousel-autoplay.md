# Carousel Autoplay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add on-watch carousel autoplay, local carousel settings, and the Instrument Iris transition.

**Architecture:** Keep timing and settings logic in pure firmware modules that compile under the native PlatformIO test environment. Wire hardware-specific button polling, NVS persistence, rendering, and lazy BLE reads only after the pure state machines are tested. Use the existing single-sprite renderer with a black mask overlay for the iris transition.

**Tech Stack:** PlatformIO, Arduino, M5Unified, C++17, Unity native tests, existing firmware `App`, `Anim`, `Renderer`, `SnapshotStore`, and `Views` patterns.

---

## File Map

- Create `firmware/src/CarouselSettings.h`: pure settings model, defaults, validation, value cycling helpers.
- Create `firmware/src/CarouselController.h`: pure autoplay timing controller and pause context.
- Create `firmware/src/CarouselTransition.h`: pure transition clock for Iris, Fade, Instant modes.
- Modify `firmware/src/Anim.h`: add pure iris and fade motion helper functions.
- Modify `firmware/src/App.h` and `firmware/src/App.cpp`: settings mode, selected settings row, settings mutation, `ButtonEvent::BothLong` handling.
- Modify `firmware/src/Buttons.h` and `firmware/src/Buttons.cpp`: add both-button long chord event.
- Modify `firmware/src/SnapshotStore.h` and `firmware/src/SnapshotStore.cpp`: add typed carousel settings load/save using the existing `swq` NVS namespace.
- Create `firmware/src/Views/CarouselSettings.h` and `firmware/src/Views/CarouselSettings.cpp`: draw local settings screen.
- Modify `firmware/src/main.cpp`: instantiate settings/controller/transition, load/save settings, advance automatically, pause on user input/loading/detail, render transition overlay, and prevent autoplay from resetting idle sleep.
- Modify `firmware/test/test_state_machine/test_main.cpp`: settings-mode and value-cycling tests.
- Create `firmware/test/test_carousel_controller/test_main.cpp`: autoplay timing and pause tests.
- Create `firmware/test/test_carousel_transition/test_main.cpp`: transition clock tests.
- Modify `firmware/test/test_anim/test_main.cpp`: iris/fade helper tests.
- Update `README.md`: document autoplay settings and correct idle sleep wording if it still says 15 seconds.

---

## Task 1: Carousel Settings Model

**Files:**
- Create: `firmware/src/CarouselSettings.h`
- Test: `firmware/test/test_state_machine/test_main.cpp`

- [ ] **Step 1: Add failing tests for defaults and value cycling**

Append these tests to `firmware/test/test_state_machine/test_main.cpp` before `main()`:

```cpp
void test_carouselSettingsDefaultsAndValidation(void) {
    CarouselSettings s = CarouselSettings::defaults();
    TEST_ASSERT_TRUE(s.autoplayEnabled);
    TEST_ASSERT_EQUAL_UINT16(10, s.intervalSeconds);
    TEST_ASSERT_EQUAL((int)CarouselMotionMode::Iris, (int)s.motionMode);
    TEST_ASSERT_EQUAL_UINT16(20, s.resumeSeconds);

    CarouselSettings invalid;
    invalid.autoplayEnabled = true;
    invalid.intervalSeconds = 7;
    invalid.motionMode = (CarouselMotionMode)99;
    invalid.resumeSeconds = 11;
    invalid.validate();

    TEST_ASSERT_EQUAL_UINT16(10, invalid.intervalSeconds);
    TEST_ASSERT_EQUAL((int)CarouselMotionMode::Iris, (int)invalid.motionMode);
    TEST_ASSERT_EQUAL_UINT16(20, invalid.resumeSeconds);
}

void test_carouselSettingsCyclesValues(void) {
    CarouselSettings s = CarouselSettings::defaults();

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
```

Add the runs in `main()`:

```cpp
RUN_TEST(test_carouselSettingsDefaultsAndValidation);
RUN_TEST(test_carouselSettingsCyclesValues);
```

Add the include at the top:

```cpp
#include "../../src/CarouselSettings.h"
```

- [ ] **Step 2: Run the state-machine test to verify it fails**

Run:

```bash
cd firmware && pio test -e native -f test_state_machine
```

Expected: compile fails because `CarouselSettings.h`, `CarouselSettings`, `CarouselMotionMode`, and `CarouselSettingRow` do not exist.

- [ ] **Step 3: Create the settings model**

Create `firmware/src/CarouselSettings.h`:

```cpp
#pragma once
#include <cstdint>

namespace stopwatch {

enum class CarouselMotionMode : uint8_t { Iris = 0, Fade = 1, Instant = 2 };
enum class CarouselSettingRow : uint8_t { Autoplay = 0, Interval = 1, Motion = 2, Resume = 3 };

struct CarouselSettings {
    bool autoplayEnabled = true;
    uint16_t intervalSeconds = 10;
    CarouselMotionMode motionMode = CarouselMotionMode::Iris;
    uint16_t resumeSeconds = 20;

    static constexpr uint8_t kRowCount = 4;

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
        case CarouselSettingRow::Autoplay: return CarouselSettingRow::Interval;
        case CarouselSettingRow::Interval: return CarouselSettingRow::Motion;
        case CarouselSettingRow::Motion:   return CarouselSettingRow::Resume;
        case CarouselSettingRow::Resume:   return CarouselSettingRow::Autoplay;
    }
    return CarouselSettingRow::Autoplay;
}

}  // namespace stopwatch
```

- [ ] **Step 4: Run the state-machine test to verify it passes**

Run:

```bash
cd firmware && pio test -e native -f test_state_machine
```

Expected: all `test_state_machine` cases pass, including the two new carousel settings tests.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/CarouselSettings.h firmware/test/test_state_machine/test_main.cpp
git commit -m "firmware: add carousel settings model"
```

---

## Task 2: App Settings Mode

**Files:**
- Modify: `firmware/src/App.h`
- Modify: `firmware/src/App.cpp`
- Test: `firmware/test/test_state_machine/test_main.cpp`

- [ ] **Step 1: Add failing tests for settings mode**

Append these tests to `firmware/test/test_state_machine/test_main.cpp` before `main()`:

```cpp
void test_bothLongEntersAndExitsCarouselSettings(void) {
    App app; app.begin();
    CarouselSettings settings = CarouselSettings::defaults();

    TEST_ASSERT_FALSE(app.inCarouselSettings());
    bool changed = app.handleEvent(ButtonEvent::BothLong, settings);
    TEST_ASSERT_TRUE(changed);
    TEST_ASSERT_TRUE(app.inCarouselSettings());
    TEST_ASSERT_EQUAL((int)CarouselSettingRow::Autoplay, (int)app.carouselSettingRow());

    changed = app.handleEvent(ButtonEvent::BothLong, settings);
    TEST_ASSERT_TRUE(changed);
    TEST_ASSERT_FALSE(app.inCarouselSettings());
}

void test_carouselSettingsRowsAndValuesChange(void) {
    App app; app.begin();
    CarouselSettings settings = CarouselSettings::defaults();
    app.handleEvent(ButtonEvent::BothLong, settings);

    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyBShort, settings));
    TEST_ASSERT_EQUAL((int)CarouselSettingRow::Interval, (int)app.carouselSettingRow());

    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyAShort, settings));
    TEST_ASSERT_EQUAL_UINT16(15, settings.intervalSeconds);

    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyBShort, settings));
    TEST_ASSERT_EQUAL((int)CarouselSettingRow::Motion, (int)app.carouselSettingRow());
    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyAShort, settings));
    TEST_ASSERT_EQUAL((int)CarouselMotionMode::Fade, (int)settings.motionMode);
}

void test_carouselSettingsResetDefaults(void) {
    App app; app.begin();
    CarouselSettings settings = CarouselSettings::defaults();
    app.handleEvent(ButtonEvent::BothLong, settings);
    app.handleEvent(ButtonEvent::KeyAShort, settings);
    TEST_ASSERT_FALSE(settings.autoplayEnabled);

    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyALong, settings));
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
```

Add the runs in `main()`:

```cpp
RUN_TEST(test_bothLongEntersAndExitsCarouselSettings);
RUN_TEST(test_carouselSettingsRowsAndValuesChange);
RUN_TEST(test_carouselSettingsResetDefaults);
RUN_TEST(test_carouselSettingsSleepStillWorks);
```

- [ ] **Step 2: Run the state-machine test to verify it fails**

Run:

```bash
cd firmware && pio test -e native -f test_state_machine
```

Expected: compile fails because `ButtonEvent::BothLong`, `App::handleEvent(ButtonEvent, CarouselSettings&)`, `inCarouselSettings()`, and `carouselSettingRow()` do not exist.

- [ ] **Step 3: Update `Buttons.h` event enum**

Modify `firmware/src/Buttons.h`:

```cpp
enum class ButtonEvent : uint8_t {
    None,
    KeyAShort, KeyALong,
    KeyBShort, KeyBLong,
    BothLong,
};
```

- [ ] **Step 4: Update `App.h` declarations**

Add the include:

```cpp
#include "CarouselSettings.h"
```

Add public methods to `App`:

```cpp
bool handleEvent(ButtonEvent ev, CarouselSettings &settings);
bool inCarouselSettings() const { return inCarouselSettings_; }
CarouselSettingRow carouselSettingRow() const { return settingRow_; }
void exitCarouselSettings() { inCarouselSettings_ = false; }
```

Keep the existing `bool handleEvent(ButtonEvent ev);` declaration for existing call sites and tests.

Add private fields:

```cpp
bool inCarouselSettings_ = false;
CarouselSettingRow settingRow_ = CarouselSettingRow::Autoplay;
```

- [ ] **Step 5: Update `App.cpp` implementation**

In `App::begin()`, reset settings mode:

```cpp
inCarouselSettings_ = false;
settingRow_ = CarouselSettingRow::Autoplay;
```

Add this overload above the existing `handleEvent(ButtonEvent ev)` implementation:

```cpp
bool App::handleEvent(ButtonEvent ev, CarouselSettings &settings) {
    if (ev == ButtonEvent::BothLong) {
        inCarouselSettings_ = !inCarouselSettings_;
        settingRow_ = CarouselSettingRow::Autoplay;
        settings.validate();
        return true;
    }

    if (inCarouselSettings_) {
        switch (ev) {
            case ButtonEvent::KeyBShort:
                settingRow_ = nextSettingRow(settingRow_);
                return true;
            case ButtonEvent::KeyAShort:
                settings.cycle(settingRow_);
                settings.validate();
                return true;
            case ButtonEvent::KeyALong:
                settings.resetDefaults();
                return true;
            case ButtonEvent::KeyBLong:
                wantsSleep_ = true;
                return false;
            case ButtonEvent::None:
            case ButtonEvent::BothLong:
                return false;
        }
        return false;
    }

    return handleEvent(ev);
}
```

In the existing `App::handleEvent(ButtonEvent ev)`, add `ButtonEvent::BothLong` to each switch as a no-op fallback:

```cpp
case ButtonEvent::BothLong: return false;
```

In `noteWakeFromSleep()`, exit settings:

```cpp
inCarouselSettings_ = false;
```

- [ ] **Step 6: Run the state-machine test to verify it passes**

Run:

```bash
cd firmware && pio test -e native -f test_state_machine
```

Expected: all `test_state_machine` cases pass.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/App.h firmware/src/App.cpp firmware/src/Buttons.h firmware/test/test_state_machine/test_main.cpp
git commit -m "firmware: add carousel settings app state"
```

---

## Task 3: Carousel Controller

**Files:**
- Create: `firmware/src/CarouselController.h`
- Create: `firmware/test/test_carousel_controller/test_main.cpp`

- [ ] **Step 1: Write failing controller tests**

Create `firmware/test/test_carousel_controller/test_main.cpp`:

```cpp
#include <unity.h>
#include "../../src/CarouselController.h"

using namespace stopwatch;

void test_autoplayAdvancesAfterInterval(void) {
    CarouselController c;
    CarouselSettings s = CarouselSettings::defaults();
    c.begin(1000, s);

    CarouselContext ctx;
    TEST_ASSERT_FALSE(c.shouldAdvance(1000 + 9999, s, ctx));
    TEST_ASSERT_TRUE(c.shouldAdvance(1000 + 10000, s, ctx));
    c.recordAdvance(1000 + 10000);
    TEST_ASSERT_FALSE(c.shouldAdvance(1000 + 19999, s, ctx));
    TEST_ASSERT_TRUE(c.shouldAdvance(1000 + 20000, s, ctx));
}

void test_autoplayDisabledDoesNotAdvance(void) {
    CarouselController c;
    CarouselSettings s = CarouselSettings::defaults();
    s.autoplayEnabled = false;
    c.begin(0, s);
    CarouselContext ctx;
    TEST_ASSERT_FALSE(c.shouldAdvance(60000, s, ctx));
}

void test_userActivityDelaysResume(void) {
    CarouselController c;
    CarouselSettings s = CarouselSettings::defaults();
    c.begin(0, s);
    CarouselContext ctx;
    c.noteUserActivity(9000);

    TEST_ASSERT_FALSE(c.shouldAdvance(28000, s, ctx));
    TEST_ASSERT_TRUE(c.shouldAdvance(29000, s, ctx));
}

void test_pauseContextsBlockAdvance(void) {
    CarouselController c;
    CarouselSettings s = CarouselSettings::defaults();
    c.begin(0, s);

    CarouselContext ctx;
    ctx.inSettings = true;
    TEST_ASSERT_FALSE(c.shouldAdvance(60000, s, ctx));
    ctx = CarouselContext{};
    ctx.inBalanceDetail = true;
    TEST_ASSERT_FALSE(c.shouldAdvance(60000, s, ctx));
    ctx = CarouselContext{};
    ctx.touchActive = true;
    TEST_ASSERT_FALSE(c.shouldAdvance(60000, s, ctx));
    ctx = CarouselContext{};
    ctx.loading = true;
    TEST_ASSERT_FALSE(c.shouldAdvance(60000, s, ctx));
    ctx = CarouselContext{};
    ctx.transitionActive = true;
    TEST_ASSERT_FALSE(c.shouldAdvance(60000, s, ctx));
}

void test_manualAdvanceResetsSchedule(void) {
    CarouselController c;
    CarouselSettings s = CarouselSettings::defaults();
    c.begin(0, s);
    CarouselContext ctx;
    c.recordManualViewChange(5000);

    TEST_ASSERT_FALSE(c.shouldAdvance(14999, s, ctx));
    TEST_ASSERT_TRUE(c.shouldAdvance(25000, s, ctx));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_autoplayAdvancesAfterInterval);
    RUN_TEST(test_autoplayDisabledDoesNotAdvance);
    RUN_TEST(test_userActivityDelaysResume);
    RUN_TEST(test_pauseContextsBlockAdvance);
    RUN_TEST(test_manualAdvanceResetsSchedule);
    return UNITY_END();
}
```

- [ ] **Step 2: Run the controller test to verify it fails**

Run:

```bash
cd firmware && pio test -e native -f test_carousel_controller
```

Expected: compile fails because `CarouselController.h`, `CarouselController`, and `CarouselContext` do not exist.

- [ ] **Step 3: Create the controller**

Create `firmware/src/CarouselController.h`:

```cpp
#pragma once
#include <cstdint>
#include "CarouselSettings.h"

namespace stopwatch {

struct CarouselContext {
    bool inSettings = false;
    bool inBalanceDetail = false;
    bool touchActive = false;
    bool loading = false;
    bool transitionActive = false;
};

class CarouselController {
public:
    void begin(uint32_t nowMs, const CarouselSettings &) {
        lastAdvanceMs_ = nowMs;
        lastUserActivityMs_ = nowMs;
    }

    void noteUserActivity(uint32_t nowMs) {
        lastUserActivityMs_ = nowMs;
    }

    void recordAdvance(uint32_t nowMs) {
        lastAdvanceMs_ = nowMs;
    }

    void recordManualViewChange(uint32_t nowMs) {
        lastAdvanceMs_ = nowMs;
        lastUserActivityMs_ = nowMs;
    }

    bool shouldAdvance(uint32_t nowMs, const CarouselSettings &settings,
                       const CarouselContext &ctx) const {
        if (!settings.autoplayEnabled) return false;
        if (ctx.inSettings || ctx.inBalanceDetail || ctx.touchActive ||
            ctx.loading || ctx.transitionActive) {
            return false;
        }
        if ((uint32_t)(nowMs - lastUserActivityMs_) <
            (uint32_t)settings.resumeSeconds * 1000u) {
            return false;
        }
        return (uint32_t)(nowMs - lastAdvanceMs_) >=
               (uint32_t)settings.intervalSeconds * 1000u;
    }

private:
    uint32_t lastAdvanceMs_ = 0;
    uint32_t lastUserActivityMs_ = 0;
};

}  // namespace stopwatch
```

- [ ] **Step 4: Run the controller test to verify it passes**

Run:

```bash
cd firmware && pio test -e native -f test_carousel_controller
```

Expected: all `test_carousel_controller` cases pass.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/CarouselController.h firmware/test/test_carousel_controller/test_main.cpp
git commit -m "firmware: add carousel autoplay controller"
```

---

## Task 4: Motion Helpers And Transition Clock

**Files:**
- Modify: `firmware/src/Anim.h`
- Create: `firmware/src/CarouselTransition.h`
- Modify: `firmware/test/test_anim/test_main.cpp`
- Create: `firmware/test/test_carousel_transition/test_main.cpp`

- [ ] **Step 1: Add failing animation helper tests**

Append to `firmware/test/test_anim/test_main.cpp` before `main()`:

```cpp
void test_irisHelpersClampAndMove(void) {
    TEST_ASSERT_EQUAL_FLOAT(1.0f, motion::irisCover(0));
    TEST_ASSERT_TRUE(motion::irisCover(80) > 0.0f);
    TEST_ASSERT_TRUE(motion::irisCover(80) < 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::irisCover(motion::kIrisCloseMs));

    TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::irisReveal(0));
    TEST_ASSERT_TRUE(motion::irisReveal(100) > 0.0f);
    TEST_ASSERT_TRUE(motion::irisReveal(100) < 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, motion::irisReveal(motion::kIrisOpenMs));
}

void test_haloAndFadeClamp(void) {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::irisHalo(0));
    TEST_ASSERT_TRUE(motion::irisHalo(80) > 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::irisHalo(100000));

    TEST_ASSERT_EQUAL_FLOAT(0.0f, motion::fadeReveal(0));
    TEST_ASSERT_TRUE(motion::fadeReveal(80) > 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, motion::fadeReveal(motion::kFadeMs));
}
```

Add runs in `main()`:

```cpp
RUN_TEST(test_irisHelpersClampAndMove);
RUN_TEST(test_haloAndFadeClamp);
```

- [ ] **Step 2: Create failing transition clock tests**

Create `firmware/test/test_carousel_transition/test_main.cpp`:

```cpp
#include <unity.h>
#include "../../src/CarouselTransition.h"

using namespace stopwatch;

void test_irisTransitionSwitchesOnce(void) {
    CarouselTransition t;
    t.start(1000, CarouselMotionMode::Iris);
    TEST_ASSERT_TRUE(t.isAnimating());
    TEST_ASSERT_FALSE(t.hasSwitched());

    t.tick(1000 + motion::kIrisSwitchMs - 1);
    TEST_ASSERT_FALSE(t.hasSwitched());

    t.tick(1000 + motion::kIrisSwitchMs);
    TEST_ASSERT_TRUE(t.hasSwitched());
    TEST_ASSERT_TRUE(t.consumeSwitch());
    TEST_ASSERT_FALSE(t.consumeSwitch());

    t.tick(1000 + motion::kIrisTransitionMs);
    TEST_ASSERT_FALSE(t.isAnimating());
    TEST_ASSERT_TRUE(t.hasSwitched());
}

void test_instantTransitionSwitchesImmediately(void) {
    CarouselTransition t;
    t.start(2000, CarouselMotionMode::Instant);
    TEST_ASSERT_FALSE(t.isAnimating());
    TEST_ASSERT_TRUE(t.hasSwitched());
    TEST_ASSERT_TRUE(t.consumeSwitch());
}

void test_fadeTransitionDuration(void) {
    CarouselTransition t;
    t.start(0, CarouselMotionMode::Fade);
    TEST_ASSERT_TRUE(t.isAnimating());
    TEST_ASSERT_TRUE(t.hasSwitched());
    TEST_ASSERT_TRUE(t.consumeSwitch());
    t.tick(motion::kFadeMs - 1);
    TEST_ASSERT_TRUE(t.isAnimating());
    t.tick(motion::kFadeMs);
    TEST_ASSERT_FALSE(t.isAnimating());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_irisTransitionSwitchesOnce);
    RUN_TEST(test_instantTransitionSwitchesImmediately);
    RUN_TEST(test_fadeTransitionDuration);
    return UNITY_END();
}
```

- [ ] **Step 3: Run tests to verify they fail**

Run:

```bash
cd firmware && pio test -e native -f test_anim -f test_carousel_transition
```

Expected: compile fails because iris/fade helpers and `CarouselTransition.h` do not exist.

- [ ] **Step 4: Add motion helpers to `Anim.h`**

Append inside `namespace motion` in `firmware/src/Anim.h`:

```cpp
// -- Carousel transition: restrained instrument-style iris. ------------------
constexpr uint32_t kIrisCloseMs      = 150;
constexpr uint32_t kIrisOpenMs       = 210;
constexpr uint32_t kIrisSwitchMs     = kIrisCloseMs;
constexpr uint32_t kIrisTransitionMs = kIrisCloseMs + kIrisOpenMs;
constexpr uint32_t kIrisHaloStartMs  = 170;
constexpr uint32_t kIrisHaloMs       = 140;
constexpr uint32_t kFadeMs           = 180;

inline float irisCover(uint32_t elapsedMs) {
    if (elapsedMs >= kIrisCloseMs) return 0.0f;
    return 1.0f - ease::outExpo((float)elapsedMs / (float)kIrisCloseMs);
}

inline float irisReveal(uint32_t localOpenMs) {
    if (localOpenMs >= kIrisOpenMs) return 1.0f;
    return ease::outExpo((float)localOpenMs / (float)kIrisOpenMs);
}

inline float irisHalo(uint32_t elapsedMs) {
    if (elapsedMs <= kIrisHaloStartMs) return 0.0f;
    uint32_t local = elapsedMs - kIrisHaloStartMs;
    if (local >= kIrisHaloMs) return 0.0f;
    float t = (float)local / (float)kIrisHaloMs;
    return t < 0.5f ? ease::outExpo(t * 2.0f) : 1.0f - ease::outExpo((t - 0.5f) * 2.0f);
}

inline float fadeReveal(uint32_t elapsedMs) {
    if (elapsedMs >= kFadeMs) return 1.0f;
    return ease::outExpo((float)elapsedMs / (float)kFadeMs);
}
```

- [ ] **Step 5: Create `CarouselTransition.h`**

Create `firmware/src/CarouselTransition.h`:

```cpp
#pragma once
#include <cstdint>
#include "Anim.h"
#include "CarouselSettings.h"

namespace stopwatch {

class CarouselTransition {
public:
    void start(uint32_t nowMs, CarouselMotionMode mode) {
        mode_ = mode;
        startMs_ = nowMs;
        nowMs_ = nowMs;
        switched_ = mode == CarouselMotionMode::Fade || mode == CarouselMotionMode::Instant;
        pendingSwitch_ = switched_;
        active_ = mode != CarouselMotionMode::Instant;
    }

    void tick(uint32_t nowMs) {
        nowMs_ = nowMs;
        if (!active_) return;
        if (mode_ == CarouselMotionMode::Iris &&
            !switched_ &&
            elapsed() >= motion::kIrisSwitchMs) {
            switched_ = true;
            pendingSwitch_ = true;
        }
        if (elapsed() >= durationMs()) active_ = false;
    }

    bool isAnimating() const { return active_; }
    bool hasSwitched() const { return switched_; }

    bool consumeSwitch() {
        bool out = pendingSwitch_;
        pendingSwitch_ = false;
        return out;
    }

    CarouselMotionMode mode() const { return mode_; }
    uint32_t elapsed() const { return (uint32_t)(nowMs_ - startMs_); }

    uint32_t durationMs() const {
        switch (mode_) {
            case CarouselMotionMode::Iris:    return motion::kIrisTransitionMs;
            case CarouselMotionMode::Fade:    return motion::kFadeMs;
            case CarouselMotionMode::Instant: return 0;
        }
        return 0;
    }

private:
    CarouselMotionMode mode_ = CarouselMotionMode::Iris;
    uint32_t startMs_ = 0;
    uint32_t nowMs_ = 0;
    bool active_ = false;
    bool switched_ = false;
    bool pendingSwitch_ = false;
};

}  // namespace stopwatch
```

- [ ] **Step 6: Run animation and transition tests**

Run:

```bash
cd firmware && pio test -e native -f test_anim -f test_carousel_transition
```

Expected: both test suites pass.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/Anim.h firmware/src/CarouselTransition.h firmware/test/test_anim/test_main.cpp firmware/test/test_carousel_transition/test_main.cpp
git commit -m "firmware: add carousel transition timing"
```

---

## Task 5: Persist Carousel Settings

**Files:**
- Modify: `firmware/src/SnapshotStore.h`
- Modify: `firmware/src/SnapshotStore.cpp`

- [ ] **Step 1: Add storage declarations**

Modify `firmware/src/SnapshotStore.h` to include settings:

```cpp
#include "CarouselSettings.h"
```

Add public methods:

```cpp
bool loadCarouselSettings(CarouselSettings &out);
void saveCarouselSettings(const CarouselSettings &settings);
```

- [ ] **Step 2: Implement typed NVS storage**

Modify `firmware/src/SnapshotStore.cpp`:

```cpp
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
```

Add methods:

```cpp
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
```

- [ ] **Step 3: Build firmware to verify storage compiles**

Run:

```bash
cd firmware && pio run -e stopwatch
```

Expected: firmware build succeeds.

- [ ] **Step 4: Commit**

```bash
git add firmware/src/SnapshotStore.h firmware/src/SnapshotStore.cpp
git commit -m "firmware: persist carousel settings"
```

---

## Task 6: Button Chord Polling

**Files:**
- Modify: `firmware/src/Buttons.cpp`

- [ ] **Step 1: Implement both-button long detection**

Replace the anonymous namespace in `firmware/src/Buttons.cpp` with:

```cpp
namespace {
constexpr uint32_t kLongMs = 800;

struct State {
    bool wasPressed = false;
    uint32_t pressedAt = 0;
    bool longFired = false;
};
State sA, sB;
bool sBothWasPressed = false;
uint32_t sBothPressedAt = 0;
bool sBothLongFired = false;

void beginPress(State &s, uint32_t now) {
    s.wasPressed = true;
    s.pressedAt = now;
    s.longFired = false;
}

void cancelSingleLongs() {
    sA.longFired = true;
    sB.longFired = true;
}

ButtonEvent step(State &s, bool pressed, ButtonEvent shortEv, ButtonEvent longEv, uint32_t now) {
    if (pressed && !s.wasPressed) {
        beginPress(s, now);
    } else if (pressed && s.wasPressed && !s.longFired && (now - s.pressedAt) >= kLongMs) {
        s.longFired = true;
        return longEv;
    } else if (!pressed && s.wasPressed) {
        s.wasPressed = false;
        if (!s.longFired) return shortEv;
    }
    return ButtonEvent::None;
}
}  // namespace
```

Replace `pollButtons()` with:

```cpp
ButtonEvent pollButtons() {
    uint32_t now = millis();
    bool a = M5.BtnA.isPressed();
    bool b = M5.BtnB.isPressed();

    if (a && b) {
        if (!sBothWasPressed) {
            sBothWasPressed = true;
            sBothPressedAt = now;
            sBothLongFired = false;
        }
        if (!sBothLongFired && (now - sBothPressedAt) >= kLongMs) {
            sBothLongFired = true;
            cancelSingleLongs();
            return ButtonEvent::BothLong;
        }
    } else {
        sBothWasPressed = false;
    }

    auto evA = step(sA, a, ButtonEvent::KeyAShort, ButtonEvent::KeyALong, now);
    if (evA != ButtonEvent::None) return evA;
    return step(sB, b, ButtonEvent::KeyBShort, ButtonEvent::KeyBLong, now);
}
```

- [ ] **Step 2: Build firmware to verify button code compiles**

Run:

```bash
cd firmware && pio run -e stopwatch
```

Expected: firmware build succeeds.

- [ ] **Step 3: Commit**

```bash
git add firmware/src/Buttons.cpp
git commit -m "firmware: detect carousel settings button chord"
```

---

## Task 7: Carousel Settings View

**Files:**
- Create: `firmware/src/Views/CarouselSettings.h`
- Create: `firmware/src/Views/CarouselSettings.cpp`

- [ ] **Step 1: Create the view header**

Create `firmware/src/Views/CarouselSettings.h`:

```cpp
#pragma once
#include "../CarouselSettings.h"
#include "../Renderer.h"

namespace stopwatch::views {

void drawCarouselSettings(Renderer &renderer, const CarouselSettings &settings,
                          CarouselSettingRow selected);

}  // namespace stopwatch::views
```

- [ ] **Step 2: Create the view implementation**

Create `firmware/src/Views/CarouselSettings.cpp`:

```cpp
#include "CarouselSettings.h"
#include "../Theme.h"
#include <cstdio>

namespace stopwatch::views {

namespace {
constexpr uint32_t kSettingsBg = 0x05080C;
constexpr uint32_t kRowFill = 0x0C1218;
constexpr uint32_t kSelectedFill = 0x141D28;
constexpr int kRowX = 68;
constexpr int kRowW = 330;
constexpr int kRowH = 46;
constexpr int kRowR = 8;

const char *valueText(const CarouselSettings &settings, CarouselSettingRow row,
                      char *buf, size_t n) {
    switch (row) {
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
    c.drawString("CAROUSEL", theme::kCenterX, 60);

    drawRow(c, settings, CarouselSettingRow::Autoplay, selected, 126);
    drawRow(c, settings, CarouselSettingRow::Interval, selected, 182);
    drawRow(c, settings, CarouselSettingRow::Motion, selected, 238);
    drawRow(c, settings, CarouselSettingRow::Resume, selected, 294);

    c.setFont(theme::kFontMicro);
    c.setTextColor(theme::kTextMuted);
    c.drawString("A CHANGE  B NEXT", theme::kCenterX, 366);
    c.drawString("A+B HOLD SAVE", theme::kCenterX, 392);
}

}  // namespace stopwatch::views
```

- [ ] **Step 3: Build firmware**

Run:

```bash
cd firmware && pio run -e stopwatch
```

Expected: firmware build succeeds.

- [ ] **Step 4: Commit**

```bash
git add firmware/src/Views/CarouselSettings.h firmware/src/Views/CarouselSettings.cpp
git commit -m "firmware: add carousel settings view"
```

---

## Task 8: Main Loop Wiring Without Transition Overlay

**Files:**
- Modify: `firmware/src/main.cpp`

- [ ] **Step 1: Add includes and globals**

In `firmware/src/main.cpp`, add includes:

```cpp
#include "CarouselController.h"
#include "CarouselSettings.h"
#include "CarouselTransition.h"
#include "Views/CarouselSettings.h"
```

Add globals near `g_usageLoaded`:

```cpp
stopwatch::CarouselSettings   g_carouselSettings;
stopwatch::CarouselController g_carousel;
stopwatch::CarouselTransition g_transition;
static bool                   g_loading = false;
```

- [ ] **Step 2: Draw settings screen**

At the top of `drawCurrentView()`, before the switch:

```cpp
    if (g_app.inCarouselSettings()) {
        views::drawCarouselSettings(g_renderer, g_carouselSettings, g_app.carouselSettingRow());
        return;
    }
```

- [ ] **Step 3: Wrap loading state**

Modify `renderRefreshingOverlay()`:

```cpp
static void renderRefreshingOverlay(const char *label) {
    using namespace stopwatch;
    g_loading = true;
    drawCurrentView();
    auto &c = g_renderer.canvas();
    c.fillRect(0, theme::kCenterY + theme::kRingOuterR / 2 - 18,
               M5.Display.width(), 36, 0x303030);
    c.setTextDatum(middle_center);
    c.setTextColor(theme::kTextPrimary);
    c.setFont(theme::kFontTitle);
    c.drawString(label, theme::kCenterX, theme::kCenterY + theme::kRingOuterR / 2);
    g_renderer.present();
}
```

At the end of `fetchAndApply`, `fetchCostAndApply`, `fetchBalancesAndApply`, `fetchUsageAndApply`, and `applyRefreshRequest`, set:

```cpp
g_loading = false;
```

If the implementation uses early returns, set `g_loading = false` before each early return.

- [ ] **Step 4: Load settings in setup**

After `g_store.begin();`:

```cpp
    if (!g_store.loadCarouselSettings(g_carouselSettings)) {
        g_carouselSettings = stopwatch::CarouselSettings::defaults();
    }
    g_carousel.begin(millis(), g_carouselSettings);
```

- [ ] **Step 5: Use settings-aware event handling**

In `loop()`, replace:

```cpp
bool changed = g_app.handleEvent(ev);
```

with:

```cpp
bool wasInSettings = g_app.inCarouselSettings();
bool changed = g_app.handleEvent(ev, g_carouselSettings);
if (wasInSettings && !g_app.inCarouselSettings()) {
    g_store.saveCarouselSettings(g_carouselSettings);
}
g_carousel.noteUserActivity(millis());
```

After a manual view change, replace direct schedule drift with:

```cpp
if (changed && !g_app.inCarouselSettings()) {
    g_carousel.recordManualViewChange(millis());
}
```

- [ ] **Step 6: Add autoplay advance path**

Before the sleep check at the end of `loop()`, add:

```cpp
    stopwatch::CarouselContext cctx;
    cctx.inSettings = g_app.inCarouselSettings();
    cctx.inBalanceDetail = g_app.inBalanceDetail();
    cctx.touchActive = isBalanceView(g_app.currentView()) && !g_balScroll.isResting();
    cctx.loading = g_loading;
    cctx.transitionActive = g_transition.isAnimating();
    if (g_carousel.shouldAdvance(millis(), g_carouselSettings, cctx)) {
        g_app.handleEvent(ButtonEvent::KeyBShort);
        if (!isBalanceView(g_app.currentView())) g_balScroll.reset();
        ensureCostLoaded();
        ensureBalanceLoaded();
        startViewAnim();
        g_carousel.recordAdvance(millis());
    }
```

Do not call `g_power.noteActivity()` in this block.

- [ ] **Step 7: Build firmware**

Run:

```bash
cd firmware && pio run -e stopwatch
```

Expected: firmware build succeeds.

- [ ] **Step 8: Commit**

```bash
git add firmware/src/main.cpp
git commit -m "firmware: wire carousel autoplay settings"
```

---

## Task 9: Transition Overlay Rendering

**Files:**
- Modify: `firmware/src/main.cpp`

- [ ] **Step 1: Add overlay helper functions**

Add these helpers near `renderCurrent()` in `firmware/src/main.cpp`:

```cpp
static uint32_t accentForView(stopwatch::ViewId v) {
    using namespace stopwatch;
    switch (v) {
        case ViewId::Codex:
        case ViewId::CodexCost:  return theme::kCodex;
        case ViewId::Claude:
        case ViewId::ClaudeCost: return theme::kClaude;
        case ViewId::Gemini:     return theme::kGemini;
        case ViewId::Balances:   return theme::kCodex;
        case ViewId::Overview:
        case ViewId::TotalSpend: return theme::kTextMuted;
    }
    return theme::kTextMuted;
}

static void drawIrisMask(float visibleFraction, uint32_t accent, float halo) {
    using namespace stopwatch;
    auto &c = g_renderer.canvas();
    int maxR = theme::kRingOuterR + 42;
    int visibleR = (int)(visibleFraction * (float)maxR + 0.5f);
    if (visibleR < maxR) {
        c.fillArc(theme::kCenterX, theme::kCenterY, maxR, visibleR, 0, 360, theme::kBackground);
    }
    if (halo > 0.0f) {
        int haloR = visibleR + 10 + (int)(halo * 12.0f);
        uint32_t color = accent;
        c.drawCircle(theme::kCenterX, theme::kCenterY, haloR, color);
        c.drawCircle(theme::kCenterX, theme::kCenterY, haloR + 1, color);
    }
}

static void drawFadeMask(float revealFraction) {
    if (revealFraction >= 1.0f) return;
    if (revealFraction <= 0.0f) {
        g_renderer.canvas().fillRect(0, 0, M5.Display.width(), M5.Display.height(), stopwatch::theme::kBackground);
        return;
    }
    int coveredColumns = 4 - (int)(revealFraction * 4.0f + 0.5f);
    if (coveredColumns <= 0) return;
    if (coveredColumns > 4) coveredColumns = 4;
    auto &c = g_renderer.canvas();
    for (int x = 0; x < M5.Display.width(); x += 4) {
        c.fillRect(x, 0, coveredColumns, M5.Display.height(), stopwatch::theme::kBackground);
    }
}

static void renderTransitionFrame() {
    using namespace stopwatch;
    drawCurrentView();
    if (g_transition.mode() == CarouselMotionMode::Iris) {
        uint32_t e = g_transition.elapsed();
        float visible = 0.0f;
        if (!g_transition.hasSwitched()) {
            visible = motion::irisCover(e);
        } else {
            uint32_t local = e > motion::kIrisSwitchMs ? e - motion::kIrisSwitchMs : 0;
            visible = motion::irisReveal(local);
        }
        drawIrisMask(visible, accentForView(g_app.currentView()), motion::irisHalo(e));
    } else if (g_transition.mode() == CarouselMotionMode::Fade) {
        drawFadeMask(motion::fadeReveal(g_transition.elapsed()));
    }
    g_renderer.present();
}
```

- [ ] **Step 2: Start transition on autoplay advance**

In the autoplay block from Task 8, replace direct `g_app.handleEvent(ButtonEvent::KeyBShort)` with:

```cpp
        g_transition.start(millis(), g_carouselSettings.motionMode);
        if (g_carouselSettings.motionMode == CarouselMotionMode::Instant) {
            g_app.handleEvent(ButtonEvent::KeyBShort);
            if (!isBalanceView(g_app.currentView())) g_balScroll.reset();
            ensureCostLoaded();
            ensureBalanceLoaded();
            startViewAnim();
            g_carousel.recordAdvance(millis());
        }
```

Only run the direct advance in the instant branch. Non-instant advances happen when the transition consumes its switch.

- [ ] **Step 3: Tick transition in the loop**

Before the existing entrance animation block in `loop()`, add:

```cpp
    if (g_transition.isAnimating()) {
        g_transition.tick(millis());
        if (g_transition.consumeSwitch()) {
            g_app.handleEvent(ButtonEvent::KeyBShort);
            if (!isBalanceView(g_app.currentView())) g_balScroll.reset();
            ensureCostLoaded();
            ensureBalanceLoaded();
            uint32_t dur = entranceDurationForView(g_app.currentView());
            if (g_carouselSettings.motionMode == CarouselMotionMode::Instant) dur = 0;
            if (dur > 0) g_entrance.start(millis(), dur);
            else g_entrance.cancel();
            g_carousel.recordAdvance(millis());
        }
        renderTransitionFrame();
    }
```

Then change the existing entrance block from:

```cpp
if (g_entrance.isAnimating()) {
```

to:

```cpp
if (!g_transition.isAnimating() && g_entrance.isAnimating()) {
```

- [ ] **Step 4: Cancel transitions on user input**

Add a `cancel()` method to `CarouselTransition` in `firmware/src/CarouselTransition.h`:

```cpp
void cancel() {
    active_ = false;
    pendingSwitch_ = false;
}
```

In the button input block in `loop()`, after `g_power.noteActivity();`, add:

```cpp
g_transition.cancel();
```

- [ ] **Step 5: Build firmware**

Run:

```bash
cd firmware && pio run -e stopwatch
```

Expected: firmware build succeeds.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/main.cpp firmware/src/CarouselTransition.h
git commit -m "firmware: render carousel iris transition"
```

---

## Task 10: README And Full Verification

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update daily use docs**

In `README.md`, update the Daily use table to include settings:

```markdown
| KEYA + KEYB long | Open/close carousel settings |
```

Change the idle line to match firmware:

```markdown
| 5 min idle | Sleep display |
```

Add a short section after Daily use:

```markdown
## Carousel autoplay

Autoplay is enabled by default while the watch is awake. It advances through the main views every 10 seconds and pauses on any button press, touch scroll, row tap, refresh, loading state, or balance detail. It resumes after the configured quiet period.

Hold KEYA + KEYB to open carousel settings on the watch. KEYB moves between rows, KEYA changes the selected value, KEYA-long resets defaults, and KEYA + KEYB long saves and exits.

Settings:

- Autoplay: On / Off
- Interval: 5s / 10s / 15s / 30s
- Motion: Iris / Fade / Instant
- Resume: 10s / 20s / 30s after input
```

- [ ] **Step 2: Run full native tests**

Run:

```bash
cd firmware && pio test -e native
```

Expected: all native firmware tests pass.

- [ ] **Step 3: Run device firmware build**

Run:

```bash
cd firmware && pio run -e stopwatch
```

Expected: build succeeds.

- [ ] **Step 4: Run full repo tests**

Run:

```bash
make test
```

Expected: Swift tests pass, firmware native tests pass, bump-version checks pass.

- [ ] **Step 5: Commit**

```bash
git add README.md
git commit -m "docs: document carousel autoplay"
```

---

## Final Manual Device Check

After all automated checks pass, flash the watch:

```bash
make flash
```

Manual expectations:

- Watch wakes to the cached or live view as before.
- With no input, it advances to the next main view after the configured interval.
- Instrument Iris closes and opens smoothly, with no edge artifacts on the round panel.
- Pressing KEYA or KEYB during autoplay cancels the transition and navigates manually.
- Hold KEYA + KEYB opens settings.
- KEYB cycles rows, KEYA changes values.
- KEYA-long in settings resets defaults.
- KEYA + KEYB long exits settings and persists values after reboot.
- Opening balance detail pauses autoplay.
- Leaving the watch alone still allows idle sleep; autoplay does not keep it awake indefinitely.
