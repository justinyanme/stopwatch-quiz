# CodexBar StopWatch — Design

**Status:** Approved
**Date:** 2026-05-28
**Owner:** Justin Yan
**Implementation repo:** `/Users/justinyan/Documents/JProj/stopwatch-quiz`

## 1. Summary

Bring three CodexBar usage indicators (Codex, Claude Code, Gemini) onto an M5Stack StopWatch worn or sat on the desk. Press a button → the watch wakes, pulls a fresh snapshot from this Mac over Bluetooth LE, and renders activity-style rings showing how close each provider is to its session and weekly limits. No agent credentials live on the device.

The work splits into two new artifacts plus zero changes to CodexBar:

- **`stopwatch-bridge`** — a tiny Swift CLI that runs in the background on macOS, calls CodexBar's existing `codexbar serve` localhost JSON API, and re-publishes a compact binary snapshot over a custom GATT service.
- **`stopwatch-firmware`** — PlatformIO + Arduino + M5Unified + NimBLE firmware that sleeps by default, wakes on button press, fetches a snapshot, renders rings, and sleeps again.

## 2. Goals

- **Glance latency ≤ 1 s** from button press to rings visible (with cached snapshot for the first frame and BLE-fetched fresh values landing in <1 s typical).
- **Days of battery** under wake-to-glance usage; no measurable drain when asleep.
- **Zero secrets on the device.** All provider auth stays inside CodexBar on the Mac.
- **No changes to CodexBar.app.** Bridge consumes CodexBar's existing `codexbar serve` localhost API.
- **Two-way schema safety.** A version byte lets either side detect drift and refuse to render misleading data.

## 3. Non-goals (v1)

Explicitly out of scope so they don't bleed in:

- Touchscreen, microphone, speaker, IMU.
- Always-on display.
- BLE bonding / encryption (custom service UUID + short-range BLE is the threat model).
- Cost / spend history charts.
- Providers beyond Codex / Claude Code / Gemini.
- On-device configuration UI; firmware values are baked, config changes = reflash.
- OTA firmware updates; USB-C flash only.
- Multi-Mac support; one bridge advertising, one watch connecting.
- Threshold notifications or haptics.
- Direct provider scraping fallback if CodexBar is uninstalled.

## 4. Architecture

```
┌────────────────────┐    HTTP GET           ┌─────────────────────────┐    BLE GATT     ┌────────────────────┐
│ CodexBar.app       │ ◄─── 127.0.0.1:<P> ── │ stopwatch-bridge        │ ◄── < 5 m ─────►│ M5Stack StopWatch  │
│ (untouched)        │  /usage?provider=both │ (Swift CLI, launchd)    │  custom service │ (PlatformIO fw)    │
│                    │                       │                         │  + 2 chars      │                    │
│ codexbar serve ──► │                       │ • spawns codexbar serve │                 │ • sleep by default │
│   (child of bridge)│                       │ • CoreBluetooth peri    │                 │ • wake → fetch     │
└────────────────────┘                       │ • binary snapshot       │                 │ • render rings     │
                                             └─────────────────────────┘                 └────────────────────┘
                                                          ▲
                                                          │ ~/Library/Application Support/
                                                          │   stopwatch-bridge/config.json
                                                          │ (codexbarPort, serviceUUID)
```

`<P>` is a random ephemeral port picked at install time with `jot -r 1 49152 65535` and persisted to the bridge's config file. The bridge owns the `codexbar serve` child process; if the user already runs `codexbar serve` themselves they can set `codexbarPort` manually and the bridge skips the spawn.

## 5. Component: `stopwatch-bridge` (Swift CLI)

### 5.1 Layout

```
bridge/
├── Package.swift                       # Swift 6.2+, single executable target
├── Sources/StopwatchBridge/
│   ├── main.swift                      # arg parsing → dispatch
│   ├── BridgeService.swift             # top-level actor; owns lifecycle
│   ├── CodexbarClient.swift            # URLSession around codexbar serve
│   ├── CodexbarSupervisor.swift        # spawns + restarts codexbar serve
│   ├── SnapshotEncoder.swift           # CodexbarUsage → Data (binary protocol)
│   ├── GATTPeripheral.swift            # CBPeripheralManagerDelegate
│   └── Config.swift                    # JSON config read/write
└── Tests/StopwatchBridgeTests/
    ├── SnapshotEncoderTests.swift
    └── CodexbarClientTests.swift       # URLProtocol stubs
```

### 5.2 Commands

| Command | Behavior |
|---|---|
| `stopwatch-bridge run` | Foreground; what launchd invokes. |
| `stopwatch-bridge install` | Generates port, writes config, installs launchd plist (`~/Library/LaunchAgents/dev.stopwatch.bridge.plist`), bootstraps it. |
| `stopwatch-bridge pair` | Foreground with verbose logging; for first-time setup so the user sees the watch connect. |
| `stopwatch-bridge decode-snapshot <hex>` | Dumps a captured binary snapshot as readable JSON. |

### 5.3 Lifecycle

1. launchd starts `stopwatch-bridge run` at login (`RunAtLoad`, `KeepAlive`).
2. `BridgeService` boots. Reads `Config`. If `spawnCodexbar` is `true` (default) and `GET <codexbarPort>/health` is not reachable, `CodexbarSupervisor` spawns `codexbar serve --port <codexbarPort>` as a child `Process`. Crash → exponential backoff (1 s → 2 s → 4 s → cap 30 s). If `spawnCodexbar` is `false`, the supervisor is skipped and the bridge assumes the user runs `codexbar serve` themselves on the configured port.
3. `GATTPeripheral` starts advertising the custom service UUID. macOS prompts for Bluetooth permission once.
4. On watch connect → wait for `RefreshTrigger` write (1 byte: provider scope) → `CodexbarClient.fetch()` → `SnapshotEncoder.encode()` → update `UsageSnapshot` characteristic + send notify.
5. On disconnect → keep advertising.

### 5.4 Permissions

Bluetooth is the only required system permission. The launchd plist is `LimitLoadToSessionType: Aqua` (user session) so Bluetooth context is available.

### 5.5 Tests

- `SnapshotEncoderTests` — fixture CodexBar JSON → expected hex bytes for every edge case (unknown values, error flags, all-three vs single-provider scope).
- `CodexbarClientTests` — `URLProtocol` stub returns canned JSON; assert decode and timeout paths.
- GATT is integration-tested via `stopwatch-bridge pair` against a real watch; the CoreBluetooth surface is too thin to mock usefully.

## 6. Component: `stopwatch-firmware` (PlatformIO)

### 6.1 Layout

```
firmware/
├── platformio.ini                  # board = m5stack-stopwatch (may need custom board JSON)
├── include/
├── src/
│   ├── main.cpp                    # setup() + loop() entry
│   ├── App.{h,cpp}                 # top-level state machine
│   ├── Buttons.{h,cpp}             # KEYA / KEYB short + long press detection
│   ├── Power.{h,cpp}               # sleep / wake / dim
│   ├── BleClient.{h,cpp}           # NimBLE central
│   ├── SnapshotCodec.{h,cpp}       # decode binary payload
│   ├── SnapshotStore.{h,cpp}       # NVS-backed cache
│   ├── Renderer.{h,cpp}            # LovyanGFX sprite wrapper
│   ├── Theme.h                     # colors, fonts
│   └── Views/
│       ├── Overview.cpp            # three concentric rings (Codex/Claude/Gemini)
│       └── Provider.cpp            # per-provider concentric rings
└── test/
    ├── test_snapshot_codec/        # native, host-side
    └── test_state_machine/         # native, host-side
```

### 6.2 Libraries

`M5Unified`, `LovyanGFX` (transitive), `NimBLE-Arduino`. No `ArduinoJson` on the hot path — snapshot is binary.

### 6.3 State machine

```
[Booting]
   │ M5Unified.begin → display → NVS.load → Renderer ready
   ▼
[ShowingView: Overview]  ◄──┐
   │ KEYB short ─► next provider in cycle
   │ KEYA short ─► previous provider in cycle
   │              (Overview ↔ Codex ↔ Claude ↔ Gemini ↔ Overview)
   │ KEYA long  ─► force refresh + show spinner
   │ KEYB long  ─► immediate sleep
   │ 15s idle   ─► [Sleeping]
   ▼
[Sleeping]
   │ KEYA or KEYB wake (ext1)
   ▼
[Booting] (warm; cached snapshot loads instantly, BLE fetch races in background)
```

### 6.4 Wake-to-first-paint budget (target ≤ 1 s)

- T+0 ms: ext1 wakeup ISR; light-sleep exit.
- T+~50 ms: M5Unified re-inits display; cached snapshot pushed to screen (rings already visible).
- T+~200 ms: BLE scan starts.
- T+~700 ms: scan hit → connect → write `RefreshTrigger` → read `UsageSnapshot` → re-render with fresh data; rings animate from cached → fresh over 250 ms.

If BLE doesn't respond within 3 s, a `● no bridge` pill appears under the rings and the cached values stay on screen.

### 6.5 Renderer notes

A single 466 × 466 RGB565 sprite in PSRAM (~434 KB) is the draw target. All views draw to the sprite, then a single `pushSprite(0, 0)` blits to the QSPI display. This avoids tearing on the AMOLED and saves QSPI bandwidth versus per-element draws.

Activity rings: outer ring radius 80, middle 58, inner 36 (canvas units; scaled in the sprite). Stroke 14 px. Anti-aliasing via `LovyanGFX::fillArc` with `setCircle` smoothness.

### 6.6 Tests

- `test_snapshot_codec` (native): decode known-good byte fixtures, verify struct fields. Mirrors bridge's encoder tests so wire compat is diff-checkable.
- `test_state_machine` (native): drive `App::handleEvent()` with synthetic button + BLE events, assert view transitions.
- No on-device tests in v1; flash + glance is faster for display/BLE validation.

## 7. Wire protocol (GATT)

Single source of truth: `shared/PROTOCOL.md`. Both sides include it; the binary format is reproduced below.

### 7.1 Service

```
Service UUID: <minted at project setup, persisted in PROTOCOL.md>
```

UUID is generated once via `uuidgen` at project setup and locked into `PROTOCOL.md`. It is the only "soft identifier" between bridge and watch in v1.

### 7.2 Characteristics

Two characteristic UUIDs are minted alongside the service UUID via `uuidgen` and locked into `PROTOCOL.md`. They are fully independent UUIDs (not derived from the service UUID by suffix manipulation) so the bridge and watch each store three constant UUIDs.

| Name | Properties | Purpose |
|---|---|---|
| `UsageSnapshot` | Read + Notify | Latest snapshot bytes. Watch reads on wake; bridge notifies on snapshot change while watch is connected. |
| `RefreshTrigger` | Write (no response) | Watch writes one byte (provider scope): `0x00` = all, `0x01` = codex, `0x02` = claude, `0x03` = gemini. Bridge re-queries `codexbar serve` and updates `UsageSnapshot`. |

### 7.3 Payload format (binary)

```
Header (8 bytes)
  uint8  versionMajor      = 0x01    (structural: changing this breaks the watch)
  uint8  versionMinor      = 0x00    (additive: new trailing fields per provider)
  uint8  providerCount     = 3
  uint8  flags             bit0 = stale, bit1 = bridge_error, bit2 = provider_missing
  uint32 capturedAt        unix seconds (when bridge captured this snapshot)

Per provider (repeats providerCount times, 16 bytes each):
  uint8  providerID        1 = codex, 2 = claude, 3 = gemini
  uint8  status            0 = ok, 1 = warn, 2 = critical, 3 = error, 4 = disabled
  uint8  sessionPct        0–100; 0xFF = unknown
  uint8  weekPct           0–100; 0xFF = unknown
  uint32 sessionResetAt    unix seconds; 0 = unknown
  uint32 weekResetAt       unix seconds; 0 = unknown
  uint16 credits           value × 10 (so 112.4 → 1124); 0xFFFF = unknown
  uint8  plan              0 = unknown, 1 = free, 2 = plus, 3 = pro, 4 = team, 5 = enterprise
  uint8  reserved          0x00
```

Total for 3 providers: 8 + (3 × 16) = **56 bytes**. Well below the BLE 244-byte MTU; no fragmentation.

### 7.4 Versioning rule

Two version bytes form a `(major, minor)` pair with distinct semantics:

- **Major (`versionMajor`)** governs structural compatibility. Bridge always sends the highest major it knows. Watch refuses to decode any major greater than what it knows and renders an "update firmware" text screen instead of misinterpreting bytes. Bumping major = reordering or removing fields.
- **Minor (`versionMinor`)** is additive. New trailing fields can be appended to the per-provider block. Watch decodes only the fields it knows and ignores any trailing bytes within each provider record (using `providerCount` and a known per-major record stride). Watch shows partial data without warning when minor exceeds its own.

This lets the bridge ship new payload fields without forcing a firmware reflash, while still guaranteeing safety against structural drift.

## 8. UX

### 8.1 Overview screen (home)

Three concentric activity rings on a black background:

- **Outer ring** — Codex (`#ff8a3d`), filled clockwise to `sessionPct`.
- **Middle ring** — Claude (`#c47bff`), filled to `sessionPct`.
- **Inner ring** — Gemini (`#4dc4ff`), filled to `sessionPct`.

Center text shows the **worst-off provider's percent** in that provider's color, with its reset countdown below (e.g., `72%` then `CX · resets 2:15p`).

Bottom of the circle shows three small color-coded legend chips: `●CX ●CL ●GM`.

### 8.2 Per-provider screen

Same ring language, zoomed in on one provider:

- **Outer ring** — session usage (provider color).
- **Inner ring** — week usage (lighter shade of provider color).
- **Top label** — provider name + colored dot.
- **Center text** — `SESSION` label, big session percent in provider color, time-to-reset under it.
- **Bottom strap** — small text: `Week 41% · 112 cr` (or `Week 41% · Pro` if no credits).

### 8.3 Navigation

| Input | Action |
|---|---|
| KEYA short | Previous view (Overview → Gemini → Claude → Codex → Overview …) |
| KEYB short | Next view (Overview → Codex → Claude → Gemini → Overview …) |
| KEYA long (>800 ms) | Force refresh — fires `RefreshTrigger` again, shows tiny spinner |
| KEYB long (>800 ms) | Immediate sleep |
| 15 s idle | Auto-dim then sleep |

### 8.4 Status pills

Rendered under the rings when relevant. Each pill maps to exactly one failure path:

- `● no bridge` — watch's BLE scan didn't find a peripheral (set by firmware after 3 s scan timeout).
- `● link error` — connect succeeded but characteristic read failed (set by firmware after one retry).
- `● stale` — bridge responded successfully, but the payload's `stale` flag is set (CodexBar fetch failed bridge-side, payload holds prior values).
- `● no source` — payload's `provider_missing` flag is set (the `codexbar` CLI isn't installed on the Mac).

## 9. Error handling

### 9.1 Bridge-side

| Failure | Bridge response | Watch sees |
|---|---|---|
| `codexbar` not on PATH | Empty snapshot, `bridge_error` + `provider_missing` flags | Gray rings, `● no source` pill |
| `codexbar serve` child crashes | Supervisor restarts (1→2→4→cap 30 s) | `bridge_error` flag during the gap |
| `codexbar serve` returns 5xx / >5 s timeout | Keep prior values, set `stale` flag, bump `capturedAt` | `● stale` pill |
| Bluetooth permission denied | One clear log line, exit non-zero (launchd surfaces in Console) | Never finds peripheral; cached snapshot + `● no bridge` pill |
| Two `stopwatch-bridge` instances on same Mac | Each tags advertising with 2-byte instance hash | Watch picks strongest RSSI |

### 9.2 Firmware-side

| Failure | Firmware response |
|---|---|
| No BLE scan hit within 3 s | Cached snapshot + `● no bridge` pill |
| Connect succeeds, read fails | One retry, then cached + `● link error` pill |
| `UsageSnapshot.version` unknown | Single text screen: "Firmware older than bridge — flash with `make flash`" |
| NVS empty on first cold boot, no bridge in range | "Connecting to Mac…" splash; retry scan every 5 s up to 30 s, then sleep |
| Battery < 10% | Battery indicator dims red; refresh cadence unchanged |

RTC drift is bounded by the RX8130CE's ~5 ppm; over a 5-hour session window that's <1 s, no resync protocol needed in v1.

## 10. Repo layout

```
stopwatch-quiz/
├── bridge/                         # Swift Package — the macOS CLI
│   ├── Package.swift
│   ├── Sources/StopwatchBridge/
│   └── Tests/StopwatchBridgeTests/
├── firmware/                       # PlatformIO project
│   ├── platformio.ini
│   ├── src/
│   ├── include/
│   └── test/
├── shared/                         # Single source of truth for the wire format
│   ├── PROTOCOL.md                 # GATT UUIDs + payload schema
│   └── snapshot.schema.json        # JSON Schema for the JSON form (used by decode-snapshot)
├── scripts/
│   ├── install-bridge.sh           # generates random port, writes config, installs launchd plist
│   ├── flash-firmware.sh           # pio run -t upload
│   └── decode-snapshot.sh          # convenience wrapper around the Swift subcommand
├── docs/superpowers/specs/
│   └── 2026-05-28-codexbar-stopwatch-design.md   # this file
├── Makefile                        # build, install, flash, monitor, test targets
├── README.md
└── .gitignore
```

`bridge/` and `firmware/` are siblings inside one repo because they share `shared/PROTOCOL.md` and want lockstep versioning. Splitting them across repos would require a release dance every time the wire format changes.

## 11. Configuration files

### 11.1 Bridge config

`~/Library/Application Support/stopwatch-bridge/config.json`

```json
{
  "codexbarPort": 51237,
  "serviceUUID": "<minted at install>",
  "logLevel": "info",
  "spawnCodexbar": true,
  "instanceHash": "<2-byte hex, set on first run>"
}
```

`spawnCodexbar: false` skips the supervisor and assumes the user runs `codexbar serve --port <codexbarPort>` themselves.

### 11.2 Firmware config

Baked into the firmware (compile-time constants in `Theme.h` and `Config.h`). Changing values = rebuild + flash.

## 12. Tests summary

| Layer | Test type | Tool |
|---|---|---|
| Bridge: snapshot encoder | Unit (fixture-based) | `swift test` |
| Bridge: codexbar client | Unit (URLProtocol stub) | `swift test` |
| Bridge: GATT peripheral | Manual integration via `pair` command | real watch |
| Firmware: snapshot codec | Native unit (host-side, mirrors bridge fixtures) | `pio test -e native` |
| Firmware: state machine | Native unit (synthetic events) | `pio test -e native` |
| Firmware: display + BLE | Manual via `make flash + glance` | real watch |

The bridge's `SnapshotEncoderTests` and the firmware's `test_snapshot_codec` share byte-level fixtures so wire compatibility is diff-checkable at PR time.

## 13. Open questions / risks

- **Board JSON availability.** PlatformIO may not yet ship a board definition for the new M5Stack StopWatch. Mitigation: clone an existing ESP32-S3 board JSON (e.g., `m5stack-cores3`), adjust flash size / PSRAM / partition table, vendor under `firmware/boards/`.
- **CO5300 + LovyanGFX driver support.** Need to confirm LovyanGFX has a panel class for the CO5300 at 466 × 466 or whether we use a generic init sequence. If neither works, fall back to a hand-rolled QSPI init from M5Stack's reference firmware before next iteration.
- **`codexbar serve` startup time.** Empirically the first `/usage` call after spawn can take 2–4 s while CodexBar warms its providers. Bridge mitigates by sending a `stale` flag during the warm-up window rather than blocking the watch.
- **Bluetooth permission flow.** First launch requires the user to grant Bluetooth in System Settings. `install` command should print a clear "next: open Bluetooth settings and grant access" line.

## 14. Implementation order (preview, not the plan)

1. `shared/PROTOCOL.md` + mint UUIDs.
2. Bridge: `SnapshotEncoder` + `CodexbarClient` (with tests, no BLE yet).
3. Bridge: `GATTPeripheral` + `BridgeService`; smoke test with `lightblue` / `bluetility`.
4. Firmware: project scaffold, board JSON, hello-world display.
5. Firmware: `SnapshotCodec` (native tests first, mirrored fixtures with bridge).
6. Firmware: `Renderer` + `Views/Overview` with hard-coded snapshot.
7. Firmware: `BleClient` + integration with real bridge.
8. Firmware: `Views/Provider` + `App` state machine + buttons + sleep.
9. `install-bridge.sh`, launchd plist, README.
10. Polish: error pills, animations, the `decode-snapshot` helper.

The actual implementation plan with steps and review checkpoints will be written by the `writing-plans` skill next.
