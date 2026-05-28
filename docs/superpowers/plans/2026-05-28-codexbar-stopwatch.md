# CodexBar StopWatch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Swift CLI bridge on macOS that re-publishes CodexBar's `codexbar serve` JSON as a 56-byte binary BLE GATT snapshot, plus PlatformIO firmware for an M5Stack StopWatch that wakes on button press, fetches the snapshot, and renders activity-style rings for Codex / Claude Code / Gemini usage.

**Architecture:** Three artifacts in one repo. `shared/` holds the wire protocol (UUIDs, byte layout, test fixtures) — single source of truth. `bridge/` is a Swift Package executable: CoreBluetooth peripheral that owns a `codexbar serve` child process and serves a binary snapshot over a custom GATT service. `firmware/` is a PlatformIO + Arduino + M5Unified + NimBLE project: wake-to-glance state machine that fetches the snapshot over BLE and renders concentric rings. CodexBar.app is untouched.

**Tech Stack:** Swift 6.2+ (Swift Package, Swift Testing, CoreBluetooth, URLSession, Process), launchd, PlatformIO + Arduino framework, M5Unified, LovyanGFX (transitive), NimBLE-Arduino, ESP32-S3.

**Spec:** `docs/superpowers/specs/2026-05-28-codexbar-stopwatch-design.md`

---

## File Structure

This is the complete file inventory the plan will create. Files are grouped by phase; each task below names the exact files it touches.

### Repo root
- `Makefile` — build / install / flash / monitor / test targets
- `README.md` — project overview, first-time setup, troubleshooting
- `.gitignore` — already exists
- `docs/superpowers/specs/2026-05-28-codexbar-stopwatch-design.md` — already committed

### `shared/`
- `PROTOCOL.md` — service UUID + characteristic UUIDs + binary layout, the canonical reference both sides cite
- `fixtures/codexbar-three-providers.json` — sample CodexBar response covering all three providers happy path
- `fixtures/codexbar-three-providers.hex` — expected binary snapshot bytes for that input
- `fixtures/codexbar-codex-only.json` — single-provider scope response
- `fixtures/codexbar-codex-only.hex` — expected bytes for that
- `fixtures/codexbar-error.json` — error response with `stale` + `bridge_error` flags
- `fixtures/codexbar-error.hex` — expected bytes for that

### `bridge/`
- `Package.swift` — Swift Package manifest, single executable target + test target
- `Sources/StopwatchBridge/main.swift` — argument parsing, command dispatch
- `Sources/StopwatchBridge/Protocol.swift` — UUIDs and binary layout constants (mirrors `PROTOCOL.md`)
- `Sources/StopwatchBridge/SnapshotEncoder.swift` — pure function: `CodexbarUsage → Data`
- `Sources/StopwatchBridge/CodexbarClient.swift` — `URLSession` around `codexbar serve`
- `Sources/StopwatchBridge/CodexbarSupervisor.swift` — `Process` wrapper for `codexbar serve` child
- `Sources/StopwatchBridge/Config.swift` — load/save JSON config
- `Sources/StopwatchBridge/GATTPeripheral.swift` — `CBPeripheralManagerDelegate`
- `Sources/StopwatchBridge/BridgeService.swift` — top-level actor wiring it all together
- `Sources/StopwatchBridge/InstallCommand.swift` — generates port, writes config, installs launchd plist
- `Sources/StopwatchBridge/DecodeCommand.swift` — `decode-snapshot <hex>` → JSON helper
- `Tests/StopwatchBridgeTests/SnapshotEncoderTests.swift`
- `Tests/StopwatchBridgeTests/CodexbarClientTests.swift`
- `Tests/StopwatchBridgeTests/ConfigTests.swift`
- `Tests/StopwatchBridgeTests/Fixtures.swift` — loads shared fixtures from `../../shared/fixtures/`

### `firmware/`
- `platformio.ini` — board + framework + libs
- `boards/m5stack-stopwatch.json` — custom board definition (vendored, in case PlatformIO doesn't ship one)
- `src/main.cpp` — `setup()` + `loop()` entry
- `src/Protocol.h` — UUIDs and binary layout constants (mirrors `PROTOCOL.md`)
- `src/SnapshotCodec.h` / `.cpp` — decode binary payload
- `src/SnapshotStore.h` / `.cpp` — NVS-backed cache
- `src/Theme.h` — colors, font sizes, ring radii
- `src/Renderer.h` / `.cpp` — LovyanGFX sprite wrapper
- `src/Views/Overview.h` / `.cpp` — three concentric rings
- `src/Views/Provider.h` / `.cpp` — per-provider concentric rings
- `src/Buttons.h` / `.cpp` — KEYA / KEYB short + long press detection
- `src/Power.h` / `.cpp` — sleep / wake / dim management
- `src/BleClient.h` / `.cpp` — NimBLE central
- `src/App.h` / `.cpp` — top-level state machine
- `test/test_snapshot_codec/test_main.cpp` — native unit test for decoder
- `test/test_state_machine/test_main.cpp` — native unit test for `App` transitions

### `scripts/`
- `scripts/install-bridge.sh` — convenience wrapper around `stopwatch-bridge install`
- `scripts/flash-firmware.sh` — `pio run -t upload`
- `scripts/decode-snapshot.sh` — wraps the Swift `decode-snapshot` subcommand

---

## Phase 0 — Shared protocol scaffolding

The wire protocol must exist before either side can be built. Output of this phase: a frozen UUID set + a small library of byte-level fixtures that both Swift and C++ test against.

### Task 0.1: Initialize repo skeleton

**Files:**
- Create: `bridge/.gitkeep`, `firmware/.gitkeep`, `shared/.gitkeep`, `scripts/.gitkeep`, `Makefile`, `README.md`

- [ ] **Step 1: Create directories**

```bash
cd /Users/justinyan/Documents/JProj/stopwatch-quiz
mkdir -p bridge firmware shared/fixtures scripts
touch bridge/.gitkeep firmware/.gitkeep shared/.gitkeep scripts/.gitkeep
```

- [ ] **Step 2: Create top-level Makefile**

Write `Makefile`:

```makefile
.PHONY: help build test install pair flash monitor clean

help:
	@echo "Targets:"
	@echo "  build           Build bridge (swift build -c release)"
	@echo "  test            Run bridge tests + firmware native tests"
	@echo "  install         Install bridge as launchd agent"
	@echo "  pair            Run bridge in foreground (verbose) to see the watch connect"
	@echo "  flash           Flash firmware to a connected M5Stack StopWatch"
	@echo "  monitor         Open serial monitor on the watch"
	@echo "  clean           Remove build artifacts"

build:
	cd bridge && swift build -c release

test:
	cd bridge && swift test
	cd firmware && pio test -e native

install: build
	./bridge/.build/release/stopwatch-bridge install

pair: build
	./bridge/.build/release/stopwatch-bridge pair

flash:
	cd firmware && pio run -t upload

monitor:
	cd firmware && pio device monitor -b 115200

clean:
	cd bridge && swift package clean
	cd firmware && pio run -t clean
```

- [ ] **Step 3: Create stub README**

Write `README.md`:

```markdown
# CodexBar StopWatch

Brings CodexBar usage indicators for Codex / Claude Code / Gemini onto an M5Stack StopWatch over Bluetooth LE.

See `docs/superpowers/specs/2026-05-28-codexbar-stopwatch-design.md` for design and `docs/superpowers/plans/2026-05-28-codexbar-stopwatch.md` for the implementation plan.

## Quick start

```
make build         # build the Swift bridge
make install       # install as launchd agent (prompts for Bluetooth permission)
make flash         # flash the firmware to a connected watch
```

See `make help` for all targets.
```

- [ ] **Step 4: Commit**

```bash
git add Makefile README.md bridge firmware shared scripts
git commit -m "scaffold: repo skeleton (bridge/firmware/shared/scripts dirs, Makefile, README)"
```

### Task 0.2: Mint UUIDs and write `shared/PROTOCOL.md`

**Files:**
- Create: `shared/PROTOCOL.md`

- [ ] **Step 1: Mint three fresh UUIDs**

Run:

```bash
echo "SERVICE_UUID=$(uuidgen)"
echo "SNAPSHOT_CHAR_UUID=$(uuidgen)"
echo "TRIGGER_CHAR_UUID=$(uuidgen)"
```

Record the three values — they go into the document below verbatim. They never change for the lifetime of v1.

- [ ] **Step 2: Write `shared/PROTOCOL.md`**

Write `shared/PROTOCOL.md` (replace the three placeholder UUIDs with the ones from Step 1):

```markdown
# CodexBar StopWatch — Wire Protocol v1.0

Authoritative source for the BLE GATT service and binary payload shared between `bridge/` and `firmware/`. Any change here MUST land alongside matching updates to `bridge/Sources/StopwatchBridge/Protocol.swift` and `firmware/src/Protocol.h`.

## 1. GATT Service

| Item | Value |
|---|---|
| Service UUID | `REPLACE-WITH-MINTED-SERVICE-UUID` |
| Local name | `Stopwatch Bridge` |
| Advertising interval | default (~100 ms) |

## 2. Characteristics

| Name | UUID | Properties | Direction | Notes |
|---|---|---|---|---|
| `UsageSnapshot` | `REPLACE-WITH-MINTED-SNAPSHOT-UUID` | Read + Notify | bridge → watch | Latest binary snapshot. Watch reads on wake; bridge notifies on snapshot change while watch is connected. |
| `RefreshTrigger` | `REPLACE-WITH-MINTED-TRIGGER-UUID` | Write Without Response | watch → bridge | Watch writes 1 byte (provider scope) to ask for a fresh fetch. |

### 2.1 `RefreshTrigger` payload

Single byte:

| Value | Meaning |
|---|---|
| `0x00` | All three providers |
| `0x01` | Codex only |
| `0x02` | Claude Code only |
| `0x03` | Gemini only |

Any other value → bridge logs warning and treats as `0x00`.

## 3. `UsageSnapshot` payload (binary)

All integers little-endian. Total size for v1.0 with 3 providers = 8 + 3×16 = **56 bytes**.

### 3.1 Header (8 bytes)

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0 | uint8 | `versionMajor` | `0x01` for v1.x. Structural; bumping breaks the watch. |
| 1 | uint8 | `versionMinor` | `0x00` for v1.0. Additive; new trailing per-provider fields. |
| 2 | uint8 | `providerCount` | `0x03` for v1.x (always Codex + Claude + Gemini). |
| 3 | uint8 | `flags` | bit0 = stale, bit1 = bridge_error, bit2 = provider_missing, bits 3-7 reserved (must be 0). |
| 4 | uint32 | `capturedAt` | Unix seconds when bridge captured this snapshot. |

### 3.2 Per-provider record (16 bytes each, repeated `providerCount` times)

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0 | uint8 | `providerID` | 1 = codex, 2 = claude, 3 = gemini |
| 1 | uint8 | `status` | 0 = ok, 1 = warn, 2 = critical, 3 = error, 4 = disabled |
| 2 | uint8 | `sessionPct` | 0–100; `0xFF` = unknown |
| 3 | uint8 | `weekPct` | 0–100; `0xFF` = unknown |
| 4 | uint32 | `sessionResetAt` | Unix seconds; `0` = unknown |
| 8 | uint32 | `weekResetAt` | Unix seconds; `0` = unknown |
| 12 | uint16 | `credits` | value × 10 (so 112.4 → 1124); `0xFFFF` = unknown |
| 14 | uint8 | `plan` | 0 = unknown, 1 = free, 2 = plus, 3 = pro, 4 = team, 5 = enterprise |
| 15 | uint8 | `reserved` | `0x00` |

### 3.3 Versioning rules

- Bridge always sends the highest (major, minor) it knows.
- Watch refuses to decode any `versionMajor` greater than its own; renders "update firmware" instead.
- Watch decodes only the per-provider bytes it knows; trailing bytes inside each record are ignored (using a known per-major record stride). This lets the bridge append new fields without forcing a reflash, as long as `versionMajor` is unchanged.

## 4. Test fixtures

`shared/fixtures/*.json` holds canned `codexbar serve` responses. `shared/fixtures/*.hex` holds the expected output of `SnapshotEncoder.encode(json) → Data` for each input.

Both `bridge/Tests/StopwatchBridgeTests/SnapshotEncoderTests` and `firmware/test/test_snapshot_codec` load these files. Wire compatibility is diff-checkable: if a code change to either side breaks a fixture, the test fails on both sides.
```

- [ ] **Step 3: Commit**

```bash
git add shared/PROTOCOL.md
git commit -m "shared: define wire protocol (service + 2 characteristics + 56B binary snapshot)"
```

### Task 0.3: Write shared test fixtures

**Files:**
- Create: `shared/fixtures/codexbar-three-providers.json`
- Create: `shared/fixtures/codexbar-three-providers.hex`
- Create: `shared/fixtures/codexbar-codex-only.json`
- Create: `shared/fixtures/codexbar-codex-only.hex`
- Create: `shared/fixtures/codexbar-error.json`
- Create: `shared/fixtures/codexbar-error.hex`

The hex files are *initially* placeholders — they get their canonical bytes filled in by Task A.3 after `SnapshotEncoder` exists. That's intentional: we use TDD to drive the encoder against expected behavior, then snapshot the result for cross-implementation parity.

- [ ] **Step 1: Write `codexbar-three-providers.json`**

This represents a normal happy-path `codexbar serve` response covering all three providers. The shape is taken from `docs/cli.md` in the CodexBar repo (Sample output (JSON, pretty) section), trimmed to the fields `SnapshotEncoder` actually reads.

```json
{
  "capturedAt": "2026-05-28T18:10:22Z",
  "providers": [
    {
      "provider": "codex",
      "status": { "indicator": "none" },
      "usage": {
        "primary":   { "usedPercent": 72, "resetsAt": "2026-05-28T21:15:00Z" },
        "secondary": { "usedPercent": 41, "resetsAt": "2026-05-29T17:00:00Z" }
      },
      "credits":  { "remaining": 112.4 },
      "plan":     "plus"
    },
    {
      "provider": "claude",
      "status": { "indicator": "none" },
      "usage": {
        "primary":   { "usedPercent": 12, "resetsAt": "2026-05-29T07:00:00Z" },
        "secondary": { "usedPercent": 37, "resetsAt": "2026-05-31T13:00:00Z" }
      },
      "credits":  null,
      "plan":     "pro"
    },
    {
      "provider": "gemini",
      "status": { "indicator": "none" },
      "usage": {
        "primary":   { "usedPercent": 8,  "resetsAt": "2026-05-29T00:00:00Z" },
        "secondary": null
      },
      "credits":  null,
      "plan":     "free"
    }
  ]
}
```

> **Note:** real `codexbar serve` returns an array of per-provider objects with much richer structure (see `docs/cli.md` in the CodexBar repo for the full schema). `CodexbarClient.swift` (Task A.4) will normalize the live response into this minimal shape before handing it to `SnapshotEncoder`.

- [ ] **Step 2: Write `codexbar-codex-only.json`**

```json
{
  "capturedAt": "2026-05-28T18:10:22Z",
  "providers": [
    {
      "provider": "codex",
      "status": { "indicator": "none" },
      "usage": {
        "primary":   { "usedPercent": 72, "resetsAt": "2026-05-28T21:15:00Z" },
        "secondary": { "usedPercent": 41, "resetsAt": "2026-05-29T17:00:00Z" }
      },
      "credits":  { "remaining": 112.4 },
      "plan":     "plus"
    }
  ]
}
```

- [ ] **Step 3: Write `codexbar-error.json`**

```json
{
  "capturedAt": "2026-05-28T18:10:22Z",
  "providers": [],
  "flags": { "stale": true, "bridgeError": true }
}
```

- [ ] **Step 4: Create empty `.hex` files**

The hex contents are filled in Task A.3 when the encoder exists. Create them as empty placeholders so the directory layout is committed.

```bash
touch shared/fixtures/codexbar-three-providers.hex
touch shared/fixtures/codexbar-codex-only.hex
touch shared/fixtures/codexbar-error.hex
```

- [ ] **Step 5: Commit**

```bash
git add shared/fixtures/
git commit -m "shared: add test-fixture JSON inputs (hex outputs come in Task A.3)"
```

---

## Phase A — Bridge (Swift CLI)

Builds the macOS-side daemon. Output of this phase: a `stopwatch-bridge` binary that runs as a launchd agent, supervises `codexbar serve`, and exposes a working GATT peripheral verifiable with a generic BLE inspector like LightBlue.

### Task A.1: Swift Package skeleton

**Files:**
- Create: `bridge/Package.swift`
- Create: `bridge/Sources/StopwatchBridge/main.swift`
- Create: `bridge/Tests/StopwatchBridgeTests/SmokeTest.swift`
- Delete: `bridge/.gitkeep`

- [ ] **Step 1: Write `bridge/Package.swift`**

```swift
// swift-tools-version: 6.2
import PackageDescription

let package = Package(
    name: "StopwatchBridge",
    platforms: [.macOS(.v14)],
    products: [
        .executable(name: "stopwatch-bridge", targets: ["StopwatchBridge"])
    ],
    targets: [
        .executableTarget(
            name: "StopwatchBridge",
            path: "Sources/StopwatchBridge"
        ),
        .testTarget(
            name: "StopwatchBridgeTests",
            dependencies: ["StopwatchBridge"],
            path: "Tests/StopwatchBridgeTests",
            resources: [
                .copy("../../../shared/fixtures")
            ]
        )
    ]
)
```

- [ ] **Step 2: Write a stub `main.swift`**

```swift
import Foundation

@main
struct StopwatchBridge {
    static func main() {
        let args = Array(CommandLine.arguments.dropFirst())
        guard let cmd = args.first else {
            print("Usage: stopwatch-bridge [run|install|pair|decode-snapshot <hex>]")
            exit(2)
        }
        switch cmd {
        case "version":
            print("stopwatch-bridge 0.1.0")
        default:
            print("unknown command: \(cmd)")
            exit(2)
        }
    }
}
```

- [ ] **Step 3: Write a smoke test**

```swift
// Tests/StopwatchBridgeTests/SmokeTest.swift
import Testing
@testable import StopwatchBridge

@Test func packageCompiles() {
    // If this test runs at all, the target imported successfully.
    #expect(true)
}
```

- [ ] **Step 4: Build and test**

```bash
rm bridge/.gitkeep
cd bridge && swift build && swift test
```

Expected output: `Build complete!` and `Test Suite 'StopwatchBridgeTests' passed`. The `version` command should also work:

```bash
swift run stopwatch-bridge version
```

Expected: `stopwatch-bridge 0.1.0`.

- [ ] **Step 5: Commit**

```bash
git add bridge/
git commit -m "bridge: Swift Package skeleton (executable target + Swift Testing test target)"
```

### Task A.2: Protocol constants and snapshot types

**Files:**
- Create: `bridge/Sources/StopwatchBridge/Protocol.swift`

- [ ] **Step 1: Write `Protocol.swift`**

Replace the three UUID literal strings with the actual UUIDs from `shared/PROTOCOL.md` Task 0.2.

```swift
import CoreBluetooth
import Foundation

/// Wire-protocol constants. Mirrors `shared/PROTOCOL.md`. Any change here MUST
/// land alongside the matching change in `firmware/src/Protocol.h`.
public enum Protocol {
    public static let serviceUUID   = CBUUID(string: "REPLACE-WITH-MINTED-SERVICE-UUID")
    public static let snapshotUUID  = CBUUID(string: "REPLACE-WITH-MINTED-SNAPSHOT-UUID")
    public static let triggerUUID   = CBUUID(string: "REPLACE-WITH-MINTED-TRIGGER-UUID")

    public static let localName     = "Stopwatch Bridge"
    public static let versionMajor: UInt8 = 1
    public static let versionMinor: UInt8 = 0

    public static let headerSize       = 8
    public static let perProviderSize  = 16
    public static let providerCount    = 3
    public static let snapshotSize     = headerSize + perProviderSize * providerCount  // 56
}

public enum SnapshotFlag: UInt8 {
    case stale            = 0b0000_0001
    case bridgeError      = 0b0000_0010
    case providerMissing  = 0b0000_0100
}

public enum ProviderID: UInt8 {
    case codex  = 1
    case claude = 2
    case gemini = 3
}

public enum ProviderStatus: UInt8 {
    case ok = 0, warn = 1, critical = 2, error = 3, disabled = 4
}

public enum ProviderPlan: UInt8 {
    case unknown = 0, free = 1, plus = 2, pro = 3, team = 4, enterprise = 5

    public init(fromString s: String?) {
        guard let s else { self = .unknown; return }
        switch s.lowercased() {
        case "free":       self = .free
        case "plus":       self = .plus
        case "pro":        self = .pro
        case "team":       self = .team
        case "enterprise": self = .enterprise
        default:           self = .unknown
        }
    }
}

/// Normalized input that `SnapshotEncoder` consumes. `CodexbarClient` produces
/// this by transforming the live `codexbar serve` response.
public struct NormalizedUsage: Equatable, Sendable {
    public struct Provider: Equatable, Sendable {
        public var providerID: ProviderID
        public var status: ProviderStatus
        public var sessionPct: UInt8?       // nil → 0xFF on the wire
        public var weekPct: UInt8?
        public var sessionResetAt: Date?    // nil → 0 on the wire
        public var weekResetAt: Date?
        public var credits: Double?         // nil → 0xFFFF on the wire
        public var plan: ProviderPlan
    }

    public var capturedAt: Date
    public var flags: Set<SnapshotFlag>
    public var providers: [Provider]
}
```

- [ ] **Step 2: Build to confirm it compiles**

```bash
cd bridge && swift build
```

Expected: `Build complete!`

- [ ] **Step 3: Commit**

```bash
git add bridge/Sources/StopwatchBridge/Protocol.swift
git commit -m "bridge: protocol constants + NormalizedUsage types (mirror of PROTOCOL.md)"
```

### Task A.3: `SnapshotEncoder` (TDD with fixtures)

**Files:**
- Create: `bridge/Tests/StopwatchBridgeTests/Fixtures.swift`
- Create: `bridge/Tests/StopwatchBridgeTests/SnapshotEncoderTests.swift`
- Create: `bridge/Sources/StopwatchBridge/SnapshotEncoder.swift`
- Modify: `shared/fixtures/*.hex` (fill in expected bytes)

- [ ] **Step 1: Write the fixtures helper**

```swift
// Tests/StopwatchBridgeTests/Fixtures.swift
import Foundation
import Testing

enum Fixtures {
    /// Loads a fixture by its base filename (without extension) and known extension.
    /// Resources are bundled via `.copy("../../../shared/fixtures")` in Package.swift.
    static func load(_ name: String, ext: String) throws -> Data {
        guard let url = Bundle.module.url(forResource: "fixtures/\(name)", withExtension: ext) else {
            Issue.record("missing fixture \(name).\(ext)")
            throw FixturesError.notFound
        }
        return try Data(contentsOf: url)
    }

    static func loadJSON(_ name: String) throws -> Data {
        try load(name, ext: "json")
    }

    static func loadHex(_ name: String) throws -> Data {
        let raw = try load(name, ext: "hex")
        let cleaned = String(decoding: raw, as: UTF8.self)
            .filter { !$0.isWhitespace }
        var bytes = Data()
        var i = cleaned.startIndex
        while i < cleaned.endIndex {
            let next = cleaned.index(i, offsetBy: 2, limitedBy: cleaned.endIndex) ?? cleaned.endIndex
            guard let byte = UInt8(cleaned[i..<next], radix: 16) else {
                Issue.record("bad hex byte at offset \(cleaned.distance(from: cleaned.startIndex, to: i))")
                throw FixturesError.badHex
            }
            bytes.append(byte)
            i = next
        }
        return bytes
    }

    enum FixturesError: Error { case notFound, badHex }
}
```

- [ ] **Step 2: Write the failing test for `SnapshotEncoder`**

```swift
// Tests/StopwatchBridgeTests/SnapshotEncoderTests.swift
import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct SnapshotEncoderTests {

    @Test func headerSizeAndTotalSizeMatchProtocol() throws {
        let input = NormalizedUsage(
            capturedAt: Date(timeIntervalSince1970: 1748455822),  // 2026-05-28T18:10:22Z
            flags: [],
            providers: [
                .init(providerID: .codex,  status: .ok, sessionPct: 72, weekPct: 41,
                      sessionResetAt: Date(timeIntervalSince1970: 1748467200),
                      weekResetAt:    Date(timeIntervalSince1970: 1748538000),
                      credits: 112.4, plan: .plus),
                .init(providerID: .claude, status: .ok, sessionPct: 12, weekPct: 37,
                      sessionResetAt: Date(timeIntervalSince1970: 1748502000),
                      weekResetAt:    Date(timeIntervalSince1970: 1748696400),
                      credits: nil, plan: .pro),
                .init(providerID: .gemini, status: .ok, sessionPct: 8, weekPct: nil,
                      sessionResetAt: Date(timeIntervalSince1970: 1748476800),
                      weekResetAt: nil,
                      credits: nil, plan: .free),
            ]
        )

        let bytes = SnapshotEncoder.encode(input)
        #expect(bytes.count == Protocol.snapshotSize)
        #expect(bytes[0] == Protocol.versionMajor)
        #expect(bytes[1] == Protocol.versionMinor)
        #expect(bytes[2] == 3)
        #expect(bytes[3] == 0)  // flags
    }

    @Test func unknownNumericsEncodeAsSentinels() {
        let input = NormalizedUsage(
            capturedAt: Date(timeIntervalSince1970: 0),
            flags: [.stale, .bridgeError],
            providers: [
                .init(providerID: .codex, status: .error,
                      sessionPct: nil, weekPct: nil,
                      sessionResetAt: nil, weekResetAt: nil,
                      credits: nil, plan: .unknown),
                .init(providerID: .claude, status: .disabled,
                      sessionPct: nil, weekPct: nil,
                      sessionResetAt: nil, weekResetAt: nil,
                      credits: nil, plan: .unknown),
                .init(providerID: .gemini, status: .disabled,
                      sessionPct: nil, weekPct: nil,
                      sessionResetAt: nil, weekResetAt: nil,
                      credits: nil, plan: .unknown),
            ]
        )

        let bytes = SnapshotEncoder.encode(input)
        // flags byte: bit0 (stale) | bit1 (bridgeError) = 0b011 = 0x03
        #expect(bytes[3] == 0x03)
        // First provider record starts at offset 8.
        #expect(bytes[8] == ProviderID.codex.rawValue)     // providerID
        #expect(bytes[9] == ProviderStatus.error.rawValue) // status
        #expect(bytes[10] == 0xFF)                          // sessionPct sentinel
        #expect(bytes[11] == 0xFF)                          // weekPct sentinel
        // sessionResetAt + weekResetAt: 8 bytes of 0
        #expect(bytes[12..<20].allSatisfy { $0 == 0 })
        // credits sentinel: 0xFFFF (LE)
        #expect(bytes[20] == 0xFF && bytes[21] == 0xFF)
        #expect(bytes[22] == ProviderPlan.unknown.rawValue) // plan
        #expect(bytes[23] == 0)                              // reserved
    }

    @Test func creditsAreScaledByTen() {
        let input = NormalizedUsage(
            capturedAt: Date(timeIntervalSince1970: 0),
            flags: [],
            providers: [
                .init(providerID: .codex, status: .ok, sessionPct: 0, weekPct: 0,
                      sessionResetAt: nil, weekResetAt: nil,
                      credits: 112.4, plan: .plus),
                .init(providerID: .claude, status: .ok, sessionPct: 0, weekPct: 0,
                      sessionResetAt: nil, weekResetAt: nil,
                      credits: nil, plan: .pro),
                .init(providerID: .gemini, status: .ok, sessionPct: 0, weekPct: 0,
                      sessionResetAt: nil, weekResetAt: nil,
                      credits: nil, plan: .free),
            ]
        )
        let bytes = SnapshotEncoder.encode(input)
        // 1124 LE = 0x64 0x04
        #expect(bytes[20] == 0x64)
        #expect(bytes[21] == 0x04)
    }
}
```

- [ ] **Step 3: Run tests to see them fail**

```bash
cd bridge && swift test --filter SnapshotEncoderTests
```

Expected: build error — `cannot find 'SnapshotEncoder' in scope`.

- [ ] **Step 4: Implement `SnapshotEncoder.swift`**

```swift
// Sources/StopwatchBridge/SnapshotEncoder.swift
import Foundation

public enum SnapshotEncoder {

    /// Encodes the normalized usage into the 56-byte v1.0 wire format.
    /// See `shared/PROTOCOL.md` §3 for the layout.
    public static func encode(_ usage: NormalizedUsage) -> Data {
        var out = Data(capacity: Protocol.snapshotSize)

        // --- Header (8 bytes) ---
        out.append(Protocol.versionMajor)
        out.append(Protocol.versionMinor)
        out.append(UInt8(usage.providers.count))
        out.append(flagsByte(usage.flags))
        appendU32(&out, UInt32(usage.capturedAt.timeIntervalSince1970))

        // --- Per-provider records (16 bytes each, in input order) ---
        for p in usage.providers {
            out.append(p.providerID.rawValue)
            out.append(p.status.rawValue)
            out.append(p.sessionPct ?? 0xFF)
            out.append(p.weekPct ?? 0xFF)
            appendU32(&out, p.sessionResetAt.map { UInt32($0.timeIntervalSince1970) } ?? 0)
            appendU32(&out, p.weekResetAt.map    { UInt32($0.timeIntervalSince1970) } ?? 0)
            appendU16(&out, p.credits.map { UInt16(min(($0 * 10).rounded(), 65534)) } ?? 0xFFFF)
            out.append(p.plan.rawValue)
            out.append(0)  // reserved
        }
        return out
    }

    private static func flagsByte(_ flags: Set<SnapshotFlag>) -> UInt8 {
        flags.reduce(UInt8(0)) { $0 | $1.rawValue }
    }

    private static func appendU16(_ out: inout Data, _ v: UInt16) {
        out.append(UInt8(v & 0xFF))
        out.append(UInt8((v >> 8) & 0xFF))
    }

    private static func appendU32(_ out: inout Data, _ v: UInt32) {
        out.append(UInt8(v & 0xFF))
        out.append(UInt8((v >>  8) & 0xFF))
        out.append(UInt8((v >> 16) & 0xFF))
        out.append(UInt8((v >> 24) & 0xFF))
    }
}
```

- [ ] **Step 5: Run tests to confirm they pass**

```bash
cd bridge && swift test --filter SnapshotEncoderTests
```

Expected: all three tests pass.

- [ ] **Step 6: Capture the canonical hex bytes into fixtures**

Now that the encoder is correct, snapshot its output into the `.hex` files so the firmware can diff against the same bytes.

Add a temporary throwaway test that writes the hex files (run once, then revert):

```swift
// Tests/StopwatchBridgeTests/SnapshotEncoderTests.swift  (add at end of the suite)
@Test func dumpFixtures_throwaway() throws {
    // One-shot helper: run with `swift test --filter dumpFixtures_throwaway`
    // to (re)generate shared/fixtures/*.hex from the canonical inputs.
    // Comment out after running so it doesn't write on every CI invocation.

    let inputs: [(name: String, usage: NormalizedUsage)] = [
        ("codexbar-three-providers", .threeProvidersFixture),
        ("codexbar-codex-only",      .codexOnlyFixture),
        ("codexbar-error",           .errorFixture),
    ]
    let repoRoot = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent().deletingLastPathComponent()
        .deletingLastPathComponent().deletingLastPathComponent()
    for (name, usage) in inputs {
        let bytes = SnapshotEncoder.encode(usage)
        let hex = bytes.map { String(format: "%02x", $0) }.joined()
        let path = repoRoot.appendingPathComponent("shared/fixtures/\(name).hex")
        try hex.write(to: path, atomically: true, encoding: .utf8)
        print("wrote \(bytes.count) bytes → \(path.path)")
    }
}

extension NormalizedUsage {
    static var threeProvidersFixture: NormalizedUsage {
        .init(
            capturedAt: Date(timeIntervalSince1970: 1748455822),
            flags: [],
            providers: [
                .init(providerID: .codex,  status: .ok, sessionPct: 72, weekPct: 41,
                      sessionResetAt: Date(timeIntervalSince1970: 1748467200),
                      weekResetAt:    Date(timeIntervalSince1970: 1748538000),
                      credits: 112.4, plan: .plus),
                .init(providerID: .claude, status: .ok, sessionPct: 12, weekPct: 37,
                      sessionResetAt: Date(timeIntervalSince1970: 1748502000),
                      weekResetAt:    Date(timeIntervalSince1970: 1748696400),
                      credits: nil, plan: .pro),
                .init(providerID: .gemini, status: .ok, sessionPct: 8, weekPct: nil,
                      sessionResetAt: Date(timeIntervalSince1970: 1748476800),
                      weekResetAt: nil,
                      credits: nil, plan: .free),
            ]
        )
    }

    static var codexOnlyFixture: NormalizedUsage {
        .init(
            capturedAt: Date(timeIntervalSince1970: 1748455822),
            flags: [],
            providers: [
                .init(providerID: .codex, status: .ok, sessionPct: 72, weekPct: 41,
                      sessionResetAt: Date(timeIntervalSince1970: 1748467200),
                      weekResetAt:    Date(timeIntervalSince1970: 1748538000),
                      credits: 112.4, plan: .plus),
            ]
        )
    }

    static var errorFixture: NormalizedUsage {
        .init(
            capturedAt: Date(timeIntervalSince1970: 1748455822),
            flags: [.stale, .bridgeError],
            providers: []
        )
    }
}
```

Run it:

```bash
cd bridge && swift test --filter dumpFixtures_throwaway
```

Expected: three `wrote NN bytes → …/shared/fixtures/<name>.hex` lines. Verify the files are non-empty.

- [ ] **Step 7: Add round-trip tests against the saved fixtures**

Replace the throwaway test with a permanent fixture-driven test:

```swift
// Tests/StopwatchBridgeTests/SnapshotEncoderTests.swift  (replace dumpFixtures_throwaway)
@Test func threeProvidersFixtureMatchesSavedHex() throws {
    let expected = try Fixtures.loadHex("codexbar-three-providers")
    let actual = SnapshotEncoder.encode(.threeProvidersFixture)
    #expect(actual == expected)
}

@Test func codexOnlyFixtureMatchesSavedHex() throws {
    let expected = try Fixtures.loadHex("codexbar-codex-only")
    let actual = SnapshotEncoder.encode(.codexOnlyFixture)
    #expect(actual == expected)
}

@Test func errorFixtureMatchesSavedHex() throws {
    let expected = try Fixtures.loadHex("codexbar-error")
    let actual = SnapshotEncoder.encode(.errorFixture)
    #expect(actual == expected)
}
```

- [ ] **Step 8: Run all encoder tests**

```bash
cd bridge && swift test --filter SnapshotEncoderTests
```

Expected: all six tests pass (the three unit tests plus the three fixture round-trips).

- [ ] **Step 9: Commit**

```bash
git add bridge/Tests/StopwatchBridgeTests/ bridge/Sources/StopwatchBridge/SnapshotEncoder.swift shared/fixtures/*.hex
git commit -m "bridge: SnapshotEncoder + fixture round-trip tests; shared hex fixtures populated"
```

### Task A.4: `CodexbarClient` (TDD with URLProtocol stubs)

**Files:**
- Create: `bridge/Sources/StopwatchBridge/CodexbarClient.swift`
- Create: `bridge/Tests/StopwatchBridgeTests/CodexbarClientTests.swift`

- [ ] **Step 1: Write the failing test**

```swift
// Tests/StopwatchBridgeTests/CodexbarClientTests.swift
import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct CodexbarClientTests {

    @Test func parsesThreeProvidersFixture() async throws {
        let json = try Fixtures.loadJSON("codexbar-three-providers")
        StubURLProtocol.stub = .init(status: 200, body: json)

        let client = CodexbarClient(port: 51111, session: stubSession())
        let usage = try await client.fetch(scope: .all)

        #expect(usage.providers.count == 3)
        #expect(usage.providers[0].providerID == .codex)
        #expect(usage.providers[0].sessionPct == 72)
        #expect(usage.providers[0].weekPct == 41)
        #expect(usage.providers[0].credits == 112.4)
        #expect(usage.providers[0].plan == .plus)
        #expect(usage.providers[2].weekPct == nil)
        #expect(usage.providers[2].weekResetAt == nil)
    }

    @Test func timeoutSurfacesAsError() async {
        StubURLProtocol.stub = .init(error: URLError(.timedOut))
        let client = CodexbarClient(port: 51111, session: stubSession(), timeout: 1)
        await #expect(throws: CodexbarClient.FetchError.self) {
            _ = try await client.fetch(scope: .all)
        }
    }

    @Test func providerScopeReachesURL() async throws {
        let json = try Fixtures.loadJSON("codexbar-codex-only")
        StubURLProtocol.stub = .init(status: 200, body: json)
        StubURLProtocol.captured = []

        let client = CodexbarClient(port: 51111, session: stubSession())
        _ = try await client.fetch(scope: .codex)

        #expect(StubURLProtocol.captured.first?.absoluteString.contains("provider=codex") == true)
    }

    // MARK: - URLProtocol stub plumbing

    private func stubSession() -> URLSession {
        let cfg = URLSessionConfiguration.ephemeral
        cfg.protocolClasses = [StubURLProtocol.self]
        return URLSession(configuration: cfg)
    }
}

final class StubURLProtocol: URLProtocol {
    struct Stub {
        var status: Int = 200
        var body: Data = .init()
        var error: Error? = nil
    }
    nonisolated(unsafe) static var stub: Stub = .init()
    nonisolated(unsafe) static var captured: [URL] = []

    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }

    override func startLoading() {
        if let url = request.url { Self.captured.append(url) }
        if let err = Self.stub.error {
            client?.urlProtocol(self, didFailWithError: err)
            return
        }
        let resp = HTTPURLResponse(url: request.url!, statusCode: Self.stub.status,
                                   httpVersion: "HTTP/1.1", headerFields: nil)!
        client?.urlProtocol(self, didReceive: resp, cacheStoragePolicy: .notAllowed)
        client?.urlProtocol(self, didLoad: Self.stub.body)
        client?.urlProtocolDidFinishLoading(self)
    }

    override func stopLoading() {}
}
```

- [ ] **Step 2: Run the test to see it fail**

```bash
cd bridge && swift test --filter CodexbarClientTests
```

Expected: build error — `cannot find 'CodexbarClient' in scope`.

- [ ] **Step 3: Implement `CodexbarClient.swift`**

```swift
// Sources/StopwatchBridge/CodexbarClient.swift
import Foundation

/// Thin client around `codexbar serve` localhost JSON. Produces `NormalizedUsage`
/// so `SnapshotEncoder` doesn't need to know about the CodexBar wire format.
public actor CodexbarClient {

    public enum Scope: String {
        case all = "all"
        case codex, claude, gemini
    }

    public enum FetchError: Error {
        case http(Int)
        case transport(Error)
        case decode(Error)
    }

    public init(port: UInt16, session: URLSession = .shared, timeout: TimeInterval = 5) {
        self.port = port
        self.session = session
        self.timeout = timeout
    }

    public func fetch(scope: Scope = .all) async throws -> NormalizedUsage {
        var components = URLComponents()
        components.scheme = "http"
        components.host   = "127.0.0.1"
        components.port   = Int(port)
        components.path   = "/usage"
        if scope != .all {
            components.queryItems = [.init(name: "provider", value: scope.rawValue)]
        }
        var req = URLRequest(url: components.url!)
        req.timeoutInterval = timeout

        let data: Data
        let response: URLResponse
        do {
            (data, response) = try await session.data(for: req)
        } catch {
            throw FetchError.transport(error)
        }
        if let http = response as? HTTPURLResponse, http.statusCode != 200 {
            throw FetchError.http(http.statusCode)
        }
        do {
            return try decode(data)
        } catch {
            throw FetchError.decode(error)
        }
    }

    // MARK: - Decoding the CodexBar response shape

    private func decode(_ data: Data) throws -> NormalizedUsage {
        let raw = try JSONDecoder.iso8601.decode(RawResponse.self, from: data)
        let flags: Set<SnapshotFlag> = [
            raw.flags?.stale       == true ? .stale          : nil,
            raw.flags?.bridgeError == true ? .bridgeError    : nil,
            raw.providers.isEmpty            ? .providerMissing : nil,
        ].compactMap { $0 } .reduce(into: Set<SnapshotFlag>()) { $0.insert($1) }

        let providers = raw.providers.compactMap { p -> NormalizedUsage.Provider? in
            guard let id = ProviderID(fromString: p.provider) else { return nil }
            return .init(
                providerID:     id,
                status:         ProviderStatus(fromIndicator: p.status?.indicator),
                sessionPct:     p.usage?.primary?.usedPercent.map { UInt8(clamping: $0) },
                weekPct:        p.usage?.secondary?.usedPercent.map { UInt8(clamping: $0) },
                sessionResetAt: p.usage?.primary?.resetsAt,
                weekResetAt:    p.usage?.secondary?.resetsAt,
                credits:        p.credits?.remaining,
                plan:           ProviderPlan(fromString: p.plan)
            )
        }

        return .init(capturedAt: raw.capturedAt ?? Date(), flags: flags, providers: providers)
    }

    // MARK: - Wire shapes (subset of `codexbar serve` JSON we actually use)

    private struct RawResponse: Decodable {
        var capturedAt: Date?
        var flags: Flags?
        var providers: [Provider]

        struct Flags: Decodable { var stale, bridgeError: Bool? }

        struct Provider: Decodable {
            var provider: String
            var status: Status?
            var usage: Usage?
            var credits: Credits?
            var plan: String?

            struct Status: Decodable { var indicator: String? }
            struct Usage: Decodable {
                var primary: Window?
                var secondary: Window?
                struct Window: Decodable { var usedPercent: Int?; var resetsAt: Date? }
            }
            struct Credits: Decodable { var remaining: Double? }
        }
    }

    private let port: UInt16
    private let session: URLSession
    private let timeout: TimeInterval
}

private extension JSONDecoder {
    static let iso8601: JSONDecoder = {
        let d = JSONDecoder()
        d.dateDecodingStrategy = .iso8601
        return d
    }()
}

private extension ProviderID {
    init?(fromString s: String) {
        switch s.lowercased() {
        case "codex":  self = .codex
        case "claude": self = .claude
        case "gemini": self = .gemini
        default:       return nil
        }
    }
}

private extension ProviderStatus {
    init(fromIndicator s: String?) {
        switch (s ?? "").lowercased() {
        case "none":     self = .ok
        case "minor":    self = .warn
        case "major":    self = .critical
        case "critical": self = .critical
        case "":         self = .ok
        default:         self = .ok
        }
    }
}
```

- [ ] **Step 4: Run tests to confirm they pass**

```bash
cd bridge && swift test --filter CodexbarClientTests
```

Expected: all three tests pass.

- [ ] **Step 5: Commit**

```bash
git add bridge/Sources/StopwatchBridge/CodexbarClient.swift bridge/Tests/StopwatchBridgeTests/CodexbarClientTests.swift
git commit -m "bridge: CodexbarClient with URLProtocol-stubbed tests"
```

### Task A.5: `Config` (TDD)

**Files:**
- Create: `bridge/Sources/StopwatchBridge/Config.swift`
- Create: `bridge/Tests/StopwatchBridgeTests/ConfigTests.swift`

- [ ] **Step 1: Write the failing test**

```swift
// Tests/StopwatchBridgeTests/ConfigTests.swift
import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct ConfigTests {

    @Test func roundTripsDefaults() throws {
        let tmp = FileManager.default.temporaryDirectory.appendingPathComponent("cfg-\(UUID()).json")
        defer { try? FileManager.default.removeItem(at: tmp) }

        let original = Config.makeDefault(at: tmp)
        try Config.save(original, to: tmp)

        let loaded = try Config.load(from: tmp)
        #expect(loaded == original)
        #expect(loaded.codexbarPort >= 49152 && loaded.codexbarPort <= 65535)
        #expect(loaded.spawnCodexbar == true)
        #expect(loaded.instanceHash.count == 4)  // 2 bytes → 4 hex chars
    }

    @Test func loadingMissingFileReturnsNil() throws {
        let tmp = FileManager.default.temporaryDirectory.appendingPathComponent("nope-\(UUID()).json")
        let loaded = try Config.load(from: tmp)
        #expect(loaded == nil)
    }
}
```

- [ ] **Step 2: Run to see it fail**

```bash
cd bridge && swift test --filter ConfigTests
```

Expected: `cannot find 'Config' in scope`.

- [ ] **Step 3: Implement `Config.swift`**

```swift
// Sources/StopwatchBridge/Config.swift
import Foundation

public struct Config: Codable, Equatable {
    public var codexbarPort: UInt16
    public var serviceUUID: String
    public var logLevel: String
    public var spawnCodexbar: Bool
    public var instanceHash: String  // 2-byte hex, e.g. "ab12"

    public static let defaultPath: URL = {
        let support = FileManager.default
            .urls(for: .applicationSupportDirectory, in: .userDomainMask)
            .first!
        return support.appendingPathComponent("stopwatch-bridge/config.json")
    }()

    public static func makeDefault(at _: URL = defaultPath) -> Config {
        // Random ephemeral port via the same range as `jot -r 1 49152 65535`.
        let port = UInt16.random(in: 49152...65535)
        var hashBytes = [UInt8](repeating: 0, count: 2)
        _ = SecRandomCopyBytes(kSecRandomDefault, hashBytes.count, &hashBytes)
        let hash = hashBytes.map { String(format: "%02x", $0) }.joined()
        return .init(
            codexbarPort: port,
            serviceUUID: Protocol.serviceUUID.uuidString,
            logLevel: "info",
            spawnCodexbar: true,
            instanceHash: hash
        )
    }

    public static func load(from url: URL = defaultPath) throws -> Config? {
        guard FileManager.default.fileExists(atPath: url.path) else { return nil }
        let data = try Data(contentsOf: url)
        return try JSONDecoder().decode(Config.self, from: data)
    }

    public static func save(_ cfg: Config, to url: URL = defaultPath) throws {
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(),
                                                withIntermediateDirectories: true)
        let enc = JSONEncoder()
        enc.outputFormatting = [.prettyPrinted, .sortedKeys]
        try enc.encode(cfg).write(to: url, options: .atomic)
        // Restrictive perms: owner read/write only.
        try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: url.path)
    }
}

import Security
```

- [ ] **Step 4: Run tests to confirm**

```bash
cd bridge && swift test --filter ConfigTests
```

Expected: both tests pass.

- [ ] **Step 5: Commit**

```bash
git add bridge/Sources/StopwatchBridge/Config.swift bridge/Tests/StopwatchBridgeTests/ConfigTests.swift
git commit -m "bridge: Config with round-trip tests (random port, 0600 perms)"
```

### Task A.6: `CodexbarSupervisor`

The supervisor wraps `Process` running `codexbar serve --port <port>` as a child. No unit tests (it's a thin `Process` wrapper); we validate via the `pair` command later.

**Files:**
- Create: `bridge/Sources/StopwatchBridge/CodexbarSupervisor.swift`

- [ ] **Step 1: Implement**

```swift
// Sources/StopwatchBridge/CodexbarSupervisor.swift
import Foundation

/// Spawns and restarts `codexbar serve --port <port>` as a child process.
/// Exponential backoff caps at 30 seconds.
public actor CodexbarSupervisor {

    public init(port: UInt16, codexbarPath: String? = nil) {
        self.port = port
        self.codexbarPath = codexbarPath ?? Self.findCodexbar()
    }

    public func start() {
        guard task == nil else { return }
        task = Task { await runLoop() }
    }

    public func stop() {
        task?.cancel()
        task = nil
        process?.terminate()
        process = nil
    }

    // MARK: - Internals

    private func runLoop() async {
        var backoff: TimeInterval = 1
        while !Task.isCancelled {
            guard let path = codexbarPath else {
                FileHandle.standardError.write(Data("codexbar binary not found on PATH; supervisor idle\n".utf8))
                try? await Task.sleep(nanoseconds: UInt64(30 * 1_000_000_000))
                continue
            }
            let proc = Process()
            proc.executableURL = URL(fileURLWithPath: path)
            proc.arguments = ["serve", "--port", String(port)]
            self.process = proc

            do {
                try proc.run()
                proc.waitUntilExit()
                FileHandle.standardError.write(Data("codexbar serve exited (status \(proc.terminationStatus)); restarting in \(backoff)s\n".utf8))
            } catch {
                FileHandle.standardError.write(Data("codexbar serve failed to start: \(error); backing off \(backoff)s\n".utf8))
            }
            try? await Task.sleep(nanoseconds: UInt64(backoff * 1_000_000_000))
            backoff = min(backoff * 2, 30)
        }
    }

    private static func findCodexbar() -> String? {
        let candidates = [
            "/opt/homebrew/bin/codexbar",
            "/usr/local/bin/codexbar",
            "/Applications/CodexBar.app/Contents/Helpers/CodexBarCLI",
        ]
        return candidates.first { FileManager.default.isExecutableFile(atPath: $0) }
    }

    private let port: UInt16
    private let codexbarPath: String?
    private var task: Task<Void, Never>?
    private var process: Process?
}
```

- [ ] **Step 2: Build to confirm it compiles**

```bash
cd bridge && swift build
```

Expected: `Build complete!`

- [ ] **Step 3: Commit**

```bash
git add bridge/Sources/StopwatchBridge/CodexbarSupervisor.swift
git commit -m "bridge: CodexbarSupervisor (spawn + restart codexbar serve with backoff)"
```

### Task A.7: `GATTPeripheral`

The CoreBluetooth peripheral. No unit tests — validated through `pair` against a generic BLE inspector.

**Files:**
- Create: `bridge/Sources/StopwatchBridge/GATTPeripheral.swift`

- [ ] **Step 1: Implement**

```swift
// Sources/StopwatchBridge/GATTPeripheral.swift
import CoreBluetooth
import Foundation

public protocol GATTPeripheralDelegate: AnyObject, Sendable {
    /// Called when the watch writes to `RefreshTrigger`. The supervisor should
    /// re-fetch and then call `updateSnapshot(_:)` on this peripheral.
    func gattPeripheral(_ peripheral: GATTPeripheral, refreshRequestedFor scope: UInt8) async
}

public final class GATTPeripheral: NSObject, @unchecked Sendable {

    public weak var delegate: (any GATTPeripheralDelegate)?

    private let manager: CBPeripheralManager
    private let snapshotChar: CBMutableCharacteristic
    private let triggerChar:  CBMutableCharacteristic
    private let service:      CBMutableService

    private var currentSnapshot: Data = Data(count: Protocol.snapshotSize)
    private var isAdvertising = false

    public override init() {
        self.snapshotChar = CBMutableCharacteristic(
            type: Protocol.snapshotUUID,
            properties: [.read, .notify],
            value: nil,
            permissions: [.readable]
        )
        self.triggerChar = CBMutableCharacteristic(
            type: Protocol.triggerUUID,
            properties: [.writeWithoutResponse],
            value: nil,
            permissions: [.writeable]
        )
        let svc = CBMutableService(type: Protocol.serviceUUID, primary: true)
        svc.characteristics = [self.snapshotChar, self.triggerChar]
        self.service = svc

        self.manager = CBPeripheralManager(delegate: nil, queue: nil)
        super.init()
        self.manager.delegate = self
    }

    public func updateSnapshot(_ data: Data) {
        precondition(data.count == Protocol.snapshotSize, "snapshot must be \(Protocol.snapshotSize) bytes")
        currentSnapshot = data
        snapshotChar.value = data
        _ = manager.updateValue(data, for: snapshotChar, onSubscribedCentrals: nil)
    }
}

extension GATTPeripheral: CBPeripheralManagerDelegate {

    public func peripheralManagerDidUpdateState(_ peripheral: CBPeripheralManager) {
        guard peripheral.state == .poweredOn else {
            FileHandle.standardError.write(Data("Bluetooth not powered on (state=\(peripheral.state.rawValue))\n".utf8))
            return
        }
        if !isAdvertising {
            peripheral.add(service)
            peripheral.startAdvertising([
                CBAdvertisementDataLocalNameKey: Protocol.localName,
                CBAdvertisementDataServiceUUIDsKey: [Protocol.serviceUUID]
            ])
            isAdvertising = true
            FileHandle.standardOutput.write(Data("advertising \(Protocol.localName)\n".utf8))
        }
    }

    public func peripheralManager(_ peripheral: CBPeripheralManager, didReceiveRead request: CBATTRequest) {
        guard request.characteristic.uuid == Protocol.snapshotUUID else {
            peripheral.respond(to: request, withResult: .attributeNotFound)
            return
        }
        if request.offset > currentSnapshot.count {
            peripheral.respond(to: request, withResult: .invalidOffset)
            return
        }
        request.value = currentSnapshot.subdata(in: request.offset..<currentSnapshot.count)
        peripheral.respond(to: request, withResult: .success)
    }

    public func peripheralManager(_ peripheral: CBPeripheralManager, didReceiveWrite requests: [CBATTRequest]) {
        for req in requests {
            guard req.characteristic.uuid == Protocol.triggerUUID,
                  let v = req.value,
                  v.count >= 1
            else { continue }
            let scope = v[0]
            FileHandle.standardOutput.write(Data("trigger write: scope=\(scope)\n".utf8))
            Task { await self.delegate?.gattPeripheral(self, refreshRequestedFor: scope) }
        }
    }
}
```

- [ ] **Step 2: Build to confirm**

```bash
cd bridge && swift build
```

Expected: `Build complete!`

- [ ] **Step 3: Commit**

```bash
git add bridge/Sources/StopwatchBridge/GATTPeripheral.swift
git commit -m "bridge: GATTPeripheral (advertise + serve snapshot + accept refresh writes)"
```

### Task A.8: `BridgeService` — wire everything together

**Files:**
- Create: `bridge/Sources/StopwatchBridge/BridgeService.swift`
- Modify: `bridge/Sources/StopwatchBridge/main.swift`

- [ ] **Step 1: Implement `BridgeService.swift`**

```swift
// Sources/StopwatchBridge/BridgeService.swift
import Foundation

/// Top-level coordinator. Owns the supervisor, the codexbar client, and the GATT peripheral.
public actor BridgeService {

    private let config: Config
    private let supervisor: CodexbarSupervisor
    private let client: CodexbarClient
    private let peripheral: GATTPeripheral

    public init(config: Config) {
        self.config = config
        self.supervisor = CodexbarSupervisor(port: config.codexbarPort)
        self.client = CodexbarClient(port: config.codexbarPort)
        self.peripheral = GATTPeripheral()
    }

    public func run() async {
        if config.spawnCodexbar {
            await supervisor.start()
        } else {
            FileHandle.standardOutput.write(Data("spawnCodexbar=false; expecting external codexbar serve on port \(config.codexbarPort)\n".utf8))
        }
        await peripheral.setDelegate(self)

        // Block forever so launchd doesn't reap us.
        await withCheckedContinuation { (_: CheckedContinuation<Void, Never>) in }
    }

    fileprivate func handleRefresh(scope: UInt8) async {
        let s = CodexbarClient.Scope(rawByte: scope)
        do {
            let usage = try await client.fetch(scope: s)
            let bytes = SnapshotEncoder.encode(usage)
            await peripheral.updateSnapshot(bytes)
        } catch {
            FileHandle.standardError.write(Data("fetch failed: \(error)\n".utf8))
            // Build an error snapshot with the bridge_error flag set.
            let errUsage = NormalizedUsage(
                capturedAt: Date(),
                flags: [.bridgeError, .stale],
                providers: []
            )
            await peripheral.updateSnapshot(SnapshotEncoder.encode(errUsage))
        }
    }
}

extension BridgeService: GATTPeripheralDelegate {
    public nonisolated func gattPeripheral(_ peripheral: GATTPeripheral, refreshRequestedFor scope: UInt8) async {
        await handleRefresh(scope: scope)
    }
}

private extension CodexbarClient.Scope {
    init(rawByte: UInt8) {
        switch rawByte {
        case 1: self = .codex
        case 2: self = .claude
        case 3: self = .gemini
        default: self = .all
        }
    }
}

extension GATTPeripheral {
    /// Convenience to bridge the non-actor peripheral into actor-isolated callers.
    func setDelegate(_ d: any GATTPeripheralDelegate) async {
        self.delegate = d
    }

    /// Snapshot update entry-point that other actors can await.
    func updateSnapshot(_ bytes: Data) async {
        updateSnapshot(bytes)
    }
}
```

- [ ] **Step 2: Update `main.swift` to dispatch the `run` command**

```swift
// Sources/StopwatchBridge/main.swift
import Foundation

@main
struct StopwatchBridge {
    static func main() async {
        let args = Array(CommandLine.arguments.dropFirst())
        guard let cmd = args.first else { usage(); exit(2) }
        switch cmd {
        case "run":               await runCommand()
        case "version":           print("stopwatch-bridge 0.1.0")
        case "install":           print("install command added in Task A.10"); exit(2)
        case "pair":              print("pair command added in Task A.10"); exit(2)
        case "decode-snapshot":   print("decode-snapshot added in Task A.11"); exit(2)
        default: usage(); exit(2)
        }
    }

    static func usage() {
        print("""
        Usage: stopwatch-bridge <command>
          run                       Foreground daemon (launchd invokes this)
          install                   Install as launchd agent
          pair                      Foreground with verbose logging for first-time setup
          decode-snapshot <hex>     Print a captured snapshot as JSON
          version                   Print version
        """)
    }

    static func runCommand() async {
        let cfg: Config
        do {
            if let loaded = try Config.load() {
                cfg = loaded
            } else {
                cfg = Config.makeDefault()
                try Config.save(cfg)
                FileHandle.standardOutput.write(Data("wrote default config to \(Config.defaultPath.path)\n".utf8))
            }
        } catch {
            FileHandle.standardError.write(Data("config error: \(error)\n".utf8))
            exit(1)
        }
        let service = BridgeService(config: cfg)
        await service.run()
    }
}
```

- [ ] **Step 3: Build**

```bash
cd bridge && swift build
```

Expected: `Build complete!`

- [ ] **Step 4: Smoke-run (without a watch yet)**

Bluetooth permission may prompt on this run. If it does, grant access and re-run.

```bash
swift run stopwatch-bridge run
```

Expected: `wrote default config to …`, then `advertising Stopwatch Bridge` (after a moment as CoreBluetooth comes up).

Verify the advertisement from a separate terminal using `system_profiler` (or a BLE inspector app like LightBlue on the Mac App Store):

```bash
system_profiler SPBluetoothDataType | head -50
```

Stop the bridge with Ctrl-C.

- [ ] **Step 5: Commit**

```bash
git add bridge/Sources/StopwatchBridge/BridgeService.swift bridge/Sources/StopwatchBridge/main.swift
git commit -m "bridge: BridgeService wires supervisor + client + peripheral; run command works"
```

### Task A.9: `pair` and `decode-snapshot` commands

**Files:**
- Create: `bridge/Sources/StopwatchBridge/DecodeCommand.swift`
- Modify: `bridge/Sources/StopwatchBridge/main.swift`

- [ ] **Step 1: Implement `DecodeCommand.swift`**

```swift
// Sources/StopwatchBridge/DecodeCommand.swift
import Foundation

enum DecodeCommand {
    /// Decodes a hex string (from a BLE capture) into readable JSON.
    /// Used for debugging: `stopwatch-bridge decode-snapshot 010003000ea2..`.
    static func run(_ hex: String) -> Int32 {
        let cleaned = hex.filter { !$0.isWhitespace }
        var bytes = [UInt8]()
        var i = cleaned.startIndex
        while i < cleaned.endIndex {
            let next = cleaned.index(i, offsetBy: 2, limitedBy: cleaned.endIndex) ?? cleaned.endIndex
            guard let b = UInt8(cleaned[i..<next], radix: 16) else {
                FileHandle.standardError.write(Data("bad hex at offset \(cleaned.distance(from: cleaned.startIndex, to: i))\n".utf8))
                return 2
            }
            bytes.append(b)
            i = next
        }
        guard bytes.count >= Protocol.headerSize else {
            FileHandle.standardError.write(Data("payload too short: \(bytes.count) bytes\n".utf8))
            return 2
        }
        let major = bytes[0], minor = bytes[1], count = bytes[2], flags = bytes[3]
        let capturedAt = readU32(bytes, 4)
        var out: [String: Any] = [
            "versionMajor": Int(major),
            "versionMinor": Int(minor),
            "providerCount": Int(count),
            "flags": [
                "stale":           (flags & 0x01) != 0,
                "bridgeError":     (flags & 0x02) != 0,
                "providerMissing": (flags & 0x04) != 0,
            ],
            "capturedAt": Date(timeIntervalSince1970: TimeInterval(capturedAt)).description,
        ]
        var providers: [[String: Any]] = []
        var off = Protocol.headerSize
        for _ in 0..<Int(count) {
            guard off + Protocol.perProviderSize <= bytes.count else { break }
            let pid = bytes[off]
            providers.append([
                "providerID": Int(pid),
                "status":     Int(bytes[off + 1]),
                "sessionPct": bytes[off + 2] == 0xFF ? "unknown" : Int(bytes[off + 2]).description,
                "weekPct":    bytes[off + 3] == 0xFF ? "unknown" : Int(bytes[off + 3]).description,
                "sessionResetAt": dateOrUnknown(readU32(bytes, off + 4)),
                "weekResetAt":    dateOrUnknown(readU32(bytes, off + 8)),
                "credits":    readU16(bytes, off + 12) == 0xFFFF ? "unknown" : Double(readU16(bytes, off + 12)) / 10,
                "plan":       Int(bytes[off + 14]),
            ])
            off += Protocol.perProviderSize
        }
        out["providers"] = providers
        let data = try! JSONSerialization.data(withJSONObject: out, options: [.prettyPrinted, .sortedKeys])
        FileHandle.standardOutput.write(data)
        FileHandle.standardOutput.write(Data("\n".utf8))
        return 0
    }

    private static func readU16(_ b: [UInt8], _ off: Int) -> UInt16 {
        UInt16(b[off]) | (UInt16(b[off + 1]) << 8)
    }
    private static func readU32(_ b: [UInt8], _ off: Int) -> UInt32 {
        UInt32(b[off]) | (UInt32(b[off + 1]) << 8) | (UInt32(b[off + 2]) << 16) | (UInt32(b[off + 3]) << 24)
    }
    private static func dateOrUnknown(_ t: UInt32) -> String {
        t == 0 ? "unknown" : Date(timeIntervalSince1970: TimeInterval(t)).description
    }
}
```

- [ ] **Step 2: Add `pair` and `decode-snapshot` dispatch in `main.swift`**

Replace `main.swift` with:

```swift
// Sources/StopwatchBridge/main.swift
import Foundation

@main
struct StopwatchBridge {
    static func main() async {
        let args = Array(CommandLine.arguments.dropFirst())
        guard let cmd = args.first else { usage(); exit(2) }
        switch cmd {
        case "run":               await runCommand(verbose: false)
        case "pair":              await runCommand(verbose: true)
        case "install":           print("install command added in Task A.10"); exit(2)
        case "decode-snapshot":
            guard args.count >= 2 else { usage(); exit(2) }
            exit(DecodeCommand.run(args[1]))
        case "version":           print("stopwatch-bridge 0.1.0")
        default: usage(); exit(2)
        }
    }

    static func usage() {
        print("""
        Usage: stopwatch-bridge <command>
          run                       Foreground daemon (launchd invokes this)
          install                   Install as launchd agent
          pair                      Foreground with verbose logging
          decode-snapshot <hex>     Print a captured snapshot as JSON
          version                   Print version
        """)
    }

    static func runCommand(verbose: Bool) async {
        if verbose {
            FileHandle.standardOutput.write(Data("pair mode: verbose logging on\n".utf8))
        }
        let cfg: Config
        do {
            if let loaded = try Config.load() {
                cfg = loaded
            } else {
                cfg = Config.makeDefault()
                try Config.save(cfg)
                FileHandle.standardOutput.write(Data("wrote default config to \(Config.defaultPath.path)\n".utf8))
            }
        } catch {
            FileHandle.standardError.write(Data("config error: \(error)\n".utf8))
            exit(1)
        }
        let service = BridgeService(config: cfg)
        await service.run()
    }
}
```

- [ ] **Step 3: Verify decode against a saved fixture**

```bash
cd bridge && swift build
HEX=$(cat ../shared/fixtures/codexbar-three-providers.hex)
.build/debug/stopwatch-bridge decode-snapshot "$HEX"
```

Expected: a JSON dump showing `versionMajor: 1`, `providerCount: 3`, and three providers with sensible fields.

- [ ] **Step 4: Commit**

```bash
git add bridge/Sources/StopwatchBridge/DecodeCommand.swift bridge/Sources/StopwatchBridge/main.swift
git commit -m "bridge: pair + decode-snapshot subcommands"
```

### Task A.10: `install` command + launchd plist

**Files:**
- Create: `bridge/Sources/StopwatchBridge/InstallCommand.swift`
- Create: `scripts/install-bridge.sh`
- Modify: `bridge/Sources/StopwatchBridge/main.swift`

- [ ] **Step 1: Implement `InstallCommand.swift`**

```swift
// Sources/StopwatchBridge/InstallCommand.swift
import Foundation

enum InstallCommand {
    static let plistLabel = "dev.stopwatch.bridge"

    static func run() -> Int32 {
        // 1. Ensure config exists (creates default with random port if needed).
        let cfg: Config
        do {
            if let loaded = try Config.load() {
                cfg = loaded
                print("re-using existing config at \(Config.defaultPath.path)")
            } else {
                cfg = Config.makeDefault()
                try Config.save(cfg)
                print("created config at \(Config.defaultPath.path) (port \(cfg.codexbarPort))")
            }
        } catch {
            FileHandle.standardError.write(Data("config write failed: \(error)\n".utf8))
            return 1
        }

        // 2. Resolve the release binary path. The user is expected to have
        //    built with `swift build -c release` (the Makefile does this).
        let binary = FileManager.default.currentDirectoryPath + "/.build/release/stopwatch-bridge"
        guard FileManager.default.isExecutableFile(atPath: binary) else {
            FileHandle.standardError.write(Data("missing release binary at \(binary); run `swift build -c release` first\n".utf8))
            return 1
        }

        // 3. Write the launchd plist.
        let plistURL = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/LaunchAgents/\(plistLabel).plist")
        let plist = """
        <?xml version="1.0" encoding="UTF-8"?>
        <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
        <plist version="1.0">
        <dict>
          <key>Label</key><string>\(plistLabel)</string>
          <key>ProgramArguments</key>
          <array>
            <string>\(binary)</string>
            <string>run</string>
          </array>
          <key>RunAtLoad</key><true/>
          <key>KeepAlive</key><true/>
          <key>LimitLoadToSessionType</key><string>Aqua</string>
          <key>StandardOutPath</key><string>/tmp/stopwatch-bridge.log</string>
          <key>StandardErrorPath</key><string>/tmp/stopwatch-bridge.log</string>
        </dict>
        </plist>
        """
        do {
            try FileManager.default.createDirectory(at: plistURL.deletingLastPathComponent(),
                                                    withIntermediateDirectories: true)
            try plist.write(to: plistURL, atomically: true, encoding: .utf8)
            print("wrote launchd plist to \(plistURL.path)")
        } catch {
            FileHandle.standardError.write(Data("plist write failed: \(error)\n".utf8))
            return 1
        }

        // 4. Bootstrap into launchd (idempotent: unload first, ignore errors).
        _ = shell("/bin/launchctl", "bootout", "gui/\(getuid())", plistURL.path)
        let result = shell("/bin/launchctl", "bootstrap", "gui/\(getuid())", plistURL.path)
        if result != 0 {
            FileHandle.standardError.write(Data("launchctl bootstrap exited \(result); run manually: launchctl bootstrap gui/$UID \(plistURL.path)\n".utf8))
            return result
        }

        print("""

        Installed. Next steps:
          1. macOS will prompt for Bluetooth permission on first launch.
             If you don't see a prompt, open System Settings → Privacy & Security → Bluetooth
             and ensure stopwatch-bridge is allowed.
          2. Tail logs: tail -f /tmp/stopwatch-bridge.log
          3. To pair, press a button on the watch (after flashing firmware).
        """)
        return 0
    }

    private static func shell(_ cmd: String, _ args: String...) -> Int32 {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: cmd)
        p.arguments = args
        do {
            try p.run()
            p.waitUntilExit()
            return p.terminationStatus
        } catch {
            return -1
        }
    }
}
```

- [ ] **Step 2: Wire `install` into `main.swift`**

Replace the install branch in the switch:

```swift
        case "install":           exit(InstallCommand.run())
```

- [ ] **Step 3: Create `scripts/install-bridge.sh`**

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
echo "==> building release binary"
cd bridge && swift build -c release
echo "==> installing"
.build/release/stopwatch-bridge install
```

```bash
chmod +x scripts/install-bridge.sh
```

- [ ] **Step 4: Test the install command (it will register a real launchd job — uninstall afterward if you don't want it)**

```bash
cd bridge && swift build -c release
.build/release/stopwatch-bridge install
launchctl print "gui/$(id -u)/dev.stopwatch.bridge" | head -5
```

Expected: prints the launchd job status. If you want to remove it:

```bash
launchctl bootout "gui/$(id -u)" "$HOME/Library/LaunchAgents/dev.stopwatch.bridge.plist"
rm "$HOME/Library/LaunchAgents/dev.stopwatch.bridge.plist"
```

- [ ] **Step 5: Commit**

```bash
git add bridge/Sources/StopwatchBridge/InstallCommand.swift bridge/Sources/StopwatchBridge/main.swift scripts/install-bridge.sh
git commit -m "bridge: install command (writes config + launchd plist) and install-bridge.sh wrapper"
```

---

## Phase B — Firmware (PlatformIO)

Builds the watch-side firmware. Output of this phase: a flashable `.bin` that, with the bridge running, shows live activity rings and reacts to KEYA / KEYB.

### Task B.1: PlatformIO project skeleton + hello-world display

**Files:**
- Create: `firmware/platformio.ini`
- Create: `firmware/boards/m5stack-stopwatch.json`
- Create: `firmware/src/main.cpp`
- Delete: `firmware/.gitkeep`

- [ ] **Step 1: Check whether PlatformIO already ships a board for the M5Stack StopWatch**

```bash
pio boards | grep -i stopwatch || echo "no built-in board"
```

If a board is listed (e.g., `m5stack-stopwatch`), use it directly in `platformio.ini` and skip Step 2. If not, vendor a custom board JSON as below.

- [ ] **Step 2: Write a vendored board JSON (only if Step 1 found no built-in board)**

Write `firmware/boards/m5stack-stopwatch.json`:

```json
{
  "build": {
    "arduino": { "ldscript": "esp32s3_out.ld" },
    "core": "esp32",
    "extra_flags": [
      "-DARDUINO_M5STACK_STOPWATCH",
      "-DBOARD_HAS_PSRAM",
      "-DARDUINO_USB_MODE=1",
      "-DARDUINO_USB_CDC_ON_BOOT=1"
    ],
    "f_cpu": "240000000L",
    "f_flash": "80000000L",
    "flash_mode": "qio",
    "hwids": [["0x303A", "0x1001"]],
    "mcu": "esp32s3",
    "variant": "esp32s3"
  },
  "connectivity": ["wifi", "bluetooth"],
  "debug": { "default_tool": "esp-builtin", "onboard_tools": ["esp-builtin"], "openocd_target": "esp32s3.cfg" },
  "frameworks": ["arduino", "espidf"],
  "name": "M5Stack StopWatch (ESP32-S3, 16MB flash, 8MB PSRAM)",
  "upload": {
    "flash_size": "16MB",
    "maximum_ram_size": 327680,
    "maximum_size": 16777216,
    "require_upload_port": true,
    "speed": 921600
  },
  "url": "https://docs.m5stack.com/en/core/StopWatch",
  "vendor": "M5Stack"
}
```

- [ ] **Step 3: Write `firmware/platformio.ini`**

If you used the built-in board in Step 1, set `board = <its-name>` and drop the `board_dir` line.

```ini
[platformio]
default_envs = stopwatch

[env]
framework = arduino
monitor_speed = 115200

[env:stopwatch]
platform = espressif32
board = m5stack-stopwatch
board_dir = boards
board_build.partitions = default_16MB.csv
board_build.f_flash = 80000000L
board_build.flash_mode = qio
build_flags =
  -DCORE_DEBUG_LEVEL=3
  -DBOARD_HAS_PSRAM
  -DARDUINO_USB_MODE=1
  -DARDUINO_USB_CDC_ON_BOOT=1
lib_deps =
  m5stack/M5Unified@^0.2.7
  h2zero/NimBLE-Arduino@^2.2.1

[env:native]
platform = native
test_framework = unity
build_flags = -std=gnu++17
```

- [ ] **Step 4: Write a hello-world `main.cpp`**

```cpp
// firmware/src/main.cpp
#include <Arduino.h>
#include <M5Unified.h>

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.fillScreen(BLACK);
    M5.Display.setTextColor(WHITE);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setFont(&fonts::Font4);
    M5.Display.drawString("hello, stopwatch",
                          M5.Display.width() / 2,
                          M5.Display.height() / 2);
}

void loop() {
    M5.update();
    delay(50);
}
```

- [ ] **Step 5: Build (no flash needed yet)**

```bash
rm firmware/.gitkeep
cd firmware && pio run
```

Expected: `SUCCESS` from PlatformIO. Address any board/library resolution errors before continuing — if `m5stack-stopwatch` is unknown to your platform-espressif32 version, double-check the vendored board JSON path or update the platform.

- [ ] **Step 6: Flash if a watch is connected**

```bash
pio run -t upload && pio device monitor -b 115200
```

Expected: watch displays `hello, stopwatch` on the AMOLED. Press Ctrl-C to exit monitor.

- [ ] **Step 7: Commit**

```bash
git add firmware/platformio.ini firmware/src/main.cpp firmware/boards/
git commit -m "firmware: PlatformIO skeleton + vendored board JSON + hello-world display"
```

### Task B.2: `Protocol.h` + `SnapshotCodec` (TDD, native tests)

**Files:**
- Create: `firmware/src/Protocol.h`
- Create: `firmware/src/SnapshotCodec.h`
- Create: `firmware/src/SnapshotCodec.cpp`
- Create: `firmware/test/test_snapshot_codec/test_main.cpp`

- [ ] **Step 1: Write `Protocol.h`**

Replace the three UUID strings with the actual UUIDs from `shared/PROTOCOL.md`.

```cpp
// firmware/src/Protocol.h
#pragma once
#include <cstdint>

namespace stopwatch {

constexpr const char *kServiceUUID  = "REPLACE-WITH-MINTED-SERVICE-UUID";
constexpr const char *kSnapshotUUID = "REPLACE-WITH-MINTED-SNAPSHOT-UUID";
constexpr const char *kTriggerUUID  = "REPLACE-WITH-MINTED-TRIGGER-UUID";
constexpr const char *kLocalName    = "Stopwatch Bridge";

constexpr uint8_t  kVersionMajor       = 1;
constexpr uint8_t  kVersionMinor       = 0;
constexpr uint8_t  kHeaderSize         = 8;
constexpr uint8_t  kPerProviderSize    = 16;
constexpr uint8_t  kProviderCount      = 3;
constexpr uint16_t kSnapshotSize       = kHeaderSize + kPerProviderSize * kProviderCount;  // 56

enum class ProviderID : uint8_t { Codex = 1, Claude = 2, Gemini = 3 };
enum class ProviderStatus : uint8_t { Ok = 0, Warn = 1, Critical = 2, Error = 3, Disabled = 4 };
enum class ProviderPlan   : uint8_t { Unknown = 0, Free = 1, Plus = 2, Pro = 3, Team = 4, Enterprise = 5 };

constexpr uint8_t kFlagStale           = 0b0000'0001;
constexpr uint8_t kFlagBridgeError     = 0b0000'0010;
constexpr uint8_t kFlagProviderMissing = 0b0000'0100;

}  // namespace stopwatch
```

- [ ] **Step 2: Write `SnapshotCodec.h`**

```cpp
// firmware/src/SnapshotCodec.h
#pragma once
#include "Protocol.h"
#include <cstddef>
#include <optional>

namespace stopwatch {

struct ProviderSlot {
    ProviderID     id;
    ProviderStatus status;
    std::optional<uint8_t> sessionPct;     // nullopt iff wire byte was 0xFF
    std::optional<uint8_t> weekPct;
    std::optional<uint32_t> sessionResetAt; // nullopt iff wire bytes were 0
    std::optional<uint32_t> weekResetAt;
    std::optional<uint16_t> creditsTimesTen; // nullopt iff 0xFFFF
    ProviderPlan plan;
};

struct Snapshot {
    uint8_t versionMajor   = 0;
    uint8_t versionMinor   = 0;
    uint8_t providerCount  = 0;
    uint8_t flags          = 0;
    uint32_t capturedAt    = 0;
    ProviderSlot providers[kProviderCount] = {};

    bool isStale()             const { return flags & kFlagStale; }
    bool isBridgeError()       const { return flags & kFlagBridgeError; }
    bool isProviderMissing()   const { return flags & kFlagProviderMissing; }
};

enum class DecodeResult : uint8_t {
    Ok,
    TooShort,
    MajorVersionTooNew,    // versionMajor > kVersionMajor → render "update firmware"
    InvalidProviderCount,
};

DecodeResult decodeSnapshot(const uint8_t *bytes, size_t len, Snapshot &out);

}  // namespace stopwatch
```

- [ ] **Step 3: Write the failing native test**

```cpp
// firmware/test/test_snapshot_codec/test_main.cpp
#include <unity.h>
#include <cstdio>
#include <vector>
#include <fstream>
#include "../../src/SnapshotCodec.h"

using namespace stopwatch;

// Locate the hex fixture relative to the repo root.
// PIO runs native tests from `firmware/` so we walk up one level.
static std::vector<uint8_t> readHexFixture(const char *name) {
    std::string path = std::string("../shared/fixtures/") + name + ".hex";
    std::ifstream f(path);
    if (!f) {
        char buf[256];
        snprintf(buf, sizeof(buf), "missing fixture: %s", path.c_str());
        TEST_FAIL_MESSAGE(buf);
    }
    std::string hex((std::istreambuf_iterator<char>(f)), {});
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        if (isspace(hex[i])) { i--; continue; }
        unsigned v = 0;
        sscanf(hex.c_str() + i, "%2x", &v);
        out.push_back((uint8_t)v);
    }
    return out;
}

void test_threeProvidersFixtureDecodes(void) {
    auto bytes = readHexFixture("codexbar-three-providers");
    TEST_ASSERT_EQUAL(kSnapshotSize, bytes.size());

    Snapshot snap;
    auto rc = decodeSnapshot(bytes.data(), bytes.size(), snap);
    TEST_ASSERT_EQUAL((int)DecodeResult::Ok, (int)rc);
    TEST_ASSERT_EQUAL(1, snap.versionMajor);
    TEST_ASSERT_EQUAL(0, snap.versionMinor);
    TEST_ASSERT_EQUAL(3, snap.providerCount);
    TEST_ASSERT_EQUAL(0, snap.flags);

    TEST_ASSERT_EQUAL((int)ProviderID::Codex, (int)snap.providers[0].id);
    TEST_ASSERT_TRUE(snap.providers[0].sessionPct.has_value());
    TEST_ASSERT_EQUAL(72, snap.providers[0].sessionPct.value());
    TEST_ASSERT_EQUAL(41, snap.providers[0].weekPct.value());
    TEST_ASSERT_TRUE(snap.providers[0].creditsTimesTen.has_value());
    TEST_ASSERT_EQUAL(1124, snap.providers[0].creditsTimesTen.value());
    TEST_ASSERT_EQUAL((int)ProviderPlan::Plus, (int)snap.providers[0].plan);

    TEST_ASSERT_FALSE(snap.providers[2].weekPct.has_value());
    TEST_ASSERT_FALSE(snap.providers[2].weekResetAt.has_value());
}

void test_errorFixtureDecodes(void) {
    auto bytes = readHexFixture("codexbar-error");
    Snapshot snap;
    auto rc = decodeSnapshot(bytes.data(), bytes.size(), snap);
    TEST_ASSERT_EQUAL((int)DecodeResult::Ok, (int)rc);
    TEST_ASSERT_EQUAL(0, snap.providerCount);
    TEST_ASSERT_TRUE(snap.isStale());
    TEST_ASSERT_TRUE(snap.isBridgeError());
}

void test_futureMajorIsRejected(void) {
    uint8_t bytes[kHeaderSize] = { 99 /*major*/, 0, 0, 0, 0, 0, 0, 0 };
    Snapshot snap;
    auto rc = decodeSnapshot(bytes, sizeof(bytes), snap);
    TEST_ASSERT_EQUAL((int)DecodeResult::MajorVersionTooNew, (int)rc);
}

void test_tooShortIsRejected(void) {
    uint8_t bytes[3] = { 1, 0, 3 };
    Snapshot snap;
    auto rc = decodeSnapshot(bytes, sizeof(bytes), snap);
    TEST_ASSERT_EQUAL((int)DecodeResult::TooShort, (int)rc);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_threeProvidersFixtureDecodes);
    RUN_TEST(test_errorFixtureDecodes);
    RUN_TEST(test_futureMajorIsRejected);
    RUN_TEST(test_tooShortIsRejected);
    return UNITY_END();
}
```

- [ ] **Step 4: Run to see them fail**

```bash
cd firmware && pio test -e native -f test_snapshot_codec
```

Expected: build error — `undefined reference to stopwatch::decodeSnapshot`.

- [ ] **Step 5: Implement `SnapshotCodec.cpp`**

```cpp
// firmware/src/SnapshotCodec.cpp
#include "SnapshotCodec.h"

namespace stopwatch {

namespace {
uint16_t readU16(const uint8_t *b) { return (uint16_t)b[0] | ((uint16_t)b[1] << 8); }
uint32_t readU32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
}  // namespace

DecodeResult decodeSnapshot(const uint8_t *bytes, size_t len, Snapshot &out) {
    if (len < kHeaderSize) return DecodeResult::TooShort;
    out.versionMajor  = bytes[0];
    out.versionMinor  = bytes[1];
    out.providerCount = bytes[2];
    out.flags         = bytes[3];
    out.capturedAt    = readU32(bytes + 4);

    if (out.versionMajor > kVersionMajor) return DecodeResult::MajorVersionTooNew;
    if (out.providerCount > kProviderCount) return DecodeResult::InvalidProviderCount;
    if (len < (size_t)(kHeaderSize + out.providerCount * kPerProviderSize)) {
        return DecodeResult::TooShort;
    }

    for (uint8_t i = 0; i < out.providerCount; ++i) {
        const uint8_t *p = bytes + kHeaderSize + i * kPerProviderSize;
        auto &slot = out.providers[i];
        slot.id     = (ProviderID)p[0];
        slot.status = (ProviderStatus)p[1];
        slot.sessionPct      = (p[2] == 0xFF) ? std::nullopt : std::optional<uint8_t>(p[2]);
        slot.weekPct         = (p[3] == 0xFF) ? std::nullopt : std::optional<uint8_t>(p[3]);
        uint32_t sr = readU32(p + 4);
        uint32_t wr = readU32(p + 8);
        slot.sessionResetAt  = (sr == 0) ? std::nullopt : std::optional<uint32_t>(sr);
        slot.weekResetAt     = (wr == 0) ? std::nullopt : std::optional<uint32_t>(wr);
        uint16_t cr = readU16(p + 12);
        slot.creditsTimesTen = (cr == 0xFFFF) ? std::nullopt : std::optional<uint16_t>(cr);
        slot.plan            = (ProviderPlan)p[14];
    }
    return DecodeResult::Ok;
}

}  // namespace stopwatch
```

- [ ] **Step 6: Run tests to confirm**

```bash
cd firmware && pio test -e native -f test_snapshot_codec
```

Expected: `4 Tests 0 Failures 0 Ignored`.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/Protocol.h firmware/src/SnapshotCodec.h firmware/src/SnapshotCodec.cpp firmware/test/
git commit -m "firmware: SnapshotCodec decoder + native tests against shared hex fixtures"
```

### Task B.3: `Theme.h` and `Renderer` (sprite-based; visual verification)

**Files:**
- Create: `firmware/src/Theme.h`
- Create: `firmware/src/Renderer.h`
- Create: `firmware/src/Renderer.cpp`
- Modify: `firmware/src/main.cpp`

- [ ] **Step 1: Write `Theme.h`**

```cpp
// firmware/src/Theme.h
#pragma once
#include <M5Unified.h>
#include "Protocol.h"

namespace stopwatch::theme {

// Hex colors converted to 24-bit ints; M5Unified uses 24-bit RGB at the API layer.
constexpr uint32_t kBackground   = 0x000000;
constexpr uint32_t kCodex        = 0xFF8A3D;
constexpr uint32_t kCodexDim     = 0x995230;
constexpr uint32_t kClaude       = 0xC47BFF;
constexpr uint32_t kClaudeDim    = 0x76499A;
constexpr uint32_t kGemini       = 0x4DC4FF;
constexpr uint32_t kGeminiDim    = 0x2E7699;
constexpr uint32_t kTextPrimary  = 0xFFFFFF;
constexpr uint32_t kTextMuted    = 0x8A8D92;
constexpr uint32_t kRingTrack    = 0x1E1E1E;

constexpr int kRingStroke    = 14;   // pixels
constexpr int kRingOuterR    = 200;
constexpr int kRingMiddleR   = 150;
constexpr int kRingInnerR    = 100;
constexpr int kCenterX       = 233;  // 466 / 2
constexpr int kCenterY       = 233;

inline uint32_t colorFor(ProviderID id) {
    switch (id) {
        case ProviderID::Codex:  return kCodex;
        case ProviderID::Claude: return kClaude;
        case ProviderID::Gemini: return kGemini;
    }
    return kTextMuted;
}

inline uint32_t colorDimFor(ProviderID id) {
    switch (id) {
        case ProviderID::Codex:  return kCodexDim;
        case ProviderID::Claude: return kClaudeDim;
        case ProviderID::Gemini: return kGeminiDim;
    }
    return kRingTrack;
}

}  // namespace stopwatch::theme
```

- [ ] **Step 2: Write `Renderer.h`**

```cpp
// firmware/src/Renderer.h
#pragma once
#include <M5Unified.h>

namespace stopwatch {

/// Owns the full-screen sprite that all views draw to. Single pushSprite per frame.
class Renderer {
public:
    void begin();
    M5Canvas &canvas() { return sprite_; }
    void present();   // pushSprite to display
    void clear(uint32_t color = 0x000000);

    /// Draws an arc from -90° (top of circle) clockwise by `fillFraction` of 360°.
    /// fillFraction in [0.0, 1.0]. Track is drawn underneath.
    void drawRing(int cx, int cy, int radius, int stroke,
                  uint32_t trackColor, uint32_t fillColor,
                  float fillFraction);

private:
    M5Canvas sprite_{&M5.Display};
};

}  // namespace stopwatch
```

- [ ] **Step 3: Write `Renderer.cpp`**

```cpp
// firmware/src/Renderer.cpp
#include "Renderer.h"
#include <algorithm>

namespace stopwatch {

void Renderer::begin() {
    sprite_.setColorDepth(16);
    sprite_.setPsram(true);
    sprite_.createSprite(M5.Display.width(), M5.Display.height());
    sprite_.fillSprite(0x000000);
}

void Renderer::clear(uint32_t color) {
    sprite_.fillSprite(color);
}

void Renderer::present() {
    sprite_.pushSprite(0, 0);
}

void Renderer::drawRing(int cx, int cy, int radius, int stroke,
                        uint32_t trackColor, uint32_t fillColor,
                        float fillFraction) {
    fillFraction = std::clamp(fillFraction, 0.0f, 1.0f);
    int innerR = radius - stroke;
    // Track (full ring).
    sprite_.fillArc(cx, cy, radius, innerR, 0, 360, trackColor);
    // Fill from 12 o'clock clockwise.
    if (fillFraction > 0.0f) {
        float endDeg = 360.0f * fillFraction;
        // LovyanGFX fillArc takes (startAngle, endAngle) in degrees with 0 = 3 o'clock.
        // Map "12 o'clock clockwise" → start at -90°, sweep forward.
        sprite_.fillArc(cx, cy, radius, innerR, -90, -90 + (int)endDeg, fillColor);
    }
}

}  // namespace stopwatch
```

- [ ] **Step 4: Update `main.cpp` to draw a single test ring**

```cpp
// firmware/src/main.cpp
#include <Arduino.h>
#include <M5Unified.h>
#include "Renderer.h"
#include "Theme.h"

stopwatch::Renderer g_renderer;

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    g_renderer.begin();
    g_renderer.clear(stopwatch::theme::kBackground);
    g_renderer.drawRing(stopwatch::theme::kCenterX,
                        stopwatch::theme::kCenterY,
                        stopwatch::theme::kRingOuterR,
                        stopwatch::theme::kRingStroke,
                        stopwatch::theme::kRingTrack,
                        stopwatch::theme::kCodex,
                        0.72f);
    g_renderer.present();
}

void loop() {
    M5.update();
    delay(50);
}
```

- [ ] **Step 5: Build + flash + verify**

```bash
cd firmware && pio run -t upload
```

Expected: watch shows a single orange Codex ring filled to ~72% on a black background.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/Theme.h firmware/src/Renderer.h firmware/src/Renderer.cpp firmware/src/main.cpp
git commit -m "firmware: Renderer with PSRAM sprite + single-ring smoke test"
```

### Task B.4: `Views::Overview` (three concentric rings)

**Files:**
- Create: `firmware/src/Views/Overview.h`
- Create: `firmware/src/Views/Overview.cpp`
- Modify: `firmware/src/main.cpp`

- [ ] **Step 1: Write `Views/Overview.h`**

```cpp
// firmware/src/Views/Overview.h
#pragma once
#include "../SnapshotCodec.h"
#include "../Renderer.h"

namespace stopwatch::views {

/// Draws the three-ring overview into the renderer's sprite.
/// Caller must call renderer.present() afterward.
void drawOverview(Renderer &renderer, const Snapshot &snap);

}  // namespace stopwatch::views
```

- [ ] **Step 2: Write `Views/Overview.cpp`**

```cpp
// firmware/src/Views/Overview.cpp
#include "Overview.h"
#include "../Theme.h"
#include <cstdio>

namespace stopwatch::views {

namespace {
// Returns the provider in `snap` with the highest sessionPct, or nullptr if none.
const ProviderSlot *worstOff(const Snapshot &snap) {
    const ProviderSlot *best = nullptr;
    int bestPct = -1;
    for (uint8_t i = 0; i < snap.providerCount; ++i) {
        const auto &p = snap.providers[i];
        int pct = p.sessionPct.value_or(0);
        if (pct > bestPct) { bestPct = pct; best = &p; }
    }
    return best;
}

// Find a specific provider by ID.
const ProviderSlot *findProvider(const Snapshot &snap, ProviderID id) {
    for (uint8_t i = 0; i < snap.providerCount; ++i) {
        if (snap.providers[i].id == id) return &snap.providers[i];
    }
    return nullptr;
}

float fractionOf(const ProviderSlot *p) {
    if (!p || !p->sessionPct.has_value()) return 0.0f;
    return (float)p->sessionPct.value() / 100.0f;
}
}  // namespace

void drawOverview(Renderer &renderer, const Snapshot &snap) {
    auto &c = renderer.canvas();
    renderer.clear(theme::kBackground);

    const auto *codex  = findProvider(snap, ProviderID::Codex);
    const auto *claude = findProvider(snap, ProviderID::Claude);
    const auto *gemini = findProvider(snap, ProviderID::Gemini);

    renderer.drawRing(theme::kCenterX, theme::kCenterY, theme::kRingOuterR,  theme::kRingStroke,
                      theme::kRingTrack, theme::kCodex,  fractionOf(codex));
    renderer.drawRing(theme::kCenterX, theme::kCenterY, theme::kRingMiddleR, theme::kRingStroke,
                      theme::kRingTrack, theme::kClaude, fractionOf(claude));
    renderer.drawRing(theme::kCenterX, theme::kCenterY, theme::kRingInnerR,  theme::kRingStroke,
                      theme::kRingTrack, theme::kGemini, fractionOf(gemini));

    // Center: worst-off provider's % in its color.
    const auto *worst = worstOff(snap);
    c.setTextDatum(middle_center);
    c.setTextColor(theme::kTextMuted);
    c.setFont(&fonts::Font2);
    c.drawString("Most used", theme::kCenterX, theme::kCenterY - 36);

    if (worst && worst->sessionPct.has_value()) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u%%", worst->sessionPct.value());
        c.setTextColor(theme::colorFor(worst->id));
        c.setFont(&fonts::Font7);
        c.drawString(buf, theme::kCenterX, theme::kCenterY + 4);
    } else {
        c.setTextColor(theme::kTextMuted);
        c.setFont(&fonts::Font4);
        c.drawString("--", theme::kCenterX, theme::kCenterY + 4);
    }

    // Bottom legend chips.
    c.setFont(&fonts::Font2);
    int by = theme::kCenterY + theme::kRingOuterR - 36;
    c.setTextColor(theme::kCodex);  c.drawString("\xE2\x97\x8F CX", theme::kCenterX - 60, by);
    c.setTextColor(theme::kClaude); c.drawString("\xE2\x97\x8F CL", theme::kCenterX,      by);
    c.setTextColor(theme::kGemini); c.drawString("\xE2\x97\x8F GM", theme::kCenterX + 60, by);
}

}  // namespace stopwatch::views
```

- [ ] **Step 3: Update `main.cpp` to draw the overview with hard-coded data**

```cpp
// firmware/src/main.cpp
#include <Arduino.h>
#include <M5Unified.h>
#include "Renderer.h"
#include "Theme.h"
#include "Views/Overview.h"

stopwatch::Renderer g_renderer;
stopwatch::Snapshot g_snap;

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    g_renderer.begin();

    // Hard-coded sample snapshot until BLE is wired up.
    g_snap.versionMajor  = 1;
    g_snap.providerCount = 3;
    g_snap.providers[0]  = { stopwatch::ProviderID::Codex,  stopwatch::ProviderStatus::Ok,
                             72, 41, 1748467200, 1748538000, 1124, stopwatch::ProviderPlan::Plus };
    g_snap.providers[1]  = { stopwatch::ProviderID::Claude, stopwatch::ProviderStatus::Ok,
                             12, 37, 1748502000, 1748696400, std::nullopt, stopwatch::ProviderPlan::Pro };
    g_snap.providers[2]  = { stopwatch::ProviderID::Gemini, stopwatch::ProviderStatus::Ok,
                             8, std::nullopt, 1748476800, std::nullopt, std::nullopt, stopwatch::ProviderPlan::Free };

    stopwatch::views::drawOverview(g_renderer, g_snap);
    g_renderer.present();
}

void loop() {
    M5.update();
    delay(50);
}
```

- [ ] **Step 4: Build + flash + verify**

```bash
cd firmware && pio run -t upload
```

Expected: watch shows three concentric rings (orange outer, purple middle, blue inner), with "Most used" + "72%" in the center.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/Views/
git commit -m "firmware: Overview view (3 concentric rings + center metric + legend)"
```

### Task B.5: `Views::Provider` (per-provider rings)

**Files:**
- Create: `firmware/src/Views/Provider.h`
- Create: `firmware/src/Views/Provider.cpp`

- [ ] **Step 1: Write `Views/Provider.h`**

```cpp
// firmware/src/Views/Provider.h
#pragma once
#include "../SnapshotCodec.h"
#include "../Renderer.h"

namespace stopwatch::views {

/// Draws the per-provider concentric-rings screen for `id` from `snap`.
/// If the provider is missing in `snap`, draws a placeholder.
void drawProvider(Renderer &renderer, const Snapshot &snap, ProviderID id);

}  // namespace stopwatch::views
```

- [ ] **Step 2: Write `Views/Provider.cpp`**

```cpp
// firmware/src/Views/Provider.cpp
#include "Provider.h"
#include "../Theme.h"
#include <cstdio>
#include <time.h>

namespace stopwatch::views {

namespace {
const char *labelFor(ProviderID id) {
    switch (id) {
        case ProviderID::Codex:  return "CODEX";
        case ProviderID::Claude: return "CLAUDE";
        case ProviderID::Gemini: return "GEMINI";
    }
    return "?";
}

const char *planLabel(ProviderPlan plan) {
    switch (plan) {
        case ProviderPlan::Free:       return "Free";
        case ProviderPlan::Plus:       return "Plus";
        case ProviderPlan::Pro:        return "Pro";
        case ProviderPlan::Team:       return "Team";
        case ProviderPlan::Enterprise: return "Ent";
        default:                       return "";
    }
}

void formatResetIn(char *buf, size_t bufSize, uint32_t resetAt, uint32_t now) {
    if (resetAt == 0 || resetAt <= now) { snprintf(buf, bufSize, "—"); return; }
    uint32_t delta = resetAt - now;
    if (delta < 3600)       snprintf(buf, bufSize, "%um left", delta / 60);
    else if (delta < 86400) snprintf(buf, bufSize, "%uh %02um left", delta / 3600, (delta % 3600) / 60);
    else                    snprintf(buf, bufSize, "%ud left", delta / 86400);
}

const ProviderSlot *findProvider(const Snapshot &snap, ProviderID id) {
    for (uint8_t i = 0; i < snap.providerCount; ++i) {
        if (snap.providers[i].id == id) return &snap.providers[i];
    }
    return nullptr;
}
}  // namespace

void drawProvider(Renderer &renderer, const Snapshot &snap, ProviderID id) {
    auto &c = renderer.canvas();
    renderer.clear(theme::kBackground);

    const auto *p = findProvider(snap, id);
    uint32_t color    = theme::colorFor(id);
    uint32_t colorDim = theme::colorDimFor(id);

    // Top: name + plan
    c.setTextDatum(middle_center);
    c.setTextColor(color);
    c.setFont(&fonts::Font2);
    char header[24];
    if (p && p->plan != ProviderPlan::Unknown) {
        snprintf(header, sizeof(header), "%s · %s", labelFor(id), planLabel(p->plan));
    } else {
        snprintf(header, sizeof(header), "%s", labelFor(id));
    }
    c.drawString(header, theme::kCenterX, 24);

    // Rings: outer = session, inner = week
    float sessionFrac = (p && p->sessionPct.has_value()) ? p->sessionPct.value() / 100.0f : 0.0f;
    float weekFrac    = (p && p->weekPct.has_value())    ? p->weekPct.value()    / 100.0f : 0.0f;
    renderer.drawRing(theme::kCenterX, theme::kCenterY, theme::kRingOuterR,  theme::kRingStroke,
                      theme::kRingTrack, color,    sessionFrac);
    renderer.drawRing(theme::kCenterX, theme::kCenterY, theme::kRingOuterR - theme::kRingStroke * 2 - 4,
                      theme::kRingStroke, theme::kRingTrack, colorDim, weekFrac);

    // Center: session %
    c.setTextColor(theme::kTextMuted);
    c.setFont(&fonts::Font2);
    c.drawString("Session", theme::kCenterX, theme::kCenterY - 30);
    if (p && p->sessionPct.has_value()) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u%%", p->sessionPct.value());
        c.setTextColor(color);
        c.setFont(&fonts::Font7);
        c.drawString(buf, theme::kCenterX, theme::kCenterY + 6);
    } else {
        c.setTextColor(theme::kTextMuted);
        c.setFont(&fonts::Font4);
        c.drawString("--", theme::kCenterX, theme::kCenterY + 6);
    }

    // Reset countdown under session
    if (p && p->sessionResetAt.has_value()) {
        char buf[24];
        formatResetIn(buf, sizeof(buf), p->sessionResetAt.value(), snap.capturedAt);
        c.setTextColor(theme::kTextMuted);
        c.setFont(&fonts::Font2);
        c.drawString(buf, theme::kCenterX, theme::kCenterY + 60);
    }

    // Bottom strap: week % + credits
    int by = theme::kCenterY + theme::kRingOuterR - 30;
    char bottom[32];
    if (p) {
        if (p->creditsTimesTen.has_value()) {
            snprintf(bottom, sizeof(bottom), "Week %u%% · %u cr",
                     p->weekPct.value_or(0), p->creditsTimesTen.value() / 10);
        } else if (p->weekPct.has_value()) {
            snprintf(bottom, sizeof(bottom), "Week %u%%", p->weekPct.value());
        } else {
            bottom[0] = '\0';
        }
        if (bottom[0]) {
            c.setTextColor(theme::kTextMuted);
            c.setFont(&fonts::Font2);
            c.drawString(bottom, theme::kCenterX, by);
        }
    }
}

}  // namespace stopwatch::views
```

- [ ] **Step 3: Smoke-test by drawing the Codex view from `main.cpp`**

Temporarily swap the `drawOverview` call in `main.cpp` for `drawProvider(g_renderer, g_snap, stopwatch::ProviderID::Codex);` to verify visually. Revert after the smoke test (the App state machine in Task B.10 picks the view).

```bash
cd firmware && pio run -t upload
```

Expected: watch shows the per-provider Codex screen — orange outer ring at 72%, dim-orange inner ring at 41%, "CODEX · Plus" at top, "72%" in center, "Week 41% · 112 cr" at bottom.

Revert `main.cpp` to draw `drawOverview` once verified.

- [ ] **Step 4: Commit**

```bash
git add firmware/src/Views/Provider.h firmware/src/Views/Provider.cpp
git commit -m "firmware: Provider view (session outer + week inner rings + countdown + bottom strap)"
```

### Task B.6: `Buttons` (KEYA/KEYB short + long press)

**Files:**
- Create: `firmware/src/Buttons.h`
- Create: `firmware/src/Buttons.cpp`

- [ ] **Step 1: Write `Buttons.h`**

```cpp
// firmware/src/Buttons.h
#pragma once
#include <cstdint>

namespace stopwatch {

enum class ButtonEvent : uint8_t {
    None,
    KeyAShort, KeyALong,
    KeyBShort, KeyBLong,
};

/// Polls M5 button state. Call once per loop tick; debounces at ~20ms.
/// Long press threshold: 800ms held.
ButtonEvent pollButtons();

}  // namespace stopwatch
```

- [ ] **Step 2: Write `Buttons.cpp`**

```cpp
// firmware/src/Buttons.cpp
#include "Buttons.h"
#include <M5Unified.h>

namespace stopwatch {

namespace {
constexpr uint32_t kLongMs = 800;

struct State {
    bool wasPressed = false;
    uint32_t pressedAt = 0;
    bool longFired = false;
};
State sA, sB;

ButtonEvent step(State &s, bool pressed, ButtonEvent shortEv, ButtonEvent longEv) {
    uint32_t now = millis();
    if (pressed && !s.wasPressed) {
        s.wasPressed = true;
        s.pressedAt = now;
        s.longFired = false;
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

ButtonEvent pollButtons() {
    M5.update();
    auto evA = step(sA, M5.BtnA.isPressed(), ButtonEvent::KeyAShort, ButtonEvent::KeyALong);
    if (evA != ButtonEvent::None) return evA;
    return step(sB, M5.BtnB.isPressed(), ButtonEvent::KeyBShort, ButtonEvent::KeyBLong);
}

}  // namespace stopwatch
```

- [ ] **Step 3: Build to confirm**

```bash
cd firmware && pio run
```

Expected: `SUCCESS`.

- [ ] **Step 4: Commit**

```bash
git add firmware/src/Buttons.h firmware/src/Buttons.cpp
git commit -m "firmware: Buttons (KEYA/KEYB short + long press detection)"
```

### Task B.7: `SnapshotStore` (NVS-backed cache)

**Files:**
- Create: `firmware/src/SnapshotStore.h`
- Create: `firmware/src/SnapshotStore.cpp`

- [ ] **Step 1: Write `SnapshotStore.h`**

```cpp
// firmware/src/SnapshotStore.h
#pragma once
#include "Protocol.h"
#include <cstdint>
#include <cstddef>

namespace stopwatch {

/// Persists the most-recent raw snapshot bytes to NVS so the watch can render
/// last-known data on a cold boot before the bridge responds.
class SnapshotStore {
public:
    void begin();
    bool load(uint8_t *outBytes, size_t bufSize, size_t &outLen);
    void save(const uint8_t *bytes, size_t len);

private:
    bool open_ = false;
};

}  // namespace stopwatch
```

- [ ] **Step 2: Write `SnapshotStore.cpp`**

```cpp
// firmware/src/SnapshotStore.cpp
#include "SnapshotStore.h"
#include <Preferences.h>

namespace stopwatch {

namespace { Preferences prefs; constexpr const char *kNs = "swq"; constexpr const char *kKey = "snap"; }

void SnapshotStore::begin() {
    open_ = prefs.begin(kNs, false);
}

bool SnapshotStore::load(uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    if (!open_) return false;
    size_t sz = prefs.getBytesLength(kKey);
    if (sz == 0 || sz > bufSize) { outLen = 0; return false; }
    outLen = prefs.getBytes(kKey, outBytes, sz);
    return outLen == sz;
}

void SnapshotStore::save(const uint8_t *bytes, size_t len) {
    if (!open_) return;
    prefs.putBytes(kKey, bytes, len);
}

}  // namespace stopwatch
```

- [ ] **Step 3: Build**

```bash
cd firmware && pio run
```

Expected: `SUCCESS`.

- [ ] **Step 4: Commit**

```bash
git add firmware/src/SnapshotStore.h firmware/src/SnapshotStore.cpp
git commit -m "firmware: SnapshotStore (NVS-backed cache of last raw snapshot bytes)"
```

### Task B.8: `BleClient` (NimBLE central)

**Files:**
- Create: `firmware/src/BleClient.h`
- Create: `firmware/src/BleClient.cpp`

- [ ] **Step 1: Write `BleClient.h`**

```cpp
// firmware/src/BleClient.h
#pragma once
#include <cstdint>
#include <cstddef>

namespace stopwatch {

class BleClient {
public:
    enum class FetchResult : uint8_t { Ok, NoPeripheral, ConnectFailed, ReadFailed };

    void begin();

    /// Scans for the service UUID, connects, writes scope to RefreshTrigger,
    /// reads UsageSnapshot. Blocks up to ~3s total.
    /// On Ok, fills `outBytes` (capacity = bufSize) and sets `outLen`.
    FetchResult fetch(uint8_t scope, uint8_t *outBytes, size_t bufSize, size_t &outLen);
};

}  // namespace stopwatch
```

- [ ] **Step 2: Write `BleClient.cpp`**

```cpp
// firmware/src/BleClient.cpp
#include "BleClient.h"
#include "Protocol.h"
#include <NimBLEDevice.h>

namespace stopwatch {

void BleClient::begin() {
    NimBLEDevice::init("stopwatch");
}

BleClient::FetchResult BleClient::fetch(uint8_t scope, uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    outLen = 0;
    auto *scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    scan->setInterval(45);
    scan->setWindow(15);

    NimBLEScanResults results = scan->getResults(2000);  // ms

    NimBLEUUID svcUuid(kServiceUUID);
    const NimBLEAdvertisedDevice *target = nullptr;
    int8_t bestRssi = -127;
    for (int i = 0; i < results.getCount(); ++i) {
        const auto *dev = results.getDevice(i);
        if (dev->isAdvertisingService(svcUuid) && dev->getRSSI() > bestRssi) {
            target = dev;
            bestRssi = dev->getRSSI();
        }
    }
    if (!target) return FetchResult::NoPeripheral;

    auto *client = NimBLEDevice::createClient();
    if (!client->connect(target)) {
        NimBLEDevice::deleteClient(client);
        return FetchResult::ConnectFailed;
    }

    // Inner read attempt: writes trigger, then reads snapshot. Tried up to
    // twice on transient read failure per spec §9.2.
    auto tryReadOnce = [&]() -> FetchResult {
        auto *svc = client->getService(svcUuid);
        if (!svc) return FetchResult::ReadFailed;

        auto *trigger = svc->getCharacteristic(NimBLEUUID(kTriggerUUID));
        if (!trigger) return FetchResult::ReadFailed;
        uint8_t scopeBuf[1] = { scope };
        trigger->writeValue(scopeBuf, 1, /*response=*/false);

        // Small grace period for the bridge to refresh before we read.
        delay(150);

        auto *snap = svc->getCharacteristic(NimBLEUUID(kSnapshotUUID));
        if (!snap) return FetchResult::ReadFailed;
        std::string value = snap->readValue();
        if (value.empty() || value.size() > bufSize) return FetchResult::ReadFailed;
        memcpy(outBytes, value.data(), value.size());
        outLen = value.size();
        return FetchResult::Ok;
    };

    FetchResult result = tryReadOnce();
    if (result == FetchResult::ReadFailed) {
        delay(100);
        result = tryReadOnce();
    }

    client->disconnect();
    NimBLEDevice::deleteClient(client);
    return result;
}

}  // namespace stopwatch
```

- [ ] **Step 3: Build**

```bash
cd firmware && pio run
```

Expected: `SUCCESS`. (If `NimBLE-Arduino` API names differ in the resolved version — confirm against `context7` `/h2zero/nimble-arduino` — adjust accordingly; the lib's central-mode API is stable enough that the methods above match the v2.x line.)

- [ ] **Step 4: Commit**

```bash
git add firmware/src/BleClient.h firmware/src/BleClient.cpp
git commit -m "firmware: BleClient (NimBLE central: scan by service UUID, write trigger, read snapshot)"
```

### Task B.9: `Power` (sleep / wake)

**Files:**
- Create: `firmware/src/Power.h`
- Create: `firmware/src/Power.cpp`

- [ ] **Step 1: Write `Power.h`**

```cpp
// firmware/src/Power.h
#pragma once
#include <cstdint>

namespace stopwatch {

class Power {
public:
    void begin();
    void noteActivity();     // call on every user input
    bool shouldSleep() const; // true after kIdleSleepMs of no activity
    void enterLightSleep();   // configures wake-on-button + sleeps; returns after wake

private:
    uint32_t lastActivityMs_ = 0;
    static constexpr uint32_t kIdleSleepMs = 15'000;
};

}  // namespace stopwatch
```

- [ ] **Step 2: Write `Power.cpp`**

```cpp
// firmware/src/Power.cpp
#include "Power.h"
#include <Arduino.h>
#include <esp_sleep.h>
#include <M5Unified.h>

namespace stopwatch {

// IMPORTANT: Confirm the actual GPIOs for KEYA / KEYB on the M5Stack StopWatch
// before flashing. M5Unified's pin map abstracts BtnA/BtnB but ext1 wake needs
// raw GPIO numbers. Update these from the M5Stack StopWatch schematic / repo.
namespace { constexpr gpio_num_t kPinKeyA = GPIO_NUM_0;   // placeholder; verify
              constexpr gpio_num_t kPinKeyB = GPIO_NUM_46;  // placeholder; verify
}

void Power::begin() {
    noteActivity();
    M5.Display.setBrightness(160);
}

void Power::noteActivity() {
    lastActivityMs_ = millis();
}

bool Power::shouldSleep() const {
    return (millis() - lastActivityMs_) >= kIdleSleepMs;
}

void Power::enterLightSleep() {
    M5.Display.sleep();
    uint64_t mask = (1ULL << kPinKeyA) | (1ULL << kPinKeyB);
    esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_light_sleep_start();
    // Woke up.
    M5.Display.wakeup();
    noteActivity();
}

}  // namespace stopwatch
```

- [ ] **Step 3: Build**

```bash
cd firmware && pio run
```

Expected: `SUCCESS`. If `gpio_num_t` values for KEYA / KEYB are wrong (ext1 wake won't fire), that surfaces during the integration test in Task B.10 and is fixed by updating the constants from the M5Stack StopWatch schematic or M5Unified config probe.

- [ ] **Step 4: Commit**

```bash
git add firmware/src/Power.h firmware/src/Power.cpp
git commit -m "firmware: Power (idle tracking, light-sleep with ext1 KEYA/KEYB wake)"
```

### Task B.10: `App` state machine + state-machine native test

**Files:**
- Create: `firmware/src/App.h`
- Create: `firmware/src/App.cpp`
- Create: `firmware/test/test_state_machine/test_main.cpp`
- Modify: `firmware/src/main.cpp`

- [ ] **Step 1: Write `App.h`**

```cpp
// firmware/src/App.h
#pragma once
#include "Buttons.h"
#include "Protocol.h"
#include "SnapshotCodec.h"

namespace stopwatch {

enum class ViewId : uint8_t { Overview = 0, Codex = 1, Claude = 2, Gemini = 3 };

class App {
public:
    void begin();
    /// Drive one event into the state machine; returns true if the view changed.
    bool handleEvent(ButtonEvent ev);
    ViewId currentView() const { return view_; }
    bool wantsRefresh() const { return wantsRefresh_; }
    void clearRefreshRequest() { wantsRefresh_ = false; }
    bool wantsImmediateSleep() const { return wantsSleep_; }
    void clearSleepRequest() { wantsSleep_ = false; }

private:
    ViewId view_ = ViewId::Overview;
    bool wantsRefresh_ = false;
    bool wantsSleep_   = false;
};

constexpr ViewId nextView(ViewId v);
constexpr ViewId prevView(ViewId v);

}  // namespace stopwatch
```

- [ ] **Step 2: Write `App.cpp`**

```cpp
// firmware/src/App.cpp
#include "App.h"

namespace stopwatch {

constexpr ViewId nextView(ViewId v) {
    switch (v) {
        case ViewId::Overview: return ViewId::Codex;
        case ViewId::Codex:    return ViewId::Claude;
        case ViewId::Claude:   return ViewId::Gemini;
        case ViewId::Gemini:   return ViewId::Overview;
    }
    return ViewId::Overview;
}

constexpr ViewId prevView(ViewId v) {
    switch (v) {
        case ViewId::Overview: return ViewId::Gemini;
        case ViewId::Codex:    return ViewId::Overview;
        case ViewId::Claude:   return ViewId::Codex;
        case ViewId::Gemini:   return ViewId::Claude;
    }
    return ViewId::Overview;
}

void App::begin() {
    view_ = ViewId::Overview;
    wantsRefresh_ = false;
    wantsSleep_ = false;
}

bool App::handleEvent(ButtonEvent ev) {
    switch (ev) {
        case ButtonEvent::KeyBShort: view_ = nextView(view_); return true;
        case ButtonEvent::KeyAShort: view_ = prevView(view_); return true;
        case ButtonEvent::KeyALong:  wantsRefresh_ = true;    return false;
        case ButtonEvent::KeyBLong:  wantsSleep_   = true;    return false;
        case ButtonEvent::None:                                return false;
    }
    return false;
}

}  // namespace stopwatch
```

- [ ] **Step 3: Write the failing native test**

```cpp
// firmware/test/test_state_machine/test_main.cpp
#include <unity.h>
#include "../../src/App.h"

using namespace stopwatch;

void test_keyBShortCyclesForward(void) {
    App app;
    app.begin();
    TEST_ASSERT_EQUAL((int)ViewId::Overview, (int)app.currentView());
    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyBShort));
    TEST_ASSERT_EQUAL((int)ViewId::Codex, (int)app.currentView());
    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyBShort));
    TEST_ASSERT_EQUAL((int)ViewId::Claude, (int)app.currentView());
    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyBShort));
    TEST_ASSERT_EQUAL((int)ViewId::Gemini, (int)app.currentView());
    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyBShort));
    TEST_ASSERT_EQUAL((int)ViewId::Overview, (int)app.currentView());
}

void test_keyAShortCyclesBackward(void) {
    App app;
    app.begin();
    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyAShort));
    TEST_ASSERT_EQUAL((int)ViewId::Gemini, (int)app.currentView());
    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyAShort));
    TEST_ASSERT_EQUAL((int)ViewId::Claude, (int)app.currentView());
}

void test_longPressesSetFlags(void) {
    App app;
    app.begin();
    TEST_ASSERT_FALSE(app.wantsRefresh());
    app.handleEvent(ButtonEvent::KeyALong);
    TEST_ASSERT_TRUE(app.wantsRefresh());
    app.clearRefreshRequest();
    TEST_ASSERT_FALSE(app.wantsRefresh());

    app.handleEvent(ButtonEvent::KeyBLong);
    TEST_ASSERT_TRUE(app.wantsImmediateSleep());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_keyBShortCyclesForward);
    RUN_TEST(test_keyAShortCyclesBackward);
    RUN_TEST(test_longPressesSetFlags);
    return UNITY_END();
}
```

- [ ] **Step 4: Run native tests**

```bash
cd firmware && pio test -e native -f test_state_machine
```

Expected: `3 Tests 0 Failures 0 Ignored`.

- [ ] **Step 5: Wire `App` into `main.cpp`**

```cpp
// firmware/src/main.cpp
#include <Arduino.h>
#include <M5Unified.h>
#include "App.h"
#include "BleClient.h"
#include "Buttons.h"
#include "Power.h"
#include "Renderer.h"
#include "SnapshotCodec.h"
#include "SnapshotStore.h"
#include "Theme.h"
#include "Views/Overview.h"
#include "Views/Provider.h"

stopwatch::App         g_app;
stopwatch::BleClient   g_ble;
stopwatch::Renderer    g_renderer;
stopwatch::Power       g_power;
stopwatch::SnapshotStore g_store;
stopwatch::Snapshot    g_snap;

static void renderCurrent() {
    using namespace stopwatch;
    switch (g_app.currentView()) {
        case ViewId::Overview: views::drawOverview(g_renderer, g_snap); break;
        case ViewId::Codex:    views::drawProvider(g_renderer, g_snap, ProviderID::Codex);  break;
        case ViewId::Claude:   views::drawProvider(g_renderer, g_snap, ProviderID::Claude); break;
        case ViewId::Gemini:   views::drawProvider(g_renderer, g_snap, ProviderID::Gemini); break;
    }
    g_renderer.present();
}

static bool fetchAndApply(uint8_t scope) {
    uint8_t buf[stopwatch::kSnapshotSize];
    size_t len = 0;
    auto rc = g_ble.fetch(scope, buf, sizeof(buf), len);
    if (rc != stopwatch::BleClient::FetchResult::Ok) return false;
    stopwatch::Snapshot snap;
    if (stopwatch::decodeSnapshot(buf, len, snap) != stopwatch::DecodeResult::Ok) return false;
    g_snap = snap;
    g_store.save(buf, len);
    return true;
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    g_renderer.begin();
    g_store.begin();
    g_app.begin();
    g_power.begin();
    g_ble.begin();

    // Load last cached snapshot for instant first paint.
    uint8_t buf[stopwatch::kSnapshotSize];
    size_t len = 0;
    if (g_store.load(buf, sizeof(buf), len)) {
        stopwatch::decodeSnapshot(buf, len, g_snap);
    }
    renderCurrent();

    // Fire a refresh in the background to get fresh data.
    fetchAndApply(0x00);
    renderCurrent();
}

void loop() {
    using namespace stopwatch;
    auto ev = pollButtons();
    if (ev != ButtonEvent::None) {
        g_power.noteActivity();
        bool changed = g_app.handleEvent(ev);
        if (g_app.wantsRefresh()) {
            fetchAndApply(0x00);
            g_app.clearRefreshRequest();
            changed = true;
        }
        if (g_app.wantsImmediateSleep()) {
            g_app.clearSleepRequest();
            g_power.enterLightSleep();
            renderCurrent();
        } else if (changed) {
            renderCurrent();
        }
    }
    if (g_power.shouldSleep()) {
        g_power.enterLightSleep();
        renderCurrent();
    }
    delay(20);
}
```

- [ ] **Step 6: Build + flash + integration check**

```bash
cd firmware && pio run -t upload && pio device monitor -b 115200
```

Expected: bridge running on Mac → flash watch → watch shows overview rings, KEYB cycles forward through Codex/Claude/Gemini/Overview, KEYA cycles backward, KEYA-long triggers a refresh, KEYB-long sleeps the display, 15s idle sleeps.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/App.h firmware/src/App.cpp firmware/src/main.cpp firmware/test/test_state_machine/
git commit -m "firmware: App state machine + integration in main.cpp + native tests"
```

### Task B.11: Status pills (LinkStatus + per-view rendering)

Implements spec §8.4 pills. Watch tracks last BLE outcome as `LinkStatus`; views render the appropriate `●` pill based on `LinkStatus` plus the snapshot's flag bits.

**Files:**
- Modify: `firmware/src/App.h` / `firmware/src/App.cpp`
- Modify: `firmware/src/Theme.h`
- Modify: `firmware/src/Views/Overview.h` / `Overview.cpp`
- Modify: `firmware/src/Views/Provider.h` / `Provider.cpp`
- Modify: `firmware/src/main.cpp`
- Modify: `firmware/test/test_state_machine/test_main.cpp`

- [ ] **Step 1: Add `LinkStatus` to `App.h`**

Add at the top of the `stopwatch` namespace, before `class App`:

```cpp
enum class LinkStatus : uint8_t {
    Connected,    // last fetch returned Ok
    NoBridge,     // BleClient::FetchResult::NoPeripheral
    LinkError,    // ConnectFailed or ReadFailed (after retry)
};
```

Add to `class App`:

```cpp
    LinkStatus linkStatus() const { return link_; }
    void setLinkStatus(LinkStatus s) { link_ = s; }
```

And the private field:

```cpp
    LinkStatus link_ = LinkStatus::NoBridge;
```

- [ ] **Step 2: Add pill colors to `Theme.h`**

```cpp
constexpr uint32_t kPillStale    = 0xFFB37A;   // amber
constexpr uint32_t kPillError    = 0xFF6666;   // red
constexpr uint32_t kPillInfo     = 0x8A8D92;   // muted
```

- [ ] **Step 3: Add a shared `drawPill` helper to `Renderer.h` / `Renderer.cpp`**

In `Renderer.h`, add to the `Renderer` class:

```cpp
    /// Draws a small "● label" pill at (cx, baselineY). No-op if label is null.
    void drawPill(int cx, int baselineY, const char *label, uint32_t color);
```

In `Renderer.cpp`, implement:

```cpp
void Renderer::drawPill(int cx, int baselineY, const char *label, uint32_t color) {
    if (!label) return;
    char buf[40];
    snprintf(buf, sizeof(buf), "\xE2\x97\x8F %s", label);
    sprite_.setTextDatum(middle_center);
    sprite_.setTextColor(color);
    sprite_.setFont(&fonts::Font2);
    sprite_.drawString(buf, cx, baselineY);
}
```

- [ ] **Step 4: Extend view signatures to take `LinkStatus`**

`Views/Overview.h`:

```cpp
void drawOverview(Renderer &renderer, const Snapshot &snap, LinkStatus link);
```

`Views/Provider.h`:

```cpp
void drawProvider(Renderer &renderer, const Snapshot &snap, ProviderID id, LinkStatus link);
```

- [ ] **Step 5: Render the pill in both views**

Add a small helper (define once in `Views/Overview.cpp`, declare in an internal header or inline-duplicate in `Provider.cpp` — duplication is fine here; two short helpers are clearer than a shared "view utilities" file at this size):

```cpp
// At the top of Views/Overview.cpp inside the anonymous namespace:
struct Pill { const char *label; uint32_t color; };
Pill pillFor(LinkStatus link, const Snapshot &snap) {
    if (link == LinkStatus::NoBridge)            return { "no bridge", theme::kPillInfo };
    if (link == LinkStatus::LinkError)           return { "link error", theme::kPillError };
    if (snap.isProviderMissing())                return { "no source", theme::kPillInfo };
    if (snap.isStale() || snap.isBridgeError())  return { "stale", theme::kPillStale };
    return { nullptr, 0 };
}
```

Add at the end of `drawOverview()`, just before the function closes:

```cpp
    auto pill = pillFor(link, snap);
    renderer.drawPill(theme::kCenterX,
                      theme::kCenterY + theme::kRingOuterR - 8,
                      pill.label, pill.color);
```

Repeat the same helper + call in `Views/Provider.cpp` (duplicate the `pillFor` helper inside `Provider.cpp`'s anonymous namespace; it's six lines).

- [ ] **Step 6: Update `main.cpp` to thread `LinkStatus` end-to-end**

Update `fetchAndApply()`:

```cpp
static bool fetchAndApply(uint8_t scope) {
    uint8_t buf[stopwatch::kSnapshotSize];
    size_t len = 0;
    auto rc = g_ble.fetch(scope, buf, sizeof(buf), len);
    switch (rc) {
        case stopwatch::BleClient::FetchResult::NoPeripheral:
            g_app.setLinkStatus(stopwatch::LinkStatus::NoBridge);  return false;
        case stopwatch::BleClient::FetchResult::ConnectFailed:
        case stopwatch::BleClient::FetchResult::ReadFailed:
            g_app.setLinkStatus(stopwatch::LinkStatus::LinkError); return false;
        case stopwatch::BleClient::FetchResult::Ok:
            break;
    }
    stopwatch::Snapshot snap;
    if (stopwatch::decodeSnapshot(buf, len, snap) != stopwatch::DecodeResult::Ok) {
        g_app.setLinkStatus(stopwatch::LinkStatus::LinkError);
        return false;
    }
    g_snap = snap;
    g_store.save(buf, len);
    g_app.setLinkStatus(stopwatch::LinkStatus::Connected);
    return true;
}
```

Update `renderCurrent()`:

```cpp
static void renderCurrent() {
    using namespace stopwatch;
    auto link = g_app.linkStatus();
    switch (g_app.currentView()) {
        case ViewId::Overview: views::drawOverview(g_renderer, g_snap, link); break;
        case ViewId::Codex:    views::drawProvider(g_renderer, g_snap, ProviderID::Codex,  link); break;
        case ViewId::Claude:   views::drawProvider(g_renderer, g_snap, ProviderID::Claude, link); break;
        case ViewId::Gemini:   views::drawProvider(g_renderer, g_snap, ProviderID::Gemini, link); break;
    }
    g_renderer.present();
}
```

- [ ] **Step 7: Add a native test for `LinkStatus` default and mutation**

Append to `firmware/test/test_state_machine/test_main.cpp`:

```cpp
void test_linkStatusDefaultsToNoBridgeAndMutates(void) {
    App app;
    app.begin();
    TEST_ASSERT_EQUAL((int)LinkStatus::NoBridge, (int)app.linkStatus());
    app.setLinkStatus(LinkStatus::Connected);
    TEST_ASSERT_EQUAL((int)LinkStatus::Connected, (int)app.linkStatus());
}
```

And register it inside `main()`:

```cpp
    RUN_TEST(test_linkStatusDefaultsToNoBridgeAndMutates);
```

- [ ] **Step 8: Build native + on-device**

```bash
cd firmware && pio test -e native -f test_state_machine
cd firmware && pio run -t upload
```

Expected native: `4 Tests 0 Failures 0 Ignored`. Expected on-device: with the bridge off you see `● no bridge` under the rings; with the bridge running but `codexbar` removed from PATH you see `● no source`.

- [ ] **Step 9: Commit**

```bash
git add firmware/src/App.h firmware/src/App.cpp firmware/src/Theme.h firmware/src/Renderer.h firmware/src/Renderer.cpp firmware/src/Views/ firmware/src/main.cpp firmware/test/test_state_machine/test_main.cpp
git commit -m "firmware: status pills (LinkStatus + snapshot flag bits → labeled pill under rings)"
```

---

## Phase C — Integration & polish

Wires everything together end-to-end on real hardware, exercises failure paths, and writes the user-facing README.

### Task C.1: End-to-end smoke test against real bridge + real watch

This is an interactive validation step; no code changes unless something breaks.

- [ ] **Step 1: Make sure bridge is running**

```bash
launchctl print "gui/$(id -u)/dev.stopwatch.bridge" | grep state
tail -n 20 /tmp/stopwatch-bridge.log
```

Expected: `state = running` and the log shows `advertising Stopwatch Bridge`.

- [ ] **Step 2: Confirm Bluetooth permission was granted**

System Settings → Privacy & Security → Bluetooth — `stopwatch-bridge` should be present and enabled. If not, toggle it on and restart with:

```bash
launchctl kickstart -k "gui/$(id -u)/dev.stopwatch.bridge"
```

- [ ] **Step 3: Flash latest firmware**

```bash
make flash && make monitor
```

- [ ] **Step 4: Press KEYA on the watch and walk through each view**

Expected: rings render for each of overview / codex / claude / gemini. Serial log shows BLE scan → connect → read. Bridge log shows trigger write → snapshot updated.

- [ ] **Step 5: Capture a snapshot off the wire and decode it**

Open LightBlue (Mac App Store) → connect to `Stopwatch Bridge` → read `UsageSnapshot` → copy the hex.

```bash
.build/release/stopwatch-bridge decode-snapshot "<paste hex here>"
```

Expected: JSON dump matching what the rings show.

- [ ] **Step 6: No commit needed** unless the test surfaced a defect, in which case fix and commit normally.

### Task C.2: Failure-injection sanity checks

- [ ] **Step 1: Kill `codexbar serve` mid-test**

```bash
pkill -f "codexbar serve"
# Press KEYA on the watch a few times.
tail -f /tmp/stopwatch-bridge.log
```

Expected: bridge log shows backoff messages; watch shows cached rings (or `● stale` pill once the bridge sends a `stale` flag). Within ~30 s the supervisor restarts `codexbar serve` and fresh data flows again.

- [ ] **Step 2: Disable Mac Bluetooth temporarily**

System Settings → Bluetooth → off. Press KEYA on watch.

Expected: watch shows cached snapshot with `● no bridge` pill after the 3 s timeout. Re-enable Bluetooth and verify recovery.

- [ ] **Step 3: Uninstall CodexBar CLI to simulate missing source**

Temporarily move `codexbar` off PATH:

```bash
sudo mv /opt/homebrew/bin/codexbar /opt/homebrew/bin/codexbar.bak
launchctl kickstart -k "gui/$(id -u)/dev.stopwatch.bridge"
```

Expected: bridge log shows `codexbar binary not found on PATH`; watch shows gray rings + `● no source` pill (after `provider_missing` flag arrives). Restore the binary:

```bash
sudo mv /opt/homebrew/bin/codexbar.bak /opt/homebrew/bin/codexbar
```

- [ ] **Step 4: Verify pills appear in each failure mode**

Each of Steps 1–3 above should produce a visible pill: Step 1 → `● stale`, Step 2 → `● no bridge`, Step 3 → `● no source`. If any pill is missing or wrong, the bug is in `pillFor()` (Task B.11 Step 5) — fix and add a TDD test that decodes a fixture with the relevant flag set and asserts the pill string the renderer produces.

### Task C.3: README polish

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Replace `README.md` with the user-facing version**

```markdown
# CodexBar StopWatch

Brings CodexBar usage indicators for Codex / Claude Code / Gemini onto an M5Stack StopWatch over Bluetooth LE. No agent credentials live on the device.

## What you get

- Three concentric activity rings — outer Codex, middle Claude, inner Gemini — at a wrist-glance.
- Per-provider drill-down: session ring + week ring + reset countdown + credits.
- Wake-to-glance: press KEYA or KEYB to wake, ~1 s to first paint.

## Requirements

- macOS 14 (Sonoma) or newer.
- CodexBar.app (or the `codexbar` CLI) installed and signed into Codex / Claude Code / Gemini.
- Swift 6.2+ (Xcode 16.2+ or `swiftly`).
- PlatformIO Core (`pip install platformio` or `brew install platformio`).
- An M5Stack StopWatch.

## Install

```bash
git clone <this repo>
cd stopwatch-quiz
make build       # builds the Swift bridge in release mode
make install     # generates a random port, writes config, registers a launchd agent
```

The first launch will prompt for Bluetooth permission. Grant it in System Settings → Privacy & Security → Bluetooth.

## Flash the watch

Connect the watch over USB-C, then:

```bash
make flash
```

Power-cycle the watch. It will wake into the overview with cached or live data.

## Daily use

| Input | Action |
|---|---|
| KEYA short | Previous view |
| KEYB short | Next view |
| KEYA long  | Force refresh from Mac |
| KEYB long  | Sleep display now |
| 15 s idle  | Auto-sleep |

Status pills under the rings:

- `● no bridge` — the watch can't see the Mac (out of range, BT off, daemon not running)
- `● link error` — found the Mac but couldn't read the snapshot
- `● stale` — bridge has prior data but the latest CodexBar fetch failed
- `● no source` — the `codexbar` CLI isn't installed on the Mac

## Troubleshooting

- **Watch shows `● no bridge`:** `launchctl print "gui/$(id -u)/dev.stopwatch.bridge"` should say `state = running`. If not, `launchctl kickstart -k "gui/$(id -u)/dev.stopwatch.bridge"`.
- **Bridge logs `Bluetooth not powered on`:** turn Bluetooth on in System Settings, then kickstart the bridge.
- **Bridge logs `codexbar binary not found`:** install CodexBar.app and run its **Install CLI** action, or symlink the CLI manually (see CodexBar's docs).

## Layout

- `bridge/` — Swift Package, the macOS CLI daemon.
- `firmware/` — PlatformIO project, the watch firmware.
- `shared/` — wire-protocol single source of truth (UUIDs, byte layout, test fixtures).
- `scripts/` — install/flash convenience wrappers.
- `docs/superpowers/specs/` — design doc.
- `docs/superpowers/plans/` — implementation plan.

## Uninstall

```bash
launchctl bootout "gui/$(id -u)" "$HOME/Library/LaunchAgents/dev.stopwatch.bridge.plist"
rm "$HOME/Library/LaunchAgents/dev.stopwatch.bridge.plist"
rm -rf "$HOME/Library/Application Support/stopwatch-bridge"
```
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: user-facing README (install, flash, daily use, troubleshooting)"
```

---

## Self-review

Run through the spec section by section and verify the plan covers it:

1. **Spec §2 Goals**
   - Glance latency ≤ 1 s — covered by Task B.10 setup() (cached snapshot before BLE) + Task C.1 integration test.
   - Days of battery — covered by Task B.9 light-sleep + Task B.10 idle timer.
   - Zero secrets on device — architectural: no credentials are ever written from bridge to firmware.
   - No CodexBar changes — bridge only consumes `codexbar serve`; no Task touches the CodexBar repo.
   - Version safety — covered by Task B.2 `MajorVersionTooNew` test + Protocol.h major/minor bytes.

2. **Spec §3 Non-goals** — none of the plan tasks build touch / mic / speaker / IMU / AOD / BLE encryption / cost charts / extra providers / on-device config / OTA / multi-Mac / notifications. ✓

3. **Spec §5 Bridge component** — every file in §5.1 is created (Tasks A.1–A.10). Each command in §5.2 has a task: `run` (A.8), `install` (A.10), `pair` (A.9), `decode-snapshot` (A.9). Lifecycle steps from §5.3 are implemented across A.6 (supervisor), A.7 (peripheral), A.8 (service wires them).

4. **Spec §6 Firmware component** — every file in §6.1 is created (Tasks B.1–B.10). State machine matches §6.3. Wake-to-first-paint sequence matches §6.4 (cached load in setup, then BLE refresh).

5. **Spec §7 Wire protocol** — Task 0.2 writes PROTOCOL.md with the major/minor split. Task A.2 / B.2 mirror it in Swift / C++. Task A.3 / B.2 round-trip fixtures from `shared/fixtures/` on both sides — diff-checkable wire compat. ✓

6. **Spec §8 UX** — Task B.4 overview rings + center + legend. Task B.5 per-provider rings + bottom strap + countdown. Task B.6 button mapping matches §8.3 exactly. Status pills from §8.4 (`● no bridge`, `● link error`, `● stale`, `● no source`) — Task B.11 adds `LinkStatus` plus the `pillFor()` helper that selects which pill to render based on `LinkStatus` + snapshot flag bits.

7. **Spec §9 Error handling** — bridge-side: supervisor restart (A.6), stale flag set by BridgeService catch (A.8). Firmware-side: cache fallback (B.7 + B.10); `BleClient::fetch` retries once on `ReadFailed` per §9.2 (Task B.8). Major version detection covered (B.2).

8. **Spec §10 Repo layout** — Task 0.1 creates the top-level dirs; subsequent tasks populate exactly the files listed.

9. **Spec §11 Config files** — Task A.5 implements `Config` per §11.1 schema (codexbarPort, serviceUUID, logLevel, spawnCodexbar, instanceHash).

10. **Spec §12 Tests** — covered: SnapshotEncoder (A.3), CodexbarClient (A.4), Config (A.5), SnapshotCodec native (B.2), state machine native (B.10 + B.11). GATT/Renderer manual integration (C.1).

**Placeholder scan:** All UUIDs are marked `REPLACE-WITH-MINTED-…` at exactly three source-of-truth locations (`shared/PROTOCOL.md`, `Protocol.swift`, `Protocol.h`) — those are explicit placeholders the engineer fills in Task 0.2 Step 1. The `Power.h` GPIO numbers (`GPIO_NUM_0`, `GPIO_NUM_46`) are flagged in Task B.9 Step 2 as "placeholder; verify" — the engineer confirms from the M5Stack StopWatch schematic before the integration test in C.1. Every code step contains complete code.

**Type consistency:** spot-checked `Snapshot` / `ProviderSlot` between Swift and C++ — both use the same field names (`sessionPct`, `weekPct`, `sessionResetAt`, `weekResetAt`, with `creditsTimesTen` on C++ and `credits` on Swift since Swift's encoder handles the scaling to fixed-point). The 16-byte per-provider layout matches byte-for-byte, validated by the cross-side fixture round-trip.
