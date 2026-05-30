# Per-provider API Usage & Spend — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add per-provider usage/spend detail screens (balance + 30-day cost/token chart) to the API Balances view, drilled into by tapping a row. OpenRouter ships first end-to-end; AIHubMix and DeepSeek follow behind a verification spike.

**Architecture:** Mirror the existing **Cost** pipeline. A new, independently-versioned `BalanceUsage` BLE snapshot carries only usage-capable providers (per-record `cost[30]` + `tokens[30]` arrays plus today/30-day totals). The bridge fetches it lazily via a new trigger scope; the firmware decodes it with a `CostCodec`-style decoder and renders a detail view reusing the SPEND & BURN sparkline + entrance animation. The existing Balance snapshot (the fast 30 s poll) is untouched.

**Tech Stack:** C++17 firmware (PlatformIO, Unity tests, `native` env), Swift 6 bridge (SwiftPM, swift-testing), little-endian binary wire protocol over BLE GATT.

**Reference spec:** `docs/superpowers/specs/2026-05-30-balance-usage-design.md`

---

## Conventions (read before starting)

- **Firmware build/test:** from `firmware/`, `pio test -e native` runs all Unity suites (host-compiled). `pio run -e stopwatch` cross-compiles for the device. The `native` env's `build_src_filter` (in `firmware/platformio.ini`) compiles top-level `src/*.cpp` **but not** `src/Views/*.cpp` — so codec/model code (testable) lives at `src/`, view code (not host-testable) lives at `src/Views/`.
- **Bridge build/test:** from `bridge/`, `swift test` runs swift-testing suites; `swift build` compiles.
- **IDE squiggles lie here.** clang/SourceKit lack the PlatformIO/SwiftPM flags. Trust `pio` and `swift`, never the editor's inline errors.
- **Wire changes are two-sided.** Every constant added to `firmware/src/Protocol.h` has a twin in `bridge/Sources/StopwatchBridge/Protocol.swift`, documented in `shared/PROTOCOL.md`. Keep all three in lockstep within the same task.
- **Commit after every green step.** Small commits.

## Wire format: `BalanceUsage` snapshot (the contract both sides implement)

Independent versioning. All integers little-endian. Size = `12 + 96 × recordCount`.

**Header (12 bytes)** — identical shape to CostSnapshot's:

| Offset | Field | Type | Notes |
|--------|-------|------|-------|
| 0 | `versionMajor` | u8 | `0x01` |
| 1 | `versionMinor` | u8 | `0x00` |
| 2 | `recordCount` | u8 | 0–4 |
| 3 | `flags` | u8 | bit0 stale, bit1 bridge_error, bit2 unavailable |
| 4 | `capturedAt` | u32 | unix seconds |
| 8 | `historyDays` | u8 | always 30 |
| 9 | reserved | u8 | 0 |
| 10 | reserved | u16 | 0 (scales are per-record; see below) |

**Per-record (96 bytes)** — per-record scales because currencies/magnitudes differ across providers (the chart is viewed one provider at a time):

| Offset | Field | Type | Notes |
|--------|-------|------|-------|
| 0 | `kind` | u8 | BalanceKind enum (1 openrouter, 2 deepseek, …) |
| 1 | `status` | u8 | BalanceStatus enum (0 ok, 1 stale, …) |
| 2 | `currency[3]` | char[3] | ASCII, e.g. "USD" |
| 5 | `decimals` | u8 | currency minor-unit exponent (2) |
| 6 | `costUnit` | u16 | `costHistory[i] × costUnit = minor currency units` |
| 8 | `tokenUnit` | u32 | `tokenHistory[i] × tokenUnit = tokens` |
| 12 | `todayCostMinor` | u32 | `0xFFFFFFFF` = unknown |
| 16 | `monthCostMinor` | u32 | |
| 20 | `todayTokens` | u32 | |
| 24 | `monthTokens` | u32 | |
| 28 | `todayRequests` | u32 | |
| 32 | `monthRequests` | u32 | |
| 36 | `costHistory[30]` | u8×30 | daily cost, scaled by `costUnit` |
| 66 | `tokenHistory[30]` | u8×30 | daily tokens, scaled by `tokenUnit` |

Constants: `kUsageVersionMajor=1`, `kUsageVersionMinor=0`, `kUsageHeaderSize=12`, `kUsageRecordSize=96`, `kUsageHistoryDays=30`, `kUsageMaxRecords=4`, `kUsageSnapshotMaxSize = 12 + 96×4 = 396`, `kTriggerScopeUsage=0x06`. New characteristic UUID: `7E2C5A19-4B8F-4D3A-9E61-2F7A8C0B5D34`.

---

## File Structure

**Firmware — new:**
- `firmware/src/UsageCodec.h` / `.cpp` — `UsageRecord`, `UsageSnapshot`, `decodeUsageSnapshot()`. Host-testable (top-level `src/`).
- `firmware/src/Views/ProviderUsage.h` / `.cpp` — `drawProviderUsage()` detail view.
- `firmware/test/test_usage_codec/test_main.cpp` — Unity decoder tests.

**Firmware — modified:**
- `firmware/src/Protocol.h` — usage wire constants + UUID + scope.
- `firmware/src/BleClient.h` / `.cpp` — `fetchUsage()`.
- `firmware/src/App.h` — detail sub-state (selected provider index + chart metric) and tap/back/toggle transitions.
- `firmware/src/main.cpp` — global `g_usage`, lazy fetch, tap-to-enter wiring, dispatch to detail view.

**Bridge — new:**
- `bridge/Sources/StopwatchBridge/UsageSnapshot.swift` — `NormalizedUsageSpend` model.
- `bridge/Sources/StopwatchBridge/UsageEncoder.swift` — wire encoder.
- `bridge/Sources/StopwatchBridge/UsageCache.swift` — last-good cache.
- `bridge/Sources/StopwatchBridge/UsageClient.swift` — per-provider fetch behind one protocol; OpenRouter impl in Phase 1.
- `bridge/Tests/StopwatchBridgeTests/UsageEncoderTests.swift`, `UsageClientTests.swift`.

**Bridge — modified:**
- `bridge/Sources/StopwatchBridge/Protocol.swift` — usage constants + UUID + scope.
- `bridge/Sources/StopwatchBridge/GATTPeripheral.swift` — usage characteristic + read/notify.
- `bridge/Sources/StopwatchBridge/BridgeService.swift` — usage refresh on scope `0x06`.
- `bridge/Sources/StopwatchBridge/ProvidersConfig.swift` — `usageKind` + management-key/token credential ids.

**Shared:**
- `shared/PROTOCOL.md` — new §3C.
- `shared/fixtures/usage-openrouter.json` / `.hex` — cross-side fixture.

---

# PHASE 1 — OpenRouter end-to-end

Builds the entire vertical slice and proves it on the one clean, documented API.

## Task 1: Wire constants (both sides + protocol doc)

**Files:**
- Modify: `firmware/src/Protocol.h` (after line 53, the balance block)
- Modify: `bridge/Sources/StopwatchBridge/Protocol.swift` (after line 35)
- Modify: `shared/PROTOCOL.md` (after the §3B block, before `## 4. Test fixtures`)

- [ ] **Step 1: Add firmware constants**

In `firmware/src/Protocol.h`, immediately after the line `constexpr uint8_t kTriggerScopeBalances = 0x05;` (line 53), add:

```cpp
constexpr const char *kUsageSnapshotUUID = "7E2C5A19-4B8F-4D3A-9E61-2F7A8C0B5D34";

constexpr uint8_t  kUsageVersionMajor  = 1;
constexpr uint8_t  kUsageHeaderSize    = 12;
constexpr uint8_t  kUsageRecordSize    = 96;
constexpr uint8_t  kUsageHistoryDays   = 30;
constexpr uint8_t  kUsageMaxRecords    = 4;    // openrouter, deepseek, aihubmix (+1 headroom)
constexpr uint16_t kUsageSnapshotMaxSize = kUsageHeaderSize + kUsageRecordSize * kUsageMaxRecords;  // 396

constexpr uint8_t kUsageFlagStale       = 0b00000001;
constexpr uint8_t kUsageFlagBridgeError = 0b00000010;
constexpr uint8_t kUsageFlagUnavailable = 0b00000100;

constexpr uint8_t kTriggerScopeUsage    = 0x06;
```

- [ ] **Step 2: Add bridge constants**

In `bridge/Sources/StopwatchBridge/Protocol.swift`, immediately after the line `public static let balanceRecordFlagLow: UInt8 = 0b0000_0001` (line 35), add:

```swift
    public static let usageSnapshotUUID = CBUUID(string: "7E2C5A19-4B8F-4D3A-9E61-2F7A8C0B5D34")
    public static let usageVersionMajor: UInt8 = 1
    public static let usageVersionMinor: UInt8 = 0
    public static let usageHeaderSize   = 12
    public static let usageRecordSize   = 96
    public static let usageHistoryDays  = 30
    public static let usageMaxRecords   = 4
    public static let triggerScopeUsage: UInt8 = 0x06
```

- [ ] **Step 3: Document the format in PROTOCOL.md**

In `shared/PROTOCOL.md`, find the line `## 4. Test fixtures` and insert this block immediately before it:

```markdown
## 3C. `BalanceUsage` payload (binary)

Independent versioning. Size = `12 + 96 × recordCount`. Carries only usage-capable
providers (OpenRouter, DeepSeek, AIHubMix). Watch reads lazily on entering a provider
detail screen. Characteristic `7E2C5A19-4B8F-4D3A-9E61-2F7A8C0B5D34`, trigger scope `0x06`.

### 3C.1 Header (12 bytes)

| Offset | Field | Type | Notes |
|--------|-------|------|-------|
| 0 | `versionMajor` | u8 | `0x01` |
| 1 | `versionMinor` | u8 | `0x00` |
| 2 | `recordCount` | u8 | 0–4 |
| 3 | `flags` | u8 | bit0 stale, bit1 bridge_error, bit2 unavailable |
| 4 | `capturedAt` | u32 | unix seconds |
| 8 | `historyDays` | u8 | always 30 |
| 9 | reserved | u8 | 0 |
| 10 | reserved | u16 | 0 (scales are per-record) |

### 3C.2 Per-record (96 bytes, repeated `recordCount` times)

| Offset | Field | Type | Notes |
|--------|-------|------|-------|
| 0 | `kind` | u8 | BalanceKind enum |
| 1 | `status` | u8 | BalanceStatus enum |
| 2 | `currency` | char[3] | ASCII |
| 5 | `decimals` | u8 | currency minor-unit exponent |
| 6 | `costUnit` | u16 | `costHistory[i] × costUnit = minor units` |
| 8 | `tokenUnit` | u32 | `tokenHistory[i] × tokenUnit = tokens` |
| 12 | `todayCostMinor` | u32 | `0xFFFFFFFF` = unknown |
| 16 | `monthCostMinor` | u32 | |
| 20 | `todayTokens` | u32 | |
| 24 | `monthTokens` | u32 | |
| 28 | `todayRequests` | u32 | |
| 32 | `monthRequests` | u32 | |
| 36 | `costHistory[30]` | u8×30 | scaled by `costUnit` |
| 66 | `tokenHistory[30]` | u8×30 | scaled by `tokenUnit` |
```

- [ ] **Step 4: Verify both sides still build**

Run: `cd firmware && pio run -e stopwatch -t buildfs 2>/dev/null; pio run -e stopwatch`
Expected: `[SUCCESS]` (constants are header-only; nothing references them yet).
Run: `cd bridge && swift build`
Expected: `Build complete!`

- [ ] **Step 5: Commit**

```bash
git add firmware/src/Protocol.h bridge/Sources/StopwatchBridge/Protocol.swift shared/PROTOCOL.md
git commit -m "protocol: add BalanceUsage wire constants (firmware + bridge + doc)"
```

---

## Task 2: Firmware usage codec

**Files:**
- Create: `firmware/src/UsageCodec.h`
- Create: `firmware/src/UsageCodec.cpp`
- Test: `firmware/test/test_usage_codec/test_main.cpp`

- [ ] **Step 1: Write the header**

Create `firmware/src/UsageCodec.h`:

```cpp
#pragma once
#include "Protocol.h"
#include <cstdint>
#include <cstddef>
#include <optional>

namespace stopwatch {

struct UsageRecord {
    BalanceKind kind = BalanceKind::Generic;
    BalanceStatus status = BalanceStatus::Ok;
    char     currency[4] = {0};            // 3 chars + null
    uint8_t  decimals = 2;
    std::optional<uint32_t> todayCostMinor;  // nullopt iff 0xFFFFFFFF
    std::optional<uint32_t> monthCostMinor;
    std::optional<uint32_t> todayTokens;
    std::optional<uint32_t> monthTokens;
    std::optional<uint32_t> todayRequests;
    std::optional<uint32_t> monthRequests;
    uint16_t costUnit  = 1;                 // costHistory[i] * costUnit = minor units
    uint32_t tokenUnit = 1;                 // tokenHistory[i] * tokenUnit = tokens
    uint8_t  costHistory[kUsageHistoryDays]  = {0};
    uint8_t  tokenHistory[kUsageHistoryDays] = {0};
};

struct UsageSnapshot {
    uint8_t  versionMajor = 0;
    uint8_t  versionMinor = 0;
    uint8_t  recordCount  = 0;
    uint8_t  flags        = 0;
    uint32_t capturedAt   = 0;
    uint8_t  historyDays  = 0;
    UsageRecord records[kUsageMaxRecords] = {};

    bool isStale()       const { return flags & kUsageFlagStale; }
    bool isBridgeError() const { return flags & kUsageFlagBridgeError; }
    bool isUnavailable() const { return flags & kUsageFlagUnavailable; }

    const UsageRecord *find(BalanceKind k) const {
        for (uint8_t i = 0; i < recordCount; ++i)
            if (records[i].kind == k) return &records[i];
        return nullptr;
    }
};

enum class UsageDecodeResult : uint8_t { Ok, TooShort, MajorVersionTooNew, InvalidRecordCount };

UsageDecodeResult decodeUsageSnapshot(const uint8_t *bytes, size_t len, UsageSnapshot &out);

}  // namespace stopwatch
```

- [ ] **Step 2: Write the failing test**

Create `firmware/test/test_usage_codec/test_main.cpp`:

```cpp
#include <unity.h>
#include <vector>
#include "../../src/UsageCodec.h"
using namespace stopwatch;

// Builds one 12+96 byte snapshot with a single OpenRouter record by hand.
static std::vector<uint8_t> buildOne() {
    std::vector<uint8_t> b(kUsageHeaderSize + kUsageRecordSize, 0);
    b[0] = 1;                       // versionMajor
    b[2] = 1;                       // recordCount
    b[8] = 30;                      // historyDays
    uint8_t *r = b.data() + kUsageHeaderSize;
    r[0] = (uint8_t)BalanceKind::OpenRouter;
    r[1] = (uint8_t)BalanceStatus::Ok;
    r[2] = 'U'; r[3] = 'S'; r[4] = 'D';
    r[5] = 2;                       // decimals
    r[6] = 50; r[7] = 0;            // costUnit = 50
    r[8] = 100; r[9]=0; r[10]=0; r[11]=0;   // tokenUnit = 100
    r[12] = 0x90; r[13]=0x01;       // todayCostMinor = 400
    // leave monthCost..monthRequests as 0 except set today tokens unknown:
    r[20] = 0xFF; r[21]=0xFF; r[22]=0xFF; r[23]=0xFF;  // todayTokens unknown
    r[36 + 29] = 200;               // costHistory[29] = 200 → 200*50 = 10000 minor
    r[66 + 29] = 123;               // tokenHistory[29] = 123 → 123*100 = 12300 tokens
    return b;
}

void test_decodesRecord(void) {
    auto b = buildOne();
    UsageSnapshot u;
    TEST_ASSERT_EQUAL((int)UsageDecodeResult::Ok, (int)decodeUsageSnapshot(b.data(), b.size(), u));
    TEST_ASSERT_EQUAL(1, u.versionMajor);
    TEST_ASSERT_EQUAL(1, u.recordCount);
    TEST_ASSERT_EQUAL(30, u.historyDays);
    const UsageRecord *r = u.find(BalanceKind::OpenRouter);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_STRING("USD", r->currency);
    TEST_ASSERT_EQUAL(2, r->decimals);
    TEST_ASSERT_EQUAL(50, r->costUnit);
    TEST_ASSERT_EQUAL(100, r->tokenUnit);
    TEST_ASSERT_TRUE(r->todayCostMinor.has_value());
    TEST_ASSERT_EQUAL(400, r->todayCostMinor.value());
    TEST_ASSERT_FALSE(r->todayTokens.has_value());     // 0xFFFFFFFF sentinel
    TEST_ASSERT_EQUAL(200, r->costHistory[29]);
    TEST_ASSERT_EQUAL(123, r->tokenHistory[29]);
}

void test_futureMajorRejected(void) {
    uint8_t b[kUsageHeaderSize] = {99,0,0,0,0,0,0,0,30,0,0,0};
    UsageSnapshot u;
    TEST_ASSERT_EQUAL((int)UsageDecodeResult::MajorVersionTooNew,
                      (int)decodeUsageSnapshot(b, sizeof(b), u));
}

void test_tooShortRejected(void) {
    uint8_t b[4] = {1,0,1,0};
    UsageSnapshot u;
    TEST_ASSERT_EQUAL((int)UsageDecodeResult::TooShort,
                      (int)decodeUsageSnapshot(b, sizeof(b), u));
}

void test_recordCountOverMaxRejected(void) {
    uint8_t b[kUsageHeaderSize] = {1,0,(uint8_t)(kUsageMaxRecords+1),0,0,0,0,0,30,0,0,0};
    UsageSnapshot u;
    TEST_ASSERT_EQUAL((int)UsageDecodeResult::InvalidRecordCount,
                      (int)decodeUsageSnapshot(b, sizeof(b), u));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_decodesRecord);
    RUN_TEST(test_futureMajorRejected);
    RUN_TEST(test_tooShortRejected);
    RUN_TEST(test_recordCountOverMaxRejected);
    return UNITY_END();
}
```

- [ ] **Step 2b: Run it to confirm it fails to link**

Run: `cd firmware && pio test -e native -f test_usage_codec`
Expected: FAIL — undefined reference to `decodeUsageSnapshot` (impl not written yet).

- [ ] **Step 3: Write the implementation**

Create `firmware/src/UsageCodec.cpp`:

```cpp
#include "UsageCodec.h"
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

UsageDecodeResult decodeUsageSnapshot(const uint8_t *bytes, size_t len, UsageSnapshot &out) {
    if (len < kUsageHeaderSize) return UsageDecodeResult::TooShort;

    uint8_t major = bytes[0];
    uint8_t count = bytes[2];
    if (major > kUsageVersionMajor) return UsageDecodeResult::MajorVersionTooNew;
    if (count > kUsageMaxRecords)   return UsageDecodeResult::InvalidRecordCount;
    if (len < (size_t)(kUsageHeaderSize + count * kUsageRecordSize)) return UsageDecodeResult::TooShort;

    out.versionMajor = major;
    out.versionMinor = bytes[1];
    out.recordCount  = count;
    out.flags        = bytes[3];
    out.capturedAt   = readU32(bytes + 4);
    out.historyDays  = bytes[8];

    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t *r = bytes + kUsageHeaderSize + i * kUsageRecordSize;
        UsageRecord &rec = out.records[i];
        rec.kind   = (BalanceKind)r[0];
        rec.status = (BalanceStatus)r[1];
        memcpy(rec.currency, r + 2, 3); rec.currency[3] = '\0';
        rec.decimals = r[5];
        rec.costUnit  = readU16(r + 6);
        rec.tokenUnit = readU32(r + 8);
        rec.todayCostMinor = optU32(r + 12);
        rec.monthCostMinor = optU32(r + 16);
        rec.todayTokens    = optU32(r + 20);
        rec.monthTokens    = optU32(r + 24);
        rec.todayRequests  = optU32(r + 28);
        rec.monthRequests  = optU32(r + 32);
        memcpy(rec.costHistory,  r + 36, kUsageHistoryDays);
        memcpy(rec.tokenHistory, r + 66, kUsageHistoryDays);
    }
    return UsageDecodeResult::Ok;
}

}  // namespace stopwatch
```

- [ ] **Step 4: Run the test, expect pass**

Run: `cd firmware && pio test -e native -f test_usage_codec`
Expected: `test_usage_codec [PASSED]`, 4 tests pass.

- [ ] **Step 5: Confirm full native suite + device build**

Run: `cd firmware && pio test -e native`
Expected: all suites pass (existing + new).
Run: `cd firmware && pio run -e stopwatch`
Expected: `[SUCCESS]`.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/UsageCodec.h firmware/src/UsageCodec.cpp firmware/test/test_usage_codec/
git commit -m "firmware: BalanceUsage codec + decoder tests"
```

---

## Task 3: Bridge normalized model + encoder

**Files:**
- Create: `bridge/Sources/StopwatchBridge/UsageSnapshot.swift`
- Create: `bridge/Sources/StopwatchBridge/UsageEncoder.swift`
- Test: `bridge/Tests/StopwatchBridgeTests/UsageEncoderTests.swift`

- [ ] **Step 1: Write the model**

Create `bridge/Sources/StopwatchBridge/UsageSnapshot.swift`:

```swift
// bridge/Sources/StopwatchBridge/UsageSnapshot.swift
import Foundation

public struct UsageFlags: OptionSet, Sendable {
    public let rawValue: UInt8
    public init(rawValue: UInt8) { self.rawValue = rawValue }
    public static let stale       = UsageFlags(rawValue: 0b0000_0001)
    public static let bridgeError = UsageFlags(rawValue: 0b0000_0010)
    public static let unavailable = UsageFlags(rawValue: 0b0000_0100)
}

/// Normalized per-provider usage/spend that `UsageEncoder` consumes.
public struct NormalizedUsageSpend: Equatable, Sendable {
    public struct Provider: Equatable, Sendable {
        public var kind: BalanceKind
        public var status: BalanceStatus
        public var currencyCode: String        // 1–3 ASCII chars
        public var currencyDecimals: Int
        public var todayCost: Double?          // currency units; nil → unknown
        public var monthCost: Double?
        public var todayTokens: UInt64?
        public var monthTokens: UInt64?
        public var todayRequests: UInt64?
        public var monthRequests: UInt64?
        public var costHistory: [Double]       // length 30, currency/day, index 29 = capturedAt day
        public var tokenHistory: [UInt64]      // length 30, tokens/day

        public init(kind: BalanceKind, status: BalanceStatus, currencyCode: String,
                    currencyDecimals: Int = 2, todayCost: Double?, monthCost: Double?,
                    todayTokens: UInt64?, monthTokens: UInt64?, todayRequests: UInt64?,
                    monthRequests: UInt64?, costHistory: [Double], tokenHistory: [UInt64]) {
            self.kind = kind; self.status = status
            self.currencyCode = currencyCode; self.currencyDecimals = currencyDecimals
            self.todayCost = todayCost; self.monthCost = monthCost
            self.todayTokens = todayTokens; self.monthTokens = monthTokens
            self.todayRequests = todayRequests; self.monthRequests = monthRequests
            self.costHistory = costHistory; self.tokenHistory = tokenHistory
        }
    }
    public var capturedAt: Date
    public var flags: UsageFlags
    public var providers: [Provider]
    public init(capturedAt: Date, flags: UsageFlags, providers: [Provider]) {
        self.capturedAt = capturedAt; self.flags = flags; self.providers = providers
    }
}
```

- [ ] **Step 2: Write the failing test**

Create `bridge/Tests/StopwatchBridgeTests/UsageEncoderTests.swift`:

```swift
import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct UsageEncoderTests {

    private func fixture() -> NormalizedUsageSpend {
        var cost = [Double](repeating: 0, count: 30); cost[29] = 100.0   // $100 on the last day
        var tok  = [UInt64](repeating: 0, count: 30);  tok[29] = 1_000_000
        return .init(capturedAt: Date(timeIntervalSince1970: 1_748_455_822), flags: [], providers: [
            .init(kind: .openrouter, status: .ok, currencyCode: "USD", currencyDecimals: 2,
                  todayCost: 4.00, monthCost: 41.80, todayTokens: 1_000_000, monthTokens: 2_100_000,
                  todayRequests: 1240, monthRequests: 9000, costHistory: cost, tokenHistory: tok)
        ])
    }

    @Test func encodesHeaderAndRecord() {
        let bytes = [UInt8](UsageEncoder.encode(fixture()))
        #expect(bytes.count == 12 + 96)             // one record
        #expect(bytes[0] == Protocol.usageVersionMajor)
        #expect(bytes[2] == 1)                       // recordCount
        #expect(bytes[3] == 0)                       // flags
        #expect(bytes[8] == 30)                      // historyDays

        let r = 12
        #expect(bytes[r+0] == BalanceKind.openrouter.rawValue)
        #expect(bytes[r+1] == BalanceStatus.ok.rawValue)
        #expect(Array(bytes[(r+2)..<(r+5)]) == Array("USD".utf8))
        #expect(bytes[r+5] == 2)                     // decimals
        // todayCostMinor = 400 (4.00 * 100) little-endian at r+12
        #expect(bytes[r+12] == 0x90 && bytes[r+13] == 0x01)
        // costHistory[29]: max day = $100 = 10000 minor; costUnit = ceil(10000/255) = 40
        let costUnit = UInt16(bytes[r+6]) | (UInt16(bytes[r+7]) << 8)
        #expect(costUnit == 40)
        // encoder rounds: (10000 + costUnit/2) / costUnit = (10000+20)/40 = 250
        #expect(bytes[r+36+29] == 250)
        // tokenHistory[29]: max = 1_000_000; tokenUnit = ceil(1_000_000/255) = 3922
        let tokenUnit = UInt32(bytes[r+8]) | (UInt32(bytes[r+9])<<8) | (UInt32(bytes[r+10])<<16) | (UInt32(bytes[r+11])<<24)
        #expect(tokenUnit == 3922)
        // encoder rounds: (1_000_000 + 1961) / 3922 = 255
        #expect(bytes[r+66+29] == 255)
    }

    @Test func unknownsBecomeSentinels() {
        let p = NormalizedUsageSpend.Provider(
            kind: .openrouter, status: .ok, currencyCode: "USD",
            todayCost: nil, monthCost: nil, todayTokens: nil, monthTokens: nil,
            todayRequests: nil, monthRequests: nil,
            costHistory: [Double](repeating: 0, count: 30), tokenHistory: [UInt64](repeating: 0, count: 30))
        let bytes = [UInt8](UsageEncoder.encode(.init(capturedAt: Date(timeIntervalSince1970: 0), flags: [], providers: [p])))
        // todayCostMinor..monthRequests (offsets 12..36) all 0xFF
        #expect(bytes[(12+12)..<(12+36)].allSatisfy { $0 == 0xFF })
    }
}
```

- [ ] **Step 2b: Run it, expect failure**

Run: `cd bridge && swift test --filter UsageEncoderTests`
Expected: build error — `UsageEncoder` undefined.

- [ ] **Step 3: Write the encoder**

Create `bridge/Sources/StopwatchBridge/UsageEncoder.swift`:

```swift
// bridge/Sources/StopwatchBridge/UsageEncoder.swift
import Foundation

public enum UsageEncoder {

    /// Encodes normalized usage into the `12 + 96*N` byte BalanceUsage wire format
    /// (see `shared/PROTOCOL.md §3C`). Clamps to `usageMaxRecords`.
    public static func encode(_ snap: NormalizedUsageSpend) -> Data {
        let providers = Array(snap.providers.prefix(Protocol.usageMaxRecords))
        var out = Data(capacity: Protocol.usageHeaderSize + Protocol.usageRecordSize * providers.count)
        out.append(Protocol.usageVersionMajor)
        out.append(Protocol.usageVersionMinor)
        out.append(UInt8(providers.count))
        out.append(snap.flags.rawValue)
        appendU32(&out, u32Seconds(snap.capturedAt))
        out.append(UInt8(Protocol.usageHistoryDays))
        out.append(0)              // reserved
        appendU16(&out, 0)         // reserved (per-record scales)

        for p in providers {
            let costUnit  = unitU16(p.costHistory.map { minor($0, p.currencyDecimals) })
            let tokenUnit = unitU32(p.tokenHistory)
            out.append(p.kind.rawValue)
            out.append(p.status.rawValue)
            appendCurrency(&out, p.currencyCode)
            out.append(UInt8(max(0, min(p.currencyDecimals, 255))))
            appendU16(&out, costUnit)
            appendU32(&out, tokenUnit)
            appendU32(&out, minorOrUnknown(p.todayCost, p.currencyDecimals))
            appendU32(&out, minorOrUnknown(p.monthCost, p.currencyDecimals))
            appendU32(&out, u32OrUnknown(p.todayTokens))
            appendU32(&out, u32OrUnknown(p.monthTokens))
            appendU32(&out, u32OrUnknown(p.todayRequests))
            appendU32(&out, u32OrUnknown(p.monthRequests))
            appendScaled(&out, p.costHistory.map { UInt64(minor($0, p.currencyDecimals)) }, unit: UInt64(costUnit))
            appendScaled(&out, p.tokenHistory, unit: UInt64(tokenUnit))
        }
        return out
    }

    public static func staleEmpty() -> Data {
        encode(.init(capturedAt: Date(timeIntervalSince1970: 0), flags: [.stale, .unavailable], providers: []))
    }
    public static func errorEmpty(capturedAt: Date = Date()) -> Data {
        encode(.init(capturedAt: capturedAt, flags: [.stale, .bridgeError], providers: []))
    }
    /// Sets stale (+extra flags) and refreshes capturedAt on an encoded snapshot.
    public static func markStale(_ snapshot: Data, capturedAt: Date, extraFlags: UsageFlags = []) -> Data {
        guard snapshot.count >= Protocol.usageHeaderSize else { return snapshot }
        var out = snapshot
        out[3] |= UsageFlags.stale.rawValue | extraFlags.rawValue
        writeU32(&out, u32Seconds(capturedAt), at: 4)
        return out
    }

    // MARK: - helpers
    private static func minor(_ v: Double, _ decimals: Int) -> Int {
        Int((v * pow(10.0, Double(max(0, decimals)))).rounded())
    }
    private static func unitU16(_ values: [Int]) -> UInt16 {
        let maxV = values.max() ?? 0
        if maxV <= 0 { return 1 }
        return UInt16(min(max((maxV + 254) / 255, 1), 65535))
    }
    private static func unitU32(_ values: [UInt64]) -> UInt32 {
        let maxV = values.max() ?? 0
        if maxV == 0 { return 1 }
        return UInt32(min(max((maxV + 254) / 255, 1), UInt64(UInt32.max)))
    }
    private static func minorOrUnknown(_ v: Double?, _ decimals: Int) -> UInt32 {
        guard let v else { return 0xFFFF_FFFF }
        let m = minor(v, decimals)
        if m < 0 { return 0 }
        if m >= Int(UInt32.max) { return 0xFFFF_FFFE }
        return UInt32(m)
    }
    private static func u32OrUnknown(_ v: UInt64?) -> UInt32 {
        guard let v else { return 0xFFFF_FFFF }
        return v >= UInt64(UInt32.max) ? 0xFFFF_FFFE : UInt32(v)
    }
    private static func appendScaled(_ out: inout Data, _ values: [UInt64], unit: UInt64) {
        let u = max(unit, 1)
        for i in 0..<Protocol.usageHistoryDays {
            let v = i < values.count ? values[i] : 0
            out.append(UInt8(min((v + u/2) / u, 255)))
        }
    }
    private static func appendCurrency(_ out: inout Data, _ code: String) {
        var f = [UInt8](repeating: 0, count: 3)
        for (i, b) in code.uppercased().utf8.prefix(3).enumerated() { f[i] = b }
        out.append(contentsOf: f)
    }
    private static func u32Seconds(_ d: Date) -> UInt32 { UInt32(max(0, d.timeIntervalSince1970)) }
    private static func appendU16(_ out: inout Data, _ v: UInt16) {
        out.append(UInt8(v & 0xFF)); out.append(UInt8((v >> 8) & 0xFF))
    }
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

- [ ] **Step 4: Run the test, expect pass**

Run: `cd bridge && swift test --filter UsageEncoderTests`
Expected: all UsageEncoderTests pass. (If the `costUnit`/`tokenUnit` expectations are off by rounding, recompute by hand from the formulas above and fix the test's expected literals; the encoder formula is canonical.)

- [ ] **Step 5: Commit**

```bash
git add bridge/Sources/StopwatchBridge/UsageSnapshot.swift bridge/Sources/StopwatchBridge/UsageEncoder.swift bridge/Tests/StopwatchBridgeTests/UsageEncoderTests.swift
git commit -m "bridge: BalanceUsage normalized model + wire encoder"
```

---

## Task 4: Cross-side fixture (golden wire bytes)

Locks the firmware decoder and bridge encoder to the same bytes, the same way `codexbar-cost-two` does for cost.

**Files:**
- Create: `shared/fixtures/usage-openrouter.json` (human reference; documents the values)
- Create: `shared/fixtures/usage-openrouter.hex` (generated)
- Modify: `bridge/Tests/StopwatchBridgeTests/Fixtures.swift` (add `NormalizedUsageSpend.openRouterFixture`)
- Modify: `bridge/Tests/StopwatchBridgeTests/UsageEncoderTests.swift` (add fixture round-trip test)
- Modify: `firmware/test/test_usage_codec/test_main.cpp` (add fixture decode test)

- [ ] **Step 1: Add the shared fixture struct in Fixtures.swift**

Append to `bridge/Tests/StopwatchBridgeTests/Fixtures.swift`:

```swift
extension NormalizedUsageSpend {
    /// Round-number fixture for byte-exact cross-side assertions.
    static var openRouterFixture: NormalizedUsageSpend {
        var cost = [Double](repeating: 0, count: 30); cost[28] = 50.0; cost[29] = 100.0
        var tok  = [UInt64](repeating: 0, count: 30);  tok[28] = 500_000; tok[29] = 1_000_000
        return .init(capturedAt: Date(timeIntervalSince1970: 1_748_455_822), flags: [], providers: [
            .init(kind: .openrouter, status: .ok, currencyCode: "USD", currencyDecimals: 2,
                  todayCost: 100.0, monthCost: 150.0, todayTokens: 1_000_000, monthTokens: 1_500_000,
                  todayRequests: 1240, monthRequests: 9000, costHistory: cost, tokenHistory: tok)
        ])
    }
}
```

- [ ] **Step 2: Write the .hex generator test (writes the file, then asserts)**

Add to `UsageEncoderTests.swift`:

```swift
    @Test func writesAndMatchesOpenRouterFixture() throws {
        let bytes = UsageEncoder.encode(.openRouterFixture)
        // Regenerate the golden file when intentionally changing the format:
        //   set REGEN=1 in the environment to overwrite.
        let url = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent()
            .appendingPathComponent("shared/fixtures/usage-openrouter.hex")
        if ProcessInfo.processInfo.environment["REGEN"] == "1" {
            let hex = bytes.map { String(format: "%02x", $0) }.joined()
            try hex.write(to: url, atomically: true, encoding: .utf8)
        }
        let expected = try Fixtures.loadHex("usage-openrouter")
        #expect(bytes == expected)
    }
```

- [ ] **Step 3: Generate the fixture file**

Run: `cd bridge && REGEN=1 swift test --filter writesAndMatchesOpenRouterFixture`
Expected: PASS (it writes then reads back the same bytes). Confirm the file exists:
Run: `cat shared/fixtures/usage-openrouter.hex` — expect a 816-char hex string (408 bytes = 12 + 96; wait, one record = 108 bytes total). Verify length: `wc -c shared/fixtures/usage-openrouter.hex` ≈ 216 hex chars + newline.

- [ ] **Step 4: Write the human-readable JSON reference**

Create `shared/fixtures/usage-openrouter.json`:

```json
{
  "_comment": "Reference values for usage-openrouter.hex. Generated from NormalizedUsageSpend.openRouterFixture via UsageEncoder. Regenerate the .hex with REGEN=1 swift test --filter writesAndMatchesOpenRouterFixture.",
  "capturedAt": 1748455822,
  "providers": [
    {
      "kind": "openrouter", "currency": "USD", "decimals": 2,
      "todayCost": 100.0, "monthCost": 150.0,
      "todayTokens": 1000000, "monthTokens": 1500000,
      "todayRequests": 1240, "monthRequests": 9000,
      "costHistory_last2": [50.0, 100.0],
      "tokenHistory_last2": [500000, 1000000]
    }
  ]
}
```

- [ ] **Step 5: Add the firmware decode-the-fixture test**

Add to `firmware/test/test_usage_codec/test_main.cpp` (a fixture loader mirroring `test_cost_codec`), and register it in `main()`:

```cpp
#include <fstream>
#include <string>
#include <cctype>

static std::vector<uint8_t> readHexFixture(const char *name) {
    std::string path = std::string("../shared/fixtures/") + name + ".hex";
    std::ifstream f(path);
    if (!f) { char b[256]; snprintf(b, sizeof(b), "missing fixture: %s", path.c_str()); TEST_FAIL_MESSAGE(b); }
    std::string raw((std::istreambuf_iterator<char>(f)), {});
    std::string hex;
    for (char c : raw) if (!isspace((unsigned char)c)) hex.push_back(c);
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) { unsigned v = 0; sscanf(hex.c_str()+i, "%2x", &v); out.push_back((uint8_t)v); }
    return out;
}

void test_openRouterFixtureDecodes(void) {
    auto bytes = readHexFixture("usage-openrouter");
    TEST_ASSERT_EQUAL(12 + 96, (int)bytes.size());
    UsageSnapshot u;
    TEST_ASSERT_EQUAL((int)UsageDecodeResult::Ok, (int)decodeUsageSnapshot(bytes.data(), bytes.size(), u));
    const UsageRecord *r = u.find(BalanceKind::OpenRouter);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_STRING("USD", r->currency);
    TEST_ASSERT_EQUAL(10000, r->todayCostMinor.value());     // $100.00
    TEST_ASSERT_EQUAL(1000000, r->todayTokens.value());
    TEST_ASSERT_EQUAL(1240, r->todayRequests.value());
    // Scaled history bytes (encoder rounding, see UsageEncoderTests): the last day
    // is the max in both arrays. cost: (10000+20)/40 = 250; tokens: (1e6+1961)/3922 = 255.
    TEST_ASSERT_EQUAL(250, r->costHistory[29]);
    TEST_ASSERT_EQUAL(125, r->costHistory[28]);     // half the last day → (5000+20)/40
    TEST_ASSERT_EQUAL(255, r->tokenHistory[29]);
    // Reconstructed cost is within one unit of the true $100.00 (10000 minor):
    TEST_ASSERT_INT_WITHIN(r->costUnit, 10000, (int)r->costHistory[29] * (int)r->costUnit);
}
```

Add `RUN_TEST(test_openRouterFixtureDecodes);` to `main()`.

- [ ] **Step 6: Run both sides against the fixture**

Run: `cd bridge && swift test --filter UsageEncoderTests`  → PASS
Run: `cd firmware && pio test -e native -f test_usage_codec`  → PASS (5 tests)

- [ ] **Step 7: Commit**

```bash
git add shared/fixtures/usage-openrouter.* bridge/Tests/StopwatchBridgeTests/ firmware/test/test_usage_codec/
git commit -m "test: cross-side BalanceUsage golden fixture (openrouter)"
```

---

## Task 5: OpenRouter usage client

The `/api/v1/activity` endpoint (management key) returns one row per day per model with `date`, `usage` (USD), `requests`, `prompt_tokens`, `completion_tokens`, `reasoning_tokens`. We aggregate rows by day into the 30-element arrays.

**Files:**
- Create: `bridge/Sources/StopwatchBridge/UsageClient.swift`
- Test: `bridge/Tests/StopwatchBridgeTests/UsageClientTests.swift`

- [ ] **Step 1: Write the failing test**

Create `bridge/Tests/StopwatchBridgeTests/UsageClientTests.swift`:

```swift
import Foundation
import Testing
@testable import StopwatchBridge

@Suite(.serialized) struct UsageClientTests {
    private func stubSession() -> URLSession {
        let cfg = URLSessionConfiguration.ephemeral
        cfg.protocolClasses = [BalanceStubURLProtocol.self]   // reuse the stub from BalanceClientTests
        return URLSession(configuration: cfg)
    }

    @Test func openRouterAggregatesDailyRows() async {
        // Two rows on the capturedAt day (different models) sum; one row the prior day.
        // capturedAt = 2025-05-28T...; use dates the client will bucket relative to `now`.
        let body = #"""
        {"data":[
          {"date":"2025-05-28","usage":3.0,"requests":10,"prompt_tokens":100,"completion_tokens":50,"reasoning_tokens":0},
          {"date":"2025-05-28","usage":1.0,"requests":5,"prompt_tokens":20,"completion_tokens":5,"reasoning_tokens":0},
          {"date":"2025-05-27","usage":2.0,"requests":4,"prompt_tokens":10,"completion_tokens":2,"reasoning_tokens":0}
        ]}
        """#
        BalanceStubURLProtocol.routes = ["openrouter.ai": .init(status: 200, body: Data(body.utf8))]
        let client = UsageClient(keyStore: FakeKeyStore(["openrouter-mgmt": "sk-mgmt"]), session: stubSession())
        let now = ISO8601DateFormatter().date(from: "2025-05-28T12:00:00Z")!
        let snap = await client.fetchAll([.init(kind: .openrouter, credentialID: "openrouter-mgmt")], now: now)

        #expect(snap.providers.count == 1)
        let p = snap.providers[0]
        #expect(p.status == .ok)
        #expect(p.currencyCode == "USD")
        #expect(p.todayCost == 4.0)                 // 3 + 1
        #expect(p.todayRequests == 15)              // 10 + 5
        #expect(p.todayTokens == 175)               // (100+50) + (20+5)
        #expect(p.costHistory[29] == 4.0)           // today is the last bucket
        #expect(p.costHistory[28] == 2.0)           // yesterday
        #expect(p.monthCost == 6.0)                 // sum of all days
    }

    @Test func missingManagementKeyIsAuthError() async {
        let client = UsageClient(keyStore: FakeKeyStore(), session: stubSession())
        let snap = await client.fetchAll([.init(kind: .openrouter, credentialID: "openrouter-mgmt")],
                                         now: Date(timeIntervalSince1970: 100))
        #expect(snap.providers[0].status == .authError)
    }
}
```

- [ ] **Step 2: Run it, expect failure**

Run: `cd bridge && swift test --filter UsageClientTests`
Expected: build error — `UsageClient` undefined.

- [ ] **Step 3: Write the client**

Create `bridge/Sources/StopwatchBridge/UsageClient.swift`:

```swift
// bridge/Sources/StopwatchBridge/UsageClient.swift
import Foundation

/// Fetches per-provider usage/spend time-series. Phase 1 implements OpenRouter
/// (`/api/v1/activity`, management key). AIHubMix/DeepSeek land in later phases
/// behind the same `fetchAll` interface.
public actor UsageClient {
    public struct Target: Sendable, Equatable {
        public var kind: BalanceKind
        public var credentialID: String     // KeyStore id for the management key / token / cookie
        public init(kind: BalanceKind, credentialID: String) {
            self.kind = kind; self.credentialID = credentialID
        }
    }

    private let keyStore: KeyStore
    private let session: URLSession
    private let timeout: TimeInterval
    public init(keyStore: KeyStore, session: URLSession = .shared, timeout: TimeInterval = 25) {
        self.keyStore = keyStore; self.session = session; self.timeout = timeout
    }

    public func fetchAll(_ targets: [Target], now: Date = Date()) async -> NormalizedUsageSpend {
        var providers: [NormalizedUsageSpend.Provider] = []
        for t in targets {
            switch t.kind {
            case .openrouter: providers.append(await fetchOpenRouter(t, now: now))
            default:          providers.append(unavailable(t.kind))   // other kinds: later phases
            }
        }
        return NormalizedUsageSpend(capturedAt: now, flags: [], providers: providers)
    }

    private func unavailable(_ kind: BalanceKind) -> NormalizedUsageSpend.Provider {
        .init(kind: kind, status: .unreachable, currencyCode: "USD",
              todayCost: nil, monthCost: nil, todayTokens: nil, monthTokens: nil,
              todayRequests: nil, monthRequests: nil,
              costHistory: [Double](repeating: 0, count: 30), tokenHistory: [UInt64](repeating: 0, count: 30))
    }

    private func fetchOpenRouter(_ t: Target, now: Date) async -> NormalizedUsageSpend.Provider {
        guard let key = keyStore.key(for: t.credentialID) else {
            var p = unavailable(.openrouter); p.status = .authError; return p
        }
        guard let url = URL(string: "https://openrouter.ai/api/v1/activity") else {
            return unavailable(.openrouter)
        }
        var req = URLRequest(url: url); req.timeoutInterval = timeout
        req.setValue("Bearer \(key)", forHTTPHeaderField: "Authorization")
        req.setValue("application/json", forHTTPHeaderField: "Accept")
        do {
            let (data, resp) = try await session.data(for: req)
            if let http = resp as? HTTPURLResponse, http.statusCode != 200 {
                var p = unavailable(.openrouter)
                p.status = (http.statusCode == 401 || http.statusCode == 403) ? .authError : .unreachable
                return p
            }
            guard let obj = try JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let rows = obj["data"] as? [[String: Any]] else {
                return unavailable(.openrouter)
            }
            return aggregate(rows, now: now)
        } catch {
            return unavailable(.openrouter)
        }
    }

    /// Buckets activity rows into 30 daily slots ending at `now`'s UTC day (index 29).
    private func aggregate(_ rows: [[String: Any]], now: Date) -> NormalizedUsageSpend.Provider {
        var cost = [Double](repeating: 0, count: 30)
        var toks = [UInt64](repeating: 0, count: 30)
        var reqs = [UInt64](repeating: 0, count: 30)
        var cal = Calendar(identifier: .gregorian); cal.timeZone = TimeZone(identifier: "UTC")!
        let today = cal.startOfDay(for: now)
        let fmt = DateFormatter()
        fmt.calendar = cal; fmt.timeZone = cal.timeZone; fmt.dateFormat = "yyyy-MM-dd"

        for row in rows {
            guard let dateStr = row["date"] as? String, let d = fmt.date(from: dateStr) else { continue }
            let dayStart = cal.startOfDay(for: d)
            guard let diff = cal.dateComponents([.day], from: dayStart, to: today).day else { continue }
            let idx = 29 - diff
            guard idx >= 0 && idx < 30 else { continue }
            cost[idx] += num(row["usage"])
            let pt = UInt64(num(row["prompt_tokens"])); let ct = UInt64(num(row["completion_tokens"]))
            let rt = UInt64(num(row["reasoning_tokens"]))
            toks[idx] += pt + ct + rt
            reqs[idx] += UInt64(num(row["requests"]))
        }
        return .init(kind: .openrouter, status: .ok, currencyCode: "USD", currencyDecimals: 2,
                     todayCost: cost[29], monthCost: cost.reduce(0,+),
                     todayTokens: toks[29], monthTokens: toks.reduce(0,+),
                     todayRequests: reqs[29], monthRequests: reqs.reduce(0,+),
                     costHistory: cost, tokenHistory: toks)
    }

    private func num(_ v: Any?) -> Double {
        switch v {
        case let n as NSNumber: return n.doubleValue
        case let s as String:   return Double(s) ?? 0
        default:                return 0
        }
    }
}
```

- [ ] **Step 4: Run the test, expect pass**

Run: `cd bridge && swift test --filter UsageClientTests`
Expected: both tests pass.

- [ ] **Step 5: Commit**

```bash
git add bridge/Sources/StopwatchBridge/UsageClient.swift bridge/Tests/StopwatchBridgeTests/UsageClientTests.swift
git commit -m "bridge: OpenRouter usage client (/api/v1/activity daily aggregation)"
```

---

## Task 6: Usage cache + bridge service wiring + GATT characteristic

**Files:**
- Create: `bridge/Sources/StopwatchBridge/UsageCache.swift`
- Modify: `bridge/Sources/StopwatchBridge/GATTPeripheral.swift`
- Modify: `bridge/Sources/StopwatchBridge/BridgeService.swift`
- Modify: `bridge/Sources/StopwatchBridge/ProvidersConfig.swift`
- Test: add a cache test to `UsageEncoderTests.swift`

- [ ] **Step 1: Write the cache (mirrors CostCache)**

Create `bridge/Sources/StopwatchBridge/UsageCache.swift`:

```swift
// bridge/Sources/StopwatchBridge/UsageCache.swift
import Foundation

struct UsageCache {
    private var lastGood: Data?

    mutating func recordSuccess(_ usage: NormalizedUsageSpend) -> Data {
        let snapshot = UsageEncoder.encode(usage)
        let anyOk = usage.providers.contains { $0.status == .ok }
        if anyOk && !usage.flags.contains(.bridgeError) {
            lastGood = snapshot
            return snapshot
        }
        return lastGood.map { UsageEncoder.markStale($0, capturedAt: usage.capturedAt, extraFlags: usage.flags) }
            ?? snapshot
    }

    mutating func recordFailure(capturedAt: Date = Date()) -> Data {
        if let lastGood {
            return UsageEncoder.markStale(lastGood, capturedAt: capturedAt, extraFlags: .bridgeError)
        }
        return UsageEncoder.errorEmpty(capturedAt: capturedAt)
    }
}
```

- [ ] **Step 2: Add the cache test**

Add to `UsageEncoderTests.swift`:

```swift
    @Test func cacheKeepsLastGoodOnFailure() {
        var cache = UsageCache()
        let good = cache.recordSuccess(.openRouterFixture)
        #expect(good[3] == 0)
        let failed = cache.recordFailure(capturedAt: Date(timeIntervalSince1970: 1_748_500_000))
        #expect(Array(failed[Protocol.usageHeaderSize...]) == Array(good[Protocol.usageHeaderSize...]))
        #expect((failed[3] & UsageFlags.stale.rawValue) != 0)
        #expect((failed[3] & UsageFlags.bridgeError.rawValue) != 0)
    }
```

Run: `cd bridge && swift test --filter UsageEncoderTests` → PASS.

- [ ] **Step 3: Add the GATT characteristic**

In `bridge/Sources/StopwatchBridge/GATTPeripheral.swift`:

(a) After `private let balanceChar: CBMutableCharacteristic` (line 24) add:
```swift
    private let usageChar:    CBMutableCharacteristic
```
(b) After the `currentBalance`/`pendingBalanceNotify` declarations (line 35) add:
```swift
    private var currentUsage: Data = UsageEncoder.staleEmpty()
    private var pendingUsageNotify: Data?
```
(c) In `init()`, after the `balanceChar = CBMutableCharacteristic(...)` block (line 62), add:
```swift
        self.usageChar = CBMutableCharacteristic(
            type: Protocol.usageSnapshotUUID,
            properties: [.read, .notify],
            value: nil,
            permissions: [.readable]
        )
```
(d) Change the `svc.characteristics` line (line 64) to include it:
```swift
        svc.characteristics = [self.snapshotChar, self.triggerChar, self.costChar, self.balanceChar, self.usageChar]
```
(e) After `updateBalanceSnapshot(_:)` (line 101) add:
```swift
    public func updateUsageSnapshot(_ data: Data) {
        precondition(data.count >= Protocol.usageHeaderSize, "usage snapshot too short")
        currentUsage = data
        if !manager.updateValue(data, for: usageChar, onSubscribedCentrals: nil) {
            pendingUsageNotify = data
        } else {
            pendingUsageNotify = nil
        }
    }
```
(f) In `handleRead`, add a case before `default:` (line 194):
```swift
        case Protocol.usageSnapshotUUID:   source = currentUsage
```
(g) In `flushPendingNotify` (after the balance block, line 230) add:
```swift
        if let pendingUsage = pendingUsageNotify,
           peripheral.updateValue(pendingUsage, for: usageChar, onSubscribedCentrals: nil) {
            pendingUsageNotify = nil
        }
```

- [ ] **Step 4: Add usage providers config + service wiring**

In `bridge/Sources/StopwatchBridge/ProvidersConfig.swift`, add to `ProviderConfig` (after `scale`, line 17):
```swift
    public var usageKind: String?        // "openrouter" | "aihubmix" | "deepseek"; nil = no usage chart
    public var usageCredentialID: String?  // KeyStore id for the management key / token / cookie
```
Add to `Resolved` (after `scale`, line 26):
```swift
        public var usageKind: BalanceKind?
        public var usageCredentialID: String?
```
In `resolved()`, replace the trailing `scale: scale ?? 1\n        )` (the last argument + closing paren of the `Resolved(...)` call, lines 42-43) with:
```swift
            scale: scale ?? 1,
            usageKind: usageKind.map { BalanceKind(fromString: $0) },
            usageCredentialID: usageCredentialID
        )
```
(`Resolved` gets a synthesized memberwise init, so the two new stored properties become its last two parameters; passing them here keeps the call valid.)

In `bridge/Sources/StopwatchBridge/BridgeService.swift`:

(a) After `private var balanceCache = BalanceCache()` (line 14) add:
```swift
    private let usageClient: UsageClient
    private var usageCache = UsageCache()
    private let usageTargets: [UsageClient.Target]
```
(b) In `init`, after `self.balanceClient = BalanceClient(keyStore: KeychainStore())` (line 24) add:
```swift
        self.usageClient = UsageClient(keyStore: KeychainStore())
        self.usageTargets = loadedProviders.compactMap { p in
            guard let k = p.usageKind, let cid = p.usageCredentialID else { return nil }
            return UsageClient.Target(kind: k, credentialID: cid)
        }
```
(c) In `handleRefresh(scope:)`, after the `triggerScopeBalances` early-return block (line 71) add:
```swift
        if scope == Protocol.triggerScopeUsage {
            await handleUsageRefresh()
            return
        }
```
(d) After `handleCostRefresh()` (line 115) add:
```swift
    private func handleUsageRefresh() async {
        guard !usageTargets.isEmpty else {
            await peripheral.updateUsageSnapshot(UsageEncoder.staleEmpty()); return
        }
        let fresh = await usageClient.fetchAll(usageTargets)
        await peripheral.updateUsageSnapshot(usageCache.recordSuccess(fresh))
        let summary = fresh.providers.map { "\($0.kind)=\($0.status)" }.joined(separator: " ")
        FileHandle.standardOutput.write(Data("usage ok: \(summary)\n".utf8))
    }
```

- [ ] **Step 5: Build + full bridge test suite**

Run: `cd bridge && swift build` → `Build complete!`
Run: `cd bridge && swift test` → all suites pass.

- [ ] **Step 6: Commit**

```bash
git add bridge/Sources/StopwatchBridge/UsageCache.swift bridge/Sources/StopwatchBridge/GATTPeripheral.swift bridge/Sources/StopwatchBridge/BridgeService.swift bridge/Sources/StopwatchBridge/ProvidersConfig.swift bridge/Tests/StopwatchBridgeTests/UsageEncoderTests.swift
git commit -m "bridge: usage cache, GATT characteristic, and scope 0x06 wiring"
```

---

## Task 7: Firmware BLE fetch for usage

**Files:**
- Modify: `firmware/src/BleClient.h`
- Modify: `firmware/src/BleClient.cpp`

- [ ] **Step 1: Declare fetchUsage**

In `firmware/src/BleClient.h`, after the `fetchBalances` declaration (line 24) add:
```cpp
    /// Like fetchBalances(), but writes the usage trigger scope and reads BalanceUsage.
    FetchResult fetchUsage(uint8_t *outBytes, size_t bufSize, size_t &outLen);
```

- [ ] **Step 2: Implement it**

In `firmware/src/BleClient.cpp`, after `fetchBalances` (line 82) add:
```cpp
BleClient::FetchResult BleClient::fetchUsage(uint8_t *outBytes, size_t bufSize, size_t &outLen) {
    return fetchInto(kUsageSnapshotUUID, kTriggerScopeUsage, outBytes, bufSize, outLen);
}
```

- [ ] **Step 3: Build for device**

Run: `cd firmware && pio run -e stopwatch` → `[SUCCESS]`.

- [ ] **Step 4: Commit**

```bash
git add firmware/src/BleClient.h firmware/src/BleClient.cpp
git commit -m "firmware: BLE fetchUsage (scope 0x06)"
```

---

## Task 8: Navigation state machine (App)

The detail screen is a sub-state of the Balances view. App tracks the selected provider's record index (or "none" = list) and the chart metric. Tap enters, A exits, B toggles.

**Files:**
- Modify: `firmware/src/App.h`
- Test: `firmware/test/test_state_machine/test_main.cpp` (extend existing)

- [ ] **Step 1: Write failing tests**

Read `firmware/test/test_state_machine/test_main.cpp` first to match its style, then add these tests and register them in `main()`:

```cpp
void test_balanceDetailEnterExit(void) {
    App app; app.begin();
    // Navigate to Balances (Overview → ... → Balances). Simplest: prevView from Overview.
    app.handleEvent(ButtonEvent::KeyAShort);     // Overview → Balances
    TEST_ASSERT_EQUAL((int)ViewId::Balances, (int)app.currentView());
    TEST_ASSERT_FALSE(app.inBalanceDetail());

    app.enterBalanceDetail(2);                   // tap row index 2
    TEST_ASSERT_TRUE(app.inBalanceDetail());
    TEST_ASSERT_EQUAL(2, app.balanceDetailIndex());

    // Button A backs out of the detail without changing the carousel view.
    bool changed = app.handleEvent(ButtonEvent::KeyAShort);
    TEST_ASSERT_TRUE(changed);
    TEST_ASSERT_FALSE(app.inBalanceDetail());
    TEST_ASSERT_EQUAL((int)ViewId::Balances, (int)app.currentView());
}

void test_balanceDetailToggle(void) {
    App app; app.begin();
    app.handleEvent(ButtonEvent::KeyAShort);     // → Balances
    app.enterBalanceDetail(0);
    TEST_ASSERT_EQUAL((int)UsageMetric::Cost, (int)app.usageMetric());
    bool changed = app.handleEvent(ButtonEvent::KeyBShort);   // toggle in detail
    TEST_ASSERT_TRUE(changed);
    TEST_ASSERT_EQUAL((int)UsageMetric::Tokens, (int)app.usageMetric());
    app.handleEvent(ButtonEvent::KeyBShort);                  // toggle back
    TEST_ASSERT_EQUAL((int)UsageMetric::Cost, (int)app.usageMetric());
}

void test_carouselUnaffectedWhenNotInDetail(void) {
    App app; app.begin();
    app.handleEvent(ButtonEvent::KeyBShort);     // Overview → TotalSpend (normal carousel)
    TEST_ASSERT_EQUAL((int)ViewId::TotalSpend, (int)app.currentView());
    TEST_ASSERT_FALSE(app.inBalanceDetail());
}
```

- [ ] **Step 2: Run, expect failure**

Run: `cd firmware && pio test -e native -f test_state_machine`
Expected: FAIL — `inBalanceDetail`, `enterBalanceDetail`, `balanceDetailIndex`, `usageMetric`, `UsageMetric` undefined.

- [ ] **Step 3: Extend App.h**

In `firmware/src/App.h`:

(a) After the `LinkStatus` enum (line 20) add:
```cpp
enum class UsageMetric : uint8_t { Cost = 0, Tokens = 1 };
```
(b) Add public methods (after `setLinkStatus`, line 34):
```cpp
    bool inBalanceDetail() const { return detailIndex_ >= 0; }
    int  balanceDetailIndex() const { return detailIndex_; }
    void enterBalanceDetail(int recordIndex) { detailIndex_ = recordIndex; metric_ = UsageMetric::Cost; }
    void exitBalanceDetail() { detailIndex_ = -1; }
    UsageMetric usageMetric() const { return metric_; }
```
(c) Add private fields (after `link_`, line 40):
```cpp
    int detailIndex_ = -1;          // -1 = list; >=0 = showing that record's detail
    UsageMetric metric_ = UsageMetric::Cost;
```
(d) Replace `handleEvent`'s body intent: button A/B behave differently in detail. Since `handleEvent` is defined in `App.cpp`, this step edits `App.cpp` (next step). For now ensure `begin()` resets detail. In `App.cpp` `begin()` add `detailIndex_ = -1; metric_ = UsageMetric::Cost;`.

- [ ] **Step 4: Update App.cpp handleEvent**

In `firmware/src/App.cpp`, replace the `handleEvent` body with detail-aware logic:

```cpp
bool App::handleEvent(ButtonEvent ev) {
    // Inside a balance detail, A backs out and B toggles the chart metric;
    // neither moves the carousel.
    if (detailIndex_ >= 0) {
        switch (ev) {
            case ButtonEvent::KeyAShort: detailIndex_ = -1; return true;          // back to list
            case ButtonEvent::KeyBShort:
                metric_ = (metric_ == UsageMetric::Cost) ? UsageMetric::Tokens : UsageMetric::Cost;
                return true;
            case ButtonEvent::KeyALong:  wantsRefresh_ = true; return false;
            case ButtonEvent::KeyBLong:  wantsSleep_   = true; return false;
            case ButtonEvent::None:                            return false;
        }
        return false;
    }
    switch (ev) {
        case ButtonEvent::KeyBShort: view_ = nextView(view_); return true;
        case ButtonEvent::KeyAShort: view_ = prevView(view_); return true;
        case ButtonEvent::KeyALong:  wantsRefresh_ = true;    return false;
        case ButtonEvent::KeyBLong:  wantsSleep_   = true;    return false;
        case ButtonEvent::None:                                return false;
    }
    return false;
}
```

And in `begin()` add the reset (after `wantsSleep_ = false;`):
```cpp
    detailIndex_ = -1;
    metric_ = UsageMetric::Cost;
```

- [ ] **Step 5: Run tests, expect pass**

Run: `cd firmware && pio test -e native -f test_state_machine` → PASS.
Run: `cd firmware && pio test -e native` → all suites pass.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/App.h firmware/src/App.cpp firmware/test/test_state_machine/
git commit -m "firmware: balance-detail navigation sub-state (tap/back/toggle)"
```

---

## Task 9: Detail view rendering

Reuses the SPEND & BURN sparkline + entrance animation. Cost or tokens per `UsageMetric`. Falls back to a balance-only screen when there's no usage record for the tapped provider.

**Files:**
- Create: `firmware/src/Views/ProviderUsage.h`
- Create: `firmware/src/Views/ProviderUsage.cpp`

- [ ] **Step 1: Write the header**

Create `firmware/src/Views/ProviderUsage.h`:

```cpp
#pragma once
#include "../UsageCodec.h"
#include "../BalanceCodec.h"
#include "../Renderer.h"
#include "../App.h"
#include "../Anim.h"

namespace stopwatch::views {

/// Per-provider usage detail. `bal` supplies the balance hero (always present);
/// `usage` supplies the chart + totals (may be null → balance-only fallback).
/// `metric` selects the chart series. `anim` drives the entrance.
void drawProviderUsage(Renderer &renderer, const BalanceRecord &bal,
                       const UsageRecord *usage, UsageMetric metric,
                       LinkStatus link, const Entrance &anim);

}  // namespace stopwatch::views
```

- [ ] **Step 2: Write the implementation**

Create `firmware/src/Views/ProviderUsage.cpp`. (Mirrors `Spend.cpp`'s drawSparkline/drawMoneyHero idioms; `kFontDollar` for `$`.)

```cpp
#include "ProviderUsage.h"
#include "../BalanceFormat.h"
#include "../Theme.h"
#include <cstdio>
#include <cstring>

namespace stopwatch::views {

namespace {
// Vertical-bar sparkline rising from baseline left→right (matches Spend.cpp).
void drawBars(M5Canvas &c, int x, int y, int w, int h,
              const uint8_t *values, int count, int maxVal, uint32_t color, uint32_t animElapsedMs) {
    if (count <= 0 || maxVal < 1) return;
    int barW = w / count; if (barW < 1) barW = 1;
    for (int i = 0; i < count; ++i) {
        int bh = (int)((long)values[i] * h / maxVal);
        if (values[i] > 0 && bh < 2) bh = 2;
        bh = (int)(bh * motion::barRise(animElapsedMs, i, count) + 0.5f);
        if (bh <= 0) continue;
        c.fillRect(x + i * barW, y + (h - bh), barW > 1 ? barW - 1 : 1, bh, color);
    }
}

const char *providerLabel(BalanceKind k) {
    switch (k) {
        case BalanceKind::OpenRouter: return "OPENROUTER";
        case BalanceKind::DeepSeek:   return "DEEPSEEK";
        default:                      return "USAGE";
    }
}

void humanizeTokens(uint32_t t, char *buf, size_t n) {
    if (t >= 1000000) snprintf(buf, n, "%.1fM", t / 1000000.0);
    else if (t >= 1000) snprintf(buf, n, "%.0fK", t / 1000.0);
    else snprintf(buf, n, "%u", t);
}
}  // namespace

void drawProviderUsage(Renderer &renderer, const BalanceRecord &bal,
                       const UsageRecord *usage, UsageMetric metric,
                       LinkStatus link, const Entrance &anim) {
    auto &c = renderer.canvas();
    renderer.clear(theme::kBackground);
    c.setTextDatum(middle_center);
    uint32_t e = anim.elapsed();
    uint32_t color = theme::balanceColorFor(bal.kind);

    // Header: provider name.
    c.setFont(theme::kFontTitle);
    c.setTextColor(theme::kTextMuted);
    c.drawString(providerLabel(bal.kind), theme::kCenterX, theme::kCenterY - 88);

    // Hero: current balance (counts up). Reuse balance formatting.
    char num[16];
    if (bal.unlimited)        snprintf(num, sizeof(num), "\xE2\x88\x9E");
    else if (bal.balanceMinor) formatBalanceMinor(bal.balanceMinor.value(), bal.decimals, num, sizeof(num));
    else                       snprintf(num, sizeof(num), "--");
    c.setFont(theme::kFontHero);
    c.setTextColor(color);
    c.drawString(num, theme::kCenterX, theme::kCenterY - 44);
    c.setFont(theme::kFontBody);
    c.setTextColor(theme::kTextMuted);
    c.drawString("balance", theme::kCenterX, theme::kCenterY - 4);

    if (usage) {
        // Totals line: 30d cost · tokens.
        char line[40];
        if (usage->monthCostMinor) {
            char mo[16]; formatBalanceMinor(usage->monthCostMinor.value(), usage->decimals, mo, sizeof(mo));
            char tk[16]; humanizeTokens(usage->monthTokens.value_or(0), tk, sizeof(tk));
            snprintf(line, sizeof(line), "30d %s \xC2\xB7 %s tok", mo, tk);
            c.setFont(theme::kFontBody);
            c.setTextColor(theme::kTextMuted);
            c.drawString(line, theme::kCenterX, theme::kCenterY + 28);
        }

        // Chart: cost or tokens, scaled to its own max.
        const uint8_t *series = (metric == UsageMetric::Cost) ? usage->costHistory : usage->tokenHistory;
        int maxV = 1;
        for (int d = 0; d < kUsageHistoryDays; ++d) if (series[d] > maxV) maxV = series[d];
        c.setFont(theme::kFontMicro);
        c.setTextColor(theme::kTextMuted);
        c.drawString(metric == UsageMetric::Cost ? "30-DAY COST" : "30-DAY TOKENS",
                     theme::kCenterX, theme::kCenterY + 52);
        drawBars(c, theme::kCenterX - 105, theme::kCenterY + 66, 210, 52,
                 series, kUsageHistoryDays, maxV, color, e);
    } else {
        c.setFont(theme::kFontBody);
        c.setTextColor(theme::kTextMuted);
        c.drawString("usage data unavailable", theme::kCenterX, theme::kCenterY + 40);
    }

    // Back hint + pill.
    const char *pill = (link == LinkStatus::NoBridge) ? "no bridge"
                     : (link == LinkStatus::LinkError) ? "link error" : nullptr;
    renderer.drawPill(theme::kCenterX, theme::kCenterY + theme::kRingOuterR - 8,
                      pill, pill ? theme::kPillError : 0);
}

}  // namespace stopwatch::views
```

- [ ] **Step 3: Build for device (view code only compiles in the device env)**

Run: `cd firmware && pio run -e stopwatch`
Expected: `[SUCCESS]`. (Views/*.cpp are not in the native env's build_src_filter, so there is no native test for this file; correctness is verified on-device in Task 11.)

- [ ] **Step 4: Commit**

```bash
git add firmware/src/Views/ProviderUsage.h firmware/src/Views/ProviderUsage.cpp
git commit -m "firmware: per-provider usage detail view (chart + balance hero)"
```

---

## Task 10: Wire the detail view into main.cpp (tap-to-enter, lazy fetch, dispatch)

**Files:**
- Modify: `firmware/src/main.cpp`

- [ ] **Step 1: Add includes + globals**

In `firmware/src/main.cpp`, after `#include "Views/Balances.h"` (line 21) add:
```cpp
#include "Views/ProviderUsage.h"
#include "UsageCodec.h"
```
After `stopwatch::Entrance g_entrance;` (line 34) add:
```cpp
stopwatch::UsageSnapshot g_usage;
bool                     g_usageLoaded = false;
```

- [ ] **Step 2: Dispatch the detail view**

In `drawCurrentView()`, replace the `case ViewId::Balances:` block (lines 47-51) with:
```cpp
        case ViewId::Balances: {
            if (g_app.inBalanceDetail() &&
                g_app.balanceDetailIndex() < g_balance.recordCount) {
                const auto &bal = g_balance.records[g_app.balanceDetailIndex()];
                const UsageRecord *u = g_usage.find(bal.kind);
                views::drawProviderUsage(g_renderer, bal, u, g_app.usageMetric(),
                                         g_app.linkStatus(), g_entrance);
            } else {
                int contentH = views::drawBalances(g_renderer, g_balance, g_app.linkStatus(), g_balScroll.offset());
                g_balScroll.setBounds(contentH, views::balancesViewportHeight());
            }
            break;
        }
```

- [ ] **Step 3: Add a lazy usage fetch (mirrors ensureCostLoaded)**

After the `ensureBalanceLoaded()` function (around line 137) add:
```cpp
static bool fetchUsageAndApply() {
    uint8_t buf[stopwatch::kUsageSnapshotMaxSize];
    size_t len = 0;
    if (g_ble.fetchUsage(buf, sizeof(buf), len) != stopwatch::BleClient::FetchResult::Ok) return false;
    stopwatch::UsageSnapshot us;
    if (stopwatch::decodeUsageSnapshot(buf, len, us) != stopwatch::UsageDecodeResult::Ok) return false;
    g_usage = us;
    g_store.save("usage", buf, len);
    g_usageLoaded = true;
    return true;
}

// On first entry to a balance detail this wake-session, pull usage once.
static void ensureUsageLoaded() {
    if (g_usageLoaded) return;
    renderRefreshingOverlay("Loading usage\xE2\x80\xA6");
    fetchUsageAndApply();
}
```

- [ ] **Step 4: Add the entrance-duration case for the detail**

In `entranceDurationForView()` there's no per-detail branch (it switches on ViewId). Instead, drive the detail entrance from the tap handler (Step 5). No change needed here; `startViewAnim()` for `ViewId::Balances` returns 0 (list doesn't animate), which is correct.

- [ ] **Step 5: Tap-to-enter in the touch block**

In `loop()`, inside the `if (isBalanceView(...))` touch block, replace the touch handling (lines 296-311) with tap detection that enters a detail. Replace from `M5.update();` through the momentum block with:
```cpp
        M5.update();
        auto t = M5.Touch.getDetail();
        if (g_app.inBalanceDetail()) {
            // In detail: ignore scroll; only buttons act (handled above). Keep the
            // entrance animating via the shared block below.
        } else {
            if (t.isPressed()) {
                g_power.noteActivity();
                if (t.wasPressed()) g_balScroll.onPress(t.y);
                else                g_balScroll.onMove(t.y);
                renderCurrent();
            } else if (t.wasReleased()) {
                g_balScroll.onRelease();
                // A clean tap (no meaningful drag) selects the row under the finger.
                if (g_balScroll.isResting()) {
                    int idx = views::balanceRowAtY(t.y, g_balScroll.offset(), g_balance.recordCount);
                    if (idx >= 0) {
                        g_app.enterBalanceDetail(idx);
                        ensureUsageLoaded();
                        g_entrance.start(millis(), stopwatch::motion::kSpendEntranceMs);
                        renderCurrent();
                    }
                }
            }
            if (!g_balScroll.isResting()) {
                g_balScroll.tick(20);
                renderCurrent();
            }
        }
```

Note: `views::balanceRowAtY` is a new helper added in Task 10b below.

- [ ] **Step 6: Make button-driven detail changes animate**

In `loop()`, the existing `else if (changed)` branch calls `startViewAnim()`. When a detail toggle (B) or back (A) happens, `changed` is true and `startViewAnim()` runs; but for the detail we want the spend entrance. Update the `changed` branch:
```cpp
        } else if (changed) {
            if (!isBalanceView(g_app.currentView())) g_balScroll.reset();
            ensureCostLoaded();
            ensureBalanceLoaded();
            if (g_app.inBalanceDetail()) {
                ensureUsageLoaded();
                g_entrance.start(millis(), stopwatch::motion::kSpendEntranceMs);
                renderCurrent();
            } else {
                startViewAnim();
            }
        }
```

- [ ] **Step 7: Load cached usage on boot**

After the balance cache-load block (line 254-258) add:
```cpp
    uint8_t ubuf[stopwatch::kUsageSnapshotMaxSize];
    size_t ulen = 0;
    if (g_store.load("usage", ubuf, sizeof(ubuf), ulen)) {
        stopwatch::decodeUsageSnapshot(ubuf, ulen, g_usage);
    }
```

- [ ] **Step 8: Reset usage-loaded flag on wake**

In `enterSleepAndRefreshOnWake()`, alongside `g_costLoaded = false;` (line 154) add:
```cpp
    g_usageLoaded = false;
```

- [ ] **Step 9: Build**

Run: `cd firmware && pio run -e stopwatch` → `[SUCCESS]`.

- [ ] **Step 10: Commit**

```bash
git add firmware/src/main.cpp
git commit -m "firmware: wire usage detail (tap-to-enter, lazy fetch, dispatch, animation)"
```

---

## Task 10b: Row-hit-test helper

**Files:**
- Modify: `firmware/src/Views/Balances.h`
- Modify: `firmware/src/Views/Balances.cpp`

- [ ] **Step 1: Declare the helper**

In `firmware/src/Views/Balances.h`, after `balancesViewportHeight()` (line 13) add:
```cpp
/// Returns the record index under screen-y `y` given `scrollOffset`, or -1 if the
/// tap is outside any row / the viewport. `count` is the number of records.
int balanceRowAtY(int y, int scrollOffset, int count);
```

- [ ] **Step 2: Implement it (uses the same row geometry as drawBalances)**

In `firmware/src/Views/Balances.cpp`, before the closing `}  // namespace stopwatch::views` add:
```cpp
int balanceRowAtY(int y, int scrollOffset, int count) {
    if (y < kViewportTop || y > kViewportBottom) return -1;
    for (int i = 0; i < count; ++i) {
        int rowY = kViewportTop + kRowHeight / 2 + i * kRowPitch - scrollOffset;
        if (y >= rowY - kRowHeight / 2 && y <= rowY + kRowHeight / 2) return i;
    }
    return -1;
}
```
(These constants `kViewportTop`, `kRowHeight`, `kRowPitch`, `kViewportBottom` live in the anonymous namespace at the top of Balances.cpp; the helper is defined in the same file so it sees them.)

- [ ] **Step 3: Build**

Run: `cd firmware && pio run -e stopwatch` → `[SUCCESS]`.

- [ ] **Step 4: Commit**

```bash
git add firmware/src/Views/Balances.h firmware/src/Views/Balances.cpp
git commit -m "firmware: balanceRowAtY hit-test for tap-to-detail"
```

---

## Task 11: On-device verification (OpenRouter)

**Files:** none (manual verification)

- [ ] **Step 1: Configure an OpenRouter management key + provider entry**

Store the management key in the bridge KeyStore under id `openrouter-mgmt`:
Run: `cd bridge && swift run stopwatch-bridge key set openrouter-mgmt` (paste the management key when prompted; confirm the exact key subcommand via `swift run stopwatch-bridge key --help`).
Add `usageKind: "openrouter"` and `usageCredentialID: "openrouter-mgmt"` to the OpenRouter entry in `providers.json` (next to `~/.config` per `ProvidersConfig.defaultPath`).

- [ ] **Step 2: Run the bridge, confirm a usage frame**

Run: `cd bridge && swift run stopwatch-bridge` (foreground). Tap into a balance detail on the device; watch for `usage ok: openrouter=ok` in the bridge log.

- [ ] **Step 3: Flash and verify on device**

Long-press BOOT (screen off, LED blinks) then:
Run: `cd firmware && make flash` (or `pio run -e stopwatch -t upload`).
On the watch: go to API Balances, tap the OpenRouter row. Expect: detail screen with balance hero, "30d …" totals, and a 30-day cost chart that animates in. Press B → chart switches to tokens. Press A → back to the list.

- [ ] **Step 4: Note any visual adjustments**

If layout collides with the round bezel, adjust the y-offsets in `ProviderUsage.cpp` (Task 9) and re-flash. Commit any tweaks:
```bash
git add firmware/src/Views/ProviderUsage.cpp
git commit -m "firmware: tune usage detail layout from device"
```

---

# PHASE 2 — AIHubMix (gated on a DevTools spike)

AIHubMix runs the open-source `new-api` stack; the dashboard chart is served by `GET /api/data/self?start_timestamp=&end_timestamp=` (per-hour × model: `quota` cost where 500000=$1, `count`, `token_used`), authed by a System Access Token, NOT the model key. This is undocumented, so verify before coding.

## Task 12: DevTools spike — confirm AIHubMix usage endpoint

**Files:** none (investigation); record findings in the spec.

- [ ] **Step 1:** Log into aihubmix.com console with browser DevTools → Network open. Open the usage/billing chart. Identify the exact request that populates it (expected: `/api/data/self?start_timestamp=…&end_timestamp=…`).
- [ ] **Step 2:** Note: exact path, HTTP method, every required header (the `fd***` access token? a cookie? a CSRF header?), response JSON shape (fields per row, the cost unit divisor, the time bucketing).
- [ ] **Step 3:** Replay the request with `curl` using only the System Access Token (generate one in Settings) to confirm it works headlessly without a browser cookie. If it needs a cookie that can't be replayed, record that and treat AIHubMix as balance-only (skip Tasks 13).
- [ ] **Step 4:** Append the confirmed endpoint contract to `docs/superpowers/specs/2026-05-30-balance-usage-design.md` under a new "Verified endpoints" section. Commit.

## Task 13: AIHubMix usage client

**Files:**
- Modify: `bridge/Sources/StopwatchBridge/UsageClient.swift`
- Test: `bridge/Tests/StopwatchBridgeTests/UsageClientTests.swift`

- [ ] **Step 1:** Write a failing test `aihubmixAggregatesDailyRows` modeled on the OpenRouter test, using a stubbed response matching the JSON shape confirmed in Task 12 (per-hour rows summed into days; `quota / 500000 = USD`).
- [ ] **Step 2:** Run it, expect failure.
- [ ] **Step 3:** Add a `fetchAIHubMix(_:now:)` method and a `case .aihubmix:` (note: AIHubMix maps to `BalanceKind.generic` today; if it needs a distinct kind, add `aihubmix` to `BalanceKind` in BOTH `Protocol.h` and `Protocol.swift` first, as a separate committed step, and to `theme::balanceColorFor`). Aggregate per-hour rows into the 30-day arrays using the same UTC-day bucketing as `aggregate(...)`. Convert `quota / 500000` to USD.
- [ ] **Step 4:** Run the test, expect pass. Then `swift test` (full suite).
- [ ] **Step 5:** Commit.

## Task 14: AIHubMix on-device verification

- [ ] Store the access token under `aihubmix-token`, set `usageKind`/`usageCredentialID` in `providers.json`, run bridge, flash, tap the AIHubMix row, confirm chart. Commit any layout tweaks.

---

# PHASE 3 — DeepSeek (cookie replay, gated on the same spike)

DeepSeek has no usage API; the dashboard chart at platform.deepseek.com/usage is browser-only. We replay the dashboard's internal XHR with a captured session cookie. Falls back to balance-only when the cookie is stale.

## Task 15: DevTools spike — confirm DeepSeek usage XHR

**Files:** none (investigation).

- [ ] **Step 1:** Log into platform.deepseek.com/usage with DevTools → Network. Identify the XHR that returns the monthly chart data. Note path, method, headers, cookie name(s), any CSRF token, response shape (daily cost/tokens/requests, currency).
- [ ] **Step 2:** Replay with `curl` using only the captured `Cookie:` header. Confirm it returns JSON headlessly.
- [ ] **Step 3:** **Decision gate:** if it requires a short-lived CSRF token that can't be replayed from a static cookie, STOP — DeepSeek stays balance-only (the detail view already falls back gracefully). Record the decision in the spec and skip Task 16.
- [ ] **Step 4:** If replayable, append the contract to the spec. Commit.

## Task 16: DeepSeek usage client (only if Task 15 passed)

**Files:**
- Modify: `bridge/Sources/StopwatchBridge/UsageClient.swift`
- Modify: `bridge/Sources/StopwatchBridge/UsageClientTests.swift`

- [ ] **Step 1:** Failing test `deepSeekParsesDashboardUsage` with a stubbed response matching Task 15's shape; the cookie comes from `keyStore.key(for: "deepseek-cookie")` and is sent as the `Cookie` header.
- [ ] **Step 2:** Run, expect failure.
- [ ] **Step 3:** Add `fetchDeepSeek(_:now:)` + `case .deepseek:`. Send the cookie via `req.setValue(cookie, forHTTPHeaderField: "Cookie")`. Parse daily rows into the arrays; currency CNY. On 401/403/redirect-to-login → status `.authError` (cookie expired) so the view shows balance-only.
- [ ] **Step 4:** Run, expect pass. Full `swift test`.
- [ ] **Step 5:** Commit.

## Task 17: DeepSeek on-device verification

- [ ] Store the cookie under `deepseek-cookie`, set `usageKind`/`usageCredentialID`, run bridge, flash, tap DeepSeek row. Confirm chart (CNY) or graceful balance-only fallback. Commit tweaks.

---

# PHASE 4 — Polish

## Task 18: Balance-only detail for non-usage providers

The detail view already falls back to balance-only when `g_usage.find(bal.kind)` is null (Task 9/10). This task verifies and refines that path.

- [ ] **Step 1:** Flash; tap a non-usage row (e.g. Groq, or a generic key). Confirm the balance-only detail renders cleanly (name, balance hero, "usage data unavailable", back works) with no chart and no crash.
- [ ] **Step 2:** If the empty state looks bare, add the `updatedAt`/currency line to `ProviderUsage.cpp`'s `else` branch (balance-only). Re-flash.
- [ ] **Step 3:** Commit any refinement.

## Task 19: Usage-stale marker

- [ ] **Step 1:** When `g_usage.isStale()` or the matched record's `status == BalanceStatus::Stale`, show a "stale" pill on the detail (reuse `theme::kPillStale`). Add this to `ProviderUsage.cpp`'s pill logic.
- [ ] **Step 2:** Build, flash, verify by stopping the bridge and re-entering a detail (cached usage shows with the stale pill). Commit.

## Task 20: Final regression pass

- [ ] **Step 1:** Run `cd firmware && pio test -e native` → all suites pass.
- [ ] **Step 2:** Run `cd bridge && swift test` → all suites pass.
- [ ] **Step 3:** Run `cd firmware && pio run -e stopwatch` → `[SUCCESS]`.
- [ ] **Step 4:** Flash and walk the full carousel + every balance detail once. Confirm no regressions to the existing views (rings, spend, balances list scroll).
- [ ] **Step 5:** Commit any final fixes; the feature is complete.

---

## Notes for the implementer

- **Phases 2–3 are spike-gated.** Do not write AIHubMix/DeepSeek client code before Tasks 12/15 confirm the endpoints; the request shapes in those tasks are deliberately left to the spike because the endpoints are undocumented and may differ from expectations. The firmware and encoder are provider-agnostic, so they need no changes for Phases 2–3 beyond possibly adding an `aihubmix` `BalanceKind`.
- **The firmware never assumes usage exists.** Every detail render tolerates a null `UsageRecord`. A provider with no usage data is a first-class state, not an error.
- **Keep wire constants in lockstep.** Any change to record size/offsets must update `firmware/src/Protocol.h`, `bridge/Sources/StopwatchBridge/Protocol.swift`, `shared/PROTOCOL.md §3C`, and regenerate `usage-openrouter.hex` (`REGEN=1 swift test --filter writesAndMatchesOpenRouterFixture`).
