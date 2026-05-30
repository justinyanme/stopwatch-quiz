# Firmware version stamp — design

**Date:** 2026-05-31
**Status:** Approved, ready for implementation

## Goal

Give the firmware a human-facing release version (semver, starting at `0.1.1`),
bumped deliberately with a script, and display it discreetly at the bottom of the
SPEND & BURN view.

This is distinct from the `Protocol.h` constants (`kVersionMajor`, etc.), which
are BLE wire-format versions, not a release version.

## Source of truth — `firmware/src/Version.h` (new, committed)

```cpp
#pragma once
namespace stopwatch {
// Firmware release version (semver). Bump via firmware/tools/bump_version.py.
inline constexpr char kFirmwareVersion[] = "0.1.1";
}
```

Rationale (vs. a `platformio.ini` `-D` macro or a `git describe` dynamic flag):

- Mirrors the existing convention — `build_icons.py` emits committed C headers,
  hand-run and idempotent, not wired into the build.
- No `-D"string"` nested-quote escaping hazard.
- Directly readable from the `[env:native]` Unity tests with no per-env macro
  plumbing.
- A deliberately-bumped semver should not change implicitly with every commit,
  so `git describe` is the wrong fit for the primary version (it's reserved as an
  optional future build-metadata suffix).

## Bump script — `firmware/tools/bump_version.py` (new)

- Usage: `python3 firmware/tools/bump_version.py {major|minor|patch}`
- A pure `next_version(current: str, part: str) -> str` does the arithmetic;
  the script regex-reads the `kFirmwareVersion` line, replaces it, writes back,
  and prints `0.1.1 -> 0.1.2`.
- Semver reset rules: bumping `minor` resets `patch` to 0; bumping `major` resets
  `minor` and `patch` to 0.
- **No git side effects** — it only edits the file. Commit/tag is manual.
- Errors clearly if the version line can't be found or the current value isn't a
  three-part numeric semver.

## Makefile targets (root `Makefile`)

```
make bump-patch   # 0.1.1 -> 0.1.2
make bump-minor   # 0.1.1 -> 0.2.0
make bump-major   # 0.1.1 -> 1.0.0
```

Each shells out to `python3 firmware/tools/bump_version.py <part>`.

## Display — `firmware/src/Views/Spend.cpp`

- `#include "../Version.h"`.
- In `drawTotalSpend`, after the status pill, render `v<kFirmwareVersion>`
  centered at the bottom (~y=448) in `kFontMicro` (Font2), color `kTextMuted`.
- Coexists with the status pill: the pill (y≈425) only renders in
  error/stale states (`drawPill` early-returns on a null label), and the version
  sits just below it. Exact Y to be confirmed on-device — round 466 px screen,
  so it must clear both the pill and the bottom bezel.
- Format: leading `v`, e.g. `v0.1.1`.
- Scope: this one view only; firmware only (the Swift bridge is not versioned by
  this change).

## Testing

- `next_version` is verified across the three bump kinds plus the reset rules
  (e.g. `0.1.9 patch -> 0.1.10`, `0.1.1 minor -> 0.2.0`, `0.9.9 major -> 1.0.0`).
  The repo has no Python test harness, so this is exercised directly rather than
  via pytest.
- `pio run -e stopwatch` must still build (Spend.cpp include + render compiles).
- `pio test -e native` must still pass (Version.h compiles in the native env).

## Out of scope (explicitly deferred)

- Git short-hash build-metadata suffix (`v0.1.1·a1b2c3d`).
- Auto commit + tag in the bump script (`make release`).
- Showing the version on other views.
