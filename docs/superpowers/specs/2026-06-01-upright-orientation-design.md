# StopWatch Upright Orientation Design

**Status:** Approved for spec review
**Date:** 2026-06-01
**Owner:** Justin Yan
**Builds on:** `docs/superpowers/specs/2026-05-28-codexbar-stopwatch-design.md`, `docs/superpowers/specs/2026-06-01-carousel-autoplay-design.md`

## 1. Summary

Add an optional on-watch setting that keeps the StopWatch UI upright while the round device is rotated. The first version uses cardinal orientations only: `0`, `90`, `180`, and `270` degrees. The UI snaps instantly after the candidate orientation is stable for a short debounce window.

The setting is off by default and lives in the existing local settings panel. No BLE protocol or bridge change is required.

## 2. Product Context

`PRODUCT.md` defines the device as a precise, calm, glanceable status instrument. This feature should make the physical watch more readable in hand, on a desk, or on a wrist without adding distracting motion or surprising power behavior.

The original StopWatch design listed IMU as out of scope. Current hardware documentation for the M5Stack StopWatch lists a BMI270 six-axis IMU, and the firmware already uses M5Unified for board initialization, display, touch, and buttons. This feature brings IMU use into scope locally on the firmware.

## 3. Goals

- Let the user enable upright UI behavior from the on-watch settings panel.
- Keep all text and data views readable across four cardinal device orientations.
- Use debounced cardinal orientation instead of continuous rotation.
- Keep touch scrolling and row tapping aligned with the visible rotated UI.
- Avoid orientation sensor noise keeping the watch awake.
- Keep orientation logic pure and testable in the native firmware test environment.

## 4. Non-goals

- No continuous arbitrary-angle counter-rotation in the first version.
- No transition animation for orientation changes in the first version.
- No Mac bridge setting, BLE characteristic, or protocol change.
- No per-view coordinate rewrite unless hardware rotation fails.
- No IMU-based wake gesture.
- No compass or absolute heading support.

## 5. User-Facing Behavior

### 5.1 Settings

The existing carousel settings screen becomes a general local settings screen:

```text
SETTINGS

DISPLAY
  UPRIGHT    OFF | ON

CAROUSEL
  AUTOPLAY   ON | OFF
  INTERVAL   5s | 10s | 15s | 30s
  MOTION     IRIS | FADE | INSTANT
  RESUME     10s | 20s | 30s
```

Controls stay the same:

- KEYB short moves to the next row.
- KEYA short changes the selected value.
- KEYA long resets settings to defaults.
- KEYB long sleeps immediately.
- KEYA and KEYB long together saves and exits.

`UPRIGHT` defaults to `OFF`. Reset defaults returns it to `OFF`.

### 5.2 Upright Mode

When `UPRIGHT` is off, the firmware behaves exactly as it does now: fixed display orientation, existing carousel behavior, existing touch behavior, and existing power behavior.

When `UPRIGHT` is on:

- The firmware samples the IMU while the watch is awake.
- It computes the nearest cardinal display orientation from the gravity vector.
- A new orientation commits only after the candidate orientation has stayed stable for `300 ms`.
- Hysteresis prevents flicker near diagonal boundaries.
- The display snaps instantly to the committed orientation.
- A committed orientation change redraws the current view.
- Orientation changes do not call `Power::noteActivity()`.

## 6. Sensor Model

Use the IMU accelerometer/gravity vector as the source of truth for upright orientation. The gyroscope can be used later to improve spin detection, but gyro-only orientation is not acceptable because it drifts.

The first implementation should use `M5.Imu.getAccel()` or `M5.Imu.getImuData()` through M5Unified. Polling should be modest, such as 10-20 Hz while awake and only when `UPRIGHT` is enabled. `M5.update()` remains the main loop board update call.

Flat or invalid sensor readings should not trigger rotation. If the gravity vector magnitude is too small, not finite, or dominated by the device lying flat in a way that makes portrait orientation ambiguous, the controller keeps the current committed orientation.

## 7. Firmware Architecture

### 7.1 OrientationController

Add a pure `OrientationController` with no M5 dependencies.

Responsibilities:

- Convert acceleration samples into candidate cardinal orientations.
- Apply hysteresis so the controller does not bounce near thresholds.
- Apply a stable-time debounce window before committing a new orientation.
- Expose the committed orientation as `DisplayOrientation::Deg0`, `Deg90`, `Deg180`, or `Deg270`.
- Report whether a new commit happened on a tick.

Suggested API shape:

```cpp
enum class DisplayOrientation : uint8_t { Deg0 = 0, Deg90 = 1, Deg180 = 2, Deg270 = 3 };

struct OrientationSample {
    float ax;
    float ay;
    float az;
};

class OrientationController {
public:
    void begin(uint32_t nowMs, DisplayOrientation initial);
    bool tick(uint32_t nowMs, OrientationSample sample);
    DisplayOrientation committed() const;
    void reset(uint32_t nowMs, DisplayOrientation orientation);
};
```

The exact axis mapping must be verified on hardware because board orientation and M5Unified axis conversion determine which accel axis maps to the top of the circular display.

### 7.2 Settings Model

The current `CarouselSettings` struct grows one setting, or the implementation may rename it to `DeviceSettings` if that keeps ownership clearer.

Required fields:

- `uprightEnabled`, default `false`.
- Existing carousel settings unchanged.

Validation clamps invalid stored values to defaults. Persistence can continue using the existing NVS settings path if the stored format is versioned or backward-compatible with existing saved carousel settings.

### 7.3 Main Loop Integration

`main.cpp` owns hardware reads and side effects:

1. Load settings from NVS.
2. Initialize orientation controller to `Deg0`.
3. If `uprightEnabled`, poll IMU on a timer while awake.
4. Feed valid samples into `OrientationController`.
5. When a new orientation commits, apply it to display presentation and redraw the current view.

Orientation redraws do not fetch data, do not restart view entrance animations, and do not mark user activity.

## 8. Rendering Strategy

Use hardware cardinal rotation first through `M5.Display.setRotation()` or the equivalent LovyanGFX rotation API. The display is square (`466 x 466`), so all views can continue drawing in the existing logical coordinate system.

`Renderer` should own the display rotation application, or at least provide a narrow method so `main.cpp` does not scatter direct display rotation calls. `Renderer::present()` continues to push the existing full-screen sprite once per frame.

If hardware verification shows that display rotation behaves incorrectly on the StopWatch panel, the implementation should switch to rotating the full-screen sprite during presentation with LovyanGFX sprite rotation APIs. Per-view orientation-aware drawing is the last resort because it would touch every view.

## 9. Touch Behavior

Touch must align with the visible UI when `UPRIGHT` is enabled. Dragging down on the rotated Balances list should scroll down visually, and tapping a visible row should select that row.

Implementation should normalize touch coordinates exactly once:

- If M5GFX returns touch details already transformed by the current display rotation, use those values directly.
- If touch details remain in physical panel coordinates, map them through the committed orientation before passing them to `TouchScroll`, `balanceRowAtY`, or other hit testing.

Hardware verification must check this explicitly to avoid double-rotating touch coordinates.

## 10. Power And Carousel Behavior

Orientation changes are not user activity. They must not call `Power::noteActivity()` and must not keep the watch awake indefinitely.

Rules:

- Buttons and touch still count as activity.
- Passive orientation commits only redraw.
- Idle sleep wins even if orientation has just changed.
- Entering sleep does not need IMU polling.
- On wake, the display starts at `Deg0`; if `UPRIGHT` is enabled, the first stable IMU orientation may commit after the debounce window.
- Carousel timing should not reset due to orientation commits.

## 11. Error Handling

If the IMU is unavailable, fails to initialize, or repeatedly fails to read:

- Boot continues.
- BLE, rendering, buttons, touch, carousel, and sleep continue normally.
- `UPRIGHT` may remain saved as `ON`, but runtime rotation is disabled for that boot.
- The display remains at the default orientation.

The first version does not need an on-screen IMU error indicator. Serial logging is enough for hardware bring-up.

## 12. Edge Cases

| Case | Behavior |
|---|---|
| `UPRIGHT` off | Fixed orientation; current behavior preserved. |
| Stored setting invalid | Clamp to defaults, including `UPRIGHT OFF`. |
| Watch held near a diagonal boundary | Keep current orientation until hysteresis and debounce allow a stable change. |
| Watch is flat on a desk | Keep current orientation if gravity does not provide a reliable display-up direction. |
| User presses a button during debounce | Button behavior runs normally; orientation may commit later if still stable. |
| Orientation changes during carousel transition | Apply instant rotation and redraw the current transition/current view without resetting carousel timers. |
| Orientation changes while in settings | Settings screen rotates like any other screen. |
| IMU read failure after enabling | Stay at default/current orientation and keep the feature harmless. |

## 13. Testing Plan

Firmware native tests:

- `test_orientation_controller`: cardinal bucket selection, debounce timing, hysteresis, invalid sample rejection, flat/ambiguous sample handling, and no premature commit.
- `test_state_machine`: settings row order, `UPRIGHT` default off, cycling on/off, reset defaults, and existing carousel settings behavior.
- `test_touch_mapping`: coordinate mapping for all four committed orientations if manual mapping is required.

Regression checks:

- Existing native firmware tests still pass with `pio test -e native`.
- Firmware still builds for the StopWatch with `pio run -e stopwatch`.
- Existing carousel autoplay behavior remains unchanged when `UPRIGHT` is off.

Manual hardware verification:

1. Flash the StopWatch.
2. Confirm default behavior is fixed orientation.
3. Open settings, enable `UPRIGHT`, save, and reboot.
4. Rotate through `0`, `90`, `180`, and `270` degrees; text should never remain upside down after the debounce window.
5. Open Balances and verify drag/tap behavior follows the rotated UI.
6. Confirm idle sleep still happens without button or touch input.
7. Disable `UPRIGHT` and confirm fixed orientation returns.

## 14. Implementation Sequence

1. Add the pure `OrientationController` and tests.
2. Extend settings storage and settings state with `uprightEnabled`.
3. Update the settings view from `CAROUSEL` to grouped `SETTINGS`.
4. Add renderer/display rotation support.
5. Integrate IMU polling and orientation commits in `main.cpp`.
6. Normalize touch coordinates if hardware verification shows the display rotation does not already do it.
7. Run native tests and StopWatch firmware build.
8. Perform hardware verification and adjust axis mapping if the observed accel axes do not match the assumed display directions.
