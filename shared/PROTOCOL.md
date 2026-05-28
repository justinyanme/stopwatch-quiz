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
