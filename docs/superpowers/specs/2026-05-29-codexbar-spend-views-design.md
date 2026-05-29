# CodexBar StopWatch — Spend & Burn Views — Design

**Status:** Proposed
**Date:** 2026-05-29
**Owner:** Justin Yan
**Builds on:** `docs/superpowers/specs/2026-05-28-codexbar-stopwatch-design.md`

## 1. Summary

Surface CodexBar's **spend and burn** data on the watch alongside the existing usage rings. CodexBar exposes a rich `/cost` endpoint (dollars spent, token volume, top model, 30-day daily history) that the bridge does not currently call. This design adds:

- A **Total Spend & Burn** screen — combined today's spend, 30-day spend, token volume, and a combined 30-day burn sparkline.
- Per-provider **$ detail** screens (Codex, Claude) — that provider's today/30-day spend, top model, and its own burn sparkline.
- A compact **spend teaser line** (`today $X · NNNm`) on the existing Codex/Claude ring screens.

This deliberately reverses the v1 non-goal *"Cost / spend history charts."* The original ring/limits behavior is unchanged.

Spend data exists only for **Codex and Claude** (paid). **Gemini** is a free account and has no `/cost` record; it keeps its existing ring-only screen and is omitted from all spend surfaces.

## 2. Goals / non-goals

**Goals**

- Show spend/burn without slowing the existing **wake-to-rings ≤ 1 s** glance. The rings path (`UsageSnapshot`) is untouched and is not enlarged.
- Keep heavy/variable cost+history data off the hot wake path; fetch it only when a spend screen is viewed.
- No changes to CodexBar. Bridge consumes the existing `codexbar serve` `/cost` endpoint.
- Reuse the existing 2-button navigation with no new gestures.

**Non-goals (this iteration)**

- Per-model cost breakdown beyond a single "top model" label.
- Editable history range (fixed at 30 days, matching the API's `historyDays`).
- Spend data for Gemini or any future free provider.
- "% in reserve" — that figure is computed inside the CodexBar app and is **not** returned by `serve`; out of scope.
- Currency other than the `/cost` `currencyCode` (assumed USD; see §10).

## 3. Data source: `codexbar serve`

Observed live against `codexbar 0.133.0`, port from the bridge config.

### 3.1 `/cost` (new dependency)

Top-level JSON **array**, one object per provider that has local logs (Codex, Claude — **not** Gemini):

```jsonc
{
  "provider": "codex",
  "currencyCode": "USD",
  "historyDays": 30,
  "sessionCostUSD": 21.902213,        // → today's spend
  "sessionTokens": 34356111,          // → today's tokens ("latest")
  "last30DaysCostUSD": 415.024864,    // → 30-day spend
  "last30DaysTokens": 391120777,      // → 30-day tokens
  "totals": { "totalCost": 415.0, "totalTokens": 391120777, ... },
  "daily": [                          // SPARSE — only days with activity
    { "date": "2026-05-20", "totalCost": 258.92, "totalTokens": 224443252,
      "modelBreakdowns": [ { "modelName": "gpt-5.5", "cost": 258.92, "totalTokens": 224443252 } ],
      "modelsUsed": ["gpt-5.5"] },
    ...
  ]
}
```

Field mapping:

| UI value | Source |
|---|---|
| Today's $ (hero) | `sessionCostUSD` |
| Today's tokens (teaser/detail) | `sessionTokens` |
| 30-day $ | `last30DaysCostUSD` |
| 30-day tokens | `last30DaysTokens` |
| Top model | model with the highest summed `cost` across all `daily[].modelBreakdowns` |
| Burn sparkline (per day) | `daily[].totalCost`, expanded to a dense 30-element series (see §6.3) |

### 3.2 `/usage` (unchanged)

Still the source for the rings. Not modified by this design. (It also carries unused detail — `resetDescription`, `windowMinutes`, Claude `extraRateWindows`, Codex `codeReviewLimit`, exact `accountPlan` — explicitly deferred here.)

## 4. Navigation & view set

Flat carousel. **KeyB = next, KeyA = prev** (looping), exactly as today. **KeyA-long = refresh, KeyB-long = sleep** unchanged. No new gestures.

```
ViewId (cycle order):
  Overview  →  TotalSpend  →  Codex  →  CodexCost  →  Claude  →  ClaudeCost  →  Gemini  →  ↺
  (limits)     (spend)        (rings)   ($ detail)     (rings)    ($ detail)      (rings)
```

Two "glance" summaries sit up front (Overview = limits, TotalSpend = money). Each paid provider's `$` detail sits immediately after its ring screen, so the teaser line on the ring screen "drills in" with one press of KeyB. 7 stops; bidirectional, so any screen is ≤ 3 presses away.

`ViewId` enum (firmware) becomes:

```cpp
enum class ViewId : uint8_t {
  Overview = 0, TotalSpend = 1,
  Codex = 2, CodexCost = 3,
  Claude = 4, ClaudeCost = 5,
  Gemini = 6,
};
```

`nextView` / `prevView` updated to cycle in the order above.

## 5. View layouts (466 × 466 round)

### 5.1 Total Spend & Burn

```
   ╭───────────────────╮
  ╱    SPEND & BURN      ╲
 │       $39.85           │   today, combined (Font7 hero)
 │        today           │
 │   30d $745 · 820M      │   combined 30-day $ · tokens
 │  ▁▁▁▂█▁▁▃▂▅▁▂▇▂▁ 30d   │   combined burn sparkline (Σ providers)
 │  CX $21.90 · CL $17.95 │   per-provider split
  ╲      ● stale         ╱
   ╰───────────────────╯
```

All numbers are **derived on the watch** by summing the per-provider cost records (today $, 30d $, 30d tokens, and element-wise sum of the two history arrays). No aggregate is sent over the wire. If only one provider has a record, it shows that one and drops the split line.

### 5.2 Per-provider $ detail (Codex shown; Claude identical)

```
   ╭───────────────────╮
  ╱   CODEX   $          ╲
 │   ▟ gpt-5.5           │   brand mark + top model
 │       $21.90           │   today (Font7 hero, provider color)
 │        today           │
 │   30d    $415          │
 │   tok    391M          │
 │  ▁▂▁▃█▂▁▂▅▃▁▂▇▁ 30d    │   this provider's burn sparkline
  ╲      ● stale         ╱
   ╰───────────────────╯
```

If the provider has no cost record (fetch failed / `/cost` unavailable), the screen shows a muted `— waiting for Mac` with the relevant status pill instead of numbers.

### 5.3 Ring-screen teaser (added to existing `Provider.cpp`, Codex/Claude only)

The existing ring screen is unchanged except for one added line near the bottom strap:

```
 │   today $21.90 · 34M   │   ← new; omitted entirely for Gemini or when no cost record
```

### 5.4 Formatting rules

- **Cost:** today/hero → 2 decimals (`$21.90`); 30-day → whole dollars when ≥ $100 (`$415`), else 2 decimals. `0xFFFFFFFF` (unknown) → `—`.
- **Tokens:** humanized — `≥1M → "391M"`, `≥10k → "34k"`, else raw. `0xFFFFFFFF` → `—`.
- **Top model:** rendered as received (already shortened bridge-side, §6.4). Empty → no model line.
- **Sparkline:** bar heights from the normalized `uint8` history (0 = no bar). Combined chart sums the two providers' bytes per day (same scale, §6.3).

## 6. Wire protocol — `CostSnapshot` (new characteristic)

`UsageSnapshot` and `RefreshTrigger` are unchanged. We add **one** characteristic. `CostSnapshot` carries its own version bytes and versions independently of `UsageSnapshot`.

### 6.1 GATT

| Name | UUID | Properties | Direction |
|---|---|---|---|
| `CostSnapshot` | `33FAAC2D-3935-467F-A0A0-899CE2306366` | Read + Notify | bridge → watch |

Watch reads it **lazily** — only on entering a spend screen (TotalSpend / CodexCost / ClaudeCost) — and caches it. Bridge notifies on change while connected.

`RefreshTrigger` gains one additive value: `0x04 = cost (all providers)`. Existing `0x00–0x03` unchanged. A spend-screen force-refresh (KeyA-long) writes `0x04`.

### 6.2 Payload (binary, little-endian)

Total size = `12 + 60 × recordCount`. With Codex + Claude = **132 bytes**.

**Header (12 bytes)**

| Off | Type | Field | Meaning |
|---|---|---|---|
| 0 | uint8 | `versionMajor` | `0x01`. Structural; watch refuses a greater major. |
| 1 | uint8 | `versionMinor` | `0x00`. Additive (trailing fields / new enum values). |
| 2 | uint8 | `recordCount` | Number of cost records that follow (0–N; 2 today). |
| 3 | uint8 | `flags` | bit0 stale, bit1 bridge_error, bit2 cost_unavailable (`/cost` missing). |
| 4 | uint32 | `capturedAt` | Unix seconds. Anchors history day N-1 = this date. |
| 8 | uint8 | `historyDays` | `30`. Length of each `history` array. |
| 9 | uint8 | `reserved` | `0`. |
| 10 | uint16 | `historyUnitCents` | **Shared** scale: cents represented by one history unit (≥1). |

**Per cost record (60 bytes, repeated `recordCount` times)**

| Off | Type | Field | Meaning |
|---|---|---|---|
| 0 | uint8 | `providerID` | 1 = codex, 2 = claude. |
| 1 | uint8 | `reserved` | `0`. |
| 2 | uint32 | `todayCostCents` | `sessionCostUSD × 100`; `0xFFFFFFFF` = unknown. |
| 6 | uint32 | `monthCostCents` | `last30DaysCostUSD × 100`; `0xFFFFFFFF` = unknown. |
| 10 | uint32 | `todayTokens` | `sessionTokens`; `0xFFFFFFFF` = unknown. |
| 14 | uint32 | `monthTokens` | `last30DaysTokens`; `0xFFFFFFFF` = unknown. |
| 18 | char[12] | `topModel` | UTF-8, null-padded, bridge-shortened. All-zero = unknown. |
| 30 | uint8[30] | `history` | Oldest→newest; index 29 = `capturedAt` day. Each = `round(dayCents / historyUnitCents)`. `0` = no spend. |

### 6.3 History encoding (the shared-scale trick)

The API's `daily[]` is **sparse** (only days with activity) and per-provider magnitudes differ wildly. The bridge:

1. Builds a dense 30-slot array per provider, indexed by date offset from `capturedAt`'s day (idle days = 0 cents).
2. Computes `maxDayCents` = the largest single-day cents across **all** providers in the snapshot.
3. Sets `historyUnitCents = max(1, ceil(maxDayCents / 255))` and writes it **once** in the header.
4. Encodes each provider's day as `round(dayCents / historyUnitCents)` (clamped 0–255).

Because all records share one scale, the watch can sum `codex.history[i] + claude.history[i]` for the **combined** burn chart and the bars stay proportional. Per-provider detail charts use the same bytes. Sub-`historyUnitCents` days round toward 0 — acceptable for a burn sparkline dominated by spike days.

### 6.4 Top-model shortening (bridge-side)

Pick the model with the highest summed cost across `daily[].modelBreakdowns`. Then: strip a leading `claude-` / `openai-` / `google-` vendor prefix, trim whitespace, truncate to 11 chars + null. Examples: `gpt-5.5 → gpt-5.5`, `claude-opus-4-7 → opus-4-7`, `claude-sonnet-4-6 → sonnet-4-6`.

### 6.5 Versioning

`CostSnapshot` follows the same major/minor rules as `UsageSnapshot` (see `PROTOCOL.md §3.3`): per-record stride is fixed per major; new trailing fields or providers are minor; structural change is major. It versions **independently** of `UsageSnapshot`.

## 7. Bridge changes (Swift)

| File | Change |
|---|---|
| `Protocol.swift` | Add `costSnapshotUUID`, cost version constants, record/size constants, `0x04` trigger scope. |
| `CodexbarClient.swift` | Add `fetchCost(scope:) -> NormalizedCost` calling `/cost`; decode the array shape in §3.1. New `NormalizedCost` model (per-provider today/30d cost+tokens, top model, dense 30-day cents history). |
| `CostEncoder.swift` (new) | `NormalizedCost → Data` per §6: dense-fill history, compute shared `historyUnitCents`, shorten model, emit fixed-size records. |
| `GATTPeripheral.swift` | Add the `CostSnapshot` characteristic (Read + Notify); serve latest cost bytes; init value = empty header with `cost_unavailable`+`stale`. |
| `BridgeService.swift` | On refresh: fetch `/usage` → update `UsageSnapshot` + notify (fast ring path), **then** fetch `/cost` → update `CostSnapshot` + notify (background; rings never wait). Handle `0x04` scope = cost-only. |
| `Config.swift` | Add `enableCost: Bool` (default `true`) to allow disabling cost fetching. |

`CodexbarSupervisor` unaffected (same `codexbar serve` child already serves `/cost`).

## 8. Firmware changes (C++)

| File | Change |
|---|---|
| `Protocol.h` | Add `kCostSnapshotUUID`, cost version/size constants. |
| `CostCodec.{h,cpp}` (new) | `decodeCostSnapshot(bytes,len,out)` → `CostSnapshot { header; CostRecord records[2] }`. Mirrors `SnapshotCodec`. |
| `Views/Spend.cpp` (new) | `drawTotalSpend(...)` (aggregate, sums records) and `drawProviderCost(...)` (one provider). Plus a `drawSpendTeaser(...)` helper. |
| `Views/Provider.cpp` | Call `drawSpendTeaser` for Codex/Claude when a cost record exists. |
| `App.{h,cpp}` | Expand `ViewId` (§4); update `nextView`/`prevView`; track whether the cost snapshot has been fetched this wake-session; expose `wantsCostFetch()` set when entering a spend view without cached cost. |
| `BleClient.{h,cpp}` | Add `readCostSnapshot()` (second characteristic read on the existing connection). |
| `SnapshotStore.{h,cpp}` | Add a second NVS slot caching the raw cost bytes, so spend screens render last-known on cold boot. |
| `main.cpp` | On entering a spend view: if the cost snapshot hasn't been read yet this wake-session, `readCostSnapshot()` once and cache it — the bridge already refreshed `/cost` from the wake trigger, so no new fetch is needed. KeyA-long on a spend view writes `RefreshTrigger 0x04` then re-reads. Render from cache meanwhile. |

**Lazy-read flow:** wake still reads `UsageSnapshot` only → rings paint at the same speed. The first time the user reaches a spend screen, the watch reads `CostSnapshot` once and caches it for the rest of the session; subsequent spend screens are instant.

## 9. Error handling

| Condition | Bridge | Watch |
|---|---|---|
| `/cost` endpoint 404 (older codexbar) | `cost_unavailable` + `stale`, `recordCount = 0` | Spend screens show `— no cost data`; teaser hidden |
| `/cost` slow / times out | keep prior cost bytes, set `stale`, bump `capturedAt` | `● stale` pill on spend screens |
| Provider absent from `/cost` (e.g. Gemini) | record simply omitted | No `$` screen for it (not in cycle); no teaser |
| Cost not yet fetched on this wake | n/a | Cached (NVS) values + `● stale`, or `— waiting for Mac` if no cache |
| `CostSnapshot.versionMajor` too new | n/a | Spend screens show `update firmware`; rings unaffected |
| Unknown numeric field (`0xFFFFFFFF`) | written when source null | rendered as `—` |

The rings path keeps its existing pills (`no bridge`, `link error`, `stale`, `no source`) unchanged.

## 10. Open questions / risks

- **`/cost` availability across CodexBar versions.** Verified on 0.133.0. Mitigated by the `cost_unavailable` flag + graceful empty screens.
- **`/cost` latency.** It reads local logs and can be slower than `/usage`. Mitigated by fetching it *after* `/usage` and serving it lazily; rings never block on it.
- **`uint32` token ceiling** = 4.29 B. Current max ~428 M (30-day). Safe with wide margin; revisit only if a provider reports > 4 B tokens in a window.
- **Currency.** `currencyCode` is assumed `USD` and the `$` glyph is hard-coded. If a non-USD code appears, we still render the number with `$` (cosmetic only); a follow-up could carry a currency byte.
- **Top-model truncation.** Names > 11 chars after prefix-strip are truncated. Acceptable for a label; flagged for review.
- **"Today" boundary.** `sessionCostUSD`/`sessionTokens` are CodexBar's notion of the current day; aggregation across providers assumes both share that boundary (they do — same local clock).

## 11. Testing

- **Bridge `CostEncoderTests`** — fixture `/cost` JSON → expected hex for: two providers, single provider, sparse history (gaps zero-filled), shared-scale rounding, unknown/null fields, model shortening.
- **Bridge `CodexbarClient` cost decode** — `URLProtocol` stub returns canned `/cost`; assert `NormalizedCost` and the 404/timeout paths.
- **Firmware `test_cost_codec`** (native) — decode the same shared hex fixtures into `CostSnapshot`; mirrors the bridge so wire compat is diff-checkable.
- **Firmware `test_state_machine`** — extend for the 7-view cycle and the lazy-cost-fetch trigger on entering spend views.
- **Shared fixtures** — add `shared/fixtures/codexbar-cost-*.json` + matching `.hex` (loaded by both sides, per `PROTOCOL.md §4`).
- **Manual** — `make flash` + glance: rings still paint < 1 s on wake; spend screens populate on first visit.

## 12. Implementation order (preview, not the plan)

1. `shared/PROTOCOL.md`: add `CostSnapshot` schema, UUID, `0x04` trigger scope.
2. Bridge: `NormalizedCost` + `CostEncoder` + fixtures + `CostEncoderTests` (no BLE yet).
3. Bridge: `CodexbarClient.fetchCost` + `URLProtocol` test.
4. Bridge: `GATTPeripheral` cost characteristic + `BridgeService` wiring + `0x04`.
5. Firmware: `CostCodec` + `test_cost_codec` (mirrored fixtures).
6. Firmware: `Views/Spend.cpp` (Total + provider $) + teaser in `Provider.cpp`, hard-coded snapshot.
7. Firmware: `App` ViewId expansion + nav + lazy-fetch flag; `test_state_machine`.
8. Firmware: `BleClient.readCostSnapshot` + NVS cost cache + `main.cpp` wiring.
9. Manual flash + glance validation.

The detailed step-by-step plan with review checkpoints will be written by the `writing-plans` skill next.
