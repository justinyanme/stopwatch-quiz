# Cost Provider Header + Models-Used Line Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On the per-provider cost screen, replace the misleading single top-model header with the provider name plus a token-ordered "models used today" line, so the today-total number is never mislabeled as one model's cost.

**Architecture:** A `CostSnapshot` wire change (major bump v1→v2): each per-record `topModel[12]` field becomes `modelCount(1) + models[3][12]`. The bridge ranks the latest day's models by tokens (surfacing even an unpriced new model) and encodes the top 3; the firmware joins them with `·` under the provider name, appending `+N` overflow. No partial/incomplete-total handling (deliberately out of scope — see spec).

**Tech Stack:** Swift Package (bridge, `swift test`), PlatformIO/Arduino C++ (firmware, `pio test -e native` / `pio run`), shared JSON+hex golden fixtures locking both sides.

**Spec:** `docs/superpowers/specs/2026-06-01-cost-provider-models-header-design.md`

---

## Setup

- [ ] **Create a feature branch off `main`**

```bash
cd /Users/justinyan/Documents/JProj/stopwatch-quiz
git checkout -b feature/cost-models-header
```

## File Structure

**Wire contract (must stay mirrored):**
- `shared/PROTOCOL.md` — §3A CostSnapshot doc (Task 1)
- `bridge/Sources/StopwatchBridge/Protocol.swift` — Swift constants (Task 2)
- `firmware/src/Protocol.h` — C++ constants (Task 3)

**Bridge (Swift):**
- `CostSnapshot.swift` — `NormalizedCost.Provider.models` replaces `topModel` (Task 2)
- `CodexbarClient.swift` — decode `totalTokens`, `latestDayModelsByTokens` replaces `displayModel`/`topModel` (Task 2)
- `CostEncoder.swift` — `appendModels` writes count+3 slots (Task 2)
- Tests: `CostEncoderTests.swift`, `CostClientTests.swift`, `CodexbarClientTests.swift`, `Fixtures.swift` (Task 2)
- `shared/fixtures/codexbar-cost-two.{json,hex}` — golden, regenerated (Task 2)

**Firmware (C++):**
- `CostCodec.h` / `CostCodec.cpp` — `CostRecord.models[]` + decode (Task 3)
- `CostFormat.h` / `CostFormat.cpp` — new pure `costModelsLine()` (Task 4)
- `Views/Spend.cpp` — `drawProviderCost` header + models line (Task 5)
- Tests: `test/test_cost_codec/test_main.cpp` (Task 3), `test/test_cost_format/test_main.cpp` (Task 4)

Each task ends green and committed. Bridge (Task 2) regenerates the `.hex` that firmware (Task 3) consumes, so do them in order.

---

## Task 1: Update the wire contract doc

**Files:**
- Modify: `shared/PROTOCOL.md:83-113`

- [ ] **Step 1: Rewrite §3A header and per-record table**

Replace the block from `## 3A.` line 85 through the per-record table (line 111) so it reads:

```markdown
Independent of `UsageSnapshot`; its own `(versionMajor, versionMinor)`. All integers little-endian. Size = `12 + 85 × recordCount`. Codex + Claude ⇒ **182 bytes**. Gemini has no `/cost` data and is omitted.

### 3A.1 Header (12 bytes)

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0 | uint8 | `versionMajor` | `0x02`. |
| 1 | uint8 | `versionMinor` | `0x00`. |
| 2 | uint8 | `recordCount` | Number of cost records (0–2 today). |
| 3 | uint8 | `flags` | bit0 stale, bit1 bridge_error, bit2 cost_unavailable. |
| 4 | uint32 | `capturedAt` | Unix seconds. |
| 8 | uint8 | `historyDays` | `30`. |
| 9 | uint8 | `reserved` | `0`. |
| 10 | uint16 | `historyUnitCents` | Shared scale: cents per history unit (≥1). |

### 3A.2 Per-record (85 bytes, repeated `recordCount` times)

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0 | uint8 | `providerID` | 1 = codex, 2 = claude. |
| 1 | uint8 | `reserved` | `0`. |
| 2 | uint32 | `todayCostCents` | `0xFFFFFFFF` = unknown. |
| 6 | uint32 | `monthCostCents` | `0xFFFFFFFF` = unknown. |
| 10 | uint32 | `todayTokens` | `0xFFFFFFFF` = unknown. |
| 14 | uint32 | `monthTokens` | `0xFFFFFFFF` = unknown. |
| 18 | uint8 | `modelCount` | Total distinct models on the latest dated day (drives `+N` overflow). |
| 19 | char[12] | `models[0]` | Top model by today's tokens. UTF-8, null-padded, vendor-prefix-stripped. |
| 31 | char[12] | `models[1]` | 2nd by tokens; all-zero if absent. |
| 43 | char[12] | `models[2]` | 3rd by tokens; all-zero if absent. |
| 55 | uint8[30] | `history` | Oldest→newest; index 29 = `capturedAt` day; `round(dayCents / historyUnitCents)`. |
```

- [ ] **Step 2: Commit**

```bash
git add shared/PROTOCOL.md
git commit -m "protocol: CostSnapshot v2 — provider models list replaces topModel

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Bridge — emit token-ordered models list (v2)

**Files:**
- Modify: `bridge/Sources/StopwatchBridge/Protocol.swift:21-26`
- Modify: `bridge/Sources/StopwatchBridge/CostSnapshot.swift:15-46`
- Modify: `bridge/Sources/StopwatchBridge/CodexbarClient.swift:153-250`
- Modify: `bridge/Sources/StopwatchBridge/CostEncoder.swift:22-98`
- Modify: `bridge/Tests/StopwatchBridgeTests/Fixtures.swift:100-119`
- Modify: `bridge/Tests/StopwatchBridgeTests/CostEncoderTests.swift`
- Modify: `bridge/Tests/StopwatchBridgeTests/CostClientTests.swift:30-36`
- Modify: `bridge/Tests/StopwatchBridgeTests/CodexbarClientTests.swift:86-131`
- Modify: `shared/fixtures/codexbar-cost-two.json`

- [ ] **Step 1: Bump Swift protocol constants**

In `Protocol.swift`, replace the cost block (lines 21-25) with:

```swift
    public static let costVersionMajor: UInt8 = 2
    public static let costVersionMinor: UInt8 = 0
    public static let costHeaderSize    = 12
    public static let costRecordSize    = 85
    public static let costMaxModelSlots = 3
    public static let costHistoryDays   = 30
```

- [ ] **Step 2: Replace `topModel` with `models` on the Provider struct**

In `CostSnapshot.swift`, replace the `Provider` struct (lines 16-35) with:

```swift
    public struct Provider: Equatable, Sendable {
        public var providerID: ProviderID
        public var todayCostUSD: Double?      // nil → 0xFFFFFFFF on the wire
        public var monthCostUSD: Double?
        public var todayTokens: UInt64?
        public var monthTokens: UInt64?
        public var models: [String]           // today's models, token-ordered, full names (encoder shortens). count = total distinct.
        public var history: [Double]          // dense, length 30, USD/day, index 29 = capturedAt day

        public init(providerID: ProviderID, todayCostUSD: Double?, monthCostUSD: Double?,
                    todayTokens: UInt64?, monthTokens: UInt64?, models: [String], history: [Double]) {
            self.providerID = providerID
            self.todayCostUSD = todayCostUSD
            self.monthCostUSD = monthCostUSD
            self.todayTokens = todayTokens
            self.monthTokens = monthTokens
            self.models = models
            self.history = history
        }
    }
```

- [ ] **Step 3: Decode per-model tokens and rank the latest day by tokens**

In `CodexbarClient.swift`, replace `decodeCost` (lines 153-180) with:

```swift
    private func decodeCost(_ data: Data, now: Date) throws -> NormalizedCost {
        let raw = try JSONDecoder().decode([RawCost].self, from: data)

        let providers = raw.compactMap { c -> NormalizedCost.Provider? in
            guard let id = ProviderID(fromString: c.provider) else { return nil }
            let daily = c.daily ?? []
            let dailyPairs = daily.map { (date: $0.date, costUSD: $0.totalCost ?? 0) }
            let tokenDays = daily.map { day -> (date: String, models: [String: UInt64]) in
                var m: [String: UInt64] = [:]
                for b in day.modelBreakdowns ?? [] {
                    m[b.modelName, default: 0] += UInt64(max(0, (b.totalTokens ?? 0).rounded()))
                }
                return (date: day.date, models: m)
            }
            return .init(
                providerID:   id,
                todayCostUSD: c.sessionCostUSD,
                monthCostUSD: c.last30DaysCostUSD,
                todayTokens:  c.sessionTokens.map { UInt64(max(0, $0.rounded())) },
                monthTokens:  c.last30DaysTokens.map { UInt64(max(0, $0.rounded())) },
                models:       Self.latestDayModelsByTokens(from: tokenDays),
                history:      Self.alignDailyHistory(dailyPairs, anchor: now,
                                                     days: Protocol.costHistoryDays)
            )
        }

        var flags: CostFlags = []
        if providers.isEmpty { flags.insert(.costUnavailable) }
        return .init(capturedAt: now, flags: flags, providers: providers)
    }
```

- [ ] **Step 4: Add `totalTokens` to the decodable breakdown**

In `CostSnapshot.swift`'s decoder — actually in `CodexbarClient.swift`, replace the `ModelBreakdown` struct (line 194) with:

```swift
            struct ModelBreakdown: Decodable { var modelName: String; var cost: Double?; var totalTokens: Double? }
```

- [ ] **Step 5: Replace `displayModel`/`topModel` with `latestDayModelsByTokens`**

In `CodexbarClient.swift`, delete `displayModel` (lines 214-240) and `topModel` (lines 242-250) and replace them with:

```swift
    /// Models used on the newest dated daily record, ordered by today's tokens
    /// (descending), ties broken by model name ascending. Empty if no dated day
    /// has model tokens. Token order surfaces the dominant model first — including
    /// one codexbar hasn't priced yet, since the breakdown carries `totalTokens`
    /// even when `cost` is null.
    static func latestDayModelsByTokens(from daily: [(date: String, models: [String: UInt64])]) -> [String] {
        var latestDay: Date?
        var latestModels: [String: UInt64] = [:]

        for day in daily {
            guard !day.models.isEmpty, let date = costDay(day.date) else { continue }
            if let currentLatest = latestDay {
                if date > currentLatest {
                    latestDay = date
                    latestModels = day.models
                } else if date == currentLatest {
                    for (name, tokens) in day.models { latestModels[name, default: 0] += tokens }
                }
            } else {
                latestDay = date
                latestModels = day.models
            }
        }

        return latestModels
            .sorted { a, b in a.value != b.value ? a.value > b.value : a.key < b.key }
            .map(\.key)
    }
```

- [ ] **Step 6: Encode `modelCount` + 3 name slots**

In `CostEncoder.swift`, replace the model append (line 29) `appendModel(&out, p.topModel)` with:

```swift
            appendModels(&out, p.models)
```

Then replace `appendModel` (lines 91-98) with:

```swift
    private static func appendModels(_ out: inout Data, _ models: [String]) {
        out.append(UInt8(min(models.count, 255)))   // total count for +N overflow
        for i in 0..<Protocol.costMaxModelSlots {
            var field = [UInt8](repeating: 0, count: 12)
            if i < models.count {
                let bytes = Array(shortenModel(models[i]).utf8.prefix(11))
                for (j, b) in bytes.enumerated() { field[j] = b }
            }
            out.append(contentsOf: field)
        }
    }
```

- [ ] **Step 7: Update the Swift normalized fixture**

In `Fixtures.swift`, replace `costFixtureTwo` (lines 103-118) with:

```swift
    static var costFixtureTwo: NormalizedCost {
        var codexHist = [Double](repeating: 0, count: 30); codexHist[29] = 120.0
        var claudeHist = [Double](repeating: 0, count: 30); claudeHist[29] = 60.0
        return .init(
            capturedAt: Date(timeIntervalSince1970: 1748455822),
            flags: [],
            providers: [
                .init(providerID: .codex,  todayCostUSD: 12.0, monthCostUSD: 300.0,
                      todayTokens: 1_000_000, monthTokens: 100_000_000,
                      models: ["gpt-5.5"], history: codexHist),
                .init(providerID: .claude, todayCostUSD: 8.0,  monthCostUSD: 200.0,
                      todayTokens: 2_000_000, monthTokens: 50_000_000,
                      models: ["claude-opus-4-8", "claude-sonnet-4-6", "claude-haiku-4-5"],
                      history: claudeHist),
            ]
        )
    }
```

- [ ] **Step 8: Update the two inline encoder fixtures**

In `CostEncoderTests.swift`, in `unknownsEncodeAsSentinels` change `topModel: nil,` (line 49) to `models: [],`. In `sharedScaleDrivenByLargerProvider` change both `topModel: nil` occurrences (lines 82-83) to `models: []`.

- [ ] **Step 9: Rewrite the encoder byte-offset assertions for the 85-byte record**

In `CostEncoderTests.swift`, replace `encodesHeaderRecordsAndSharedScale` (lines 7-38) with:

```swift
    @Test func encodesHeaderRecordsAndSharedScale() {
        let bytes = CostEncoder.encode(.costFixtureTwo)

        // Size = 12 + 85*2
        #expect(bytes.count == 182)
        // Header
        #expect(bytes[0] == Protocol.costVersionMajor)   // 2
        #expect(bytes[1] == Protocol.costVersionMinor)   // 0
        #expect(bytes[2] == 2)                            // recordCount
        #expect(bytes[3] == 0)                            // flags
        #expect(bytes[8] == 30)                           // historyDays
        #expect(bytes[9] == 0)                            // reserved
        #expect(bytes[10] == 48 && bytes[11] == 0)        // historyUnitCents = 48

        // Record 0 = codex at offset 12
        #expect(bytes[12] == ProviderID.codex.rawValue)
        #expect(bytes[14] == 0xB0 && bytes[15] == 0x04)   // todayCents 1200
        #expect(bytes[18] == 0x30 && bytes[19] == 0x75)   // monthCents 30000
        #expect(bytes[22] == 0x40 && bytes[23] == 0x42 && bytes[24] == 0x0F && bytes[25] == 0x00) // 1_000_000
        #expect(bytes[26] == 0x00 && bytes[27] == 0xE1 && bytes[28] == 0xF5 && bytes[29] == 0x05) // 100_000_000
        #expect(bytes[30] == 1)                           // modelCount
        #expect(Array(bytes[31..<38]) == Array("gpt-5.5".utf8))   // models[0]
        #expect(bytes[38] == 0)                           // null pad
        #expect(bytes[43..<67].allSatisfy { $0 == 0 })    // models[1] + models[2] empty
        // history[29] at 12+55+29 = 96 ⇒ 12000/48 = 250; everything before it is 0
        #expect(bytes[96] == 250)
        #expect(bytes[67..<96].allSatisfy { $0 == 0 })

        // Record 1 = claude at offset 97
        #expect(bytes[97] == ProviderID.claude.rawValue)
        #expect(bytes[115] == 3)                          // modelCount
        #expect(Array(bytes[116..<124]) == Array("opus-4-8".utf8))     // shortened models[0]
        #expect(Array(bytes[128..<138]) == Array("sonnet-4-6".utf8))   // models[1]
        #expect(Array(bytes[140..<149]) == Array("haiku-4-5".utf8))    // models[2]
        #expect(bytes[181] == 125)                        // claude history[29] (97+55+29) = 6000/48
    }
```

- [ ] **Step 10: Fix sentinel + shared-scale offset assertions**

In `CostEncoderTests.swift` `unknownsEncodeAsSentinels`, replace the model assertion (lines 55-56) with:

```swift
        // modelCount 0, model slots all-zero
        #expect(bytes[30] == 0)
        #expect(bytes[31..<67].allSatisfy { $0 == 0 })
```

In `sharedScaleDrivenByLargerProvider`, replace the two history-byte assertions (lines 88-89) with:

```swift
        #expect(bytes[96] == 50)     // codex history[29] (offset 12+55+29) = 5000/100
        #expect(bytes[181] == 255)   // claude history[29] (offset 97+55+29) = 25500/100
```

- [ ] **Step 11: Add FREEZE writeback to the golden-hex test**

In `CostEncoderTests.swift`, replace `costFixtureMatchesSavedHex` (lines 40-43) with:

```swift
    @Test func costFixtureMatchesSavedHex() throws {
        let data = CostEncoder.encode(.costFixtureTwo)
        let url = URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent()
            .appendingPathComponent("shared/fixtures/codexbar-cost-two.hex")
        if ProcessInfo.processInfo.environment["FREEZE_FIXTURES"] != nil {
            let hex = data.map { String(format: "%02x", $0) }.joined()
            try (hex + "\n").write(to: url, atomically: true, encoding: .utf8)
        }
        let expected = try Fixtures.loadHex("codexbar-cost-two")
        #expect(data == expected)
    }
```

- [ ] **Step 12: Replace the `topModel` ranking test**

In `CostClientTests.swift`, replace `picksHighestCostModel` (lines 30-36) with:

```swift
    @Test func ordersLatestDayModelsByTokens() {
        let models = CodexbarClient.latestDayModelsByTokens(from: [
            ("2026-05-30", ["opus-4-8": 1_500_000, "sonnet-4-6": 400_000, "haiku-4-5": 100_000]),
            ("2026-05-29", ["older-model": 9_999_999]),
        ])
        #expect(models == ["opus-4-8", "sonnet-4-6", "haiku-4-5"])
    }

    @Test func latestDayModelsEmptyWhenNoDatedModels() {
        #expect(CodexbarClient.latestDayModelsByTokens(from: []).isEmpty)
    }
```

- [ ] **Step 13: Update the JSON fixture with per-model tokens + multi-model Claude**

Overwrite `shared/fixtures/codexbar-cost-two.json` with:

```json
[
  {
    "provider": "codex",
    "sessionCostUSD": 12.0,
    "sessionTokens": 1000000,
    "last30DaysCostUSD": 300.0,
    "last30DaysTokens": 100000000,
    "daily": [
      { "date": "2026-05-29", "totalCost": 120.0, "modelBreakdowns": [ { "modelName": "gpt-5.5", "cost": 120.0, "totalTokens": 1000000 } ] },
      { "date": "2026-05-27", "totalCost": 10.0,  "modelBreakdowns": [ { "modelName": "gpt-5.5", "cost": 10.0, "totalTokens": 80000 } ] }
    ]
  },
  {
    "provider": "claude",
    "sessionCostUSD": 8.0,
    "sessionTokens": 2000000,
    "last30DaysCostUSD": 200.0,
    "last30DaysTokens": 50000000,
    "daily": [
      { "date": "2026-05-29", "totalCost": 60.0, "modelBreakdowns": [
        { "modelName": "claude-opus-4-8",   "cost": 50.0, "totalTokens": 1500000 },
        { "modelName": "claude-sonnet-4-6", "cost": 8.0,  "totalTokens": 400000 },
        { "modelName": "claude-haiku-4-5",  "cost": 2.0,  "totalTokens": 100000 }
      ] }
    ]
  }
]
```

- [ ] **Step 14: Update the JSON-decode assertions**

In `CodexbarClientTests.swift` `parsesCostFixture`, replace the two `topModel` assertions (lines 100 and 104) with:

```swift
        #expect(codex.models == ["gpt-5.5"])
```

and

```swift
        #expect(cost.providers[1].models == ["claude-opus-4-8", "claude-sonnet-4-6", "claude-haiku-4-5"])  // token-ordered; encoder shortens
```

In `costModelLabelPrefersLatestDailyModel` (lines 108-131), add `"totalTokens"` to each breakdown and assert `models`. Replace the `daily` array body and the final assertion so the test reads:

```swift
          "daily": [
            { "date": "2026-05-30", "totalCost": 1.0,
              "modelBreakdowns": [ { "modelName": "latest-daily-model", "cost": 1.0, "totalTokens": 1000 } ] },
            { "date": "2026-05-29", "totalCost": 300.0,
              "modelBreakdowns": [ { "modelName": "older-cost-heavy-model", "cost": 300.0, "totalTokens": 9000000 } ] }
          ]
```

and the final expectation (was line 130):

```swift
        #expect(cost.providers[0].models == ["latest-daily-model"])  // newest dated day wins, not the token-heavy older one
```

- [ ] **Step 15: Regenerate the golden hex, then verify it's stable**

```bash
cd /Users/justinyan/Documents/JProj/stopwatch-quiz/bridge
FREEZE_FIXTURES=1 swift test --filter CostEncoderTests
swift test
```
Expected: first run rewrites `shared/fixtures/codexbar-cost-two.hex` (now 182 bytes → 366 hex chars + newline); second `swift test` is all-green including `costFixtureMatchesSavedHex`, `encodesHeaderRecordsAndSharedScale`, `parsesCostFixture`, `ordersLatestDayModelsByTokens`.

- [ ] **Step 16: Sanity-check the regenerated fixture size**

```bash
cd /Users/justinyan/Documents/JProj/stopwatch-quiz
tr -d '\n' < shared/fixtures/codexbar-cost-two.hex | wc -c
```
Expected: `364` (182 bytes × 2 hex chars; the trailing newline is stripped by `tr`).

- [ ] **Step 17: Commit**

```bash
git add bridge/Sources/StopwatchBridge/Protocol.swift \
        bridge/Sources/StopwatchBridge/CostSnapshot.swift \
        bridge/Sources/StopwatchBridge/CodexbarClient.swift \
        bridge/Sources/StopwatchBridge/CostEncoder.swift \
        bridge/Tests/StopwatchBridgeTests/ \
        shared/fixtures/codexbar-cost-two.json \
        shared/fixtures/codexbar-cost-two.hex
git commit -m "bridge: encode token-ordered models list (CostSnapshot v2)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Firmware — decode the v2 record

**Files:**
- Modify: `firmware/src/Protocol.h:28-33`
- Modify: `firmware/src/CostCodec.h:9-17`
- Modify: `firmware/src/CostCodec.cpp:34-45`
- Modify: `firmware/test/test_cost_codec/test_main.cpp:24-52`

- [ ] **Step 1: Update firmware protocol constants**

In `Protocol.h`, replace the cost constants block (lines 28-33) with:

```cpp
constexpr uint8_t  kCostVersionMajor    = 2;
constexpr uint8_t  kCostHeaderSize      = 12;
constexpr uint8_t  kCostRecordSize      = 85;
constexpr uint8_t  kCostHistoryDays     = 30;
constexpr uint8_t  kCostMaxModelSlots   = 3;
constexpr uint8_t  kCostMaxRecords      = 2;   // codex, claude
constexpr uint16_t kCostSnapshotMaxSize = kCostHeaderSize + kCostRecordSize * kCostMaxRecords;  // 182
```

- [ ] **Step 2: Replace `topModel` with `modelCount` + `models[][]` on CostRecord**

In `CostCodec.h`, replace the `topModel` field (line 15) so the struct's model area reads:

```cpp
    uint8_t modelCount = 0;                        // total distinct models today (for +N overflow)
    char    models[kCostMaxModelSlots][13] = {};   // token-ordered; 12 wire bytes + null each
```

- [ ] **Step 3: Update the failing test for the new fields**

In `test/test_cost_codec/test_main.cpp`, replace `test_costFixtureDecodes` (lines 24-52) with:

```cpp
void test_costFixtureDecodes(void) {
    auto bytes = readHexFixture("codexbar-cost-two");
    TEST_ASSERT_EQUAL(182, bytes.size());

    CostSnapshot cs;
    auto rc = decodeCostSnapshot(bytes.data(), bytes.size(), cs);
    TEST_ASSERT_EQUAL((int)CostDecodeResult::Ok, (int)rc);
    TEST_ASSERT_EQUAL(2, cs.versionMajor);
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
    TEST_ASSERT_EQUAL(1, codex->modelCount);
    TEST_ASSERT_EQUAL_STRING("gpt-5.5", codex->models[0]);
    TEST_ASSERT_EQUAL_STRING("", codex->models[1]);
    TEST_ASSERT_EQUAL(250, codex->history[29]);

    const CostRecord *claude = cs.find(ProviderID::Claude);
    TEST_ASSERT_NOT_NULL(claude);
    TEST_ASSERT_EQUAL(3, claude->modelCount);
    TEST_ASSERT_EQUAL_STRING("opus-4-8", claude->models[0]);
    TEST_ASSERT_EQUAL_STRING("sonnet-4-6", claude->models[1]);
    TEST_ASSERT_EQUAL_STRING("haiku-4-5", claude->models[2]);
    TEST_ASSERT_EQUAL(125, claude->history[29]);

    TEST_ASSERT_NULL(cs.find(ProviderID::Gemini));
}
```

- [ ] **Step 4: Run the test to verify it fails**

```bash
cd /Users/justinyan/Documents/JProj/stopwatch-quiz/firmware
pio test -e native -f test_cost_codec
```
Expected: FAIL — `CostRecord` has no member `modelCount`/`models` yet (compile error) — the decoder still writes `topModel`.

- [ ] **Step 5: Update the decoder to the v2 layout**

In `CostCodec.cpp`, replace the per-record body (lines 37-44) with:

```cpp
        rec.id          = (ProviderID)r[0];
        rec.todayCents  = optU32(r + 2);
        rec.monthCents  = optU32(r + 6);
        rec.todayTokens = optU32(r + 10);
        rec.monthTokens = optU32(r + 14);
        rec.modelCount  = r[18];
        for (int m = 0; m < kCostMaxModelSlots; ++m) {
            memcpy(rec.models[m], r + 19 + m * 12, 12);
            rec.models[m][12] = '\0';
        }
        memcpy(rec.history, r + 55, kCostHistoryDays);
```

- [ ] **Step 6: Run the test to verify it passes**

```bash
cd /Users/justinyan/Documents/JProj/stopwatch-quiz/firmware
pio test -e native -f test_cost_codec
```
Expected: PASS (all three tests; `test_unknownCentsBecomeNullopt` and `test_futureMajorRejected` use the constants and need no edits).

- [ ] **Step 7: Commit**

```bash
git add firmware/src/Protocol.h firmware/src/CostCodec.h firmware/src/CostCodec.cpp \
        firmware/test/test_cost_codec/test_main.cpp
git commit -m "firmware: decode CostSnapshot v2 models list

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Firmware — pure models-line formatter

**Files:**
- Modify: `firmware/src/CostFormat.h:1-13`
- Modify: `firmware/src/CostFormat.cpp` (add function + includes)
- Modify: `firmware/test/test_cost_format/test_main.cpp`

- [ ] **Step 1: Write the failing test**

In `test/test_cost_format/test_main.cpp`, add the include `#include "../../src/CostCodec.h"` after line 3, then add this test before `main`:

```cpp
void test_costModelsLine(void) {
    char buf[64];

    CostRecord r{};
    r.modelCount = 3;
    strcpy(r.models[0], "opus-4-8");
    strcpy(r.models[1], "sonnet-4-6");
    strcpy(r.models[2], "haiku-4-5");
    costModelsLine(r, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("opus-4-8 \xC2\xB7 sonnet-4-6 \xC2\xB7 haiku-4-5", buf);

    CostRecord more{};
    more.modelCount = 5;                 // used more than the 3 carried → "+2"
    strcpy(more.models[0], "opus-4-8");
    strcpy(more.models[1], "sonnet-4-6");
    strcpy(more.models[2], "haiku-4-5");
    costModelsLine(more, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("opus-4-8 \xC2\xB7 sonnet-4-6 \xC2\xB7 haiku-4-5 +2", buf);

    CostRecord one{};
    one.modelCount = 1;
    strcpy(one.models[0], "gpt-5.5");
    costModelsLine(one, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("gpt-5.5", buf);

    CostRecord none{};
    costModelsLine(none, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("", buf);
}
```

Add `RUN_TEST(test_costModelsLine);` after `RUN_TEST(test_humanizeTokens);`.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /Users/justinyan/Documents/JProj/stopwatch-quiz/firmware
pio test -e native -f test_cost_format
```
Expected: FAIL — `costModelsLine` is not declared (compile error).

- [ ] **Step 3: Declare the formatter**

In `CostFormat.h`, add `#include "CostCodec.h"` after the existing includes (after line 3), and add this declaration after the `humanizeTokens` declaration (after line 11):

```cpp
/// Joins a record's carried model names (token-ordered, up to kCostMaxModelSlots)
/// with " · " (U+00B7), appending " +N" when the day used more models than were
/// carried. Writes "" when no models. e.g. "opus-4-8 · sonnet-4-6 · haiku-4-5".
void costModelsLine(const CostRecord &r, char *buf, size_t bufSize);
```

- [ ] **Step 4: Implement the formatter**

In `CostFormat.cpp`, ensure `#include <cstdio>` and `#include <cstring>` are present at the top, then add at the end of the `namespace stopwatch { ... }` block:

```cpp
void costModelsLine(const CostRecord &r, char *buf, size_t bufSize) {
    if (bufSize == 0) return;
    buf[0] = '\0';
    int shown = 0;
    for (int i = 0; i < kCostMaxModelSlots; ++i) {
        if (r.models[i][0] == '\0') break;
        if (shown > 0) strlcat(buf, " \xC2\xB7 ", bufSize);  // " · "
        strlcat(buf, r.models[i], bufSize);
        ++shown;
    }
    int extra = (int)r.modelCount - shown;
    if (extra > 0) {
        char tail[8];
        snprintf(tail, sizeof(tail), " +%d", extra);
        strlcat(buf, tail, bufSize);
    }
}
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
cd /Users/justinyan/Documents/JProj/stopwatch-quiz/firmware
pio test -e native -f test_cost_format
```
Expected: PASS (formatDollars, humanizeTokens, costModelsLine).

- [ ] **Step 6: Commit**

```bash
git add firmware/src/CostFormat.h firmware/src/CostFormat.cpp \
        firmware/test/test_cost_format/test_main.cpp
git commit -m "firmware: add costModelsLine token-ordered models formatter

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Firmware — render provider name + models line

**Files:**
- Modify: `firmware/src/Views/Spend.cpp:11-19` (remove unused `labelFor`)
- Modify: `firmware/src/Views/Spend.cpp:243-256` (header + models line)

This view file is excluded from the native test build, so verification is a device-firmware compile (`pio run`) plus an on-device visual check.

- [ ] **Step 1: Remove the now-unused `labelFor` helper**

In `Spend.cpp`, delete the `labelFor` function (lines 12-19) inside the anonymous namespace. It was only used by the old model header; leaving it triggers an unused-function warning.

- [ ] **Step 2: Replace the header block and add the models line**

In `drawProviderCost`, replace the header block (lines 243-256, the `// Header: brand mark + top model.` braces) with:

```cpp
    // Header: brand mark + provider name. The hero below is the all-models today
    // total, so labelling it with one model misreads as that model's cost; the
    // models actually used today get their own line beneath.
    {
        c.setFont(theme::kFontTitle);
        const char *name = displayName(id);
        int tw = c.textWidth(name);
        int totalW = icons::kSize28 + 8 + tw;
        int leftX = theme::kCenterX - totalW / 2;
        c.drawBitmap(leftX, theme::kCenterY - 100 - icons::kSize28 / 2,
                     icons::bitmap28(id), icons::kSize28, icons::kSize28, color);
        c.setTextDatum(middle_left);
        c.setTextColor(theme::kTextMuted);
        c.drawString(name, leftX + icons::kSize28 + 8, theme::kCenterY - 100);
        c.setTextDatum(middle_center);
    }

    // Models used today, token-ordered (e.g. "opus-4-8 · sonnet-4-6 · haiku-4-5"),
    // "+N" if more were used than carried. Muted caption between name and hero.
    // y is provisional — confirm on-device it clears the name and the hero.
    if (r && r->modelCount > 0) {
        char modelsLine[64];
        costModelsLine(*r, modelsLine, sizeof(modelsLine));
        c.setFont(theme::kFontMicro);
        c.setTextColor(theme::kTextMuted);
        c.setTextDatum(middle_center);
        c.drawString(modelsLine, theme::kCenterX, theme::kCenterY - 70);
    }
```

(`Spend.cpp` already `#include "../CostFormat.h"` at line 3 and defines `displayName` in its anonymous namespace at line 112, so both symbols are in scope.)

- [ ] **Step 3: Compile the device firmware**

```bash
cd /Users/justinyan/Documents/JProj/stopwatch-quiz/firmware
pio run
```
Expected: build succeeds (env `stopwatch`), no unused-function or undefined-symbol errors.

- [ ] **Step 4: Commit**

```bash
git add firmware/src/Views/Spend.cpp
git commit -m "firmware: provider name + models-used line on cost screen

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: Full-suite verification

**Files:** none (verification only)

- [ ] **Step 1: Run both test suites and the device build**

```bash
cd /Users/justinyan/Documents/JProj/stopwatch-quiz/bridge && swift test
cd /Users/justinyan/Documents/JProj/stopwatch-quiz/firmware && pio test -e native && pio run
```
Expected: bridge all-green; firmware native all-green (`test_cost_codec`, `test_cost_format`, `test_state_machine`, others); `pio run` builds.

- [ ] **Step 2: On-device visual check (manual, after `make flash`)**

Confirm on the Claude cost screen: header shows "Claude" (icon + name, not a model); a muted line beneath shows the token-ordered models (e.g. `opus-4-8 · sonnet-4-6 · haiku-4-5`); the hero today number is unchanged and clears both the line above and the 30-day context below. Tune the `kCenterY - 70` y-offset if it crowds. Repeat on the Codex screen (single model `gpt-5.5`, no `·`).

---

## Self-Review

**Spec coverage:**
- Provider-name header → Task 5 Step 2. ✓
- Token-ordered models line + `+N` overflow → Task 4 (formatter) + Task 5 (render). ✓
- Wire v1→v2, `topModel[12]` → `modelCount(1)+models[3][12]`, record 60→85, max 182 → Task 1 (doc), Task 2 (Swift), Task 3 (C++). ✓
- Decode `totalTokens`; rank latest day by tokens; drop top-by-cost → Task 2 Steps 3-5. ✓
- Fixtures + both test suites moved together → Task 2 (json/hex/Swift tests), Task 3 (codec test), Task 4 (format test). ✓
- Out of scope (partial flag, per-model $) → not implemented. ✓

**Placeholder scan:** none — every step shows exact code/commands and expected output.

**Type consistency:** `models: [String]` (Swift) ↔ `modelCount` + `models[kCostMaxModelSlots][13]` (C++) used consistently; `latestDayModelsByTokens` defined in Task 2 Step 5 and called in Step 3 and tested in Step 12; `costModelsLine(const CostRecord&, char*, size_t)` defined Task 4 Step 4, declared Step 3, called Task 5 Step 2; offsets in the encoder test (Task 2 Step 9) match the decoder offsets (Task 3 Step 5) and the PROTOCOL.md table (Task 1).
