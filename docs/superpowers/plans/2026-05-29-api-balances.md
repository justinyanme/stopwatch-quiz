# API Balances Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show prepaid API credit balances (OpenRouter, DeepSeek, and any Bearer-key provider) on a new scrollable, touch-driven wallet screen on the StopWatch.

**Architecture:** A third data type beside usage rings and spend/burn. The macOS bridge polls each provider's balance endpoint on a timer (keys in the Keychain, metadata in `providers.json`), caches per-provider last-good, and serves a new `BalanceSnapshot` GATT characteristic. The watch reads it lazily on entering the Balances view, decodes it, and renders a scrollable list; touch (CST820 via M5GFX's `M5.Touch`) scrolls it. The usage and cost paths are untouched.

**Tech Stack:** Swift 6 (bridge: CoreBluetooth, URLSession, Security/Keychain, swift-testing), C++17 (firmware: PlatformIO/Arduino, M5Unified/M5GFX, NimBLE, Unity native tests). Wire format locked by `shared/fixtures/*.{json,hex}` round-tripped on both sides.

**Spec:** `docs/superpowers/specs/2026-05-29-api-balances-design.md` (committed `0bfc96b`).

---

## File Structure

**Bridge (Swift) — `bridge/Sources/StopwatchBridge/`**
- `Protocol.swift` *(modify)* — balance UUID, version/size constants, `0x05` trigger, `BalanceKind`/`BalanceStatus`, `BalanceFlags`, `NormalizedBalance` model.
- `BalanceEncoder.swift` *(create)* — `NormalizedBalance → Data` per `PROTOCOL.md §3B`.
- `ProvidersConfig.swift` *(create)* — `providers.json` Codable model + `kind` defaults.
- `KeychainStore.swift` *(create)* — `KeyStore` protocol + Keychain-backed impl.
- `KeyCommand.swift` *(create)* — `set-key` / `list-keys` / `delete-key` CLI.
- `BalanceClient.swift` *(create)* — per-provider fetch + dotted-path JSON extraction.
- `BalanceCache.swift` *(create)* — per-provider last-good merge.
- `GATTPeripheral.swift` *(modify)* — serve `BalanceSnapshot`.
- `BridgeService.swift` *(modify)* — load providers, poll loop, `0x05` handler.
- `Bridge.swift` *(modify)* — dispatch new CLI commands.

**Firmware (C++) — `firmware/src/`**
- `Protocol.h` *(modify)* — balance constants/enums.
- `BalanceCodec.{h,cpp}` *(create)* — decode `BalanceSnapshot`.
- `BalanceFormat.{h,cpp}` *(create)* — currency symbol + minor-units → string (pure, native-testable).
- `Theme.h` *(modify)* — `balanceColorFor(BalanceKind)`.
- `Views/Balances.{h,cpp}` *(create)* — scrollable wallet view.
- `TouchScroll.{h,cpp}` *(create)* — pure scroll/momentum math (native-testable).
- `App.{h,cpp}` *(modify)* — `ViewId::Balances`, nav, `isBalanceView`.
- `BleClient.{h,cpp}` *(modify)* — `fetchBalances`.
- `main.cpp` *(modify)* — touch poll, lazy read, draw case, `0x05`, boot cache.

**Shared / tests**
- `shared/PROTOCOL.md` *(modify)*, `shared/fixtures/balances-two.{json,hex}` *(create)*.
- Bridge tests: `BalanceEncoderTests`, `ProvidersConfigTests`, `KeychainStoreTests`, `BalanceClientTests` *(create)*; `Fixtures.swift` *(modify)*.
- Firmware tests: `test_balance_codec`, `test_balance_format`, `test_touch_scroll` *(create)*; `test_state_machine` *(modify)*.

---

## Task 1: Protocol contract (doc + constants + model)

**Files:**
- Modify: `shared/PROTOCOL.md`
- Modify: `bridge/Sources/StopwatchBridge/Protocol.swift`
- Modify: `firmware/src/Protocol.h`

- [ ] **Step 1: Add the `BalanceSnapshot` section to `shared/PROTOCOL.md`**

Append after the `## 3A. CostSnapshot payload` section and add the `0x05` row to the trigger table (§2.1):

```markdown
| `0x05` | Balances only (poll all configured API-balance providers) |
```

Add the characteristic to the §2 table:

```markdown
| `BalanceSnapshot` | `4D9E8F21-7C3A-4B6D-8E15-9A2F6C3B0D74` | Read + Notify | bridge → watch | API credit balances. Watch reads lazily on entering the Balances view; bridge notifies on change. Versioned independently. Read via ATT read-blob (may exceed one MTU). |

## 3B. `BalanceSnapshot` payload (binary)

Independent of the other characteristics; its own `(versionMajor, versionMinor)`. All integers little-endian. Size = `8 + 36 × recordCount`.

### 3B.1 Header (8 bytes)

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0 | uint8 | `versionMajor` | `0x01`. |
| 1 | uint8 | `versionMinor` | `0x00`. |
| 2 | uint8 | `recordCount` | 0–16. |
| 3 | uint8 | `flags` | bit0 stale, bit1 bridge_error. |
| 4 | uint32 | `capturedAt` | Unix seconds the snapshot was assembled. |

### 3B.2 Per record (36 bytes, repeated `recordCount` times)

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0 | uint8 | `kind` | 0 generic, 1 openrouter, 2 deepseek, 3 groq, 4 together, 5 fireworks, 6 siliconflow, 7 moonshot, 8 zhipu. Unknown → generic. |
| 1 | uint8 | `status` | 0 ok, 1 stale, 2 auth_error, 3 unreachable, 4 depleted. Unknown → ok. |
| 2 | uint8 | `recordFlags` | bit0 = low_balance. |
| 3 | char[3] | `currencyCode` | ASCII e.g. `USD`,`CNY`. All-zero = unknown. |
| 6 | uint8 | `currencyDecimals` | Minor-unit exponent (2 for USD/CNY). |
| 7 | uint8 | `reserved` | `0`. |
| 8 | uint32 | `balanceMinor` | remaining × 10^decimals. `0xFFFFFFFF`=unknown, `0xFFFFFFFE`=unlimited. |
| 12 | uint32 | `usageMinor` | spent × 10^decimals, or `0xFFFFFFFF`. |
| 16 | uint32 | `updatedAt` | Unix seconds of this provider's last OK poll; `0`=never. |
| 20 | char[16] | `name` | UTF-8, null-padded display label. |

Versioning follows the same major/minor rules as §3.3.
```

- [ ] **Step 2: Add balance constants + model to `Protocol.swift`**

Add inside `enum Protocol`, after the `triggerScopeCost` line:

```swift
    public static let balanceSnapshotUUID = CBUUID(string: "4D9E8F21-7C3A-4B6D-8E15-9A2F6C3B0D74")
    public static let balanceVersionMajor: UInt8 = 1
    public static let balanceVersionMinor: UInt8 = 0
    public static let balanceHeaderSize  = 8
    public static let balanceRecordSize  = 36
    public static let balanceMaxRecords  = 16
    public static let triggerScopeBalances: UInt8 = 0x05
```

Add at file scope (after `ProviderPlan`):

```swift
public struct BalanceFlags: OptionSet, Sendable {
    public let rawValue: UInt8
    public init(rawValue: UInt8) { self.rawValue = rawValue }
    public static let stale       = BalanceFlags(rawValue: 0b0000_0001)
    public static let bridgeError = BalanceFlags(rawValue: 0b0000_0010)
}

public enum BalanceKind: UInt8, Sendable {
    case generic = 0, openrouter = 1, deepseek = 2, groq = 3, together = 4
    case fireworks = 5, siliconflow = 6, moonshot = 7, zhipu = 8

    public init(fromString s: String?) {
        switch (s ?? "").lowercased() {
        case "openrouter":  self = .openrouter
        case "deepseek":    self = .deepseek
        case "groq":        self = .groq
        case "together":    self = .together
        case "fireworks":   self = .fireworks
        case "siliconflow": self = .siliconflow
        case "moonshot":    self = .moonshot
        case "zhipu":       self = .zhipu
        default:            self = .generic
        }
    }
}

public enum BalanceStatus: UInt8, Sendable {
    case ok = 0, stale = 1, authError = 2, unreachable = 3, depleted = 4
}

/// Normalized balance input that `BalanceEncoder` consumes.
public struct NormalizedBalance: Equatable, Sendable {
    public struct Provider: Equatable, Sendable {
        public var kind: BalanceKind
        public var name: String
        public var status: BalanceStatus
        public var currencyCode: String      // 1–3 ASCII chars; "" = unknown
        public var currencyDecimals: Int     // default 2
        public var remaining: Double?        // nil → 0xFFFFFFFF (unknown)
        public var unlimited: Bool           // true → 0xFFFFFFFE
        public var usage: Double?            // nil → 0xFFFFFFFF
        public var updatedAt: Date?          // nil → 0
        public var isLow: Bool               // → recordFlags bit0

        public init(kind: BalanceKind, name: String, status: BalanceStatus,
                    currencyCode: String, currencyDecimals: Int = 2,
                    remaining: Double?, unlimited: Bool = false, usage: Double? = nil,
                    updatedAt: Date?, isLow: Bool = false) {
            self.kind = kind; self.name = name; self.status = status
            self.currencyCode = currencyCode; self.currencyDecimals = currencyDecimals
            self.remaining = remaining; self.unlimited = unlimited; self.usage = usage
            self.updatedAt = updatedAt; self.isLow = isLow
        }
    }
    public var capturedAt: Date
    public var flags: BalanceFlags
    public var providers: [Provider]

    public init(capturedAt: Date, flags: BalanceFlags, providers: [Provider]) {
        self.capturedAt = capturedAt; self.flags = flags; self.providers = providers
    }
}
```

- [ ] **Step 3: Add balance constants to `firmware/src/Protocol.h`**

Add before the closing `}  // namespace stopwatch`:

```cpp
constexpr const char *kBalanceSnapshotUUID = "4D9E8F21-7C3A-4B6D-8E15-9A2F6C3B0D74";

constexpr uint8_t  kBalanceVersionMajor = 1;
constexpr uint8_t  kBalanceHeaderSize   = 8;
constexpr uint8_t  kBalanceRecordSize   = 36;
constexpr uint8_t  kBalanceMaxRecords   = 16;
constexpr uint16_t kBalanceSnapshotMaxSize = kBalanceHeaderSize + kBalanceRecordSize * kBalanceMaxRecords;  // 584

constexpr uint8_t kBalanceFlagStale       = 0b00000001;
constexpr uint8_t kBalanceFlagBridgeError = 0b00000010;
constexpr uint8_t kBalanceRecordFlagLow   = 0b00000001;

constexpr uint8_t kTriggerScopeBalances = 0x05;

enum class BalanceKind : uint8_t {
    Generic = 0, OpenRouter = 1, DeepSeek = 2, Groq = 3, Together = 4,
    Fireworks = 5, SiliconFlow = 6, Moonshot = 7, Zhipu = 8,
};
enum class BalanceStatus : uint8_t { Ok = 0, Stale = 1, AuthError = 2, Unreachable = 3, Depleted = 4 };
```

- [ ] **Step 4: Verify both sides compile**

Run: `cd bridge && swift build` → Expected: `Build complete!`
Run: `cd firmware && pio run -e native` → Expected: compiles (links the existing tests; no new code exercised yet).

- [ ] **Step 5: Commit**

```bash
git add shared/PROTOCOL.md bridge/Sources/StopwatchBridge/Protocol.swift firmware/src/Protocol.h
git commit -m "protocol: document BalanceSnapshot characteristic + trigger 0x05"
```

---

## Task 2: BalanceEncoder (bridge)

**Files:**
- Create: `bridge/Sources/StopwatchBridge/BalanceEncoder.swift`
- Modify: `bridge/Tests/StopwatchBridgeTests/Fixtures.swift`
- Test: `bridge/Tests/StopwatchBridgeTests/BalanceEncoderTests.swift`

- [ ] **Step 1: Add the fixture builder to `Fixtures.swift`**

Append at end of file:

```swift
extension NormalizedBalance {
    /// Round-number fixture for byte-exact encoder assertions.
    static var balanceFixtureTwo: NormalizedBalance {
        .init(
            capturedAt: Date(timeIntervalSince1970: 1748455822),
            flags: [],
            providers: [
                .init(kind: .openrouter, name: "OpenRouter", status: .ok,
                      currencyCode: "USD", currencyDecimals: 2,
                      remaining: 42.10, usage: 7.90,
                      updatedAt: Date(timeIntervalSince1970: 1748455822), isLow: false),
                .init(kind: .deepseek, name: "DeepSeek", status: .ok,
                      currencyCode: "CNY", currencyDecimals: 2,
                      remaining: 318.50, usage: nil,
                      updatedAt: Date(timeIntervalSince1970: 1748455822), isLow: false),
            ]
        )
    }
}
```

- [ ] **Step 2: Write the failing test**

Create `bridge/Tests/StopwatchBridgeTests/BalanceEncoderTests.swift`:

```swift
import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct BalanceEncoderTests {

    @Test func encodesHeaderAndTwoRecords() {
        let data = [UInt8](BalanceEncoder.encode(.balanceFixtureTwo))
        // Header
        #expect(data.count == 8 + 36 * 2)        // 80
        #expect(data[0] == 1)                     // versionMajor
        #expect(data[1] == 0)                     // versionMinor
        #expect(data[2] == 2)                     // recordCount
        #expect(data[3] == 0)                     // flags
        #expect(le32(data, 4) == 1748455822)      // capturedAt

        // Record 0 — OpenRouter, $42.10, used $7.90
        let r0 = 8
        #expect(data[r0 + 0] == 1)                // kind = openrouter
        #expect(data[r0 + 1] == 0)                // status = ok
        #expect(data[r0 + 2] == 0)                // recordFlags
        #expect(Array(data[r0+3..<r0+6]) == Array("USD".utf8))
        #expect(data[r0 + 6] == 2)                // decimals
        #expect(le32(data, r0 + 8) == 4210)       // 42.10 → 4210 minor
        #expect(le32(data, r0 + 12) == 790)       // 7.90 → 790
        #expect(le32(data, r0 + 16) == 1748455822)// updatedAt
        #expect(name(data, r0 + 20) == "OpenRouter")

        // Record 1 — DeepSeek, ¥318.50, usage unknown
        let r1 = 8 + 36
        #expect(data[r1 + 0] == 2)                // kind = deepseek
        #expect(Array(data[r1+3..<r1+6]) == Array("CNY".utf8))
        #expect(le32(data, r1 + 8) == 31850)      // 318.50 → 31850
        #expect(le32(data, r1 + 12) == 0xFFFF_FFFF) // usage unknown
        #expect(name(data, r1 + 20) == "DeepSeek")
    }

    @Test func unknownUnlimitedAndLowFlag() {
        let p = NormalizedBalance(
            capturedAt: Date(timeIntervalSince1970: 0), flags: [],
            providers: [
                .init(kind: .generic, name: "X", status: .ok, currencyCode: "USD",
                      remaining: nil, updatedAt: nil, isLow: true),                 // unknown + low
                .init(kind: .generic, name: "Y", status: .ok, currencyCode: "USD",
                      remaining: nil, unlimited: true, updatedAt: nil),             // unlimited
            ])
        let data = [UInt8](BalanceEncoder.encode(p))
        #expect(le32(data, 8 + 8) == 0xFFFF_FFFF)        // record0 remaining unknown
        #expect(data[8 + 2] == 1)                         // record0 low flag
        #expect(le32(data, 8 + 36 + 8) == 0xFFFF_FFFE)   // record1 unlimited
    }

    @Test func clampsToMaxRecords() {
        let many = (0..<20).map { _ in
            NormalizedBalance.Provider(kind: .generic, name: "P", status: .ok,
                                       currencyCode: "USD", remaining: 1, updatedAt: nil)
        }
        let data = BalanceEncoder.encode(.init(capturedAt: Date(), flags: [], providers: many))
        #expect(data[2] == 16)                            // recordCount clamped
        #expect(data.count == 8 + 36 * 16)
    }

    private func le32(_ b: [UInt8], _ o: Int) -> UInt32 {
        UInt32(b[o]) | UInt32(b[o+1]) << 8 | UInt32(b[o+2]) << 16 | UInt32(b[o+3]) << 24
    }
    private func name(_ b: [UInt8], _ o: Int) -> String {
        String(decoding: b[o..<o+16].prefix { $0 != 0 }, as: UTF8.self)
    }
}
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cd bridge && swift test --filter BalanceEncoderTests`
Expected: FAIL — `cannot find 'BalanceEncoder' in scope`.

- [ ] **Step 4: Implement `BalanceEncoder.swift`**

Create `bridge/Sources/StopwatchBridge/BalanceEncoder.swift`:

```swift
// bridge/Sources/StopwatchBridge/BalanceEncoder.swift
import Foundation

public enum BalanceEncoder {

    /// Encodes normalized balances into the `8 + 36*N` byte BalanceSnapshot wire
    /// format. See `shared/PROTOCOL.md §3B`. Clamps to `balanceMaxRecords`.
    public static func encode(_ snap: NormalizedBalance) -> Data {
        let providers = Array(snap.providers.prefix(Protocol.balanceMaxRecords))
        if providers.count < snap.providers.count {
            FileHandle.standardError.write(Data("balance: \(snap.providers.count) providers > max \(Protocol.balanceMaxRecords); truncating\n".utf8))
        }

        var out = Data(capacity: Protocol.balanceHeaderSize + Protocol.balanceRecordSize * providers.count)
        out.append(Protocol.balanceVersionMajor)
        out.append(Protocol.balanceVersionMinor)
        out.append(UInt8(providers.count))
        out.append(snap.flags.rawValue)
        appendU32(&out, u32Seconds(snap.capturedAt))

        for p in providers {
            out.append(p.kind.rawValue)
            out.append(p.status.rawValue)
            out.append(p.isLow ? Protocol.balanceRecordFlagLow : 0)
            appendCurrency(&out, p.currencyCode)
            out.append(UInt8(max(0, min(p.currencyDecimals, 255))))
            out.append(0)  // reserved
            appendU32(&out, minorOrSentinel(p.remaining, decimals: p.currencyDecimals, unlimited: p.unlimited))
            appendU32(&out, minorOrSentinel(p.usage, decimals: p.currencyDecimals, unlimited: false))
            appendU32(&out, p.updatedAt.map(u32Seconds) ?? 0)
            appendName(&out, p.name)
        }
        return out
    }

    public static func staleEmpty() -> Data {
        encode(NormalizedBalance(capturedAt: Date(timeIntervalSince1970: 0),
                                 flags: [.stale], providers: []))
    }

    public static func errorEmpty(capturedAt: Date = Date()) -> Data {
        encode(NormalizedBalance(capturedAt: capturedAt,
                                 flags: [.stale, .bridgeError], providers: []))
    }

    /// Sets `stale` (plus `extraFlags`) and refreshes capturedAt on an encoded snapshot.
    public static func markStale(_ snapshot: Data, capturedAt: Date, extraFlags: BalanceFlags = []) -> Data {
        guard snapshot.count >= Protocol.balanceHeaderSize else { return snapshot }
        var out = snapshot
        out[3] |= BalanceFlags.stale.rawValue | extraFlags.rawValue
        writeU32(&out, u32Seconds(capturedAt), at: 4)
        return out
    }

    // MARK: - helpers

    static let balanceUnlimited: UInt32 = 0xFFFF_FFFE
    static let balanceUnknown:   UInt32 = 0xFFFF_FFFF

    private static func minorOrSentinel(_ value: Double?, decimals: Int, unlimited: Bool) -> UInt32 {
        if unlimited { return balanceUnlimited }
        guard let value else { return balanceUnknown }
        let scale = pow(10.0, Double(max(0, decimals)))
        let minor = (value * scale).rounded()
        if minor < 0 { return 0 }
        if minor >= Double(balanceUnlimited) { return balanceUnlimited - 1 }  // clamp below sentinels
        return UInt32(minor)
    }

    private static func appendCurrency(_ out: inout Data, _ code: String) {
        var field = [UInt8](repeating: 0, count: 3)
        for (i, b) in code.uppercased().utf8.prefix(3).enumerated() { field[i] = b }
        out.append(contentsOf: field)
    }

    private static func appendName(_ out: inout Data, _ name: String) {
        var field = [UInt8](repeating: 0, count: 16)
        for (i, b) in name.utf8.prefix(15).enumerated() { field[i] = b }
        out.append(contentsOf: field)
    }

    private static func u32Seconds(_ d: Date) -> UInt32 { UInt32(max(0, d.timeIntervalSince1970)) }

    private static func appendU32(_ out: inout Data, _ v: UInt32) {
        out.append(UInt8(v & 0xFF)); out.append(UInt8((v >> 8) & 0xFF))
        out.append(UInt8((v >> 16) & 0xFF)); out.append(UInt8((v >> 24) & 0xFF))
    }
    private static func writeU32(_ out: inout Data, _ v: UInt32, at o: Int) {
        out[o] = UInt8(v & 0xFF); out[o+1] = UInt8((v >> 8) & 0xFF)
        out[o+2] = UInt8((v >> 16) & 0xFF); out[o+3] = UInt8((v >> 24) & 0xFF)
    }
}
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cd bridge && swift test --filter BalanceEncoderTests`
Expected: PASS (3 tests).

- [ ] **Step 6: Commit**

```bash
git add bridge/Sources/StopwatchBridge/BalanceEncoder.swift bridge/Tests/StopwatchBridgeTests/BalanceEncoderTests.swift bridge/Tests/StopwatchBridgeTests/Fixtures.swift
git commit -m "bridge: add BalanceEncoder with byte-exact tests"
```

---

## Task 3: Freeze the golden fixture (shared)

**Files:**
- Create: `shared/fixtures/balances-two.json`
- Create: `shared/fixtures/balances-two.hex` (generated)
- Modify: `bridge/Tests/StopwatchBridgeTests/BalanceEncoderTests.swift`

- [ ] **Step 1: Create the JSON fixture (documents the inputs; mirrors what the client would normalize)**

Create `shared/fixtures/balances-two.json`:

```json
[
  { "kind": "openrouter", "name": "OpenRouter", "currency": "USD",
    "remaining": 42.10, "usage": 7.90, "updatedAt": 1748455822 },
  { "kind": "deepseek", "name": "DeepSeek", "currency": "CNY",
    "remaining": 318.50, "updatedAt": 1748455822 }
]
```

- [ ] **Step 2: Add an env-gated freeze/lock test**

Append to `BalanceEncoderTests`:

```swift
    // Golden cross-side fixture. Run once with FREEZE_FIXTURES=1 to (re)write the
    // .hex from the encoder; thereafter (and in CI) it asserts the bytes are stable.
    @Test func goldenHexMatches() throws {
        let data = BalanceEncoder.encode(.balanceFixtureTwo)
        let url = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent()
            .appendingPathComponent("shared/fixtures/balances-two.hex")
        if ProcessInfo.processInfo.environment["FREEZE_FIXTURES"] != nil {
            let hex = data.map { String(format: "%02x", $0) }.joined()
            try (hex + "\n").write(to: url, atomically: true, encoding: .utf8)
        }
        let expected = try Fixtures.loadHex("balances-two")
        #expect(data == expected)
    }
```

- [ ] **Step 3: Freeze the hex**

Run: `cd bridge && FREEZE_FIXTURES=1 swift test --filter "BalanceEncoderTests/goldenHexMatches"`
Expected: PASS, and `shared/fixtures/balances-two.hex` now exists (80 bytes → 160 hex chars).

Run: `cat shared/fixtures/balances-two.hex` → Expected: a single 160-char hex line starting `010002008e513768`.

- [ ] **Step 4: Verify the lock holds without the env var**

Run: `cd bridge && swift test --filter "BalanceEncoderTests/goldenHexMatches"`
Expected: PASS (now comparing against the frozen file).

- [ ] **Step 5: Commit**

```bash
git add shared/fixtures/balances-two.json shared/fixtures/balances-two.hex bridge/Tests/StopwatchBridgeTests/BalanceEncoderTests.swift
git commit -m "bridge: freeze golden BalanceSnapshot hex fixture"
```

---

## Task 4: ProvidersConfig (bridge)

**Files:**
- Create: `bridge/Sources/StopwatchBridge/ProvidersConfig.swift`
- Test: `bridge/Tests/StopwatchBridgeTests/ProvidersConfigTests.swift`

- [ ] **Step 1: Write the failing test**

Create `bridge/Tests/StopwatchBridgeTests/ProvidersConfigTests.swift`:

```swift
import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct ProvidersConfigTests {

    @Test func decodesAndAppliesKindDefaults() throws {
        let json = Data("""
        [ { "id": "openrouter", "name": "OpenRouter", "kind": "openrouter", "lowThreshold": 5.0 } ]
        """.utf8)
        let providers = try ProvidersConfig.decode(json)
        #expect(providers.count == 1)
        let p = providers[0].resolved()      // kind defaults filled in
        #expect(p.kind == .openrouter)
        #expect(p.endpoint == "https://openrouter.ai/api/v1/credits")
        #expect(p.balancePath == "data.total_credits")
        #expect(p.usagePath == "data.total_usage")
        #expect(p.currency == "USD")
        #expect(p.pollSeconds == 900)
        #expect(p.lowThreshold == 5.0)
    }

    @Test func genericRequiresExplicitFields() throws {
        let json = Data("""
        [ { "id": "x", "name": "X", "kind": "generic",
            "endpoint": "https://api.x.com/bal", "balancePath": "data.balance", "currency": "USD" } ]
        """.utf8)
        let p = try ProvidersConfig.decode(json)[0].resolved()
        #expect(p.endpoint == "https://api.x.com/bal")
        #expect(p.usagePath == nil)
        #expect(p.currencyDecimals == 2)
    }

    @Test func missingFileYieldsEmpty() throws {
        let url = URL(fileURLWithPath: "/tmp/does-not-exist-\(UUID()).json")
        #expect(try ProvidersConfig.load(from: url).isEmpty)
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd bridge && swift test --filter ProvidersConfigTests`
Expected: FAIL — `cannot find 'ProvidersConfig' in scope`.

- [ ] **Step 3: Implement `ProvidersConfig.swift`**

Create `bridge/Sources/StopwatchBridge/ProvidersConfig.swift`:

```swift
// bridge/Sources/StopwatchBridge/ProvidersConfig.swift
import Foundation

/// One entry of `providers.json`. Sparse on disk; `resolved()` fills `kind` defaults.
public struct ProviderConfig: Codable, Equatable, Sendable {
    public var id: String
    public var name: String
    public var kind: String?
    public var endpoint: String?
    public var auth: String?          // "bearer" (default) | "none"
    public var balancePath: String?
    public var usagePath: String?
    public var currency: String?      // literal "USD" or "path:<dotted>"
    public var currencyDecimals: Int?
    public var pollSeconds: Int?
    public var lowThreshold: Double?

    public struct Resolved: Equatable, Sendable {
        public var id: String, name: String
        public var kind: BalanceKind
        public var endpoint: String, auth: String
        public var balancePath: String, usagePath: String?
        public var currency: String, currencyDecimals: Int
        public var pollSeconds: Int, lowThreshold: Double?
    }

    public func resolved() -> Resolved {
        let k = BalanceKind(fromString: kind)
        let d = Self.defaults(for: k)
        return Resolved(
            id: id, name: name, kind: k,
            endpoint: endpoint ?? d.endpoint,
            auth: auth ?? d.auth,
            balancePath: balancePath ?? d.balancePath,
            usagePath: usagePath ?? d.usagePath,
            currency: currency ?? d.currency,
            currencyDecimals: currencyDecimals ?? 2,
            pollSeconds: pollSeconds ?? 900,
            lowThreshold: lowThreshold
        )
    }

    struct Defaults { var endpoint, auth, balancePath, currency: String; var usagePath: String? }

    static func defaults(for kind: BalanceKind) -> Defaults {
        switch kind {
        case .openrouter:
            return .init(endpoint: "https://openrouter.ai/api/v1/credits", auth: "bearer",
                         balancePath: "data.total_credits", currency: "USD", usagePath: "data.total_usage")
        case .deepseek:
            return .init(endpoint: "https://api.deepseek.com/user/balance", auth: "bearer",
                         balancePath: "balance_infos[0].total_balance",
                         currency: "path:balance_infos[0].currency", usagePath: nil)
        default:
            return .init(endpoint: "", auth: "bearer", balancePath: "", currency: "USD", usagePath: nil)
        }
    }
}

public enum ProvidersConfig {
    public static var defaultPath: URL {
        Config.defaultPath.deletingLastPathComponent().appendingPathComponent("providers.json")
    }

    public static func decode(_ data: Data) throws -> [ProviderConfig] {
        try JSONDecoder().decode([ProviderConfig].self, from: data)
    }

    public static func load(from url: URL = defaultPath) throws -> [ProviderConfig] {
        guard FileManager.default.fileExists(atPath: url.path) else { return [] }
        return try decode(Data(contentsOf: url))
    }
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cd bridge && swift test --filter ProvidersConfigTests`
Expected: PASS (3 tests).

- [ ] **Step 5: Commit**

```bash
git add bridge/Sources/StopwatchBridge/ProvidersConfig.swift bridge/Tests/StopwatchBridgeTests/ProvidersConfigTests.swift
git commit -m "bridge: add providers.json config model with kind defaults"
```

---

## Task 5: KeyStore + Keychain (bridge)

**Files:**
- Create: `bridge/Sources/StopwatchBridge/KeychainStore.swift`
- Test: `bridge/Tests/StopwatchBridgeTests/KeychainStoreTests.swift`

- [ ] **Step 1: Write the failing test (real Keychain round-trip, skipped where unavailable)**

Create `bridge/Tests/StopwatchBridgeTests/KeychainStoreTests.swift`:

```swift
import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct KeychainStoreTests {

    @Test func fakeStoreRoundTrips() {
        let fake = FakeKeyStore()
        #expect(fake.key(for: "openrouter") == nil)
        fake.set("sk-abc", for: "openrouter")
        #expect(fake.key(for: "openrouter") == "sk-abc")
        fake.delete("openrouter")
        #expect(fake.key(for: "openrouter") == nil)
    }

    @Test func keychainRoundTripsWhenAvailable() throws {
        let store = KeychainStore(service: "dev.stopwatch.bridge.test-\(UUID().uuidString)")
        // Some CI sandboxes have no usable keychain (errSecMissingEntitlement); skip there.
        do {
            try store.set("sk-live", for: "deepseek")
        } catch KeychainStore.KeychainError.unavailable {
            return  // environment without a keychain; nothing to assert
        }
        #expect(store.key(for: "deepseek") == "sk-live")
        try store.delete("deepseek")
        #expect(store.key(for: "deepseek") == nil)
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd bridge && swift test --filter KeychainStoreTests`
Expected: FAIL — `cannot find 'FakeKeyStore' / 'KeychainStore' in scope`.

- [ ] **Step 3: Implement `KeychainStore.swift`**

Create `bridge/Sources/StopwatchBridge/KeychainStore.swift`:

```swift
// bridge/Sources/StopwatchBridge/KeychainStore.swift
import Foundation
import Security

/// Read-only key lookup used by the balance fetcher. Implementations resolve a
/// provider id to its API key.
public protocol KeyStore: Sendable {
    func key(for id: String) -> String?
}

/// macOS Keychain-backed key storage. Items are generic passwords keyed by
/// (service, account=id), accessible after first unlock so the launchd daemon
/// can read them unattended.
public final class KeychainStore: KeyStore, @unchecked Sendable {
    public enum KeychainError: Error { case unavailable, osStatus(OSStatus) }

    private let service: String
    public init(service: String = "dev.stopwatch.bridge") { self.service = service }

    public func key(for id: String) -> String? {
        var query = baseQuery(id)
        query[kSecReturnData as String] = true
        query[kSecMatchLimit as String] = kSecMatchLimitOne
        var item: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &item)
        guard status == errSecSuccess, let data = item as? Data else { return nil }
        return String(data: data, encoding: .utf8)
    }

    public func set(_ secret: String, for id: String) throws {
        try delete(id)
        var attrs = baseQuery(id)
        attrs[kSecValueData as String] = Data(secret.utf8)
        attrs[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlock
        let status = SecItemAdd(attrs as CFDictionary, nil)
        if status == errSecMissingEntitlement || status == errSecNotAvailable {
            throw KeychainError.unavailable
        }
        guard status == errSecSuccess else { throw KeychainError.osStatus(status) }
    }

    public func delete(_ id: String) throws {
        let status = SecItemDelete(baseQuery(id) as CFDictionary)
        guard status == errSecSuccess || status == errSecItemNotFound else {
            throw KeychainError.osStatus(status)
        }
    }

    public func listIDs() -> [String] {
        var query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecReturnAttributes as String: true,
            kSecMatchLimit as String: kSecMatchLimitAll,
        ]
        var items: CFTypeRef?
        guard SecItemCopyMatching(query as CFDictionary, &items) == errSecSuccess,
              let arr = items as? [[String: Any]] else { return [] }
        return arr.compactMap { $0[kSecAttrAccount as String] as? String }.sorted()
    }

    private func baseQuery(_ id: String) -> [String: Any] {
        [ kSecClass as String: kSecClassGenericPassword,
          kSecAttrService as String: service,
          kSecAttrAccount as String: id ]
    }
}

/// In-memory KeyStore for tests.
public final class FakeKeyStore: KeyStore, @unchecked Sendable {
    private var store: [String: String]
    private let lock = NSLock()
    public init(_ initial: [String: String] = [:]) { store = initial }
    public func key(for id: String) -> String? { lock.lock(); defer { lock.unlock() }; return store[id] }
    public func set(_ s: String, for id: String) { lock.lock(); store[id] = s; lock.unlock() }
    public func delete(_ id: String) { lock.lock(); store[id] = nil; lock.unlock() }
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cd bridge && swift test --filter KeychainStoreTests`
Expected: PASS (2 tests; the keychain test self-skips if the environment has no keychain).

- [ ] **Step 5: Commit**

```bash
git add bridge/Sources/StopwatchBridge/KeychainStore.swift bridge/Tests/StopwatchBridgeTests/KeychainStoreTests.swift
git commit -m "bridge: add KeyStore protocol + Keychain-backed key storage"
```

---

## Task 6: Key management CLI (bridge)

**Files:**
- Create: `bridge/Sources/StopwatchBridge/KeyCommand.swift`
- Modify: `bridge/Sources/StopwatchBridge/Bridge.swift`

- [ ] **Step 1: Implement `KeyCommand.swift`**

Create `bridge/Sources/StopwatchBridge/KeyCommand.swift`:

```swift
// bridge/Sources/StopwatchBridge/KeyCommand.swift
import Foundation

enum KeyCommand {
    static func setKey(_ id: String) -> Int32 {
        FileHandle.standardError.write(Data("Paste the API key for '\(id)' and press Enter:\n".utf8))
        guard let secret = readLine(strippingNewline: true), !secret.isEmpty else {
            FileHandle.standardError.write(Data("no key read from stdin\n".utf8))
            return 1
        }
        do {
            try KeychainStore().set(secret, for: id)
            print("stored key for '\(id)' in the Keychain")
            return 0
        } catch {
            FileHandle.standardError.write(Data("failed to store key: \(error)\n".utf8))
            return 1
        }
    }

    static func listKeys() -> Int32 {
        let ids = KeychainStore().listIDs()
        if ids.isEmpty { print("no keys stored") } else { ids.forEach { print($0) } }
        return 0
    }

    static func deleteKey(_ id: String) -> Int32 {
        do { try KeychainStore().delete(id); print("deleted key for '\(id)'"); return 0 }
        catch { FileHandle.standardError.write(Data("failed: \(error)\n".utf8)); return 1 }
    }
}
```

- [ ] **Step 2: Wire commands into `Bridge.swift`**

In `Bridge.swift`, add cases to the `switch cmd` block (after `case "decode-snapshot":`):

```swift
        case "set-key":
            guard args.count >= 2 else { usage(); exit(2) }
            exit(KeyCommand.setKey(args[1]))
        case "list-keys":   exit(KeyCommand.listKeys())
        case "delete-key":
            guard args.count >= 2 else { usage(); exit(2) }
            exit(KeyCommand.deleteKey(args[1]))
```

And extend the `usage()` text:

```swift
          set-key <id>              Store a provider API key in the Keychain (reads stdin)
          list-keys                 List provider ids with stored keys
          delete-key <id>           Remove a stored provider key
```

- [ ] **Step 3: Verify build + manual smoke**

Run: `cd bridge && swift build` → Expected: `Build complete!`
Run: `echo "sk-test" | ./.build/debug/stopwatch-bridge set-key demo && ./.build/debug/stopwatch-bridge list-keys && ./.build/debug/stopwatch-bridge delete-key demo`
Expected: prints "stored key for 'demo'…", lists `demo`, then "deleted key for 'demo'". (Skip if the local environment has no keychain.)

- [ ] **Step 4: Commit**

```bash
git add bridge/Sources/StopwatchBridge/KeyCommand.swift bridge/Sources/StopwatchBridge/Bridge.swift
git commit -m "bridge: add set-key/list-keys/delete-key CLI commands"
```

---

## Task 7: BalanceClient (bridge)

**Files:**
- Create: `bridge/Sources/StopwatchBridge/BalanceClient.swift`
- Test: `bridge/Tests/StopwatchBridgeTests/BalanceClientTests.swift`

- [ ] **Step 1: Write the failing test**

Create `bridge/Tests/StopwatchBridgeTests/BalanceClientTests.swift`:

```swift
import Foundation
import Testing
@testable import StopwatchBridge

@Suite(.serialized) struct BalanceClientTests {

    private func stubSession() -> URLSession {
        let cfg = URLSessionConfiguration.ephemeral
        cfg.protocolClasses = [BalanceStubURLProtocol.self]
        return URLSession(configuration: cfg)
    }

    private func cfg(_ id: String, kind: String, low: Double? = nil) -> ProviderConfig.Resolved {
        var c = ProviderConfig(id: id, name: id.capitalized, kind: kind)
        c.lowThreshold = low
        return c.resolved()
    }

    @Test func openRouterRemainingIsCreditsMinusUsage() async {
        BalanceStubURLProtocol.routes = [
            "openrouter.ai": .init(status: 200, body: Data(#"{"data":{"total_credits":50.0,"total_usage":7.9}}"#.utf8))
        ]
        let client = BalanceClient(keyStore: FakeKeyStore(["or": "sk-x"]), session: stubSession())
        let snap = await client.fetchAll([cfg("or", kind: "openrouter", low: 5.0)], now: .init(timeIntervalSince1970: 100))
        #expect(snap.providers.count == 1)
        let p = snap.providers[0]
        #expect(p.status == .ok)
        #expect(p.remaining == 42.1)             // 50 - 7.9
        #expect(p.usage == 7.9)
        #expect(p.currencyCode == "USD")
        #expect(p.isLow == false)                // 42.1 >= 5
    }

    @Test func deepSeekParsesStringBalanceAndInBodyCurrency() async {
        BalanceStubURLProtocol.routes = [
            "api.deepseek.com": .init(status: 200, body: Data(#"{"is_available":true,"balance_infos":[{"currency":"CNY","total_balance":"318.50"}]}"#.utf8))
        ]
        let client = BalanceClient(keyStore: FakeKeyStore(["ds": "sk-y"]), session: stubSession())
        let p = await client.fetchAll([cfg("ds", kind: "deepseek")], now: .init(timeIntervalSince1970: 100)).providers[0]
        #expect(p.remaining == 318.5)
        #expect(p.currencyCode == "CNY")
        #expect(p.status == .ok)
    }

    @Test func missingKeyIsAuthError() async {
        let client = BalanceClient(keyStore: FakeKeyStore(), session: stubSession())
        let p = await client.fetchAll([cfg("or", kind: "openrouter")], now: .init(timeIntervalSince1970: 100)).providers[0]
        #expect(p.status == .authError)
        #expect(p.remaining == nil)
    }

    @Test func http401IsAuthError_402IsDepleted_timeoutUnreachable() async {
        BalanceStubURLProtocol.routes = ["api.deepseek.com": .init(status: 401, body: Data())]
        let c = BalanceClient(keyStore: FakeKeyStore(["ds": "k"]), session: stubSession())
        #expect(await c.fetchAll([cfg("ds", kind: "deepseek")], now: .init()).providers[0].status == .authError)

        BalanceStubURLProtocol.routes = ["api.deepseek.com": .init(status: 402, body: Data())]
        #expect(await c.fetchAll([cfg("ds", kind: "deepseek")], now: .init()).providers[0].status == .depleted)

        BalanceStubURLProtocol.routes = ["api.deepseek.com": .init(error: URLError(.timedOut))]
        #expect(await c.fetchAll([cfg("ds", kind: "deepseek")], now: .init()).providers[0].status == .unreachable)
    }

    @Test func lowThresholdFlagsBalance() async {
        BalanceStubURLProtocol.routes = [
            "openrouter.ai": .init(status: 200, body: Data(#"{"data":{"total_credits":4.0,"total_usage":1.0}}"#.utf8))
        ]
        let c = BalanceClient(keyStore: FakeKeyStore(["or": "k"]), session: stubSession())
        let p = await c.fetchAll([cfg("or", kind: "openrouter", low: 5.0)], now: .init()).providers[0]
        #expect(p.remaining == 3.0)
        #expect(p.isLow == true)                 // 3 < 5
    }
}

final class BalanceStubURLProtocol: URLProtocol {
    struct Route { var status: Int = 200; var body: Data = .init(); var error: Error? = nil }
    nonisolated(unsafe) static var routes: [String: Route] = [:]   // keyed by url host substring

    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
    override func startLoading() {
        let host = request.url?.host ?? ""
        let route = Self.routes.first { host.contains($0.key) }?.value ?? Route(status: 404)
        if let err = route.error { client?.urlProtocol(self, didFailWithError: err); return }
        let resp = HTTPURLResponse(url: request.url!, statusCode: route.status, httpVersion: "HTTP/1.1", headerFields: nil)!
        client?.urlProtocol(self, didReceive: resp, cacheStoragePolicy: .notAllowed)
        client?.urlProtocol(self, didLoad: route.body)
        client?.urlProtocolDidFinishLoading(self)
    }
    override func stopLoading() {}
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd bridge && swift test --filter BalanceClientTests`
Expected: FAIL — `cannot find 'BalanceClient' in scope`.

- [ ] **Step 3: Implement `BalanceClient.swift`**

Create `bridge/Sources/StopwatchBridge/BalanceClient.swift`:

```swift
// bridge/Sources/StopwatchBridge/BalanceClient.swift
import Foundation

/// Fetches prepaid balances directly from provider consoles. One GET per
/// provider, keyed from the `KeyStore`, normalized via config selectors.
public actor BalanceClient {
    private let keyStore: KeyStore
    private let session: URLSession
    private let timeout: TimeInterval

    public init(keyStore: KeyStore, session: URLSession = .shared, timeout: TimeInterval = 20) {
        self.keyStore = keyStore; self.session = session; self.timeout = timeout
    }

    /// Fetches every provider concurrently, preserving input order. Per-provider
    /// failures become a record with the matching status (never throws).
    public func fetchAll(_ providers: [ProviderConfig.Resolved], now: Date = Date()) async -> NormalizedBalance {
        let results: [(Int, NormalizedBalance.Provider)] = await withTaskGroup(of: (Int, NormalizedBalance.Provider).self) { group in
            for (i, p) in providers.enumerated() {
                group.addTask { (i, await self.fetchOne(p, now: now)) }
            }
            var acc: [(Int, NormalizedBalance.Provider)] = []
            for await r in group { acc.append(r) }
            return acc
        }
        let ordered = results.sorted { $0.0 < $1.0 }.map { $0.1 }
        return NormalizedBalance(capturedAt: now, flags: [], providers: ordered)
    }

    private func fetchOne(_ p: ProviderConfig.Resolved, now: Date) async -> NormalizedBalance.Provider {
        func record(status: BalanceStatus, remaining: Double? = nil, unlimited: Bool = false,
                    usage: Double? = nil, currency: String = "", updatedAt: Date? = nil, isLow: Bool = false)
        -> NormalizedBalance.Provider {
            .init(kind: p.kind, name: p.name, status: status, currencyCode: currency,
                  currencyDecimals: p.currencyDecimals, remaining: remaining, unlimited: unlimited,
                  usage: usage, updatedAt: updatedAt, isLow: isLow)
        }

        // 1. Resolve key (unless keyless).
        var key: String? = nil
        if p.auth != "none" {
            guard let k = keyStore.key(for: p.id) else { return record(status: .authError) }
            key = k
        }

        // 2. Fetch (OpenRouter falls back /credits → /key).
        let attempts = openRouterFallback(p)
        var lastStatus: BalanceStatus = .unreachable
        for attempt in attempts {
            switch await get(attempt.endpoint, key: key) {
            case .failure(let s): lastStatus = s
            case .success(let obj):
                guard let bal = numberAt(attempt.balancePath, in: obj) else { lastStatus = .unreachable; continue }
                let usage = attempt.usagePath.flatMap { numberAt($0, in: obj) }
                let remaining = attempt.unlimitedIfNull && rawIsNull(attempt.balancePath, in: obj)
                    ? nil : (usage.map { bal - $0 } ?? bal)
                let unlimited = attempt.unlimitedIfNull && rawIsNull(attempt.balancePath, in: obj)
                let currency = resolveCurrency(attempt.currency, in: obj)
                let low = (p.lowThreshold).map { remaining != nil && remaining! < $0 } ?? false
                return record(status: .ok, remaining: remaining, unlimited: unlimited,
                              usage: usage, currency: currency, updatedAt: now, isLow: low)
            }
        }
        return record(status: lastStatus)
    }

    // Endpoint attempt with its own selectors (so the /key fallback can differ).
    private struct Attempt { var endpoint: String; var balancePath: String; var usagePath: String?
                             var currency: String; var unlimitedIfNull: Bool }

    private func openRouterFallback(_ p: ProviderConfig.Resolved) -> [Attempt] {
        var first = Attempt(endpoint: p.endpoint, balancePath: p.balancePath,
                            usagePath: p.usagePath, currency: p.currency, unlimitedIfNull: false)
        guard p.kind == .openrouter else { return [first] }
        // /credits may need a management key; fall back to /key (works with a normal key).
        let keyEndpoint = Attempt(endpoint: "https://openrouter.ai/api/v1/key",
                                  balancePath: "data.limit_remaining", usagePath: nil,
                                  currency: "USD", unlimitedIfNull: true)
        first.currency = "USD"
        return [first, keyEndpoint]
    }

    private enum GetResult { case success(Any); case failure(BalanceStatus) }

    private func get(_ endpoint: String, key: String?) async -> GetResult {
        guard let url = URL(string: endpoint) else { return .failure(.unreachable) }
        var req = URLRequest(url: url); req.timeoutInterval = timeout
        if let key { req.setValue("Bearer \(key)", forHTTPHeaderField: "Authorization") }
        req.setValue("application/json", forHTTPHeaderField: "Accept")
        do {
            let (data, resp) = try await session.data(for: req)
            if let http = resp as? HTTPURLResponse {
                switch http.statusCode {
                case 200: break
                case 401, 403: return .failure(.authError)
                case 402:      return .failure(.depleted)
                default:       return .failure(.unreachable)
                }
            }
            let obj = try JSONSerialization.jsonObject(with: data)
            return .success(obj)
        } catch {
            return .failure(.unreachable)
        }
    }

    // MARK: - dotted/indexed JSON path

    /// Evaluates `a.b[0].c` against a JSONSerialization object graph.
    static func value(at path: String, in root: Any) -> Any? {
        var current: Any? = root
        for rawComponent in path.split(separator: ".") {
            var comp = String(rawComponent)
            // Split trailing [n] indices.
            var indices: [Int] = []
            while let open = comp.lastIndex(of: "["), comp.hasSuffix("]") {
                let idxStr = comp[comp.index(after: open)..<comp.index(before: comp.endIndex)]
                guard let n = Int(idxStr) else { break }
                indices.insert(n, at: 0)
                comp = String(comp[comp.startIndex..<open])
            }
            if !comp.isEmpty {
                guard let dict = current as? [String: Any] else { return nil }
                current = dict[comp]
            }
            for n in indices {
                guard let arr = current as? [Any], n >= 0, n < arr.count else { return nil }
                current = arr[n]
            }
        }
        return current
    }

    private func numberAt(_ path: String, in root: Any) -> Double? {
        switch BalanceClient.value(at: path, in: root) {
        case let d as Double: return d
        case let i as Int:    return Double(i)
        case let n as NSNumber: return n.doubleValue
        case let s as String: return Double(s)
        default: return nil
        }
    }

    private func rawIsNull(_ path: String, in root: Any) -> Bool {
        let v = BalanceClient.value(at: path, in: root)
        return v == nil || v is NSNull
    }

    private func resolveCurrency(_ spec: String, in root: Any) -> String {
        if spec.hasPrefix("path:") {
            let p = String(spec.dropFirst("path:".count))
            return (BalanceClient.value(at: p, in: root) as? String)?.uppercased() ?? ""
        }
        return spec.uppercased()
    }
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cd bridge && swift test --filter BalanceClientTests`
Expected: PASS (5 tests).

- [ ] **Step 5: Add a dotted-path unit test (guards the evaluator directly)**

Append to `BalanceClientTests`:

```swift
    @Test func dottedPathEvaluator() {
        let obj = try! JSONSerialization.jsonObject(with: Data(#"{"a":{"b":[{"c":9}]},"d":"5.5"}"#.utf8))
        #expect(BalanceClient.value(at: "a.b[0].c", in: obj) as? Int == 9)
        #expect(BalanceClient.value(at: "d", in: obj) as? String == "5.5")
        #expect(BalanceClient.value(at: "a.missing", in: obj) == nil)
        #expect(BalanceClient.value(at: "a.b[3].c", in: obj) == nil)   // out of range
    }
```

Run: `cd bridge && swift test --filter BalanceClientTests` → Expected: PASS (6 tests).

- [ ] **Step 6: Commit**

```bash
git add bridge/Sources/StopwatchBridge/BalanceClient.swift bridge/Tests/StopwatchBridgeTests/BalanceClientTests.swift
git commit -m "bridge: add BalanceClient with per-provider adapters + dotted-path JSON"
```

---

## Task 8: BalanceCache (bridge)

**Files:**
- Create: `bridge/Sources/StopwatchBridge/BalanceCache.swift`
- Test: `bridge/Tests/StopwatchBridgeTests/BalanceClientTests.swift` (append a cache suite)

- [ ] **Step 1: Write the failing test**

Append to `BalanceClientTests.swift` (new suite):

```swift
@Suite struct BalanceCacheTests {
    private func ok(_ id: String, _ remaining: Double) -> NormalizedBalance.Provider {
        .init(kind: .generic, name: id, status: .ok, currencyCode: "USD",
              remaining: remaining, updatedAt: Date(timeIntervalSince1970: 100))
    }
    private func failed(_ id: String) -> NormalizedBalance.Provider {
        .init(kind: .generic, name: id, status: .unreachable, currencyCode: "", remaining: nil, updatedAt: nil)
    }

    @Test func retainsLastGoodForFailedProvider() {
        var cache = BalanceCache()
        // First poll: both succeed.
        _ = cache.record(NormalizedBalance(capturedAt: Date(timeIntervalSince1970: 100), flags: [],
                                           providers: [ok("a", 10), ok("b", 20)]))
        // Second poll: 'a' fails, 'b' updates.
        let merged = cache.record(NormalizedBalance(capturedAt: Date(timeIntervalSince1970: 200), flags: [],
                                                    providers: [failed("a"), ok("b", 15)]))
        var snap = Snapshot_decodeBalances(merged)
        #expect(snap.count == 2)
        let a = snap.first { $0.name == "a" }!
        #expect(a.status == BalanceStatus.stale.rawValue)   // last-good kept, marked stale
        #expect(a.balanceMinor == 1000)                     // $10.00 retained
        let b = snap.first { $0.name == "b" }!
        #expect(b.balanceMinor == 1500)                     // fresh $15.00
    }
}
```

This needs a tiny decode helper in the test file (mirrors the wire layout) so we can assert on bytes without the firmware:

```swift
struct DecodedBalance { var name: String; var status: UInt8; var balanceMinor: UInt32 }
func Snapshot_decodeBalances(_ data: Data) -> [DecodedBalance] {
    let b = [UInt8](data); let count = Int(b[2]); var out: [DecodedBalance] = []
    for i in 0..<count {
        let o = 8 + 36 * i
        let name = String(decoding: b[o+20..<o+36].prefix { $0 != 0 }, as: UTF8.self)
        let bal = UInt32(b[o+8]) | UInt32(b[o+9])<<8 | UInt32(b[o+10])<<16 | UInt32(b[o+11])<<24
        out.append(.init(name: name, status: b[o+1], balanceMinor: bal))
    }
    return out
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd bridge && swift test --filter BalanceCacheTests`
Expected: FAIL — `cannot find 'BalanceCache' in scope`.

- [ ] **Step 3: Implement `BalanceCache.swift`**

Create `bridge/Sources/StopwatchBridge/BalanceCache.swift`:

```swift
// bridge/Sources/StopwatchBridge/BalanceCache.swift
import Foundation

/// Per-provider last-good cache. A failed provider keeps its previous value
/// (marked stale) so one bad endpoint never blanks the others. Keyed by name
/// (the wire identity shown on the watch).
struct BalanceCache {
    private var lastGood: [String: NormalizedBalance.Provider] = [:]

    mutating func record(_ fresh: NormalizedBalance) -> Data {
        var merged: [NormalizedBalance.Provider] = []
        for p in fresh.providers {
            if p.status == .ok {
                lastGood[p.name] = p
                merged.append(p)
            } else if var prev = lastGood[p.name] {
                prev.status = .stale          // keep last-good numbers, surface staleness
                merged.append(prev)
            } else {
                merged.append(p)              // no history → show the error record as-is
            }
        }
        return BalanceEncoder.encode(NormalizedBalance(capturedAt: fresh.capturedAt,
                                                       flags: fresh.flags, providers: merged))
    }

    mutating func recordFailure(capturedAt: Date = Date()) -> Data {
        // Whole-cycle failure: re-emit last-good (all stale) + bridgeError, or an empty error frame.
        guard !lastGood.isEmpty else { return BalanceEncoder.errorEmpty(capturedAt: capturedAt) }
        let stale = lastGood.values.map { p -> NormalizedBalance.Provider in
            var q = p; q.status = .stale; return q
        }
        return BalanceEncoder.encode(NormalizedBalance(capturedAt: capturedAt,
                                                       flags: [.stale, .bridgeError], providers: stale))
    }
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cd bridge && swift test --filter BalanceCacheTests` → Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add bridge/Sources/StopwatchBridge/BalanceCache.swift bridge/Tests/StopwatchBridgeTests/BalanceClientTests.swift
git commit -m "bridge: add per-provider BalanceCache (last-good merge)"
```

---

## Task 9: Serve & poll balances (bridge integration)

**Files:**
- Modify: `bridge/Sources/StopwatchBridge/GATTPeripheral.swift`
- Modify: `bridge/Sources/StopwatchBridge/BridgeService.swift`

- [ ] **Step 1: Add the balance characteristic to `GATTPeripheral.swift`**

Add a stored property beside `costChar`:

```swift
    private let balanceChar: CBMutableCharacteristic
```

Add beside `currentCost` / `pendingCostNotify`:

```swift
    private var currentBalance: Data = BalanceEncoder.staleEmpty()
    private var pendingBalanceNotify: Data?
```

In `init()`, after constructing `costChar`:

```swift
        self.balanceChar = CBMutableCharacteristic(
            type: Protocol.balanceSnapshotUUID,
            properties: [.read, .notify],
            value: nil,
            permissions: [.readable]
        )
```

Change the service characteristics line to include it:

```swift
        svc.characteristics = [self.snapshotChar, self.triggerChar, self.costChar, self.balanceChar]
```

Add the update method after `updateCostSnapshot`:

```swift
    public func updateBalanceSnapshot(_ data: Data) {
        precondition(data.count >= Protocol.balanceHeaderSize, "balance snapshot too short")
        currentBalance = data
        if !manager.updateValue(data, for: balanceChar, onSubscribedCentrals: nil) {
            pendingBalanceNotify = data
        } else {
            pendingBalanceNotify = nil
        }
    }
```

In `handleRead`, add a case to the switch:

```swift
        case Protocol.balanceSnapshotUUID: source = currentBalance
```

In `flushPendingNotify`, add:

```swift
        if let pendingBalance = pendingBalanceNotify,
           peripheral.updateValue(pendingBalance, for: balanceChar, onSubscribedCentrals: nil) {
            pendingBalanceNotify = nil
        }
```

- [ ] **Step 2: Wire polling into `BridgeService.swift`**

Add stored properties:

```swift
    private let balanceClient: BalanceClient
    private var balanceCache = BalanceCache()
    private let providers: [ProviderConfig.Resolved]
    private var lastPolled: [String: Date] = [:]
```

In `init(config:)`, after `self.client = …`:

```swift
        let loadedProviders = (try? ProvidersConfig.load())?.map { $0.resolved() } ?? []
        self.providers = loadedProviders
        self.balanceClient = BalanceClient(keyStore: KeychainStore())
```

In `run()`, after `Task { await self.prewarmLoop() }`:

```swift
        if !providers.isEmpty { Task { await self.balancePollLoop() } }
```

In `handleRefresh(scope:)`, add the early-return branch beside the cost one:

```swift
        if scope == Protocol.triggerScopeBalances {
            await handleBalanceRefresh(force: true)
            return
        }
```

Add the poll loop + refresh handler:

```swift
    private func balancePollLoop() async {
        try? await Task.sleep(nanoseconds: 2_000_000_000)
        while !Task.isCancelled {
            await handleBalanceRefresh(force: false)
            try? await Task.sleep(nanoseconds: 30_000_000_000)   // 30 s tick; per-provider cadence below
        }
    }

    /// Polls providers whose `pollSeconds` has elapsed (or all, when `force`).
    private func handleBalanceRefresh(force: Bool) async {
        let now = Date()
        let due = providers.filter { p in
            force || (lastPolled[p.id].map { now.timeIntervalSince($0) >= Double(p.pollSeconds) } ?? true)
        }
        guard !due.isEmpty else { return }
        let fresh = await balanceClient.fetchAll(due, now: now)
        for p in due { lastPolled[p.id] = now }
        let bytes = balanceCache.record(fresh)
        await peripheral.updateBalanceSnapshot(bytes)
        FileHandle.standardOutput.write(Data("balance poll: \(due.count) provider(s)\n".utf8))
    }
```

- [ ] **Step 3: Verify build + full bridge test suite**

Run: `cd bridge && swift build` → Expected: `Build complete!`
Run: `cd bridge && swift test` → Expected: all suites PASS (existing + new balance suites).

- [ ] **Step 4: Commit**

```bash
git add bridge/Sources/StopwatchBridge/GATTPeripheral.swift bridge/Sources/StopwatchBridge/BridgeService.swift
git commit -m "bridge: serve BalanceSnapshot + periodic poll + trigger 0x05"
```

---

## Task 10: BalanceCodec (firmware)

**Files:**
- Create: `firmware/src/BalanceCodec.h`, `firmware/src/BalanceCodec.cpp`
- Test: `firmware/test/test_balance_codec/test_main.cpp`

- [ ] **Step 1: Write the header `BalanceCodec.h`**

Create `firmware/src/BalanceCodec.h`:

```cpp
#pragma once
#include "Protocol.h"
#include <cstdint>
#include <cstddef>
#include <optional>

namespace stopwatch {

struct BalanceRecord {
    BalanceKind   kind   = BalanceKind::Generic;
    BalanceStatus status = BalanceStatus::Ok;
    bool          low    = false;
    char          currency[4] = {0};        // up to 3 chars + null
    uint8_t       decimals = 2;
    std::optional<uint32_t> balanceMinor;    // nullopt iff 0xFFFFFFFF (unknown)
    bool          unlimited = false;         // true iff 0xFFFFFFFE
    std::optional<uint32_t> usageMinor;
    uint32_t      updatedAt = 0;
    char          name[17] = {0};            // 16 wire bytes + null
};

struct BalanceSnapshot {
    uint8_t  versionMajor = 0;
    uint8_t  versionMinor = 0;
    uint8_t  recordCount  = 0;
    uint8_t  flags        = 0;
    uint32_t capturedAt   = 0;
    BalanceRecord records[kBalanceMaxRecords] = {};

    bool isStale()       const { return flags & kBalanceFlagStale; }
    bool isBridgeError() const { return flags & kBalanceFlagBridgeError; }
};

enum class BalanceDecodeResult : uint8_t { Ok, TooShort, MajorVersionTooNew, InvalidRecordCount };

BalanceDecodeResult decodeBalanceSnapshot(const uint8_t *bytes, size_t len, BalanceSnapshot &out);

}  // namespace stopwatch
```

- [ ] **Step 2: Write the failing test**

Create `firmware/test/test_balance_codec/test_main.cpp`:

```cpp
#include <unity.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <fstream>
#include <cctype>
#include "../../src/BalanceCodec.h"

using namespace stopwatch;

static std::vector<uint8_t> readHexFixture(const char *name) {
    std::string path = std::string("../shared/fixtures/") + name + ".hex";
    std::ifstream f(path);
    if (!f) { char b[256]; snprintf(b, sizeof(b), "missing fixture: %s", path.c_str()); TEST_FAIL_MESSAGE(b); }
    std::string raw((std::istreambuf_iterator<char>(f)), {});
    std::string hex; for (char c : raw) if (!isspace((unsigned char)c)) hex.push_back(c);
    if (hex.size() % 2 != 0) TEST_FAIL_MESSAGE("hex fixture has odd length");
    std::vector<uint8_t> out;
    for (size_t i = 0; i < hex.size(); i += 2) { unsigned v = 0; sscanf(hex.c_str()+i, "%2x", &v); out.push_back((uint8_t)v); }
    return out;
}

void test_balancesFixtureDecodes(void) {
    auto bytes = readHexFixture("balances-two");
    TEST_ASSERT_EQUAL(80, bytes.size());

    BalanceSnapshot bs;
    auto rc = decodeBalanceSnapshot(bytes.data(), bytes.size(), bs);
    TEST_ASSERT_EQUAL((int)BalanceDecodeResult::Ok, (int)rc);
    TEST_ASSERT_EQUAL(1, bs.versionMajor);
    TEST_ASSERT_EQUAL(2, bs.recordCount);

    TEST_ASSERT_EQUAL((int)BalanceKind::OpenRouter, (int)bs.records[0].kind);
    TEST_ASSERT_EQUAL_STRING("USD", bs.records[0].currency);
    TEST_ASSERT_TRUE(bs.records[0].balanceMinor.has_value());
    TEST_ASSERT_EQUAL(4210, bs.records[0].balanceMinor.value());
    TEST_ASSERT_EQUAL(790, bs.records[0].usageMinor.value());
    TEST_ASSERT_EQUAL_STRING("OpenRouter", bs.records[0].name);
    TEST_ASSERT_FALSE(bs.records[0].low);

    TEST_ASSERT_EQUAL((int)BalanceKind::DeepSeek, (int)bs.records[1].kind);
    TEST_ASSERT_EQUAL_STRING("CNY", bs.records[1].currency);
    TEST_ASSERT_EQUAL(31850, bs.records[1].balanceMinor.value());
    TEST_ASSERT_FALSE(bs.records[1].usageMinor.has_value());   // 0xFFFFFFFF → nullopt
    TEST_ASSERT_EQUAL_STRING("DeepSeek", bs.records[1].name);
}

void test_unknownAndUnlimited(void) {
    std::vector<uint8_t> b(kBalanceHeaderSize + kBalanceRecordSize, 0);
    b[0] = 1; b[2] = 1;                                   // major, count
    for (int i = 8; i < 12; ++i) b[kBalanceHeaderSize + i] = 0xFF;   // balanceMinor = 0xFFFFFFFF
    BalanceSnapshot bs;
    TEST_ASSERT_EQUAL((int)BalanceDecodeResult::Ok, (int)decodeBalanceSnapshot(b.data(), b.size(), bs));
    TEST_ASSERT_FALSE(bs.records[0].balanceMinor.has_value());
    TEST_ASSERT_FALSE(bs.records[0].unlimited);

    b[kBalanceHeaderSize + 8] = 0xFE; b[kBalanceHeaderSize + 9] = 0xFF;
    b[kBalanceHeaderSize + 10] = 0xFF; b[kBalanceHeaderSize + 11] = 0xFF;   // 0xFFFFFFFE
    TEST_ASSERT_EQUAL((int)BalanceDecodeResult::Ok, (int)decodeBalanceSnapshot(b.data(), b.size(), bs));
    TEST_ASSERT_TRUE(bs.records[0].unlimited);
    TEST_ASSERT_FALSE(bs.records[0].balanceMinor.has_value());
}

void test_lowFlagAndFutureMajor(void) {
    std::vector<uint8_t> b(kBalanceHeaderSize + kBalanceRecordSize, 0);
    b[0] = 1; b[2] = 1; b[kBalanceHeaderSize + 2] = kBalanceRecordFlagLow;
    BalanceSnapshot bs;
    TEST_ASSERT_EQUAL((int)BalanceDecodeResult::Ok, (int)decodeBalanceSnapshot(b.data(), b.size(), bs));
    TEST_ASSERT_TRUE(bs.records[0].low);

    uint8_t f[kBalanceHeaderSize] = { 99, 0, 0, 0, 0, 0, 0, 0 };
    TEST_ASSERT_EQUAL((int)BalanceDecodeResult::MajorVersionTooNew,
                      (int)decodeBalanceSnapshot(f, sizeof(f), bs));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_balancesFixtureDecodes);
    RUN_TEST(test_unknownAndUnlimited);
    RUN_TEST(test_lowFlagAndFutureMajor);
    return UNITY_END();
}
```

- [ ] **Step 3: Run to verify it fails**

Run: `cd firmware && pio test -e native -f test_balance_codec`
Expected: FAIL — link error / `BalanceCodec.h` has no `decodeBalanceSnapshot` definition.

- [ ] **Step 4: Implement `BalanceCodec.cpp`**

Create `firmware/src/BalanceCodec.cpp`:

```cpp
#include "BalanceCodec.h"
#include <cstring>

namespace stopwatch {

namespace {
uint32_t readU32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
}  // namespace

BalanceDecodeResult decodeBalanceSnapshot(const uint8_t *bytes, size_t len, BalanceSnapshot &out) {
    if (len < kBalanceHeaderSize) return BalanceDecodeResult::TooShort;

    uint8_t major = bytes[0];
    uint8_t count = bytes[2];
    if (major > kBalanceVersionMajor) return BalanceDecodeResult::MajorVersionTooNew;
    if (count > kBalanceMaxRecords)   return BalanceDecodeResult::InvalidRecordCount;
    if (len < (size_t)(kBalanceHeaderSize + count * kBalanceRecordSize)) return BalanceDecodeResult::TooShort;

    out.versionMajor = major;
    out.versionMinor = bytes[1];
    out.recordCount  = count;
    out.flags        = bytes[3];
    out.capturedAt   = readU32(bytes + 4);

    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t *r = bytes + kBalanceHeaderSize + i * kBalanceRecordSize;
        BalanceRecord &rec = out.records[i];
        rec.kind   = (BalanceKind)r[0];
        rec.status = (BalanceStatus)r[1];
        rec.low    = (r[2] & kBalanceRecordFlagLow) != 0;
        memcpy(rec.currency, r + 3, 3); rec.currency[3] = '\0';
        rec.decimals = r[6];
        uint32_t bal = readU32(r + 8);
        rec.unlimited    = (bal == 0xFFFFFFFEu);
        rec.balanceMinor = (bal == 0xFFFFFFFFu || bal == 0xFFFFFFFEu)
                           ? std::nullopt : std::optional<uint32_t>(bal);
        uint32_t use = readU32(r + 12);
        rec.usageMinor = (use == 0xFFFFFFFFu) ? std::nullopt : std::optional<uint32_t>(use);
        rec.updatedAt  = readU32(r + 16);
        memcpy(rec.name, r + 20, 16); rec.name[16] = '\0';
    }
    return BalanceDecodeResult::Ok;
}

}  // namespace stopwatch
```

- [ ] **Step 5: Run to verify it passes**

Run: `cd firmware && pio test -e native -f test_balance_codec`
Expected: PASS (3 tests). This proves cross-side wire compatibility against the same `balances-two.hex` the bridge froze.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/BalanceCodec.h firmware/src/BalanceCodec.cpp firmware/test/test_balance_codec/test_main.cpp
git commit -m "firmware: add BalanceCodec decoding BalanceSnapshot (mirrors bridge golden hex)"
```

---

## Task 11: BalanceFormat (firmware, pure)

**Files:**
- Create: `firmware/src/BalanceFormat.h`, `firmware/src/BalanceFormat.cpp`
- Test: `firmware/test/test_balance_format/test_main.cpp`

- [ ] **Step 1: Write the header**

Create `firmware/src/BalanceFormat.h`:

```cpp
#pragma once
#include <cstdint>
#include <cstddef>

namespace stopwatch {

/// Maps a 3-letter currency code to a display symbol, or the code itself if
/// there's no known glyph (e.g. returns "$", "¥", or "CNY").
const char *currencySymbol(const char *code);

/// Formats minor units with the given decimal places: (4210, 2) → "42.10".
void formatBalanceMinor(uint32_t minor, uint8_t decimals, char *buf, size_t bufSize);

}  // namespace stopwatch
```

- [ ] **Step 2: Write the failing test**

Create `firmware/test/test_balance_format/test_main.cpp`:

```cpp
#include <unity.h>
#include <cstring>
#include "../../src/BalanceFormat.h"
using namespace stopwatch;

void test_symbols(void) {
    TEST_ASSERT_EQUAL_STRING("$", currencySymbol("USD"));
    TEST_ASSERT_EQUAL_STRING("\xC2\xA5", currencySymbol("CNY"));  // ¥ (U+00A5 UTF-8)
    TEST_ASSERT_EQUAL_STRING("\xC2\xA5", currencySymbol("JPY"));
    TEST_ASSERT_EQUAL_STRING("EUR", currencySymbol("EUR"));        // no glyph mapping → code
    TEST_ASSERT_EQUAL_STRING("", currencySymbol(""));
}

void test_formatMinor(void) {
    char buf[16];
    formatBalanceMinor(4210, 2, buf, sizeof(buf)); TEST_ASSERT_EQUAL_STRING("42.10", buf);
    formatBalanceMinor(31850, 2, buf, sizeof(buf)); TEST_ASSERT_EQUAL_STRING("318.50", buf);
    formatBalanceMinor(800, 2, buf, sizeof(buf));  TEST_ASSERT_EQUAL_STRING("8.00", buf);
    formatBalanceMinor(5, 0, buf, sizeof(buf));    TEST_ASSERT_EQUAL_STRING("5", buf);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_symbols);
    RUN_TEST(test_formatMinor);
    return UNITY_END();
}
```

- [ ] **Step 3: Run to verify it fails**

Run: `cd firmware && pio test -e native -f test_balance_format`
Expected: FAIL — undefined `currencySymbol` / `formatBalanceMinor`.

- [ ] **Step 4: Implement `BalanceFormat.cpp`**

Create `firmware/src/BalanceFormat.cpp`:

```cpp
#include "BalanceFormat.h"
#include <cstdio>
#include <cstring>

namespace stopwatch {

const char *currencySymbol(const char *code) {
    if (strcmp(code, "USD") == 0) return "$";
    if (strcmp(code, "CNY") == 0 || strcmp(code, "JPY") == 0) return "\xC2\xA5";  // ¥
    if (strcmp(code, "GBP") == 0) return "\xC2\xA3";                              // £
    return code;   // unknown → show the raw code (e.g. "EUR")
}

void formatBalanceMinor(uint32_t minor, uint8_t decimals, char *buf, size_t bufSize) {
    if (decimals == 0) { snprintf(buf, bufSize, "%u", minor); return; }
    uint32_t scale = 1; for (uint8_t i = 0; i < decimals; ++i) scale *= 10;
    uint32_t whole = minor / scale;
    uint32_t frac  = minor % scale;
    snprintf(buf, bufSize, "%u.%0*u", whole, (int)decimals, frac);
}

}  // namespace stopwatch
```

- [ ] **Step 5: Run to verify it passes**

Run: `cd firmware && pio test -e native -f test_balance_format`
Expected: PASS (2 tests).

- [ ] **Step 6: Commit**

```bash
git add firmware/src/BalanceFormat.h firmware/src/BalanceFormat.cpp firmware/test/test_balance_format/test_main.cpp
git commit -m "firmware: add pure currency/balance formatting helpers"
```

---

## Task 12: TouchScroll (firmware, pure)

**Files:**
- Create: `firmware/src/TouchScroll.h`, `firmware/src/TouchScroll.cpp`
- Test: `firmware/test/test_touch_scroll/test_main.cpp`

- [ ] **Step 1: Write the header**

Create `firmware/src/TouchScroll.h`:

```cpp
#pragma once
#include <cstdint>

namespace stopwatch {

/// Pure vertical scroll model: drag tracking + clamped offset + decaying
/// momentum. No hardware/M5 dependencies so it's unit-testable in `native`.
/// Units are pixels; +offset scrolls content up (reveals lower rows).
class TouchScroll {
public:
    void setBounds(int contentHeight, int viewHeight);
    void reset();                       // offset = 0, velocity = 0

    void onPress(int y);
    void onMove(int y);                 // drag: offset follows finger
    void onRelease();                   // captures fling velocity from recent moves
    void tick(int dtMs);                // applies + decays momentum
    bool isResting() const { return !dragging_ && velocity_ == 0.0f; }

    int  offset() const { return offset_; }
    int  maxOffset() const { return max_ > 0 ? max_ : 0; }

private:
    void clamp();
    int   contentH_ = 0, viewH_ = 0, max_ = 0;
    int   offset_ = 0;
    bool  dragging_ = false;
    int   lastY_ = 0, prevY_ = 0;
    float velocity_ = 0.0f;             // px per tick
};

}  // namespace stopwatch
```

- [ ] **Step 2: Write the failing test**

Create `firmware/test/test_touch_scroll/test_main.cpp`:

```cpp
#include <unity.h>
#include "../../src/TouchScroll.h"
using namespace stopwatch;

void test_clampsAtBothEnds(void) {
    TouchScroll s; s.setBounds(/*content*/ 300, /*view*/ 100);   // max = 200
    TEST_ASSERT_EQUAL(200, s.maxOffset());
    s.onPress(100); s.onMove(40); s.onRelease();   // dragged up 60px → offset 60
    TEST_ASSERT_EQUAL(60, s.offset());
    // Drag far past the top.
    s.onPress(0); s.onMove(1000); s.onRelease();
    TEST_ASSERT_EQUAL(200, s.offset());            // clamped to max
    // Drag back past the bottom.
    s.onPress(0); s.onMove(-1000); s.onRelease();
    TEST_ASSERT_EQUAL(0, s.offset());              // clamped to 0
}

void test_noScrollWhenContentFits(void) {
    TouchScroll s; s.setBounds(80, 100);           // content < view → max 0
    s.onPress(50); s.onMove(0); s.onRelease();
    TEST_ASSERT_EQUAL(0, s.offset());
}

void test_momentumDecaysToRest(void) {
    TouchScroll s; s.setBounds(1000, 100);
    s.onPress(200); s.onMove(160); s.onMove(120); s.onRelease();   // fling up
    TEST_ASSERT_FALSE(s.isResting());
    int guard = 0;
    while (!s.isResting() && guard++ < 1000) s.tick(16);
    TEST_ASSERT_TRUE(s.isResting());
    TEST_ASSERT_TRUE(s.offset() >= 0 && s.offset() <= s.maxOffset());
}

void test_tapDoesNotScroll(void) {
    TouchScroll s; s.setBounds(1000, 100);
    s.onPress(120); s.onRelease();                 // press + release, no move
    s.tick(16);
    TEST_ASSERT_EQUAL(0, s.offset());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_clampsAtBothEnds);
    RUN_TEST(test_noScrollWhenContentFits);
    RUN_TEST(test_momentumDecaysToRest);
    RUN_TEST(test_tapDoesNotScroll);
    return UNITY_END();
}
```

- [ ] **Step 3: Run to verify it fails**

Run: `cd firmware && pio test -e native -f test_touch_scroll`
Expected: FAIL — undefined `TouchScroll` members.

- [ ] **Step 4: Implement `TouchScroll.cpp`**

Create `firmware/src/TouchScroll.cpp`:

```cpp
#include "TouchScroll.h"

namespace stopwatch {

void TouchScroll::setBounds(int contentHeight, int viewHeight) {
    contentH_ = contentHeight; viewH_ = viewHeight;
    max_ = contentHeight - viewHeight;
    if (max_ < 0) max_ = 0;
    clamp();
}

void TouchScroll::reset() { offset_ = 0; velocity_ = 0.0f; dragging_ = false; }

void TouchScroll::onPress(int y) {
    dragging_ = true; velocity_ = 0.0f; lastY_ = y; prevY_ = y;
}

void TouchScroll::onMove(int y) {
    if (!dragging_) return;
    // Finger moving up (smaller y) scrolls content up → larger offset.
    offset_ += (lastY_ - y);
    prevY_ = lastY_;
    lastY_ = y;
    clamp();
}

void TouchScroll::onRelease() {
    if (!dragging_) return;
    velocity_ = (float)(prevY_ - lastY_);   // last delta = fling speed (px/tick)
    dragging_ = false;
}

void TouchScroll::tick(int dtMs) {
    if (dragging_ || velocity_ == 0.0f) return;
    offset_ += (int)velocity_;
    clamp();
    velocity_ *= 0.85f;                      // friction
    if (offset_ <= 0 || offset_ >= max_) velocity_ = 0.0f;   // stop at edges
    if (velocity_ > -1.0f && velocity_ < 1.0f) velocity_ = 0.0f;
}

void TouchScroll::clamp() {
    if (offset_ < 0) offset_ = 0;
    if (offset_ > max_) offset_ = max_;
}

}  // namespace stopwatch
```

- [ ] **Step 5: Run to verify it passes**

Run: `cd firmware && pio test -e native -f test_touch_scroll`
Expected: PASS (4 tests).

- [ ] **Step 6: Commit**

```bash
git add firmware/src/TouchScroll.h firmware/src/TouchScroll.cpp firmware/test/test_touch_scroll/test_main.cpp
git commit -m "firmware: add pure TouchScroll model (drag + clamp + momentum)"
```

---

## Task 13: Balances view (firmware render)

**Files:**
- Modify: `firmware/src/Theme.h`
- Create: `firmware/src/Views/Balances.h`, `firmware/src/Views/Balances.cpp`

> Note: `Views/*.cpp` and `Theme.h` pull in M5Unified, so they are excluded from the `native` test env (per `platformio.ini` `build_src_filter`). This task is verified by `pio run -e stopwatch` compiling and by the manual flash in Task 16.

- [ ] **Step 1: Add the kind→color table to `Theme.h`**

Add inside `namespace stopwatch::theme`, after `colorDimFor`:

```cpp
// Brand-ish accent per known API provider; generic → muted grey (renders an
// initials chip instead of a colored dot in the wallet list).
inline uint32_t balanceColorFor(BalanceKind k) {
    switch (k) {
        case BalanceKind::OpenRouter:  return 0x7AA2FF;  // light blue
        case BalanceKind::DeepSeek:    return 0x4D6BFE;  // indigo
        case BalanceKind::Groq:        return 0xF55036;  // orange-red
        case BalanceKind::Together:    return 0xA78BFA;  // violet
        case BalanceKind::Fireworks:   return 0xFF7A59;  // ember
        case BalanceKind::SiliconFlow: return 0x26C2A3;  // teal
        case BalanceKind::Moonshot:    return 0xC9CDD2;  // pale
        case BalanceKind::Zhipu:       return 0x5B8DEF;  // steel
        case BalanceKind::Generic:     return kTextMuted;
    }
    return kTextMuted;
}
```

Add `#include "BalanceCodec.h"` is **not** needed here (Theme uses only `BalanceKind` from `Protocol.h`, already included).

- [ ] **Step 2: Write the view header**

Create `firmware/src/Views/Balances.h`:

```cpp
#pragma once
#include "../BalanceCodec.h"
#include "../Renderer.h"
#include "../App.h"

namespace stopwatch::views {

/// Scrollable wallet list. `scrollOffset` (px, ≥0) shifts rows up. Returns the
/// total content height in pixels so the caller can set scroll bounds.
int drawBalances(Renderer &renderer, const BalanceSnapshot &bal, LinkStatus link, int scrollOffset);

}  // namespace stopwatch::views
```

- [ ] **Step 3: Implement `Balances.cpp`**

Create `firmware/src/Views/Balances.cpp`:

```cpp
#include "Balances.h"
#include "../BalanceFormat.h"
#include "../Theme.h"
#include <cstdio>
#include <cstring>

namespace stopwatch::views {

namespace {
constexpr int kListTop    = 86;    // first row baseline band
constexpr int kListBottom = 410;   // above the pill
constexpr int kRowPitch   = 44;    // Font4 row spacing

struct Pill { const char *label; uint32_t color; };
Pill pillFor(LinkStatus link, const BalanceSnapshot &bal) {
    if (link == LinkStatus::NoBridge)              return { "no bridge", theme::kPillInfo };
    if (link == LinkStatus::LinkError)             return { "link error", theme::kPillError };
    if (bal.isStale() || bal.isBridgeError())      return { "stale", theme::kPillStale };
    return { nullptr, 0 };
}

const char *statusMarker(BalanceStatus s) {
    switch (s) {
        case BalanceStatus::AuthError:   return "auth";
        case BalanceStatus::Unreachable: return "offline";
        case BalanceStatus::Stale:       return "stale";
        case BalanceStatus::Depleted:    return "empty";
        default:                          return nullptr;
    }
}

// "$42.10" / "¥318.50" / "—" / "∞" into buf.
void formatBalance(const BalanceRecord &r, char *buf, size_t n) {
    if (r.unlimited)            { snprintf(buf, n, "%s\xE2\x88\x9E", currencySymbol(r.currency)); return; }
    if (!r.balanceMinor)        { snprintf(buf, n, "\xE2\x80\x94"); return; }  // —
    char num[16]; formatBalanceMinor(r.balanceMinor.value(), r.decimals, num, sizeof(num));
    snprintf(buf, n, "%s%s", currencySymbol(r.currency), num);
}
}  // namespace

int drawBalances(Renderer &renderer, const BalanceSnapshot &bal, LinkStatus link, int scrollOffset) {
    auto &c = renderer.canvas();
    renderer.clear(theme::kBackground);

    // Title.
    c.setTextDatum(middle_center);
    c.setFont(theme::kFontTitle);
    c.setTextColor(theme::kTextMuted);
    c.drawString("API \xC2\xB7 BALANCES", theme::kCenterX, 52);

    const int contentH = bal.recordCount * kRowPitch;

    if (bal.recordCount == 0) {
        c.setFont(theme::kFontBody);
        c.setTextColor(theme::kTextMuted);
        c.drawString("no providers", theme::kCenterX, theme::kCenterY);
    }

    // Clip rows to the list band; draw each row shifted by -scrollOffset.
    for (uint8_t i = 0; i < bal.recordCount; ++i) {
        const BalanceRecord &r = bal.records[i];
        int rowY = kListTop + i * kRowPitch - scrollOffset;
        if (rowY < kListTop - kRowPitch || rowY > kListBottom) continue;  // off-screen

        uint32_t color = theme::balanceColorFor(r.kind);
        bool dim = (r.status != BalanceStatus::Ok);

        // Left dot (known) or initials chip (generic).
        int leftX = 70;
        if (r.kind == BalanceKind::Generic) {
            c.fillRoundRect(leftX - 9, rowY - 9, 20, 20, 4, theme::kTextMuted);
            char chip[3] = { (char)toupper(r.name[0]), r.name[0] ? (char)toupper(r.name[1]) : '\0', 0 };
            c.setFont(theme::kFontMicro); c.setTextColor(theme::kBackground); c.setTextDatum(middle_center);
            c.drawString(chip, leftX + 1, rowY);
        } else {
            c.fillCircle(leftX, rowY, 6, color);
        }

        // Name (left).
        c.setFont(theme::kFontTitle);
        c.setTextColor(dim ? theme::kTextMuted : theme::kTextPrimary);
        c.setTextDatum(middle_left);
        c.drawString(r.name, leftX + 24, rowY);

        // Balance (right) — amber if low, brand color otherwise, muted if dim.
        char balStr[24]; formatBalance(r, balStr, sizeof(balStr));
        uint32_t balColor = dim ? theme::kTextMuted : (r.low ? theme::kPillStale : color);
        c.setTextColor(balColor);
        c.setTextDatum(middle_right);
        c.drawString(balStr, 396, rowY);

        // Status marker under the name, if any.
        if (const char *m = statusMarker(r.status)) {
            c.setFont(theme::kFontMicro); c.setTextColor(theme::kTextMuted); c.setTextDatum(middle_left);
            c.drawString(m, leftX + 24, rowY + 15);
        }
    }

    // Scroll indicator (right arc) when content overflows.
    int maxOffset = contentH - (kListBottom - kListTop);
    if (maxOffset > 0) {
        int trackH = kListBottom - kListTop;
        int barH = trackH * (kListBottom - kListTop) / contentH;
        int barY = kListTop + (trackH - barH) * scrollOffset / maxOffset;
        c.fillRoundRect(450, kListTop, 4, trackH, 2, theme::kRingTrack);
        c.fillRoundRect(450, barY, 4, barH, 2, theme::kTextMuted);
    }

    auto pill = pillFor(link, bal);
    renderer.drawPill(theme::kCenterX, theme::kCenterY + theme::kRingOuterR - 8, pill.label, pill.color);
    c.setTextDatum(middle_center);
    return contentH;
}

}  // namespace stopwatch::views
```

- [ ] **Step 4: Verify the firmware compiles**

Run: `cd firmware && pio run -e stopwatch`
Expected: compiles and links (no upload). Fix any include/type mismatch surfaced here.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/Theme.h firmware/src/Views/Balances.h firmware/src/Views/Balances.cpp
git commit -m "firmware: add scrollable Balances wallet view + kind colors"
```

---

## Task 14: ViewId + navigation (firmware)

**Files:**
- Modify: `firmware/src/App.h`
- Test: `firmware/test/test_state_machine/test_main.cpp`

- [ ] **Step 1: Add the failing nav assertions**

In `firmware/test/test_state_machine/test_main.cpp`, add a test that the cycle now includes `Balances` (place beside the existing nav tests; adapt names to the file's existing helpers):

```cpp
void test_balancesInCarousel(void) {
    using namespace stopwatch;
    // Gemini → Balances → Overview (forward); Overview → Balances (back).
    TEST_ASSERT_EQUAL((int)ViewId::Balances, (int)nextView(ViewId::Gemini));
    TEST_ASSERT_EQUAL((int)ViewId::Overview, (int)nextView(ViewId::Balances));
    TEST_ASSERT_EQUAL((int)ViewId::Balances, (int)prevView(ViewId::Overview));
    TEST_ASSERT_EQUAL((int)ViewId::Gemini,   (int)prevView(ViewId::Balances));
    TEST_ASSERT_TRUE(isBalanceView(ViewId::Balances));
    TEST_ASSERT_FALSE(isBalanceView(ViewId::Gemini));
}
```

Register it in that file's `main()`: `RUN_TEST(test_balancesInCarousel);`

- [ ] **Step 2: Run to verify it fails**

Run: `cd firmware && pio test -e native -f test_state_machine`
Expected: FAIL — `isBalanceView` undefined / `Balances` not a member / cycle wrong.

- [ ] **Step 3: Update `App.h`**

Add `Balances = 7` to the enum:

```cpp
enum class ViewId : uint8_t {
    Overview = 0, TotalSpend = 1,
    Codex = 2, CodexCost = 3,
    Claude = 4, ClaudeCost = 5,
    Gemini = 6, Balances = 7,
};
```

In `nextView`, change the `Gemini` case and add `Balances`:

```cpp
        case ViewId::Gemini:     return ViewId::Balances;
        case ViewId::Balances:   return ViewId::Overview;
```

In `prevView`, change `Overview` and add `Balances`:

```cpp
        case ViewId::Overview:   return ViewId::Balances;
        case ViewId::Balances:   return ViewId::Gemini;
```

Add the helper after `isSpendView`:

```cpp
constexpr bool isBalanceView(ViewId v) { return v == ViewId::Balances; }
```

- [ ] **Step 4: Run to verify it passes**

Run: `cd firmware && pio test -e native -f test_state_machine`
Expected: PASS (existing nav tests + the new one).

- [ ] **Step 5: Commit**

```bash
git add firmware/src/App.h firmware/test/test_state_machine/test_main.cpp
git commit -m "firmware: add Balances view to the carousel + nav"
```

---

## Task 15: BLE read, NVS cache, touch + main wiring (firmware)

**Files:**
- Modify: `firmware/src/BleClient.h`, `firmware/src/BleClient.cpp`
- Modify: `firmware/src/main.cpp`

> `BleClient.cpp` and `main.cpp` are excluded from `native`; verified by `pio run -e stopwatch` + Task 16 on hardware.

- [ ] **Step 1: Add `fetchBalances` to `BleClient.h`**

After the `fetchCost` declaration:

```cpp
    /// Like fetchCost(), but writes the balance trigger scope and reads BalanceSnapshot.
    FetchResult fetchBalances(uint8_t *outBytes, size_t bufSize, size_t &outLen);
```

- [ ] **Step 2: Implement `fetchBalances` in `BleClient.cpp`**

Mirror the existing `fetchCost`. Add:

```cpp
BleClient::FetchResult BleClient::fetchBalances(uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    return fetchInto(kBalanceSnapshotUUID, kTriggerScopeBalances, outBytes, bufSize, outLen);
}
```

(If `fetchCost` is implemented inline differently, copy its exact body, swapping `kCostSnapshotUUID`→`kBalanceSnapshotUUID` and `kTriggerScopeCost`→`kTriggerScopeBalances`.)

- [ ] **Step 3: Wire balances into `main.cpp`**

Add includes near the top:

```cpp
#include "BalanceCodec.h"
#include "BalanceFormat.h"
#include "TouchScroll.h"
#include "Views/Balances.h"
```

Add globals beside `g_cost`/`g_costLoaded`:

```cpp
stopwatch::BalanceSnapshot g_balance;
bool                       g_balanceLoaded = false;
stopwatch::TouchScroll     g_balScroll;
```

Add a fetch+apply helper beside `fetchCostAndApply`:

```cpp
static bool fetchBalancesAndApply() {
    uint8_t buf[stopwatch::kBalanceSnapshotMaxSize];
    size_t len = 0;
    if (g_ble.fetchBalances(buf, sizeof(buf), len) != stopwatch::BleClient::FetchResult::Ok) return false;
    stopwatch::BalanceSnapshot bs;
    if (stopwatch::decodeBalanceSnapshot(buf, len, bs) != stopwatch::BalanceDecodeResult::Ok) return false;
    g_balance = bs;
    g_store.save("bal", buf, len);
    g_balanceLoaded = true;
    return true;
}

// On first entry to the Balances screen this wake-session, pull balances once.
static void ensureBalanceLoaded() {
    using namespace stopwatch;
    if (!isBalanceView(g_app.currentView()) || g_balanceLoaded) return;
    renderRefreshingOverlay("Loading balances\xE2\x80\xA6");
    fetchBalancesAndApply();
}
```

Add the draw case in `drawCurrentView`:

```cpp
        case ViewId::Balances: {
            int contentH = views::drawBalances(g_renderer, g_balance, link, g_balScroll.offset());
            g_balScroll.setBounds(contentH, theme::kRingOuterR);   // visible band ≈ ring diameter band
            break;
        }
```

In `setup()`, after loading the cached cost snapshot, load the cached balances:

```cpp
    uint8_t bbuf[stopwatch::kBalanceSnapshotMaxSize];
    size_t blen = 0;
    if (g_store.load("bal", bbuf, sizeof(bbuf), blen)) {
        stopwatch::decodeBalanceSnapshot(bbuf, blen, g_balance);
    }
```

In `enterSleepAndRefreshOnWake()`, reset the per-session balance flag (mirrors `g_costLoaded = false`):

```cpp
    g_balanceLoaded = false;
    g_balScroll.reset();
```

Make force-refresh view-aware. Replace `applyRefreshRequest`'s body with:

```cpp
static bool applyRefreshRequest(const char *label) {
    if (!g_app.wantsRefresh()) return false;
    renderRefreshingOverlay(label);
    if (stopwatch::isBalanceView(g_app.currentView())) {
        fetchBalancesAndApply();           // writes trigger 0x05, re-reads (fresh arrives next visit)
    } else {
        fetchAndApply(0x00);
    }
    g_app.clearRefreshRequest();
    return true;
}
```

Add touch handling + `ensureBalanceLoaded` in `loop()`. Replace the body of `loop()` with:

```cpp
void loop() {
    using namespace stopwatch;
    auto ev = pollButtons();
    if (ev != ButtonEvent::None) {
        g_power.noteActivity();
        bool changed = g_app.handleEvent(ev);
        if (applyRefreshRequest("Refreshing\xE2\x80\xA6")) changed = true;
        if (g_app.wantsImmediateSleep()) {
            g_app.clearSleepRequest();
            enterSleepAndRefreshOnWake();
        } else if (changed) {
            if (!isBalanceView(g_app.currentView())) g_balScroll.reset();
            ensureCostLoaded();
            ensureBalanceLoaded();
            renderCurrent();
        }
    }

    // Touch: only meaningful on the Balances screen; drives the scroll model.
    if (isBalanceView(g_app.currentView())) {
        M5.update();
        auto t = M5.Touch.getDetail();
        if (t.isPressed()) {
            g_power.noteActivity();
            if (t.wasPressed()) g_balScroll.onPress(t.y);
            else                g_balScroll.onMove(t.y);
            renderCurrent();
        } else if (t.wasReleased()) {
            g_balScroll.onRelease();
        }
        if (!g_balScroll.isResting()) {
            g_balScroll.tick(20);
            renderCurrent();
        }
    }

    if (g_power.shouldSleep()) enterSleepAndRefreshOnWake();
    delay(20);
}
```

- [ ] **Step 4: Verify the firmware compiles**

Run: `cd firmware && pio run -e stopwatch`
Expected: compiles and links. Resolve any `M5.Touch` API mismatch by checking `M5Unified.hpp` (the field is `m5::touch_detail_t` with `.x`, `.y`, `.isPressed()`, `.wasPressed()`, `.wasReleased()`).

- [ ] **Step 5: Run the full native suite (regression)**

Run: `cd firmware && pio test -e native`
Expected: all suites PASS (`test_snapshot_codec`, `test_cost_codec`, `test_cost_format`, `test_state_machine`, `test_balance_codec`, `test_balance_format`, `test_touch_scroll`).

- [ ] **Step 6: Commit**

```bash
git add firmware/src/BleClient.h firmware/src/BleClient.cpp firmware/src/main.cpp
git commit -m "firmware: lazy balance read + NVS cache + touch scroll wiring"
```

---

## Task 16: End-to-end manual validation (hardware)

**Files:** none (validation only). Records the on-device checks the spec calls out.

- [ ] **Step 1: Configure a real provider**

Create `~/Library/Application Support/stopwatch-bridge/providers.json`:

```json
[
  { "id": "openrouter", "name": "OpenRouter", "kind": "openrouter", "lowThreshold": 5.0 },
  { "id": "deepseek",   "name": "DeepSeek",   "kind": "deepseek" }
]
```

Run: `make build && ./bridge/.build/release/stopwatch-bridge set-key openrouter` (paste key), repeat for `deepseek`.

- [ ] **Step 2: Run the bridge in pair mode and confirm polling**

Run: `make pair`
Expected: log line `balance poll: N provider(s)` within a few seconds; no crash on a missing/invalid key (that provider should log unreachable/auth, others continue).

- [ ] **Step 3: Flash and glance**

Run: `make flash` (long-press BOOT until screen off + green LED blinks first, per the flashing notes).
Verify on device:
- Wake → rings still paint in ≤ 1 s (balances did **not** slow the wake path).
- KeyB to the **Balances** screen → list populates on first visit (brief "Loading balances…"), showing each provider's name + native-currency balance.
- A balance below its `lowThreshold` renders amber.
- **Touch-drag scrolls** the list when there are more rows than fit; momentum settles; the right-edge indicator tracks position. **(This confirms `M5.Touch` reports points on the custom-board/`fallback_board` path — spec §11's gating risk.)**
- KeyA-long on Balances logs a `trigger write: scope=5` on the bridge.
- Pull a provider's key (`delete-key`), force-refresh → that row dims with `auth`, others stay live.

- [ ] **Step 4: If touch reports no points**

Fallback per spec §11: in `setup()` after `M5.begin`, log `M5.Touch.isEnabled()`; if false, the autodetect didn't bind the panel on the fallback path — construct the touch explicitly (M5GFX `Touch_CST816S` at I²C addr `0x15`, SDA 47 / SCL 48) and re-test. Capture findings in a follow-up note.

- [ ] **Step 5: Update README**

Add a short "API balances" subsection to `README.md`: how to add providers (`providers.json` + `set-key`), that keys live in the Keychain, and that MiniMax is unsupported (cookie-only). Commit:

```bash
git add README.md
git commit -m "docs: document API balances setup (providers.json + Keychain)"
```

---

## Self-Review notes (author check vs. spec)

- **Spec coverage:** §3 sources → Tasks 4,7 (kind defaults + adapters incl. OpenRouter `/credits`→`/key`, DeepSeek string/multi-currency-first). §4 credentials → Tasks 5,6. §5 nav → Task 14. §6 layout → Task 13 (dot/chip, native currency, amber low, scroll indicator, status markers, `—`/`∞`). §7 wire → Tasks 1–3,10. §8 bridge → Tasks 1,2,4–9. §9 firmware → Tasks 10–15. §10 errors → Tasks 7 (auth/depleted/unreachable), 8 (stale merge), 13 (markers). §11 risks → Task 16 (touch verify) + Task 15 step 4. §12 testing → every TDD task + Task 16. §13 order → tasks follow it.
- **Type consistency:** `BalanceKind`/`BalanceStatus` shared as enums on both sides; `balanceMinor`/`usageMinor` sentinels (`0xFFFFFFFF` unknown, `0xFFFFFFFE` unlimited) consistent across encoder (Task 2), codec (Task 10), view (Task 13). `ProviderConfig.Resolved` field names match between Tasks 4 and 7. `fetchAll(_:now:)`, `record(_:)`, `decodeBalanceSnapshot`, `drawBalances`, `isBalanceView` names consistent across tasks.
- **No placeholders:** every code step shows full content; commands have expected output.
- **Deferred to execution:** exact `M5.Touch` field spelling is confirmed at compile time in Task 15 step 4 (the only API-shape uncertainty), with the verification + fallback in Task 16.
