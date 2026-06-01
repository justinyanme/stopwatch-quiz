# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

CodexBar StopWatch mirrors [CodexBar](https://codexbar.app/) usage/spend/balance data from a Mac onto an M5Stack StopWatch (ESP32-S3, 466px circular display) over Bluetooth LE. Two components talk across one binary wire protocol:

- **`bridge/`** — a Swift Package macOS daemon (`stopwatch-bridge`). Reads CodexBar's localhost HTTP API (`codexbar serve`), polls pay-as-you-go API-balance providers directly, and acts as a BLE GATT **peripheral** serving binary snapshots.
- **`firmware/`** — a PlatformIO/Arduino firmware. Acts as a BLE **central** that reads snapshots, decodes them, and renders the watch UI. No agent credentials ever live on the device.

## Commands

```bash
make build        # swift build -c release (the bridge)
make test         # bridge swift tests + firmware native tests + bump_version test
make flash        # zap.py download-mode trigger, then pio run -t upload
make monitor      # serial monitor @115200
make install      # build + register the launchd agent
make pair         # run bridge in foreground with verbose logging (watch it connect)
make bump-patch|bump-minor|bump-major   # edit firmware/src/Version.h via bump_version.py
```

Single tests:

```bash
cd bridge && swift test --filter SnapshotEncoderTests        # one Swift test suite
cd firmware && pio test -e native -f test_state_machine      # one firmware native test (folder name under test/)
```

Per-side test entry points:

```bash
cd bridge && swift test            # all Swift bridge tests
cd firmware && pio test -e native  # all firmware logic tests (no hardware)
cd firmware && pio run             # compile firmware for the device (env: stopwatch)
```

## The wire protocol is the contract

`shared/PROTOCOL.md` is the **single source of truth** for the BLE GATT service and every binary payload. Any change to it MUST land alongside matching edits to **both** `bridge/Sources/StopwatchBridge/Protocol.swift` and `firmware/src/Protocol.h` — these two files are hand-mirrored and there is no codegen.

There are four independently-versioned characteristics, each with its own `(versionMajor, versionMinor)` and encoder/codec pair: `UsageSnapshot`, `CostSnapshot`, `BalanceSnapshot`, `BalanceUsage`. Versioning rules (§3.3): a major bump = structural/stride change (watch refuses to decode a higher major); a minor bump = additive only (new flag bits, new enum values, more providers — older readers degrade gracefully). All integers are little-endian.

**Cross-side compatibility is locked by shared fixtures.** `shared/fixtures/*.json` are canned `codexbar serve` responses; `shared/fixtures/*.hex` are the expected encoder output for each. Both `bridge/Tests/.../*EncoderTests` and `firmware/test/test_*_codec` load the same files, so a breaking change on either side fails tests on both. When you change encoding, regenerate/verify the `.hex` fixtures and run both test suites.

## Bridge architecture (`bridge/Sources/StopwatchBridge/`)

- `Bridge.swift` — `@main` CLI dispatch: `run` (launchd entry), `pair`, `install`, `decode-snapshot`, `set-key`/`list-keys`/`delete-key`, `version`.
- `BridgeService.swift` — the top-level `actor` coordinator. Owns the supervisor, codexbar client, GATT peripheral, and per-characteristic caches + clients.
- `GATTPeripheral.swift` — `@MainActor` CoreBluetooth peripheral; the actor hops to main to touch it. Delegates `RefreshTrigger` writes back to `BridgeService.handleRefresh(scope:)`.
- `*Client.swift` / `*Encoder.swift` / `*Cache.swift` / `*Snapshot.swift` — one set per data domain (Codexbar usage, Cost, Balance, Usage). Encoders mirror the firmware codecs.
- `KeychainStore.swift` — provider API keys live in the macOS Keychain, read from stdin, never from argv or `providers.json`.

Key runtime fact: **`codexbar serve` is slow (5–15 s per call), so the bridge cannot fetch on demand when the watch reads.** A background `prewarmLoop` refreshes every 60 s (and re-arms BLE advertising, which a system sleep can silently drop); a `balancePollLoop` ticks every 30 s honoring each provider's `pollSeconds`. The watch always reads the latest *cached* frame. A `RefreshTrigger` write cancels any in-flight fetch — cancellation is expected, not a failure.

## Firmware architecture (`firmware/src/`)

- `main.cpp` — Arduino `setup()`/`loop()`. Owns all global state, the per-view render dispatch (`drawCurrentView`), the `fetch*AndApply` BLE round-trips, sleep/wake, and the autoplay + entrance-animation tick loop. The loop drops its 50 Hz idle throttle to ~500 Hz while a transition or entrance is animating.
- `App.h`/`App.cpp` — a **pure state machine**: the `ViewId` carousel order (`nextView`/`prevView`), button-event → view-change logic, balance-detail drill-in, and carousel-settings mode. No hardware deps; this is what `test_state_machine` exercises.
- Codecs (`SnapshotCodec`, `CostCodec`, `BalanceCodec`, `UsageCodec`) — decode wire bytes into structs. Pure and unit-tested on native against the shared fixtures.
- `Views/` — one renderer per screen (Overview rings, Provider, Spend, Balances list + Provider-usage detail, CarouselSettings), drawing to `Renderer` (a PSRAM sprite canvas).
- `Anim.h`/`Ease.h` — per-view entrance animations (`Entrance` clock + `motion::` shape functions). `CarouselController.h` decides when to auto-advance; `CarouselTransition.h` runs the iris/fade/instant transition.
- Hardware-only files (`Renderer`, `Buttons`, `SnapshotStore`, `BleClient`, `Power`) and `main.cpp` are **excluded from the native test build** via `build_src_filter` in `platformio.ini` — keep logic testable by leaving it out of those files.

## Conventions & gotchas

- **IDE squiggles lie here.** SourceKit/clang in the editor lacks the SwiftPM module graph and PlatformIO flags, so its diagnostics are false. Trust `swift build`/`swift test` and `pio run`/`pio test`, not the editor.
- **Flashing needs download mode.** `make flash` first runs `firmware/tools/zap.py`, which sends the magic bytes `STOPWATCH-DL\n` over USB-CDC; the running firmware (`onUsbCdcRx` in `main.cpp`) reboots into the ROM bootloader so esptool can sync. If that fails (`No serial data received`), manually long-press BOOT until the screen goes off and the LED blinks, then re-flash. `upload_protocol = esp-builtin` is a dead end on macOS.
- **Versioning:** firmware release semver lives in `firmware/src/Version.h` (`kFirmwareVersion`); only edit it via `make bump-*`. This is separate from the per-characteristic protocol versions in `Protocol.h`.
- **API-balance providers** are configured in `~/Library/Application Support/stopwatch-bridge/providers.json` (`0600`); keys go in the Keychain via `set-key`. After changing a key, kickstart the daemon (`launchctl kickstart -k "gui/$(id -u)/dev.stopwatch.bridge"`) — a running process caches it.
- Design specs and implementation plans live in `docs/superpowers/specs/` and `docs/superpowers/plans/`.
