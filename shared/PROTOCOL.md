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

## 4. Test fixtures

`shared/fixtures/*.json` holds canned `codexbar serve` responses. `shared/fixtures/*.hex` holds the expected output of `SnapshotEncoder.encode(json) → Data` for each input.

Both `bridge/Tests/StopwatchBridgeTests/SnapshotEncoderTests` and `firmware/test/test_snapshot_codec` load these files. Wire compatibility is diff-checkable: if a code change to either side breaks a fixture, the test fails on both sides.
