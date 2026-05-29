# CodexBar Spend & Burn Views — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Surface CodexBar `/cost` data (today/30-day spend, tokens, top model, 30-day burn history) on the watch via a new lazily-read `CostSnapshot` BLE characteristic, with a Total Spend & Burn screen, per-provider $ detail screens, and a spend teaser on the Codex/Claude ring screens.

**Architecture:** New GATT characteristic `CostSnapshot` (Read+Notify) carried independently of the untouched `UsageSnapshot`. Bridge gains a `/cost` client, a `CostEncoder`, and a `CostCache`; firmware gains a `CostCodec`, pure `CostFormat` helpers, `Views/Spend.cpp`, an expanded 7-entry `ViewId` carousel, an NVS cost cache, and a lazy cost read on entering a spend screen. Cross-language wire compatibility is locked by a shared golden hex fixture, exactly like the existing `UsageSnapshot` fixtures.

**Tech Stack:** Swift 6 + swift-testing (bridge), C++17 + PlatformIO/Unity native tests + M5Unified/NimBLE (firmware). Source of truth: `shared/PROTOCOL.md`.

**Reference spec:** `docs/superpowers/specs/2026-05-29-codexbar-spend-views-design.md`

---

## File structure

**Shared**
- Modify: `shared/PROTOCOL.md` — document `CostSnapshot` + trigger `0x04`.
- Create: `shared/fixtures/codexbar-cost-two.json` — raw `/cost` sample (client test input).
- Create: `shared/fixtures/codexbar-cost-two.hex` — golden `CostEncoder` output (both sides assert against it).

**Bridge (`bridge/Sources/StopwatchBridge/`)**
- Modify: `Protocol.swift` — cost constants + trigger scope.
- Create: `CostSnapshot.swift` — `CostFlags`, `NormalizedCost` model.
- Create: `CostEncoder.swift` — `NormalizedCost → Data`, stale/error helpers, model shortening.
- Create: `CostCache.swift` — last-good cost bytes, stale-on-failure (mirrors `SnapshotCache`).
- Modify: `CodexbarClient.swift` — `fetchCost`, `alignDailyHistory`, `topModel` helpers, raw `/cost` shapes.
- Modify: `GATTPeripheral.swift` — `costChar`, `updateCostSnapshot`, read/notify plumbing.
- Modify: `BridgeService.swift` — fetch `/cost` on refresh; handle scope `0x04`.

**Bridge tests (`bridge/Tests/StopwatchBridgeTests/`)**
- Create: `CostEncoderTests.swift`
- Create: `CostClientTests.swift`
- Modify: `Fixtures.swift` — add `NormalizedCost.costFixtureTwo`.

**Firmware (`firmware/src/`)**
- Modify: `Protocol.h` — cost constants + flags + trigger scope.
- Create: `CostCodec.{h,cpp}` — `decodeCostSnapshot`, `CostSnapshot`/`CostRecord`.
- Create: `CostFormat.{h,cpp}` — pure `formatDollars`, `humanizeTokens`.
- Create: `Views/Spend.{h,cpp}` — `drawTotalSpend`, `drawProviderCost`, `drawSpendTeaser`.
- Modify: `App.{h,cpp}` — expand `ViewId`, `nextView`/`prevView`, `isSpendView`.
- Modify: `Views/Provider.{h,cpp}` — teaser line; new `const CostRecord*` param.
- Modify: `SnapshotStore.{h,cpp}` — key parameter so a second "cost" slot can coexist.
- Modify: `BleClient.{h,cpp}` — `fetchCost` (generalized char read).
- Modify: `main.cpp` — render the new views, lazy cost fetch, cost cache load.

**Firmware tests (`firmware/test/`)**
- Create: `test_cost_codec/test_main.cpp`
- Create: `test_cost_format/test_main.cpp`
- Modify: `test_state_machine/test_main.cpp` — 7-entry cycle + `isSpendView`.

**Build/test commands used throughout**
- Bridge build: `cd bridge && swift build`
- Bridge tests: `cd bridge && swift test` (filter: `swift test --filter <SuiteName>`)
- Firmware native tests: `cd firmware && pio test -e native` (one folder: `pio test -e native -f test_cost_codec`)
- Firmware device compile (no upload): `cd firmware && pio run -e stopwatch`

---

## Task 1: Document the protocol (`shared/PROTOCOL.md`)

**Files:**
- Modify: `shared/PROTOCOL.md`

- [ ] **Step 1: Add the `CostSnapshot` characteristic row**

In the table under `## 2. Characteristics`, add a third row after the `RefreshTrigger` row:

```markdown
| `CostSnapshot` | `33FAAC2D-3935-467F-A0A0-899CE2306366` | Read + Notify | bridge → watch | Spend/burn data. Watch reads lazily on entering a spend screen; bridge notifies on change. Versioned independently of `UsageSnapshot`. |
```

- [ ] **Step 2: Add trigger value `0x04`**

In the `### 2.1 RefreshTrigger payload` value table, add a row before the "Any other value" line:

```markdown
| `0x04` | Cost only (re-fetch `/cost` for all providers) |
```

- [ ] **Step 3: Add the CostSnapshot payload section**

After `### 3.3 Versioning rules`, add:

````markdown
## 3A. `CostSnapshot` payload (binary)

Independent of `UsageSnapshot`; its own `(versionMajor, versionMinor)`. All integers little-endian. Size = `12 + 60 × recordCount`. Codex + Claude ⇒ **132 bytes**. Gemini has no `/cost` data and is omitted.

### 3A.1 Header (12 bytes)

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0 | uint8 | `versionMajor` | `0x01`. |
| 1 | uint8 | `versionMinor` | `0x00`. |
| 2 | uint8 | `recordCount` | Number of cost records (0–2 today). |
| 3 | uint8 | `flags` | bit0 stale, bit1 bridge_error, bit2 cost_unavailable. |
| 4 | uint32 | `capturedAt` | Unix seconds. |
| 8 | uint8 | `historyDays` | `30`. |
| 9 | uint8 | `reserved` | `0`. |
| 10 | uint16 | `historyUnitCents` | Shared scale: cents per history unit (≥1). |

### 3A.2 Per-record (60 bytes, repeated `recordCount` times)

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0 | uint8 | `providerID` | 1 = codex, 2 = claude. |
| 1 | uint8 | `reserved` | `0`. |
| 2 | uint32 | `todayCostCents` | `0xFFFFFFFF` = unknown. |
| 6 | uint32 | `monthCostCents` | `0xFFFFFFFF` = unknown. |
| 10 | uint32 | `todayTokens` | `0xFFFFFFFF` = unknown. |
| 14 | uint32 | `monthTokens` | `0xFFFFFFFF` = unknown. |
| 18 | char[12] | `topModel` | UTF-8, null-padded, vendor-prefix-stripped. |
| 30 | uint8[30] | `history` | Oldest→newest; index 29 = `capturedAt` day; `round(dayCents / historyUnitCents)`. |

History is normalized on one shared scale so the watch can sum providers for the combined burn chart.
````

- [ ] **Step 4: Commit**

```bash
git add shared/PROTOCOL.md
git commit -m "protocol: document CostSnapshot characteristic + trigger 0x04"
```

---

## Task 2: Bridge cost constants + model

**Files:**
- Modify: `bridge/Sources/StopwatchBridge/Protocol.swift`
- Create: `bridge/Sources/StopwatchBridge/CostSnapshot.swift`

- [ ] **Step 1: Add cost constants to `Protocol`**

In `Protocol.swift`, inside `public enum Protocol`, after the `snapshotSize` line, add:

```swift
    public static let costSnapshotUUID = CBUUID(string: "33FAAC2D-3935-467F-A0A0-899CE2306366")
    public static let costVersionMajor: UInt8 = 1
    public static let costVersionMinor: UInt8 = 0
    public static let costHeaderSize    = 12
    public static let costRecordSize    = 60
    public static let costHistoryDays   = 30
    public static let triggerScopeCost: UInt8 = 0x04
```

- [ ] **Step 2: Create the cost model**

Create `bridge/Sources/StopwatchBridge/CostSnapshot.swift`:

```swift
// bridge/Sources/StopwatchBridge/CostSnapshot.swift
import Foundation

public struct CostFlags: OptionSet, Sendable {
    public let rawValue: UInt8
    public init(rawValue: UInt8) { self.rawValue = rawValue }

    public static let stale           = CostFlags(rawValue: 0b0000_0001)
    public static let bridgeError     = CostFlags(rawValue: 0b0000_0010)
    public static let costUnavailable = CostFlags(rawValue: 0b0000_0100)
}

/// Normalized `/cost` input that `CostEncoder` consumes. `CodexbarClient.fetchCost`
/// produces this from the live `codexbar serve` `/cost` response.
public struct NormalizedCost: Equatable, Sendable {
    public struct Provider: Equatable, Sendable {
        public var providerID: ProviderID
        public var todayCostUSD: Double?      // nil → 0xFFFFFFFF on the wire
        public var monthCostUSD: Double?
        public var todayTokens: UInt64?
        public var monthTokens: UInt64?
        public var topModel: String?          // full name; encoder shortens to 12 chars
        public var history: [Double]          // dense, length 30, USD/day, index 29 = capturedAt day

        public init(providerID: ProviderID, todayCostUSD: Double?, monthCostUSD: Double?,
                    todayTokens: UInt64?, monthTokens: UInt64?, topModel: String?, history: [Double]) {
            self.providerID = providerID
            self.todayCostUSD = todayCostUSD
            self.monthCostUSD = monthCostUSD
            self.todayTokens = todayTokens
            self.monthTokens = monthTokens
            self.topModel = topModel
            self.history = history
        }
    }

    public var capturedAt: Date
    public var flags: CostFlags
    public var providers: [Provider]

    public init(capturedAt: Date, flags: CostFlags, providers: [Provider]) {
        self.capturedAt = capturedAt
        self.flags = flags
        self.providers = providers
    }
}
```

- [ ] **Step 3: Verify it builds**

Run: `cd bridge && swift build`
Expected: `Build complete!` (no errors).

- [ ] **Step 4: Commit**

```bash
git add bridge/Sources/StopwatchBridge/Protocol.swift bridge/Sources/StopwatchBridge/CostSnapshot.swift
git commit -m "bridge: add cost protocol constants + NormalizedCost model"
```

---

## Task 3: Bridge `CostEncoder`

**Files:**
- Create: `bridge/Sources/StopwatchBridge/CostEncoder.swift`
- Modify: `bridge/Tests/StopwatchBridgeTests/Fixtures.swift`
- Create: `bridge/Tests/StopwatchBridgeTests/CostEncoderTests.swift`

- [ ] **Step 1: Add the `costFixtureTwo` test fixture**

Append to `bridge/Tests/StopwatchBridgeTests/Fixtures.swift` (after the closing `}` of the `extension NormalizedUsage`):

```swift
extension NormalizedCost {
    /// Controlled round-number fixture for byte-exact encoder assertions.
    /// max day = $120 (codex) ⇒ historyUnitCents = ceil(12000/255) = 48.
    static var costFixtureTwo: NormalizedCost {
        var codexHist = [Double](repeating: 0, count: 30); codexHist[29] = 120.0
        var claudeHist = [Double](repeating: 0, count: 30); claudeHist[29] = 60.0
        return .init(
            capturedAt: Date(timeIntervalSince1970: 1748455822),
            flags: [],
            providers: [
                .init(providerID: .codex,  todayCostUSD: 12.0, monthCostUSD: 300.0,
                      todayTokens: 1_000_000, monthTokens: 100_000_000,
                      topModel: "gpt-5.5", history: codexHist),
                .init(providerID: .claude, todayCostUSD: 8.0,  monthCostUSD: 200.0,
                      todayTokens: 2_000_000, monthTokens: 50_000_000,
                      topModel: "claude-opus-4-7", history: claudeHist),
            ]
        )
    }
}
```

- [ ] **Step 2: Write the failing encoder test**

Create `bridge/Tests/StopwatchBridgeTests/CostEncoderTests.swift`:

```swift
import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct CostEncoderTests {

    @Test func encodesHeaderRecordsAndSharedScale() {
        let bytes = CostEncoder.encode(.costFixtureTwo)

        // Size = 12 + 60*2
        #expect(bytes.count == 132)
        // Header
        #expect(bytes[0] == Protocol.costVersionMajor)
        #expect(bytes[1] == Protocol.costVersionMinor)
        #expect(bytes[2] == 2)                  // recordCount
        #expect(bytes[3] == 0)                  // flags
        #expect(bytes[8] == 30)                 // historyDays
        #expect(bytes[9] == 0)                  // reserved
        #expect(bytes[10] == 48 && bytes[11] == 0)  // historyUnitCents = 48

        // Record 0 = codex at offset 12
        #expect(bytes[12] == ProviderID.codex.rawValue)
        #expect(bytes[14] == 0xB0 && bytes[15] == 0x04)  // todayCents 1200
        #expect(bytes[18] == 0x30 && bytes[19] == 0x75)  // monthCents 30000
        #expect(bytes[22] == 0x40 && bytes[23] == 0x42 && bytes[24] == 0x0F && bytes[25] == 0x00) // 1_000_000
        #expect(bytes[26] == 0x00 && bytes[27] == 0xE1 && bytes[28] == 0xF5 && bytes[29] == 0x05) // 100_000_000
        // model "gpt-5.5" at offset 30, null-padded
        #expect(Array(bytes[30..<37]) == Array("gpt-5.5".utf8))
        #expect(bytes[37] == 0)
        // history[29] at 12+30+29 = 71 ⇒ 12000/48 = 250; everything before it is 0
        #expect(bytes[71] == 250)
        #expect(bytes[42..<71].allSatisfy { $0 == 0 })

        // Record 1 = claude at offset 72; model shortened to "opus-4-7"
        #expect(bytes[72] == ProviderID.claude.rawValue)
        #expect(Array(bytes[90..<98]) == Array("opus-4-7".utf8))
        #expect(bytes[131] == 125)               // 6000/48 = 125
    }

    @Test func unknownsEncodeAsSentinels() {
        let cost = NormalizedCost(
            capturedAt: Date(timeIntervalSince1970: 0), flags: [.stale],
            providers: [.init(providerID: .codex, todayCostUSD: nil, monthCostUSD: nil,
                              todayTokens: nil, monthTokens: nil, topModel: nil,
                              history: [Double](repeating: 0, count: 30))])
        let bytes = CostEncoder.encode(cost)
        #expect(bytes[3] == CostFlags.stale.rawValue)
        // todayCents..monthTokens all 0xFFFFFFFF
        #expect(bytes[14..<30].allSatisfy { $0 == 0xFF })
        // topModel all-zero
        #expect(bytes[30..<42].allSatisfy { $0 == 0 })
    }
}
```

- [ ] **Step 3: Run it to confirm it fails**

Run: `cd bridge && swift test --filter CostEncoderTests`
Expected: FAIL — compile error, `cannot find 'CostEncoder' in scope`.

- [ ] **Step 4: Implement `CostEncoder`**

Create `bridge/Sources/StopwatchBridge/CostEncoder.swift`:

```swift
// bridge/Sources/StopwatchBridge/CostEncoder.swift
import Foundation

public enum CostEncoder {

    /// Encodes normalized cost into the `12 + 60*N` byte CostSnapshot wire format.
    /// See `shared/PROTOCOL.md §3A`.
    public static func encode(_ cost: NormalizedCost) -> Data {
        let unitCents = sharedUnitCents(cost.providers)

        var out = Data(capacity: Protocol.costHeaderSize + Protocol.costRecordSize * cost.providers.count)
        out.append(Protocol.costVersionMajor)
        out.append(Protocol.costVersionMinor)
        precondition(cost.providers.count <= 255, "too many cost records")
        out.append(UInt8(cost.providers.count))
        out.append(cost.flags.rawValue)
        appendU32(&out, UInt32(max(0, cost.capturedAt.timeIntervalSince1970)))
        out.append(UInt8(Protocol.costHistoryDays))
        out.append(0)  // reserved
        appendU16(&out, unitCents)

        for p in cost.providers {
            out.append(p.providerID.rawValue)
            out.append(0)  // reserved
            appendU32(&out, centsOrUnknown(p.todayCostUSD))
            appendU32(&out, centsOrUnknown(p.monthCostUSD))
            appendU32(&out, tokensOrUnknown(p.todayTokens))
            appendU32(&out, tokensOrUnknown(p.monthTokens))
            appendModel(&out, p.topModel)
            appendHistory(&out, p.history, unitCents: unitCents)
        }
        return out
    }

    public static func staleEmpty() -> Data {
        encode(NormalizedCost(capturedAt: Date(timeIntervalSince1970: 0),
                              flags: [.stale, .costUnavailable], providers: []))
    }

    public static func errorEmpty(capturedAt: Date = Date()) -> Data {
        encode(NormalizedCost(capturedAt: capturedAt,
                              flags: [.stale, .bridgeError], providers: []))
    }

    /// Sets stale+bridgeError flags and refreshes capturedAt on an already-encoded snapshot.
    public static func markStale(_ snapshot: Data, capturedAt: Date) -> Data {
        guard snapshot.count >= Protocol.costHeaderSize else { return snapshot }
        var out = snapshot
        out[3] |= CostFlags.stale.rawValue | CostFlags.bridgeError.rawValue
        writeU32(&out, UInt32(max(0, capturedAt.timeIntervalSince1970)), at: 4)
        return out
    }

    /// Strips a known vendor prefix, then truncates to 11 chars (+ null = 12).
    static func shortenModel(_ name: String) -> String {
        var s = name
        for prefix in ["claude-", "openai-", "google-", "gemini-"] {
            if s.hasPrefix(prefix) { s.removeFirst(prefix.count); break }
        }
        return String(s.prefix(11))
    }

    // MARK: - helpers

    private static func sharedUnitCents(_ providers: [NormalizedCost.Provider]) -> UInt16 {
        var maxCents = 0
        for p in providers {
            for usd in p.history {
                maxCents = max(maxCents, Int((usd * 100).rounded()))
            }
        }
        if maxCents <= 0 { return 1 }
        let unit = (maxCents + 254) / 255   // ceil(maxCents / 255)
        return UInt16(min(max(unit, 1), 65535))
    }

    private static func centsOrUnknown(_ usd: Double?) -> UInt32 {
        guard let usd else { return 0xFFFF_FFFF }
        let cents = (usd * 100).rounded()
        if cents < 0 { return 0 }
        if cents >= Double(UInt32.max) { return 0xFFFF_FFFE }
        return UInt32(cents)
    }

    private static func tokensOrUnknown(_ tokens: UInt64?) -> UInt32 {
        guard let tokens else { return 0xFFFF_FFFF }
        return tokens >= UInt64(UInt32.max) ? 0xFFFF_FFFE : UInt32(tokens)
    }

    private static func appendModel(_ out: inout Data, _ name: String?) {
        var field = [UInt8](repeating: 0, count: 12)
        if let name {
            let bytes = Array(shortenModel(name).utf8.prefix(11))
            for (i, b) in bytes.enumerated() { field[i] = b }
        }
        out.append(contentsOf: field)
    }

    private static func appendHistory(_ out: inout Data, _ history: [Double], unitCents: UInt16) {
        let unit = Double(unitCents)
        for i in 0..<Protocol.costHistoryDays {
            let usd = i < history.count ? history[i] : 0
            let units = (Double(Int((usd * 100).rounded())) / unit).rounded()
            out.append(UInt8(min(max(units, 0), 255)))
        }
    }

    private static func appendU16(_ out: inout Data, _ v: UInt16) {
        out.append(UInt8(v & 0xFF)); out.append(UInt8((v >> 8) & 0xFF))
    }
    private static func appendU32(_ out: inout Data, _ v: UInt32) {
        out.append(UInt8(v & 0xFF)); out.append(UInt8((v >> 8) & 0xFF))
        out.append(UInt8((v >> 16) & 0xFF)); out.append(UInt8((v >> 24) & 0xFF))
    }
    private static func writeU32(_ out: inout Data, _ v: UInt32, at offset: Int) {
        out[offset] = UInt8(v & 0xFF); out[offset + 1] = UInt8((v >> 8) & 0xFF)
        out[offset + 2] = UInt8((v >> 16) & 0xFF); out[offset + 3] = UInt8((v >> 24) & 0xFF)
    }
}
```

- [ ] **Step 5: Run the test to confirm it passes**

Run: `cd bridge && swift test --filter CostEncoderTests`
Expected: PASS (2 tests).

- [ ] **Step 6: Commit**

```bash
git add bridge/Sources/StopwatchBridge/CostEncoder.swift bridge/Tests/StopwatchBridgeTests/CostEncoderTests.swift bridge/Tests/StopwatchBridgeTests/Fixtures.swift
git commit -m "bridge: add CostEncoder with byte-exact tests"
```

---

## Task 4: Golden hex fixture + cross-language regression test

**Files:**
- Create: `shared/fixtures/codexbar-cost-two.hex`
- Modify: `bridge/Tests/StopwatchBridgeTests/CostEncoderTests.swift`

- [ ] **Step 1: Add a temporary emit test**

Add to `CostEncoderTests` (inside the suite):

```swift
    @Test func emitGoldenHex() {
        let hex = CostEncoder.encode(.costFixtureTwo).map { String(format: "%02x", $0) }.joined()
        print("COSTHEX \(hex)")
    }
```

- [ ] **Step 2: Generate the golden hex file**

Run (from repo root):

```bash
cd bridge && swift test --filter CostEncoderTests/emitGoldenHex 2>&1 \
  | sed -n 's/.*COSTHEX \([0-9a-f]*\).*/\1/p' | tr -d '\n' > ../shared/fixtures/codexbar-cost-two.hex
cd ..
wc -c shared/fixtures/codexbar-cost-two.hex
```
Expected: `264 shared/fixtures/codexbar-cost-two.hex` (132 bytes × 2 hex chars).

- [ ] **Step 3: Replace the emit test with a regression assertion**

Delete the `emitGoldenHex` test and add instead:

```swift
    @Test func costFixtureMatchesSavedHex() throws {
        let expected = try Fixtures.loadHex("codexbar-cost-two")
        #expect(CostEncoder.encode(.costFixtureTwo) == expected)
    }
```

- [ ] **Step 4: Run it**

Run: `cd bridge && swift test --filter CostEncoderTests`
Expected: PASS (3 tests; `emitGoldenHex` gone).

- [ ] **Step 5: Commit**

```bash
git add shared/fixtures/codexbar-cost-two.hex bridge/Tests/StopwatchBridgeTests/CostEncoderTests.swift
git commit -m "bridge: freeze golden CostSnapshot hex fixture"
```

---

## Task 5: Bridge `/cost` parse helpers (history alignment + top model)

**Files:**
- Modify: `bridge/Sources/StopwatchBridge/CodexbarClient.swift`
- Create: `bridge/Tests/StopwatchBridgeTests/CostClientTests.swift`

- [ ] **Step 1: Write failing helper tests**

Create `bridge/Tests/StopwatchBridgeTests/CostClientTests.swift`:

```swift
import Foundation
import Testing
@testable import StopwatchBridge

// Pure helpers only — no URLProtocol stub here, so this suite can run in
// parallel with others safely. Stub-based fetchCost tests live in
// CodexbarClientTests (the serialized suite that owns StubURLProtocol).
@Suite struct CostClientTests {

    private func utcDate(_ ymd: String) -> Date {
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.timeZone = TimeZone(identifier: "UTC")
        f.dateFormat = "yyyy-MM-dd"
        return f.date(from: ymd)!
    }

    @Test func alignsDailyHistoryByDateOffset() {
        let daily = [("2026-05-29", 120.0), ("2026-05-27", 10.0), ("2026-04-01", 999.0)]
        let hist = CodexbarClient.alignDailyHistory(daily, anchor: utcDate("2026-05-29"), days: 30)
        #expect(hist.count == 30)
        #expect(hist[29] == 120.0)   // today
        #expect(hist[27] == 10.0)    // 2 days ago
        #expect(hist[0...26].allSatisfy { $0 == 0 })
        #expect(hist[28] == 0)
        // 2026-04-01 is > 29 days back ⇒ dropped, not summed anywhere
        #expect(hist.reduce(0, +) == 130.0)
    }

    @Test func picksHighestCostModel() {
        let model = CodexbarClient.topModel(from: [
            ["gpt-5.5": 10.0, "gpt-4": 1.0],
            ["gpt-5.5": 5.0],
        ])
        #expect(model == "gpt-5.5")
    }
}
```

- [ ] **Step 2: Run to confirm failure**

Run: `cd bridge && swift test --filter CostClientTests`
Expected: FAIL — `type 'CodexbarClient' has no member 'alignDailyHistory'`.

- [ ] **Step 3: Implement the helpers**

In `CodexbarClient.swift`, add these `internal static` methods inside `public actor CodexbarClient` (e.g. just before `private let port`):

```swift
    /// Buckets sparse daily (date string, USD) into a dense `days`-length array,
    /// index `days-1` = anchor's UTC day, older days toward index 0. Out-of-window dropped.
    static func alignDailyHistory(_ daily: [(date: String, costUSD: Double)],
                                  anchor: Date, days: Int = 30) -> [Double] {
        var out = [Double](repeating: 0, count: days)
        var cal = Calendar(identifier: .gregorian)
        cal.timeZone = TimeZone(identifier: "UTC")!
        let fmt = DateFormatter()
        fmt.locale = Locale(identifier: "en_US_POSIX")
        fmt.timeZone = TimeZone(identifier: "UTC")!
        fmt.dateFormat = "yyyy-MM-dd"
        let today = cal.startOfDay(for: anchor)
        for entry in daily {
            guard let d = fmt.date(from: entry.date) else { continue }
            let offset = cal.dateComponents([.day], from: cal.startOfDay(for: d), to: today).day ?? -1
            if offset >= 0 && offset < days { out[days - 1 - offset] += entry.costUSD }
        }
        return out
    }

    /// Sums cost per model across daily breakdowns, returns the highest-cost model name.
    static func topModel(from breakdowns: [[String: Double]]) -> String? {
        var totals: [String: Double] = [:]
        for day in breakdowns {
            for (name, cost) in day { totals[name, default: 0] += cost }
        }
        return totals.max { $0.value < $1.value }?.key
    }
```

- [ ] **Step 4: Run the test to confirm it passes**

Run: `cd bridge && swift test --filter CostClientTests`
Expected: PASS (2 tests).

- [ ] **Step 5: Commit**

```bash
git add bridge/Sources/StopwatchBridge/CodexbarClient.swift bridge/Tests/StopwatchBridgeTests/CostClientTests.swift
git commit -m "bridge: add cost history alignment + top-model helpers"
```

---

## Task 6: Bridge `CodexbarClient.fetchCost`

**Files:**
- Modify: `bridge/Sources/StopwatchBridge/CodexbarClient.swift`
- Create: `shared/fixtures/codexbar-cost-two.json`
- Modify: `bridge/Tests/StopwatchBridgeTests/CostClientTests.swift`

- [ ] **Step 1: Create the raw `/cost` JSON fixture**

Create `shared/fixtures/codexbar-cost-two.json`:

```json
[
  {
    "provider": "codex",
    "sessionCostUSD": 12.0,
    "sessionTokens": 1000000,
    "last30DaysCostUSD": 300.0,
    "last30DaysTokens": 100000000,
    "daily": [
      { "date": "2026-05-29", "totalCost": 120.0, "modelBreakdowns": [ { "modelName": "gpt-5.5", "cost": 120.0 } ] },
      { "date": "2026-05-27", "totalCost": 10.0,  "modelBreakdowns": [ { "modelName": "gpt-5.5", "cost": 10.0 } ] }
    ]
  },
  {
    "provider": "claude",
    "sessionCostUSD": 8.0,
    "sessionTokens": 2000000,
    "last30DaysCostUSD": 200.0,
    "last30DaysTokens": 50000000,
    "daily": [
      { "date": "2026-05-29", "totalCost": 60.0, "modelBreakdowns": [ { "modelName": "claude-opus-4-7", "cost": 60.0 } ] }
    ]
  }
]
```

- [ ] **Step 2: Write the failing `fetchCost` test**

These use the shared `StubURLProtocol`, so add them to the existing **serialized** `CodexbarClientTests` suite (not `CostClientTests`) — swift-testing runs suites in parallel, and two suites mutating the same `StubURLProtocol.stub` would race. In `bridge/Tests/StopwatchBridgeTests/CodexbarClientTests.swift`, add a date helper plus two tests inside the `struct CodexbarClientTests` body, right before the `// MARK: - URLProtocol stub plumbing` line (reusing the existing `stubSession()` helper):

```swift
    private func utcDate(_ ymd: String) -> Date {
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.timeZone = TimeZone(identifier: "UTC")
        f.dateFormat = "yyyy-MM-dd"
        return f.date(from: ymd)!
    }

    @Test func parsesCostFixture() async throws {
        let json = try Fixtures.loadJSON("codexbar-cost-two")
        StubURLProtocol.stub = .init(status: 200, body: json)

        let client = CodexbarClient(port: 51111, session: stubSession())
        let cost = try await client.fetchCost(scope: .all, now: utcDate("2026-05-29"))

        #expect(cost.providers.count == 2)
        let codex = cost.providers[0]
        #expect(codex.providerID == .codex)
        #expect(codex.todayCostUSD == 12.0)
        #expect(codex.monthCostUSD == 300.0)
        #expect(codex.todayTokens == 1_000_000)
        #expect(codex.monthTokens == 100_000_000)
        #expect(codex.topModel == "gpt-5.5")
        #expect(codex.history.count == 30)
        #expect(codex.history[29] == 120.0)
        #expect(codex.history[27] == 10.0)
        #expect(cost.providers[1].topModel == "claude-opus-4-7")  // full name; encoder shortens
        #expect(cost.providers[1].history[29] == 60.0)
    }

    @Test func emptyCostArraySetsUnavailable() async throws {
        StubURLProtocol.stub = .init(status: 200, body: Data("[]".utf8))
        let client = CodexbarClient(port: 51111, session: stubSession())
        let cost = try await client.fetchCost(scope: .all, now: utcDate("2026-05-29"))
        #expect(cost.providers.isEmpty)
        #expect(cost.flags.contains(.costUnavailable))
    }
```

- [ ] **Step 3: Run to confirm failure**

Run: `cd bridge && swift test --filter CodexbarClientTests`
Expected: FAIL — `value of type 'CodexbarClient' has no member 'fetchCost'`.

- [ ] **Step 4: Implement `fetchCost` + raw shapes**

In `CodexbarClient.swift`, add the public method inside the actor (after `fetch`):

```swift
    public func fetchCost(scope: Scope = .all, now: Date = Date()) async throws -> NormalizedCost {
        var components = URLComponents()
        components.scheme = "http"
        components.host   = "127.0.0.1"
        components.port   = Int(port)
        components.path   = "/cost"
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
            return try decodeCost(data, now: now)
        } catch {
            throw FetchError.decode(error)
        }
    }

    private func decodeCost(_ data: Data, now: Date) throws -> NormalizedCost {
        let raw = try JSONDecoder().decode([RawCost].self, from: data)

        let providers = raw.compactMap { c -> NormalizedCost.Provider? in
            guard let id = ProviderID(fromString: c.provider) else { return nil }
            let dailyPairs = (c.daily ?? []).map { (date: $0.date, costUSD: $0.totalCost ?? 0) }
            let breakdowns = (c.daily ?? []).map { day -> [String: Double] in
                var m: [String: Double] = [:]
                for b in day.modelBreakdowns ?? [] { m[b.modelName, default: 0] += b.cost ?? 0 }
                return m
            }
            return .init(
                providerID:   id,
                todayCostUSD: c.sessionCostUSD,
                monthCostUSD: c.last30DaysCostUSD,
                todayTokens:  c.sessionTokens.map { UInt64(max(0, $0.rounded())) },
                monthTokens:  c.last30DaysTokens.map { UInt64(max(0, $0.rounded())) },
                topModel:     Self.topModel(from: breakdowns),
                history:      Self.alignDailyHistory(dailyPairs, anchor: now,
                                                     days: Protocol.costHistoryDays)
            )
        }

        var flags: CostFlags = []
        if providers.isEmpty { flags.insert(.costUnavailable) }
        return .init(capturedAt: now, flags: flags, providers: providers)
    }

    private struct RawCost: Decodable {
        var provider: String
        var sessionCostUSD: Double?
        var sessionTokens: Double?
        var last30DaysCostUSD: Double?
        var last30DaysTokens: Double?
        var daily: [Daily]?

        struct Daily: Decodable {
            var date: String
            var totalCost: Double?
            var modelBreakdowns: [ModelBreakdown]?
            struct ModelBreakdown: Decodable { var modelName: String; var cost: Double? }
        }
    }
```

- [ ] **Step 5: Run the test to confirm it passes**

Run: `cd bridge && swift test --filter CodexbarClientTests`
Expected: PASS (7 tests — the original 5 plus the 2 new cost tests).

- [ ] **Step 6: Commit**

```bash
git add bridge/Sources/StopwatchBridge/CodexbarClient.swift shared/fixtures/codexbar-cost-two.json bridge/Tests/StopwatchBridgeTests/CodexbarClientTests.swift
git commit -m "bridge: add CodexbarClient.fetchCost parsing /cost"
```

---

## Task 7: Bridge `CostCache`

**Files:**
- Create: `bridge/Sources/StopwatchBridge/CostCache.swift`
- Modify: `bridge/Tests/StopwatchBridgeTests/CostEncoderTests.swift`

- [ ] **Step 1: Write the failing cache test**

Add to `CostEncoderTests`:

```swift
    @Test func costCacheKeepsLastGoodOnFailure() {
        var cache = CostCache()
        let good = cache.recordSuccess(.costFixtureTwo)
        #expect(good[3] == 0)  // no flags

        let failed = cache.recordFailure(capturedAt: Date(timeIntervalSince1970: 1_748_500_000))
        // last-good record bytes preserved, stale+bridgeError set
        #expect(Array(failed[Protocol.costHeaderSize...]) == Array(good[Protocol.costHeaderSize...]))
        #expect((failed[3] & CostFlags.stale.rawValue) != 0)
        #expect((failed[3] & CostFlags.bridgeError.rawValue) != 0)
    }

    @Test func costCacheErrorEmptyBeforeFirstSuccess() {
        var cache = CostCache()
        let failed = cache.recordFailure()
        #expect(failed.count == Protocol.costHeaderSize)  // recordCount 0
        #expect((failed[3] & CostFlags.bridgeError.rawValue) != 0)
    }
```

- [ ] **Step 2: Run to confirm failure**

Run: `cd bridge && swift test --filter CostEncoderTests`
Expected: FAIL — `cannot find 'CostCache' in scope`.

- [ ] **Step 3: Implement `CostCache`**

Create `bridge/Sources/StopwatchBridge/CostCache.swift`:

```swift
// bridge/Sources/StopwatchBridge/CostCache.swift
import Foundation

struct CostCache {
    private var lastGood: Data?

    mutating func recordSuccess(_ cost: NormalizedCost) -> Data {
        let snapshot = CostEncoder.encode(cost)
        if !cost.providers.isEmpty && !cost.flags.contains(.bridgeError) {
            lastGood = snapshot
            return snapshot
        }
        return lastGood.map { CostEncoder.markStale($0, capturedAt: cost.capturedAt) } ?? snapshot
    }

    mutating func recordFailure(capturedAt: Date = Date()) -> Data {
        if let lastGood {
            return CostEncoder.markStale(lastGood, capturedAt: capturedAt)
        }
        return CostEncoder.errorEmpty(capturedAt: capturedAt)
    }
}
```

- [ ] **Step 4: Run the test to confirm it passes**

Run: `cd bridge && swift test --filter CostEncoderTests`
Expected: PASS (5 tests).

- [ ] **Step 5: Commit**

```bash
git add bridge/Sources/StopwatchBridge/CostCache.swift bridge/Tests/StopwatchBridgeTests/CostEncoderTests.swift
git commit -m "bridge: add CostCache (last-good + stale-on-failure)"
```

---

## Task 8: Bridge GATT — `CostSnapshot` characteristic

**Files:**
- Modify: `bridge/Sources/StopwatchBridge/GATTPeripheral.swift`

> No new test: the CoreBluetooth surface is integration-tested via `pair`, like the existing snapshot characteristic. Verify with a build.

- [ ] **Step 1: Add the cost characteristic + state**

In `GATTPeripheral.swift`, add a stored property after `triggerChar`:

```swift
    private let costChar:     CBMutableCharacteristic
```

In `init()`, after the `triggerChar` assignment and before building `svc`, add:

```swift
        self.costChar = CBMutableCharacteristic(
            type: Protocol.costSnapshotUUID,
            properties: [.read, .notify],
            value: nil,
            permissions: [.readable]
        )
```

Change the service characteristics line to include it:

```swift
        svc.characteristics = [self.snapshotChar, self.triggerChar, self.costChar]
```

Add cost state after `pendingNotify`:

```swift
    private var currentCost: Data = CostEncoder.staleEmpty()
    private var pendingCostNotify: Data?
```

- [ ] **Step 2: Add `updateCostSnapshot`**

After `updateSnapshot(_:)`, add:

```swift
    public func updateCostSnapshot(_ data: Data) {
        precondition(data.count >= Protocol.costHeaderSize, "cost snapshot too short")
        currentCost = data
        if !manager.updateValue(data, for: costChar, onSubscribedCentrals: nil) {
            pendingCostNotify = data
        } else {
            pendingCostNotify = nil
        }
    }
```

- [ ] **Step 3: Serve cost reads**

Replace the body of `handleRead(peripheral:request:)` with a UUID-dispatched version:

```swift
    private func handleRead(peripheral: CBPeripheralManager, request: CBATTRequest) {
        let source: Data
        switch request.characteristic.uuid {
        case Protocol.snapshotUUID:     source = currentSnapshot
        case Protocol.costSnapshotUUID: source = currentCost
        default:
            peripheral.respond(to: request, withResult: .attributeNotFound)
            return
        }
        if request.offset >= source.count {
            peripheral.respond(to: request, withResult: .invalidOffset)
            return
        }
        request.value = source.subdata(in: request.offset..<source.count)
        peripheral.respond(to: request, withResult: .success)
    }
```

- [ ] **Step 4: Flush the cost notify queue too**

Replace `flushPendingNotify(peripheral:)` with:

```swift
    private func flushPendingNotify(peripheral: CBPeripheralManager) {
        if let pending = pendingNotify,
           peripheral.updateValue(pending, for: snapshotChar, onSubscribedCentrals: nil) {
            pendingNotify = nil
        }
        if let pendingCost = pendingCostNotify,
           peripheral.updateValue(pendingCost, for: costChar, onSubscribedCentrals: nil) {
            pendingCostNotify = nil
        }
    }
```

- [ ] **Step 5: Verify it builds**

Run: `cd bridge && swift build`
Expected: `Build complete!`

- [ ] **Step 6: Commit**

```bash
git add bridge/Sources/StopwatchBridge/GATTPeripheral.swift
git commit -m "bridge: serve CostSnapshot characteristic (read + notify)"
```

---

## Task 9: Bridge — fetch `/cost` on refresh

**Files:**
- Modify: `bridge/Sources/StopwatchBridge/BridgeService.swift`

> Verified by build + manual `pair`. The fetch/encode paths are already unit-tested in Tasks 3–7.

- [ ] **Step 1: Add the cost cache property**

After `private var snapshotCache = SnapshotCache()` add:

```swift
    private var costCache = CostCache()
```

- [ ] **Step 2: Branch refresh on the cost scope and also refresh cost on usage refresh**

Replace `handleRefresh(scope:)` with:

```swift
    fileprivate func handleRefresh(scope: UInt8) async {
        if scope == Protocol.triggerScopeCost {
            await handleCostRefresh()
            return
        }
        let s = CodexbarClient.Scope(rawByte: scope)
        let started = Date()
        do {
            let usage = try await client.fetch(scope: s)
            let bytes = snapshotCache.recordSuccess(usage)
            await peripheral.updateSnapshot(bytes)
            let elapsed = Date().timeIntervalSince(started)
            FileHandle.standardOutput.write(Data(String(format: "fetch ok: scope=%d providers=%d %.1fs\n",
                                                        Int(scope), usage.providers.count, elapsed).utf8))
        } catch {
            FileHandle.standardError.write(Data("fetch failed: \(error)\n".utf8))
            await peripheral.updateSnapshot(snapshotCache.recordFailure())
        }
        // Refresh cost on the same trigger so the watch's lazy CostSnapshot read is fresh.
        await handleCostRefresh()
    }

    private func handleCostRefresh() async {
        do {
            let cost = try await client.fetchCost(scope: .all)
            await peripheral.updateCostSnapshot(costCache.recordSuccess(cost))
            FileHandle.standardOutput.write(Data("cost ok: providers=\(cost.providers.count)\n".utf8))
        } catch {
            FileHandle.standardError.write(Data("cost fetch failed: \(error)\n".utf8))
            await peripheral.updateCostSnapshot(costCache.recordFailure())
        }
    }
```

- [ ] **Step 3: Verify it builds and all bridge tests pass**

Run: `cd bridge && swift build && swift test`
Expected: `Build complete!` then all suites pass (including `CostEncoderTests`, `CostClientTests`, and the existing `SnapshotEncoderTests` / `CodexbarClientTests`).

- [ ] **Step 4: Commit**

```bash
git add bridge/Sources/StopwatchBridge/BridgeService.swift
git commit -m "bridge: fetch /cost on refresh and on trigger scope 0x04"
```

---

## Task 10: Firmware cost protocol constants

**Files:**
- Modify: `firmware/src/Protocol.h`

- [ ] **Step 1: Add cost constants**

In `firmware/src/Protocol.h`, before the closing `}  // namespace stopwatch`, add:

```cpp
constexpr const char *kCostSnapshotUUID = "33FAAC2D-3935-467F-A0A0-899CE2306366";

constexpr uint8_t  kCostVersionMajor    = 1;
constexpr uint8_t  kCostHeaderSize      = 12;
constexpr uint8_t  kCostRecordSize      = 60;
constexpr uint8_t  kCostHistoryDays     = 30;
constexpr uint8_t  kCostMaxRecords      = 2;   // codex, claude
constexpr uint16_t kCostSnapshotMaxSize = kCostHeaderSize + kCostRecordSize * kCostMaxRecords;  // 132

constexpr uint8_t kCostFlagStale       = 0b00000001;
constexpr uint8_t kCostFlagBridgeError = 0b00000010;
constexpr uint8_t kCostFlagUnavailable = 0b00000100;

constexpr uint8_t kTriggerScopeCost    = 0x04;
```

- [ ] **Step 2: Verify the device target still compiles**

Run: `cd firmware && pio run -e stopwatch`
Expected: `SUCCESS`.

- [ ] **Step 3: Commit**

```bash
git add firmware/src/Protocol.h
git commit -m "firmware: add cost protocol constants"
```

---

## Task 11: Firmware `CostCodec` (decode + native test on golden hex)

**Files:**
- Create: `firmware/src/CostCodec.h`
- Create: `firmware/src/CostCodec.cpp`
- Create: `firmware/test/test_cost_codec/test_main.cpp`

- [ ] **Step 1: Write the failing native test**

Create `firmware/test/test_cost_codec/test_main.cpp`:

```cpp
#include <unity.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <fstream>
#include <cctype>
#include "../../src/CostCodec.h"

using namespace stopwatch;

static std::vector<uint8_t> readHexFixture(const char *name) {
    std::string path = std::string("../shared/fixtures/") + name + ".hex";
    std::ifstream f(path);
    if (!f) { char b[256]; snprintf(b, sizeof(b), "missing fixture: %s", path.c_str()); TEST_FAIL_MESSAGE(b); }
    std::string raw((std::istreambuf_iterator<char>(f)), {});
    std::string hex;
    for (char c : raw) if (!isspace((unsigned char)c)) hex.push_back(c);
    if (hex.size() % 2 != 0) TEST_FAIL_MESSAGE("hex fixture has odd length");
    std::vector<uint8_t> out;
    for (size_t i = 0; i < hex.size(); i += 2) { unsigned v = 0; sscanf(hex.c_str()+i, "%2x", &v); out.push_back((uint8_t)v); }
    return out;
}

void test_costFixtureDecodes(void) {
    auto bytes = readHexFixture("codexbar-cost-two");
    TEST_ASSERT_EQUAL(132, bytes.size());

    CostSnapshot cs;
    auto rc = decodeCostSnapshot(bytes.data(), bytes.size(), cs);
    TEST_ASSERT_EQUAL((int)CostDecodeResult::Ok, (int)rc);
    TEST_ASSERT_EQUAL(1, cs.versionMajor);
    TEST_ASSERT_EQUAL(2, cs.recordCount);
    TEST_ASSERT_EQUAL(30, cs.historyDays);
    TEST_ASSERT_EQUAL(48, cs.historyUnitCents);

    const CostRecord *codex = cs.find(ProviderID::Codex);
    TEST_ASSERT_NOT_NULL(codex);
    TEST_ASSERT_TRUE(codex->todayCents.has_value());
    TEST_ASSERT_EQUAL(1200, codex->todayCents.value());
    TEST_ASSERT_EQUAL(30000, codex->monthCents.value());
    TEST_ASSERT_EQUAL(1000000, codex->todayTokens.value());
    TEST_ASSERT_EQUAL(100000000, codex->monthTokens.value());
    TEST_ASSERT_EQUAL_STRING("gpt-5.5", codex->topModel);
    TEST_ASSERT_EQUAL(250, codex->history[29]);

    const CostRecord *claude = cs.find(ProviderID::Claude);
    TEST_ASSERT_NOT_NULL(claude);
    TEST_ASSERT_EQUAL_STRING("opus-4-7", claude->topModel);
    TEST_ASSERT_EQUAL(125, claude->history[29]);

    TEST_ASSERT_NULL(cs.find(ProviderID::Gemini));
}

void test_unknownCentsBecomeNullopt(void) {
    // header recordCount=1 + one record with 0xFFFFFFFF cost fields
    std::vector<uint8_t> b(kCostHeaderSize + kCostRecordSize, 0);
    b[0] = 1; b[2] = 1; b[8] = 30; b[10] = 1;     // major, count, historyDays, unit
    b[kCostHeaderSize + 0] = (uint8_t)ProviderID::Codex;
    for (int i = 2; i < 2 + 16; ++i) b[kCostHeaderSize + i] = 0xFF;  // today/month cents+tokens
    CostSnapshot cs;
    TEST_ASSERT_EQUAL((int)CostDecodeResult::Ok, (int)decodeCostSnapshot(b.data(), b.size(), cs));
    const CostRecord *r = cs.find(ProviderID::Codex);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_FALSE(r->todayCents.has_value());
    TEST_ASSERT_FALSE(r->monthTokens.has_value());
}

void test_futureMajorRejected(void) {
    uint8_t b[kCostHeaderSize] = { 99, 0, 0, 0, 0, 0, 0, 0, 30, 0, 1, 0 };
    CostSnapshot cs;
    TEST_ASSERT_EQUAL((int)CostDecodeResult::MajorVersionTooNew, (int)decodeCostSnapshot(b, sizeof(b), cs));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_costFixtureDecodes);
    RUN_TEST(test_unknownCentsBecomeNullopt);
    RUN_TEST(test_futureMajorRejected);
    return UNITY_END();
}
```

- [ ] **Step 2: Run to confirm failure**

Run: `cd firmware && pio test -e native -f test_cost_codec`
Expected: FAIL — `CostCodec.h: No such file or directory`.

- [ ] **Step 3: Create `CostCodec.h`**

Create `firmware/src/CostCodec.h`:

```cpp
#pragma once
#include "Protocol.h"
#include <cstdint>
#include <cstddef>
#include <optional>

namespace stopwatch {

struct CostRecord {
    ProviderID id;
    std::optional<uint32_t> todayCents;   // nullopt iff 0xFFFFFFFF
    std::optional<uint32_t> monthCents;
    std::optional<uint32_t> todayTokens;
    std::optional<uint32_t> monthTokens;
    char topModel[13] = {0};              // 12 wire bytes + null terminator
    uint8_t history[kCostHistoryDays] = {0};
};

struct CostSnapshot {
    uint8_t  versionMajor    = 0;
    uint8_t  versionMinor    = 0;
    uint8_t  recordCount     = 0;
    uint8_t  flags           = 0;
    uint32_t capturedAt      = 0;
    uint8_t  historyDays     = 0;
    uint16_t historyUnitCents = 0;
    CostRecord records[kCostMaxRecords] = {};

    bool isStale()       const { return flags & kCostFlagStale; }
    bool isBridgeError() const { return flags & kCostFlagBridgeError; }
    bool isUnavailable() const { return flags & kCostFlagUnavailable; }

    const CostRecord *find(ProviderID pid) const {
        for (uint8_t i = 0; i < recordCount; ++i)
            if (records[i].id == pid) return &records[i];
        return nullptr;
    }
};

enum class CostDecodeResult : uint8_t { Ok, TooShort, MajorVersionTooNew, InvalidRecordCount };

CostDecodeResult decodeCostSnapshot(const uint8_t *bytes, size_t len, CostSnapshot &out);

}  // namespace stopwatch
```

- [ ] **Step 4: Create `CostCodec.cpp`**

Create `firmware/src/CostCodec.cpp`:

```cpp
#include "CostCodec.h"
#include <cstring>

namespace stopwatch {

namespace {
uint16_t readU16(const uint8_t *b) { return (uint16_t)b[0] | ((uint16_t)b[1] << 8); }
uint32_t readU32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
std::optional<uint32_t> optU32(const uint8_t *b) {
    uint32_t v = readU32(b);
    return (v == 0xFFFFFFFFu) ? std::nullopt : std::optional<uint32_t>(v);
}
}  // namespace

CostDecodeResult decodeCostSnapshot(const uint8_t *bytes, size_t len, CostSnapshot &out) {
    if (len < kCostHeaderSize) return CostDecodeResult::TooShort;

    uint8_t major = bytes[0];
    uint8_t count = bytes[2];
    if (major > kCostVersionMajor) return CostDecodeResult::MajorVersionTooNew;
    if (count > kCostMaxRecords)   return CostDecodeResult::InvalidRecordCount;
    if (len < (size_t)(kCostHeaderSize + count * kCostRecordSize)) return CostDecodeResult::TooShort;

    out.versionMajor     = major;
    out.versionMinor     = bytes[1];
    out.recordCount      = count;
    out.flags            = bytes[3];
    out.capturedAt       = readU32(bytes + 4);
    out.historyDays      = bytes[8];
    out.historyUnitCents = readU16(bytes + 10);

    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t *r = bytes + kCostHeaderSize + i * kCostRecordSize;
        CostRecord &rec = out.records[i];
        rec.id          = (ProviderID)r[0];
        rec.todayCents  = optU32(r + 2);
        rec.monthCents  = optU32(r + 6);
        rec.todayTokens = optU32(r + 10);
        rec.monthTokens = optU32(r + 14);
        memcpy(rec.topModel, r + 18, 12);
        rec.topModel[12] = '\0';
        memcpy(rec.history, r + 30, kCostHistoryDays);
    }
    return CostDecodeResult::Ok;
}

}  // namespace stopwatch
```

- [ ] **Step 5: Run the test to confirm it passes**

Run: `cd firmware && pio test -e native -f test_cost_codec`
Expected: PASS (3 tests). This proves the bridge encoder and firmware decoder agree on the wire format.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/CostCodec.h firmware/src/CostCodec.cpp firmware/test/test_cost_codec/test_main.cpp
git commit -m "firmware: add CostCodec decoding CostSnapshot (mirrors bridge golden hex)"
```

---

## Task 12: Firmware `CostFormat` (pure helpers + native test)

**Files:**
- Create: `firmware/src/CostFormat.h`
- Create: `firmware/src/CostFormat.cpp`
- Create: `firmware/test/test_cost_format/test_main.cpp`

- [ ] **Step 1: Write the failing native test**

Create `firmware/test/test_cost_format/test_main.cpp`:

```cpp
#include <unity.h>
#include <cstring>
#include "../../src/CostFormat.h"

using namespace stopwatch;

void test_formatDollars(void) {
    char buf[16];
    formatDollars(2190, buf, sizeof(buf), true);   TEST_ASSERT_EQUAL_STRING("$21.90", buf);
    formatDollars(1200, buf, sizeof(buf), true);    TEST_ASSERT_EQUAL_STRING("$12.00", buf);
    formatDollars(41502, buf, sizeof(buf), false);  TEST_ASSERT_EQUAL_STRING("$415", buf);
    formatDollars(30000, buf, sizeof(buf), false);  TEST_ASSERT_EQUAL_STRING("$300", buf);
}

void test_humanizeTokens(void) {
    char buf[16];
    humanizeTokens(391120777, buf, sizeof(buf)); TEST_ASSERT_EQUAL_STRING("391M", buf);
    humanizeTokens(34356111, buf, sizeof(buf));  TEST_ASSERT_EQUAL_STRING("34M", buf);
    humanizeTokens(1000000, buf, sizeof(buf));   TEST_ASSERT_EQUAL_STRING("1M", buf);
    humanizeTokens(999999, buf, sizeof(buf));    TEST_ASSERT_EQUAL_STRING("999k", buf);
    humanizeTokens(500, buf, sizeof(buf));       TEST_ASSERT_EQUAL_STRING("500", buf);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_formatDollars);
    RUN_TEST(test_humanizeTokens);
    return UNITY_END();
}
```

- [ ] **Step 2: Run to confirm failure**

Run: `cd firmware && pio test -e native -f test_cost_format`
Expected: FAIL — `CostFormat.h: No such file or directory`.

- [ ] **Step 3: Create `CostFormat.h`**

Create `firmware/src/CostFormat.h`:

```cpp
#pragma once
#include <cstdint>
#include <cstddef>

namespace stopwatch {

/// "$21.90" (twoDecimals) or "$415" (rounded whole dollars).
void formatDollars(uint32_t cents, char *buf, size_t bufSize, bool twoDecimals);

/// "391M" / "999k" / "500".
void humanizeTokens(uint32_t tokens, char *buf, size_t bufSize);

}  // namespace stopwatch
```

- [ ] **Step 4: Create `CostFormat.cpp`**

Create `firmware/src/CostFormat.cpp`:

```cpp
#include "CostFormat.h"
#include <cstdio>

namespace stopwatch {

void formatDollars(uint32_t cents, char *buf, size_t bufSize, bool twoDecimals) {
    if (twoDecimals) {
        snprintf(buf, bufSize, "$%u.%02u", cents / 100, cents % 100);
    } else {
        snprintf(buf, bufSize, "$%u", (cents + 50) / 100);  // round to nearest dollar
    }
}

void humanizeTokens(uint32_t tokens, char *buf, size_t bufSize) {
    if (tokens >= 1000000u)      snprintf(buf, bufSize, "%uM", tokens / 1000000u);
    else if (tokens >= 1000u)    snprintf(buf, bufSize, "%uk", tokens / 1000u);
    else                         snprintf(buf, bufSize, "%u", tokens);
}

}  // namespace stopwatch
```

- [ ] **Step 5: Run the test to confirm it passes**

Run: `cd firmware && pio test -e native -f test_cost_format`
Expected: PASS (2 tests).

- [ ] **Step 6: Commit**

```bash
git add firmware/src/CostFormat.h firmware/src/CostFormat.cpp firmware/test/test_cost_format/test_main.cpp
git commit -m "firmware: add pure CostFormat dollar/token helpers"
```

---

## Task 13: Firmware `App` — expand the carousel

**Files:**
- Modify: `firmware/src/App.h`
- Modify: `firmware/src/App.cpp`
- Modify: `firmware/test/test_state_machine/test_main.cpp`

- [ ] **Step 1: Update the failing state-machine test**

In `firmware/test/test_state_machine/test_main.cpp`, replace `test_keyBShortCyclesForward` and `test_keyAShortCyclesBackward` with the 7-entry cycle and add an `isSpendView` test:

```cpp
void test_keyBShortCyclesForward(void) {
    App app; app.begin();
    ViewId order[] = { ViewId::Overview, ViewId::TotalSpend, ViewId::Codex, ViewId::CodexCost,
                       ViewId::Claude, ViewId::ClaudeCost, ViewId::Gemini, ViewId::Overview };
    for (int i = 0; i < 7; ++i) {
        TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyBShort));
        TEST_ASSERT_EQUAL((int)order[i + 1], (int)app.currentView());
    }
}

void test_keyAShortCyclesBackward(void) {
    App app; app.begin();
    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyAShort));
    TEST_ASSERT_EQUAL((int)ViewId::Gemini, (int)app.currentView());
    TEST_ASSERT_TRUE(app.handleEvent(ButtonEvent::KeyAShort));
    TEST_ASSERT_EQUAL((int)ViewId::ClaudeCost, (int)app.currentView());
}

void test_isSpendView(void) {
    TEST_ASSERT_TRUE(isSpendView(ViewId::TotalSpend));
    TEST_ASSERT_TRUE(isSpendView(ViewId::CodexCost));
    TEST_ASSERT_TRUE(isSpendView(ViewId::ClaudeCost));
    TEST_ASSERT_FALSE(isSpendView(ViewId::Overview));
    TEST_ASSERT_FALSE(isSpendView(ViewId::Codex));
    TEST_ASSERT_FALSE(isSpendView(ViewId::Gemini));
}
```

Add `RUN_TEST(test_isSpendView);` to `main()` after the existing `RUN_TEST` lines.

- [ ] **Step 2: Run to confirm failure**

Run: `cd firmware && pio test -e native -f test_state_machine`
Expected: FAIL — `'TotalSpend' is not a member of 'stopwatch::ViewId'`.

- [ ] **Step 3: Expand `ViewId` and declare `isSpendView`**

In `firmware/src/App.h`, replace the `ViewId` enum line with:

```cpp
enum class ViewId : uint8_t {
    Overview = 0, TotalSpend = 1,
    Codex = 2, CodexCost = 3,
    Claude = 4, ClaudeCost = 5,
    Gemini = 6,
};
```

And after the `constexpr ViewId prevView(ViewId v);` line, add:

```cpp
constexpr bool isSpendView(ViewId v) {
    return v == ViewId::TotalSpend || v == ViewId::CodexCost || v == ViewId::ClaudeCost;
}
```

- [ ] **Step 4: Update the cycle in `App.cpp`**

Replace `nextView` and `prevView` in `firmware/src/App.cpp`:

```cpp
constexpr ViewId nextView(ViewId v) {
    switch (v) {
        case ViewId::Overview:   return ViewId::TotalSpend;
        case ViewId::TotalSpend: return ViewId::Codex;
        case ViewId::Codex:      return ViewId::CodexCost;
        case ViewId::CodexCost:  return ViewId::Claude;
        case ViewId::Claude:     return ViewId::ClaudeCost;
        case ViewId::ClaudeCost: return ViewId::Gemini;
        case ViewId::Gemini:     return ViewId::Overview;
    }
    return ViewId::Overview;
}

constexpr ViewId prevView(ViewId v) {
    switch (v) {
        case ViewId::Overview:   return ViewId::Gemini;
        case ViewId::TotalSpend: return ViewId::Overview;
        case ViewId::Codex:      return ViewId::TotalSpend;
        case ViewId::CodexCost:  return ViewId::Codex;
        case ViewId::Claude:     return ViewId::CodexCost;
        case ViewId::ClaudeCost: return ViewId::Claude;
        case ViewId::Gemini:     return ViewId::ClaudeCost;
    }
    return ViewId::Overview;
}
```

- [ ] **Step 5: Run the test to confirm it passes**

Run: `cd firmware && pio test -e native -f test_state_machine`
Expected: PASS (6 tests).

- [ ] **Step 6: Commit**

```bash
git add firmware/src/App.h firmware/src/App.cpp firmware/test/test_state_machine/test_main.cpp
git commit -m "firmware: expand ViewId carousel to 7 with spend screens"
```

---

## Task 14: Firmware `Views/Spend.cpp` (Total + per-provider $)

**Files:**
- Create: `firmware/src/Views/Spend.h`
- Create: `firmware/src/Views/Spend.cpp`

> View drawing is validated by `pio run` (compile) + manual flash, consistent with `Overview.cpp`/`Provider.cpp` which have no native tests. The numeric helpers it calls (`CostFormat`) are already unit-tested.

- [ ] **Step 1: Create `Spend.h`**

Create `firmware/src/Views/Spend.h`:

```cpp
// firmware/src/Views/Spend.h
#pragma once
#include "../CostCodec.h"
#include "../Renderer.h"
#include "../App.h"

namespace stopwatch::views {

void drawTotalSpend(Renderer &renderer, const CostSnapshot &cost, LinkStatus link);
void drawProviderCost(Renderer &renderer, const CostSnapshot &cost, ProviderID id, LinkStatus link);

/// Teaser line ("today $X · NNNm") drawn by the provider ring screen. No-op if rec is null.
void drawSpendTeaser(M5Canvas &c, const CostRecord *rec, int baselineY, uint32_t color);

}  // namespace stopwatch::views
```

- [ ] **Step 2: Create `Spend.cpp`**

Create `firmware/src/Views/Spend.cpp`:

```cpp
#include "Spend.h"
#include "../IconLookup.h"
#include "../CostFormat.h"
#include "../Theme.h"
#include <cstdio>
#include <cstring>

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

struct Pill { const char *label; uint32_t color; };
Pill pillFor(LinkStatus link, const CostSnapshot &cost) {
    if (link == LinkStatus::NoBridge)            return { "no bridge", theme::kPillInfo };
    if (link == LinkStatus::LinkError)           return { "link error", theme::kPillError };
    if (cost.isUnavailable())                    return { "no cost data", theme::kPillInfo };
    if (cost.isStale() || cost.isBridgeError())  return { "stale", theme::kPillStale };
    return { nullptr, 0 };
}

// Vertical-bar sparkline. `values` length `count`; scaled to `maxVal` (>=1).
void drawSparkline(M5Canvas &c, int x, int y, int w, int h,
                   const int *values, int count, int maxVal, uint32_t color) {
    if (count <= 0 || maxVal < 1) return;
    int barW = w / count;
    if (barW < 1) barW = 1;
    for (int i = 0; i < count; ++i) {
        int bh = (int)((long)values[i] * h / maxVal);
        if (values[i] > 0 && bh < 2) bh = 2;  // floor so a nonzero day is visible
        c.fillRect(x + i * barW, y + (h - bh), barW > 1 ? barW - 1 : 1, bh, color);
    }
}
}  // namespace

void drawTotalSpend(Renderer &renderer, const CostSnapshot &cost, LinkStatus link) {
    auto &c = renderer.canvas();
    renderer.clear(theme::kBackground);
    c.setTextDatum(middle_center);

    // Title
    c.setFont(&fonts::Font2);
    c.setTextColor(theme::kTextMuted);
    c.drawString("SPEND & BURN", theme::kCenterX, theme::kCenterY - 96);

    // Aggregate today's cents + 30d cents + 30d tokens; build combined history.
    uint32_t todayCents = 0, monthCents = 0, monthTokens = 0;
    int combined[kCostHistoryDays] = {0};
    int maxCombined = 1;
    bool any = false;
    for (uint8_t i = 0; i < cost.recordCount; ++i) {
        const CostRecord &r = cost.records[i];
        if (r.todayCents) { todayCents += r.todayCents.value(); any = true; }
        if (r.monthCents) monthCents += r.monthCents.value();
        if (r.monthTokens) monthTokens += r.monthTokens.value();
        for (int d = 0; d < kCostHistoryDays; ++d) {
            combined[d] += r.history[d];
            if (combined[d] > maxCombined) maxCombined = combined[d];
        }
    }

    if (any) {
        char hero[16]; formatDollars(todayCents, hero, sizeof(hero), true);
        c.setFont(&fonts::Font7);
        c.setTextColor(theme::kTextPrimary);
        c.drawString(hero, theme::kCenterX, theme::kCenterY - 36);
        c.setFont(&fonts::Font2);
        c.setTextColor(theme::kTextMuted);
        c.drawString("today", theme::kCenterX, theme::kCenterY + 2);

        char tok[16]; humanizeTokens(monthTokens, tok, sizeof(tok));
        char mo[16];  formatDollars(monthCents, mo, sizeof(mo), false);
        char line[40]; snprintf(line, sizeof(line), "30d %s \xC2\xB7 %s", mo, tok);
        c.drawString(line, theme::kCenterX, theme::kCenterY + 28);

        drawSparkline(c, theme::kCenterX - 90, theme::kCenterY + 44, 180, 40,
                      combined, kCostHistoryDays, maxCombined, theme::kTextPrimary);

        // Per-provider split line.
        char split[48] = {0};
        for (uint8_t i = 0; i < cost.recordCount; ++i) {
            const CostRecord &r = cost.records[i];
            char one[24]; char d[16];
            formatDollars(r.todayCents.value_or(0), d, sizeof(d), true);
            snprintf(one, sizeof(one), "%s%.2s %s", (i ? " \xC2\xB7 " : ""), labelFor(r.id), d);
            strncat(split, one, sizeof(split) - strlen(split) - 1);
        }
        c.drawString(split, theme::kCenterX, theme::kCenterY + 96);
    } else {
        c.setFont(&fonts::Font4);
        c.setTextColor(theme::kTextMuted);
        c.drawString("\xE2\x80\x94", theme::kCenterX, theme::kCenterY);
    }

    auto pill = pillFor(link, cost);
    renderer.drawPill(theme::kCenterX, theme::kCenterY + theme::kRingOuterR - 8, pill.label, pill.color);
}

void drawProviderCost(Renderer &renderer, const CostSnapshot &cost, ProviderID id, LinkStatus link) {
    auto &c = renderer.canvas();
    renderer.clear(theme::kBackground);
    c.setTextDatum(middle_center);
    uint32_t color = theme::colorFor(id);

    const CostRecord *r = cost.find(id);

    // Header: brand mark + top model.
    {
        c.setFont(&fonts::Font2);
        const char *model = (r && r->topModel[0]) ? r->topModel : labelFor(id);
        int tw = c.textWidth(model);
        int totalW = icons::kSize28 + 8 + tw;
        int leftX = theme::kCenterX - totalW / 2;
        c.drawBitmap(leftX, theme::kCenterY - 96 - icons::kSize28 / 2,
                     icons::bitmap28(id), icons::kSize28, icons::kSize28, color);
        c.setTextDatum(middle_left);
        c.setTextColor(theme::kTextMuted);
        c.drawString(model, leftX + icons::kSize28 + 8, theme::kCenterY - 96);
        c.setTextDatum(middle_center);
    }

    if (r && r->todayCents) {
        char hero[16]; formatDollars(r->todayCents.value(), hero, sizeof(hero), true);
        c.setFont(&fonts::Font7);
        c.setTextColor(color);
        c.drawString(hero, theme::kCenterX, theme::kCenterY - 40);
        c.setFont(&fonts::Font2);
        c.setTextColor(theme::kTextMuted);
        c.drawString("today", theme::kCenterX, theme::kCenterY - 4);

        char mo[16]; formatDollars(r->monthCents.value_or(0), mo, sizeof(mo), false);
        char l1[24]; snprintf(l1, sizeof(l1), "30d   %s", mo);
        c.drawString(l1, theme::kCenterX, theme::kCenterY + 24);
        char tok[16]; humanizeTokens(r->monthTokens.value_or(0), tok, sizeof(tok));
        char l2[24]; snprintf(l2, sizeof(l2), "tok   %s", tok);
        c.drawString(l2, theme::kCenterX, theme::kCenterY + 46);

        int hist[kCostHistoryDays]; int maxV = 1;
        for (int d = 0; d < kCostHistoryDays; ++d) { hist[d] = r->history[d]; if (hist[d] > maxV) maxV = hist[d]; }
        drawSparkline(c, theme::kCenterX - 90, theme::kCenterY + 64, 180, 36,
                      hist, kCostHistoryDays, maxV, color);
    } else {
        c.setFont(&fonts::Font2);
        c.setTextColor(theme::kTextMuted);
        c.drawString("waiting for Mac", theme::kCenterX, theme::kCenterY);
    }

    auto pill = pillFor(link, cost);
    renderer.drawPill(theme::kCenterX, theme::kCenterY + theme::kRingOuterR - 8, pill.label, pill.color);
}

void drawSpendTeaser(M5Canvas &c, const CostRecord *rec, int baselineY, uint32_t color) {
    if (!rec || !rec->todayCents) return;
    char d[16]; formatDollars(rec->todayCents.value(), d, sizeof(d), true);
    char tok[16]; humanizeTokens(rec->todayTokens.value_or(0), tok, sizeof(tok));
    char line[40]; snprintf(line, sizeof(line), "today %s \xC2\xB7 %s", d, tok);
    c.setTextDatum(middle_center);
    c.setTextColor(theme::kTextMuted);
    c.setFont(&fonts::Font2);
    c.drawString(line, theme::kCenterX, baselineY);
}

}  // namespace stopwatch::views
```

- [ ] **Step 3: Verify the device target compiles**

Run: `cd firmware && pio run -e stopwatch`
Expected: `SUCCESS` (Spend.cpp compiles; it is not yet referenced by main).

- [ ] **Step 4: Commit**

```bash
git add firmware/src/Views/Spend.h firmware/src/Views/Spend.cpp
git commit -m "firmware: add Spend views (Total + per-provider $ + teaser)"
```

---

## Task 15: Firmware — spend teaser on the ring screen

**Files:**
- Modify: `firmware/src/Views/Provider.h`
- Modify: `firmware/src/Views/Provider.cpp`

- [ ] **Step 1: Add a cost-record parameter to `drawProvider`**

In `firmware/src/Views/Provider.h`, replace the declaration with:

```cpp
#pragma once
#include "../SnapshotCodec.h"
#include "../CostCodec.h"
#include "../Renderer.h"
#include "../App.h"

namespace stopwatch::views {

void drawProvider(Renderer &renderer, const Snapshot &snap, ProviderID id, LinkStatus link,
                  const CostRecord *cost = nullptr);

}  // namespace stopwatch::views
```

- [ ] **Step 2: Render the teaser in `Provider.cpp`**

In `firmware/src/Views/Provider.cpp`, add the include near the top (after `#include "../Theme.h"`):

```cpp
#include "Spend.h"
```

Change the function signature line to match the header:

```cpp
void drawProvider(Renderer &renderer, const Snapshot &snap, ProviderID id, LinkStatus link,
                  const CostRecord *cost) {
```

Then, immediately before the `auto pill = pillFor(link, snap);` line near the end, add:

```cpp
    // Spend teaser between the bottom strap and the pill (Codex/Claude only).
    drawSpendTeaser(c, cost, theme::kCenterY + theme::kRingOuterR - 48, color);
```

- [ ] **Step 3: Verify the device target compiles**

Run: `cd firmware && pio run -e stopwatch`
Expected: `SUCCESS`.

- [ ] **Step 4: Commit**

```bash
git add firmware/src/Views/Provider.h firmware/src/Views/Provider.cpp
git commit -m "firmware: draw spend teaser on Codex/Claude ring screens"
```

---

## Task 16: Firmware `SnapshotStore` — keyed slots

**Files:**
- Modify: `firmware/src/SnapshotStore.h`
- Modify: `firmware/src/SnapshotStore.cpp`

> NVS is hardware-only (excluded from native build); verified by `pio run` + manual.

- [ ] **Step 1: Add a `key` parameter to load/save**

Replace `firmware/src/SnapshotStore.h` body with:

```cpp
// firmware/src/SnapshotStore.h
#pragma once
#include "Protocol.h"
#include <cstdint>
#include <cstddef>

namespace stopwatch {

/// Persists raw snapshot bytes to NVS (one namespace, multiple keys) so the watch
/// can render last-known data on a cold boot before the bridge responds.
class SnapshotStore {
public:
    void begin();
    bool load(const char *key, uint8_t *outBytes, size_t bufSize, size_t &outLen);
    void save(const char *key, const uint8_t *bytes, size_t len);

private:
    bool open_ = false;
};

}  // namespace stopwatch
```

- [ ] **Step 2: Thread the key through `SnapshotStore.cpp`**

Replace `firmware/src/SnapshotStore.cpp` body with:

```cpp
// firmware/src/SnapshotStore.cpp
#include "SnapshotStore.h"
#include <Preferences.h>

namespace stopwatch {

namespace { Preferences prefs; constexpr const char *kNs = "swq"; }

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

}  // namespace stopwatch
```

- [ ] **Step 3: Verify it compiles together with the existing main.cpp call sites (done in Task 18)**

Run: `cd firmware && pio run -e stopwatch`
Expected: FAIL — `main.cpp` still calls the old 3-arg `load`/`save`. This is expected; Task 18 updates the call sites. (If you are implementing strictly task-by-task, stage the commit now; the build goes green after Task 18.)

- [ ] **Step 4: Commit**

```bash
git add firmware/src/SnapshotStore.h firmware/src/SnapshotStore.cpp
git commit -m "firmware: key SnapshotStore slots so cost cache can coexist"
```

---

## Task 17: Firmware `BleClient.fetchCost`

**Files:**
- Modify: `firmware/src/BleClient.h`
- Modify: `firmware/src/BleClient.cpp`

> NimBLE is hardware-only (excluded from native build); verified by `pio run` + manual.

- [ ] **Step 1: Declare `fetchCost`**

In `firmware/src/BleClient.h`, inside `class BleClient`, after the existing `fetch(...)` declaration add:

```cpp
    /// Like fetch(), but writes the cost trigger scope and reads CostSnapshot.
    FetchResult fetchCost(uint8_t *outBytes, size_t bufSize, size_t &outLen);
```

- [ ] **Step 2: Generalize the read in `BleClient.cpp`**

In `firmware/src/BleClient.cpp`, replace the whole `BleClient::fetch(...)` function with a generalized private helper plus two thin wrappers:

```cpp
BleClient::FetchResult BleClient::fetchInto(const char *charUuid, uint8_t scope,
                                            uint8_t *outBytes, size_t bufSize, size_t &outLen) {
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
        const NimBLEAdvertisedDevice *dev = results.getDevice(i);
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

    auto tryReadOnce = [&]() -> FetchResult {
        auto *svc = client->getService(svcUuid);
        if (!svc) return FetchResult::ReadFailed;

        auto *trigger = svc->getCharacteristic(NimBLEUUID(kTriggerUUID));
        if (!trigger) return FetchResult::ReadFailed;
        uint8_t scopeBuf[1] = { scope };
        trigger->writeValue(scopeBuf, 1, /*response=*/false);

        delay(150);

        auto *ch = svc->getCharacteristic(NimBLEUUID(charUuid));
        if (!ch) return FetchResult::ReadFailed;
        NimBLEAttValue value = ch->readValue();
        if (value.size() == 0 || value.size() > bufSize) return FetchResult::ReadFailed;
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

BleClient::FetchResult BleClient::fetch(uint8_t scope, uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    return fetchInto(kSnapshotUUID, scope, outBytes, bufSize, outLen);
}

BleClient::FetchResult BleClient::fetchCost(uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    return fetchInto(kCostSnapshotUUID, kTriggerScopeCost, outBytes, bufSize, outLen);
}
```

- [ ] **Step 3: Declare the private helper in the header**

In `firmware/src/BleClient.h`, add a `private:` section before the closing `};`:

```cpp
private:
    FetchResult fetchInto(const char *charUuid, uint8_t scope,
                          uint8_t *outBytes, size_t bufSize, size_t &outLen);
```

- [ ] **Step 4: Commit (build verified in Task 18)**

```bash
git add firmware/src/BleClient.h firmware/src/BleClient.cpp
git commit -m "firmware: add BleClient.fetchCost via generalized characteristic read"
```

---

## Task 18: Firmware — wire the views and lazy cost fetch into `main.cpp`

**Files:**
- Modify: `firmware/src/main.cpp`

- [ ] **Step 1: Add includes and cost globals**

In `firmware/src/main.cpp`, add after `#include "Views/Provider.h"`:

```cpp
#include "Views/Spend.h"
#include "CostCodec.h"
```

After the `stopwatch::Snapshot g_snap;` global, add:

```cpp
stopwatch::CostSnapshot g_cost;
bool                    g_costLoaded = false;
```

- [ ] **Step 2: Replace the view-dispatch + render functions**

Replace `renderCurrent()` and `renderRefreshingOverlay()` with a shared dispatcher:

```cpp
static void drawCurrentView() {
    using namespace stopwatch;
    auto link = g_app.linkStatus();
    switch (g_app.currentView()) {
        case ViewId::Overview:   views::drawOverview(g_renderer, g_snap, link); break;
        case ViewId::TotalSpend: views::drawTotalSpend(g_renderer, g_cost, link); break;
        case ViewId::Codex:      views::drawProvider(g_renderer, g_snap, ProviderID::Codex,  link, g_cost.find(ProviderID::Codex)); break;
        case ViewId::CodexCost:  views::drawProviderCost(g_renderer, g_cost, ProviderID::Codex, link); break;
        case ViewId::Claude:     views::drawProvider(g_renderer, g_snap, ProviderID::Claude, link, g_cost.find(ProviderID::Claude)); break;
        case ViewId::ClaudeCost: views::drawProviderCost(g_renderer, g_cost, ProviderID::Claude, link); break;
        case ViewId::Gemini:     views::drawProvider(g_renderer, g_snap, ProviderID::Gemini, link, nullptr); break;
    }
}

static void renderCurrent() {
    drawCurrentView();
    g_renderer.present();
}

static void renderRefreshingOverlay(const char *label) {
    using namespace stopwatch;
    drawCurrentView();
    auto &c = g_renderer.canvas();
    c.fillRect(0, theme::kCenterY + theme::kRingOuterR / 2 - 14,
               M5.Display.width(), 28, 0x303030);
    c.setTextDatum(middle_center);
    c.setTextColor(theme::kTextPrimary);
    c.setFont(&fonts::Font2);
    c.drawString(label, theme::kCenterX, theme::kCenterY + theme::kRingOuterR / 2);
    g_renderer.present();
}
```

- [ ] **Step 3: Add the lazy cost fetch**

After `fetchAndApply(...)`, add:

```cpp
static bool fetchCostAndApply() {
    uint8_t buf[stopwatch::kCostSnapshotMaxSize];
    size_t len = 0;
    if (g_ble.fetchCost(buf, sizeof(buf), len) != stopwatch::BleClient::FetchResult::Ok) {
        return false;
    }
    stopwatch::CostSnapshot cs;
    if (stopwatch::decodeCostSnapshot(buf, len, cs) != stopwatch::CostDecodeResult::Ok) {
        return false;
    }
    g_cost = cs;
    g_store.save("cost", buf, len);
    g_costLoaded = true;
    return true;
}

// On first entry to any spend screen this wake-session, pull cost once.
static void ensureCostLoaded() {
    using namespace stopwatch;
    if (!isSpendView(g_app.currentView()) || g_costLoaded) return;
    renderRefreshingOverlay("Loading $\xE2\x80\xA6");
    fetchCostAndApply();
}
```

- [ ] **Step 4: Update the snapshot-store call sites and load the cost cache in `setup()`**

In `setup()`, change the cached-snapshot load block to pass the `"snap"` key:

```cpp
    // Load last cached snapshots for instant first paint.
    uint8_t buf[stopwatch::kSnapshotSize];
    size_t len = 0;
    if (g_store.load("snap", buf, sizeof(buf), len)) {
        stopwatch::decodeSnapshot(buf, len, g_snap);
        Serial.printf("[stopwatch-fw] loaded cached snapshot, %u bytes\n", (unsigned)len);
    } else {
        Serial.println("[stopwatch-fw] no cached snapshot");
    }
    uint8_t cbuf[stopwatch::kCostSnapshotMaxSize];
    size_t clen = 0;
    if (g_store.load("cost", cbuf, sizeof(cbuf), clen)) {
        stopwatch::decodeCostSnapshot(cbuf, clen, g_cost);  // shown until first fresh fetch
    }
```

In `fetchAndApply(...)`, change the save call to use the key:

```cpp
    g_store.save("snap", buf, len);
```

- [ ] **Step 5: Trigger the lazy fetch on view change and wake**

In `loop()`, replace the `else if (changed)` branch so a view change pulls cost when needed:

```cpp
        if (g_app.wantsImmediateSleep()) {
            g_app.clearSleepRequest();
            enterSleepAndRefreshOnWake();
        } else if (changed) {
            ensureCostLoaded();
            renderCurrent();
        }
```

In `enterSleepAndRefreshOnWake()`, reset the cost session flag so the first spend-screen visit after waking re-pulls:

```cpp
static void enterSleepAndRefreshOnWake() {
    g_power.enterLightSleep();
    g_app.noteWakeFromSleep();
    g_costLoaded = false;
    applyRefreshRequest("Refreshing\xE2\x80\xA6");
    ensureCostLoaded();
    renderCurrent();
}
```

- [ ] **Step 6: Build the firmware (all device code together)**

Run: `cd firmware && pio run -e stopwatch`
Expected: `SUCCESS` — this is the first build where Tasks 16, 17, and 18 all link together.

- [ ] **Step 7: Run the full test suite**

Run (from repo root): `make test`
Expected: bridge `swift test` all pass; firmware `pio test -e native` runs `test_cost_codec`, `test_cost_format`, `test_snapshot_codec`, `test_state_machine` — all pass.

- [ ] **Step 8: Commit**

```bash
git add firmware/src/main.cpp
git commit -m "firmware: wire spend views + lazy cost fetch + cost cache into main"
```

---

## Task 19: End-to-end manual validation

**Files:** none (manual).

- [ ] **Step 1: Run the bridge in pair mode**

Run: `make pair`
Expected log lines include `advertising Stopwatch Bridge`, `fetch ok: …`, and `cost ok: providers=2`.

- [ ] **Step 2: Flash the firmware**

Long-press BOOT (screen off, LED blinks) per `memory/flashing-procedure.md`, then run: `make flash`
Expected: `pio run -t upload` succeeds.

- [ ] **Step 3: Glance checks on the watch**
- Wake → Overview rings paint in ≤ ~1 s (unchanged; cost not fetched yet).
- KeyB to **SPEND & BURN** → brief "Loading $…", then combined today/30d/sparkline appears.
- KeyB to **Codex** ring screen → `today $… · …M` teaser line shows under the rings.
- KeyB to **Codex $** → hero today $, 30d, top model, sparkline.
- Cycle through to **Gemini** → ring screen only, no teaser, no $ screen.
- Pull the Mac off Bluetooth → spend screens show `● no bridge` / cached values; rings behave as before.

- [ ] **Step 4: Update the spec status**

In `docs/superpowers/specs/2026-05-29-codexbar-spend-views-design.md`, change `**Status:** Approved` to `**Status:** Implemented`.

```bash
git add docs/superpowers/specs/2026-05-29-codexbar-spend-views-design.md
git commit -m "docs: mark spend-views design implemented"
```

---

## Self-review notes (for the implementer)

- **Wire compat is the contract.** `shared/fixtures/codexbar-cost-two.hex` is produced by the bridge `CostEncoder` (Task 4) and decoded by the firmware `CostCodec` (Task 11). If either side drifts, one of those two tests fails. Never hand-edit the `.hex`.
- **Task 16 leaves the device build red until Task 18** (the `SnapshotStore` signature change needs the updated `main.cpp` call sites). This is called out in Task 16 Step 3. If you need every task to build green independently, do Tasks 16–18 as one combined commit.
- **History scale is shared, not per-provider** — the watch sums `records[i].history[d]` across providers for the Total burn chart, which is only valid because the bridge normalizes every provider on the one `historyUnitCents`. Per-provider screens re-normalize to their own max purely for bar height.
- **UTC day bucketing** (Task 5) keeps history alignment deterministic in tests; it can be off by one calendar day for users far from UTC near midnight — cosmetic on a sparkline, noted in the spec's risks.
