# StopWatch Carousel Autoplay And Transition Design

**Status:** Approved for spec review
**Date:** 2026-06-01
**Owner:** Justin Yan
**Builds on:** `docs/superpowers/specs/2026-05-28-codexbar-stopwatch-design.md`, `docs/superpowers/specs/2026-05-29-codexbar-spend-views-design.md`, `docs/superpowers/specs/2026-05-29-api-balances-design.md`

## 1. Summary

Add an on-watch carousel autoplay mode that advances through the main views every configured interval. The feature turns the StopWatch into a calm desk display while preserving manual two-button navigation. Transitions use a restrained "Instrument Iris" motion: the current view closes into black, the next view is drawn beneath it, then the mask opens from the center with a brief accent halo.

The watch owns all autoplay behavior locally. No BLE protocol changes are required.

## 2. Product Context

`PRODUCT.md` defines the register as `product` and the personality as precise, calm, and instrumental. This feature should feel like a physical status instrument, not a phone-app carousel. Motion must clarify navigation and time without stealing attention from the data.

## 3. Goals

- Auto-advance through the main carousel while the watch is awake.
- Provide on-watch settings for autoplay, interval, motion mode, and resume delay.
- Keep manual navigation, refresh, touch scrolling, balance detail, and sleep behavior predictable.
- Use a smooth transition that is practical on the current single-sprite renderer.
- Support reduced-motion use through non-iris modes.
- Keep the implementation testable in `pio test -e native`.

## 4. Non-goals

- No Mac helper configuration for carousel settings.
- No BLE protocol or characteristic change.
- No auto-entering balance detail screens.
- No new phone-style gesture vocabulary.
- No full two-buffer compositor in the first implementation.

## 5. Interaction Model

### 5.1 Normal Carousel

Autoplay is enabled by default while the watch is awake. It advances only through the existing main carousel order:

```text
Overview -> TotalSpend -> Codex -> CodexCost -> Claude -> ClaudeCost -> Gemini -> Balances -> Overview
```

Autoplay never opens balance detail. If the user manually opens balance detail, autoplay pauses until they exit back to the Balances list and the resume delay has elapsed.

### 5.2 Manual Control

Manual control always wins:

- KEYA short: previous view.
- KEYB short: next view.
- KEYA long: refresh.
- KEYB long: sleep.
- Touch scroll or row tap on Balances pauses autoplay.
- Entering balance detail pauses autoplay.

Every user action records carousel activity time. Autoplay resumes only after the configured resume delay.

### 5.3 Settings Access

Open settings by holding KEYA and KEYB together for about 800 ms from any main view. Settings is a modal-style screen, not part of the normal carousel order and not part of autoplay.

Settings controls:

- KEYB short: move to the next settings row.
- KEYA short: change the selected row's value.
- KEYA and KEYB long together: exit settings and save.
- KEYB long: sleep immediately.
- KEYA long: reset carousel settings to defaults.

Settings rows:

| Row | Values | Default |
|---|---|---|
| Autoplay | On, Off | On |
| Interval | 5s, 10s, 15s, 30s | 10s |
| Motion | Iris, Fade, Instant | Iris |
| Resume | 10s, 20s, 30s after input | 20s |

## 6. Power Behavior

The existing firmware idle sleep remains the backstop. Autoplay must not call `Power::noteActivity()` just because a timer advances a view. If it did, autoplay could keep the watch awake forever.

Rules:

- User input calls `noteActivity()`.
- Entrance and transition animation frames may keep rendering smoothly, but they do not reset idle sleep unless they were caused by user input.
- If the power layer decides to sleep during a passive autoplay cycle, sleep wins.
- On wake, the watch resumes at the current view and fetches fresh data as it does today.

## 7. Motion Design

### 7.1 Instrument Iris

Default transition duration: 360 ms.

Phases:

1. Close, 0-150 ms: a black circular mask closes toward the center over the current view.
2. Switch, near 150 ms: firmware advances the view and performs any lazy read needed for the destination.
3. Open, 150-360 ms: the mask opens from center, revealing the next view.
4. Halo, 170-310 ms: a thin accent ring expands slightly and fades. Accent color comes from the destination view when obvious, otherwise muted text color.

Easing:

- Use existing `ease::outExpo` or a related pure helper.
- No bounce, no overshoot, no elastic motion.
- All helpers clamp to 0..1 and are native-testable.

### 7.2 Existing Entrance Animations

The destination view may still play its existing entrance animation when it adds value:

- Overview and provider ring views: ring sweep.
- Spend and provider cost views: count-up and bar rise.
- Balance detail usage: existing bar rise.
- Balances list: no extra entrance.

The carousel transition and destination entrance must not fight. Start the destination entrance after the iris switch point, not before.

### 7.3 Reduced Motion

`Motion: Fade` performs a short opacity-style reveal where practical, without directional movement or halo.

`Motion: Instant` changes views without transition. Existing per-view entrance animation is also skipped for passive autoplay in Instant mode, but manual navigation can still use entrance animation unless the implementation chooses one consistent instant path for simplicity.

## 8. Firmware Architecture

### 8.1 CarouselSettings

A small struct stores user settings:

```cpp
struct CarouselSettings {
    bool autoplayEnabled;
    uint16_t intervalSeconds;
    uint8_t motionMode;       // Iris, Fade, Instant
    uint16_t resumeSeconds;
};
```

Persist settings in NVS under the existing `swq` namespace. This can be implemented through a small typed helper or by extending `SnapshotStore` with settings load/save methods.

### 8.2 CarouselController

Pure, native-testable state machine:

- Holds current settings.
- Tracks last automatic advance time.
- Tracks last user activity time.
- Knows whether autoplay is paused by settings, detail mode, settings mode, touch, loading, or transition.
- Exposes `tick(nowMs, context)` or equivalent.
- Returns an explicit action such as `Advance`.

The controller should not draw and should not fetch. It only decides when an advance is due.

### 8.3 Transition

Pure animation clock modeled after `Entrance`:

- `start(nowMs, fromView, toView, mode)`
- `tick(nowMs)`
- `isAnimating()`
- `hasSwitched()`
- `progress values` for iris close/open/halo

Motion helpers live in `Anim.h` so `test_anim` can cover monotonicity and clamps.

### 8.4 App State

`App` gains:

- Settings mode flag.
- Selected settings row.
- Methods to enter, exit, and mutate settings.
- Handling for a new combo button event.

The existing detail behavior remains:

- In balance detail, KEYA exits detail.
- In balance detail, KEYB toggles cost/tokens.
- Long presses preserve refresh/sleep meaning except when settings mode explicitly overrides KEYA long for reset.

### 8.5 Buttons

`Buttons` gains a combo event without disrupting existing single-button events:

```cpp
BothLong
```

Combo detection should win only when both buttons are held together before either single-button long fires. This prevents accidental settings entry during normal refresh or sleep long-presses.

### 8.6 Rendering

Add `Views/CarouselSettings.{h,cpp}`:

- Title: `CAROUSEL`
- Four compact rows with selected-row highlight.
- Values are readable in Font4 or Font2 depending on fit.
- Status copy is minimal: no instructions block. Labels and selection state should make the screen self-explanatory.

Transition rendering:

- First implementation uses the current full-screen sprite and draws a mask overlay, not a full two-buffer compositor.
- The old view remains visible during close.
- After switch, the new view is drawn normally, then the opening mask is applied.
- The mask should respect the circular display and avoid edge artifacts.

## 9. Lazy Data And Loading

Autoplay may land on spend or balance screens that need lazy data. The current code already handles first entry by rendering loading overlays and fetching.

Rules:

- If autoplay advances into a spend screen and cost is not loaded, load cost exactly as manual navigation does.
- If autoplay advances into Balances and balances are not loaded, load balances exactly as manual navigation does.
- Loading pauses the carousel timer.
- A failed lazy fetch still shows the destination view with the existing stale or unavailable state. Autoplay may continue after the resume delay unless the user interacts.

## 10. Edge Cases

| Case | Behavior |
|---|---|
| Autoplay Off | Manual carousel works exactly as today. |
| User presses a button during transition | Cancel transition, apply user event, restart resume timer. |
| User touches Balances during transition | Cancel transition if destination is Balances, then process touch when stable. |
| Watch enters sleep during transition | Sleep wins; transition state resets on wake. |
| Settings save fails | Keep in-memory settings for current session, fall back to defaults on next boot. |
| Stored settings invalid | Clamp to defaults. |
| Bridge unavailable | Existing link and stale states render; autoplay does not hide status pills. |

## 11. Testing Plan

Bridge tests are not required because there is no Mac helper or protocol change.

Firmware native tests:

- `test_state_machine`: combo-button settings entry/exit, row selection, value cycling, reset defaults.
- `test_carousel_controller`: default autoplay timing, resume delay, disabled mode, pause in settings, pause in balance detail, no advance while loading or transition active.
- `test_anim`: iris close/open helpers clamp and are monotonic, fade helpers clamp, transition clock switches exactly once.
- Existing state machine tests remain valid.

Build verification:

```bash
cd firmware && pio test -e native
cd firmware && pio run -e stopwatch
```

Full repo verification:

```bash
make test
```

## 12. Implementation Sequence

1. Add `CarouselSettings` persistence and defaults.
2. Add combo-button event support with native tests.
3. Add `CarouselController` with timing tests.
4. Add transition clock and motion helper tests.
5. Add settings-mode state to `App` with tests.
6. Add settings view rendering.
7. Wire autoplay, lazy-load handling, transitions, and power behavior in `main.cpp`.
8. Run native tests and device build.

## 13. Acceptance Criteria

- The watch auto-advances through main views by default.
- Settings can be opened, changed, saved, and persisted on watch.
- Manual input pauses autoplay and resumes after the configured delay.
- Balance detail is never entered automatically.
- The Instrument Iris transition is smooth and bounded to about 360 ms.
- Reduced-motion settings provide Fade and Instant alternatives.
- Autoplay does not keep the watch awake forever.
- Existing BLE snapshots, spend, balances, and usage detail behavior remain intact.
