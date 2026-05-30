# CodexBar StopWatch — Wire Protocol v1.0

Authoritative source for the BLE GATT service and binary payload shared between `bridge/` and `firmware/`. Any change here MUST land alongside matching updates to `bridge/Sources/StopwatchBridge/Protocol.swift` and `firmware/src/Protocol.h`.

## 1. GATT Service

| Item | Value |
|---|---|
| Service UUID | `91412041-D927-4633-A0ED-B066DF91EE55` |
| Local name | `Stopwatch Bridge` |
| Advertising interval | default (~100 ms) |

## 2. Characteristics

| Name | UUID | Properties | Direction | Notes |
|---|---|---|---|---|
| `UsageSnapshot` | `621645B4-14D2-4E58-975B-73B81D43916D` | Read + Notify | bridge → watch | Latest binary snapshot. Watch reads on wake; bridge notifies on snapshot change while watch is connected. |
| `RefreshTrigger` | `6817329E-A603-4A34-BB4D-04215218304C` | Write Without Response | watch → bridge | Watch writes 1 byte (provider scope) to ask for a fresh fetch. |
| `CostSnapshot` | `33FAAC2D-3935-467F-A0A0-899CE2306366` | Read + Notify | bridge → watch | Spend/burn data. Watch reads lazily on entering a spend screen; bridge notifies on change. Versioned independently of `UsageSnapshot`. |
| `BalanceSnapshot` | `4D9E8F21-7C3A-4B6D-8E15-9A2F6C3B0D74` | Read + Notify | bridge → watch | API credit balances. Watch reads lazily on entering the Balances view; bridge notifies on change. Versioned independently. Read via ATT read-blob (may exceed one MTU). |

### 2.1 `RefreshTrigger` payload

Single byte:

| Value | Meaning |
|---|---|
| `0x00` | All three providers |
| `0x01` | Codex only |
| `0x02` | Claude Code only |
| `0x03` | Gemini only |
| `0x04` | Cost only (re-fetch `/cost` for all providers) |
| `0x05` | Balances only (poll all configured API-balance providers) |

Any other value → bridge logs warning and treats as `0x00`.

## 3. `UsageSnapshot` payload (binary)

All integers little-endian. Total size for v1.0 with 3 providers = 8 + 3×16 = **56 bytes**.

### 3.1 Header (8 bytes)

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0 | uint8 | `versionMajor` | `0x01` for v1.x. Structural; bumping breaks the watch. |
| 1 | uint8 | `versionMinor` | `0x00` for v1.0. Additive within a fixed major (new flag bits, enum values, or providers only — see §3.3). |
| 2 | uint8 | `providerCount` | `0x03` in v1.0 (Codex, Claude, Gemini); may increase under a minor bump per §3.3 (additional providers). |
| 3 | uint8 | `flags` | bit0 = stale, bit1 = bridge_error, bit2 = provider_missing, bits 3-7 reserved (bridge MUST write 0; watch MUST ignore). |
| 4 | uint32 | `capturedAt` | Unix seconds when bridge captured this snapshot. |

### 3.2 Per-provider record (16 bytes each, repeated `providerCount` times)

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0 | uint8 | `providerID` | 1 = codex, 2 = claude, 3 = gemini |
| 1 | uint8 | `status` | 0 = ok, 1 = warn, 2 = critical, 3 = error, 4 = disabled. Unknown values: treat as `ok` (see §3.3). |
| 2 | uint8 | `sessionPct` | 0–100; `0xFF` = unknown |
| 3 | uint8 | `weekPct` | 0–100; `0xFF` = unknown |
| 4 | uint32 | `sessionResetAt` | Unix seconds; `0` = unknown |
| 8 | uint32 | `weekResetAt` | Unix seconds; `0` = unknown |
| 12 | uint16 | `credits` | CodexBar credit balance × 10 (so 112.4 → 1124; one decimal of precision); `0xFFFF` = unknown. A value of `0x0000` means zero credits, not unknown. |
| 14 | uint8 | `plan` | 0 = unknown, 1 = free, 2 = plus, 3 = pro, 4 = team, 5 = enterprise. Unknown values: treat as `unknown` (see §3.3). |
| 15 | uint8 | `reserved` | bridge MUST write `0x00`; watch MUST ignore. |

### 3.3 Versioning rules

- Bridge always sends the highest `(versionMajor, versionMinor)` it knows.

- **`versionMajor`** governs structural compatibility:
  - Any change to the header layout or to the per-provider record's byte stride (adding, removing, or reordering per-provider fields) requires a major bump.
  - Watch refuses to decode any `versionMajor` greater than its own; renders an "update firmware" text screen instead.
  - Watch refuses to decode any `versionMajor` less than its own only if it no longer carries the schema for that older major; otherwise it decodes using that older major's known layout. (For v1 there is only one major, so this case won't arise yet.)

- **`versionMinor`** is additive within a fixed major and may change ONLY:
  - New flag bits in the `flags` byte (older watches read them as 0 per the reserved-bits rule).
  - New values in the `status` or `plan` enums (older watches treat unknown values as `status=ok` / `plan=unknown`).
  - Additional providers (signalled by `providerCount` exceeding what the watch knows; the watch decodes only the providers whose `providerID` it recognizes).

- **The per-provider record's stride is fixed per `versionMajor`.** Adding or reordering per-provider fields therefore requires a major bump, never a minor bump.

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
| 2 | uint8 | `recordFlags` | bit0 = low_balance; other bits reserved (bridge writes 0, watch ignores). |
| 3 | char[3] | `currencyCode` | ASCII e.g. `USD`,`CNY`. All-zero = unknown. |
| 6 | uint8 | `currencyDecimals` | Minor-unit exponent (2 for USD/CNY). |
| 7 | uint8 | `reserved` | `0`. |
| 8 | uint32 | `balanceMinor` | remaining × 10^decimals. `0xFFFFFFFF`=unknown, `0xFFFFFFFE`=unlimited. |
| 12 | uint32 | `usageMinor` | spent × 10^decimals, or `0xFFFFFFFF`. |
| 16 | uint32 | `updatedAt` | Unix seconds of this provider's last OK poll; `0`=never. |
| 20 | char[16] | `name` | UTF-8, null-padded display label. |

Versioning follows the same major/minor rules as §3.3.

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

## 4. Test fixtures

`shared/fixtures/*.json` holds canned `codexbar serve` responses. `shared/fixtures/*.hex` holds the expected output of `SnapshotEncoder.encode(json) → Data` for each input.

Both `bridge/Tests/StopwatchBridgeTests/SnapshotEncoderTests` and `firmware/test/test_snapshot_codec` load these files. Wire compatibility is diff-checkable: if a code change to either side breaks a fixture, the test fails on both sides.
