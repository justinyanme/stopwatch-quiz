# Cost provider screen: provider header + models-used line

**Date:** 2026-06-01
**Status:** Approved design, pending implementation
**Characteristic touched:** `CostSnapshot` (major bump v1 → v2)

## Problem

The per-provider cost screen (`drawProviderCost` in `firmware/src/Views/Spend.cpp`) pairs a
single **top-model name** in the header with a **today-total** hero number. The hero is
`sessionCostUSD` — the sum across *every* model the provider used today — but the lone model
label next to it reads as *"that model cost $X."*

- **Codex:** only gpt-5.5 is used, so "GPT5.5  $78.81" happens to read true.
- **Claude:** several models are used, so "sonnet-4-6  $6.83" reads as *sonnet cost $6.83* —
  but $6.83 is the all-models total, and sonnet was merely the highest-*cost* model. When a
  dominant model is unpriced upstream (e.g. brand-new `opus-4-8` during a codexbar pricing-lag
  window), the by-cost ranking even crowns the wrong model.

This surfaced as the reported bug: watch showed `sonnet-4-6 $6.83` while CodexBar showed
`$51.98` with `opus-4-8` as the top expense. (The understated *number* is a separate upstream
codexbar issue — see Out of Scope.)

## Decision

Stop attributing the total to one model. On the per-provider screen:

- **Header** = provider brand icon + **provider name** ("Claude" / "Codex" / "Gemini").
- **Beneath it**, a muted **models-used line**: the day's models, **ordered by tokens
  (descending)**, joined by `·`, e.g. `opus-4-8 · sonnet-4-6 · haiku`. Token order naturally
  surfaces the dominant model first — including an unpriced one — which is the signal that was
  missing. No dollar figure is attached to any model.
- The hero number is unchanged (today's all-models total) but now reads unambiguously as
  "today," since no model name sits beside it.

This makes the original "wrong model" mislabel **impossible by construction** — no model is
crowned, so none can be crowned wrong.

## Wire protocol change (`CostSnapshot`, major bump v1 → v2)

The per-record `topModel char[12]` field is replaced by a small model list. Because the record
stride changes, this is a **major** bump (`versionMajor` 1 → 2, `versionMinor` → 0). Per
PROTOCOL.md §3.3 a higher major is refused by older readers; acceptable here because firmware
and bridge are flashed/run together.

### New per-record (85 bytes, was 60)

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0 | uint8 | `providerID` | 1 = codex, 2 = claude. |
| 1 | uint8 | `reserved` | `0`. |
| 2 | uint32 | `todayCostCents` | `0xFFFFFFFF` = unknown. |
| 6 | uint32 | `monthCostCents` | `0xFFFFFFFF` = unknown. |
| 10 | uint32 | `todayTokens` | `0xFFFFFFFF` = unknown. |
| 14 | uint32 | `monthTokens` | `0xFFFFFFFF` = unknown. |
| 18 | uint8 | `modelCount` | Total distinct models on the latest dated day (for `+N` overflow). |
| 19 | char[12] | `models[0]` | Top model by tokens. UTF-8, null-padded, vendor-prefix-stripped. |
| 31 | char[12] | `models[1]` | 2nd by tokens (zeroed if absent). |
| 43 | char[12] | `models[2]` | 3rd by tokens (zeroed if absent). |
| 55 | uint8[30] | `history` | Oldest→newest; index 29 = `capturedAt` day; `round(dayCents / historyUnitCents)`. |

`kCostMaxModelSlots = 3`. `modelCount` may exceed 3; only the top 3 names are carried, and the
firmware renders `+N` for the remainder. Header (12 bytes) is unchanged. New max size =
`12 + 85 × 2 = 182`.

## Bridge changes (`bridge/Sources/StopwatchBridge/`)

- **`CodexbarClient.swift`**
  - `RawCost.Daily.ModelBreakdown`: decode `totalTokens` (present even when `cost` is null —
    confirmed against live `/cost`). Keep `modelName`, `cost`.
  - Replace `displayModel`/`topModel` (top-by-cost) with a **top-by-tokens on the latest dated
    day** selector that returns the ordered list of shortened names plus the total model count.
  - `decodeCost`: populate the provider's `models` (top 3, shortened, token-ordered) and
    `modelCount`.
- **`CostSnapshot.swift`**: replace `topModel: String?` on `NormalizedCost.Provider` with
  `models: [String]` (≤3, ordered) and `modelCount: Int`.
- **`CostEncoder.swift`**: bump `Protocol.costVersionMajor` usage; write `modelCount` + 3
  twelve-byte name slots (reuse `shortenModel`, null-pad) in place of `appendModel`. Record
  size constant → 85.
- **`Protocol.swift`**: `costVersionMajor = 2`, `costVersionMinor = 0`, `costRecordSize = 85`,
  add `costMaxModelSlots = 3`.

## Firmware changes (`firmware/src/`)

- **`Protocol.h`**: `kCostVersionMajor = 2`, `kCostRecordSize = 85`, add
  `kCostMaxModelSlots = 3`; refresh `kCostSnapshotMaxSize` (→182).
- **`CostCodec.h`**: `CostRecord` replaces `char topModel[13]` with `uint8_t modelCount` and
  `char models[3][13]`.
- **`CostCodec.cpp`**: decode `modelCount` (offset 18) and the three name slots (offsets 19/31/43,
  12 bytes each, null-terminate); history now at offset 55.
- **`Views/Spend.cpp` `drawProviderCost`**:
  - Header = `icons::bitmap28(id)` + `displayName(id)` (was `r->topModel`).
  - New muted models line below the header (small font, centered): join
    `models[0..min(modelCount,3))` with ` · `; include as many names as fit a fixed max width;
    if fewer shown than `modelCount`, append ` +N` (N = `modelCount − shown`). Empty when
    `modelCount == 0`.
  - Hero, 30-day context, and sparkline unchanged.
- `drawTotalSpend` (aggregate SPEND screen) is **unchanged** — it already shows the total with
  per-provider rows and never attributed to a model.

## Fixtures & tests

- **`shared/fixtures/codexbar-cost-two.json`**: add `totalTokens` to every `modelBreakdown`;
  give Claude's latest day **three** models with distinct token counts (so ordering + the line
  are exercised), keep Codex single-model (exercises `modelCount == 1`). Regenerate
  **`codexbar-cost-two.hex`** from the encoder.
- **`bridge/Tests/.../CostEncoderTests.swift`**: assert new record layout, `modelCount`, the
  three token-ordered slots, and the v2 version bytes against the `.hex`.
- **`bridge/Tests/.../CostClientTests.swift`**: assert `decodeCost` produces token-ordered
  `models` + correct `modelCount` (including the unpriced-dominant case: a model with tokens but
  no cost still ranks first).
- **`firmware/test/test_cost_codec/test_main.cpp`**: decode the shared `.hex`, assert
  `modelCount` and `models[]` and that `history` reads correctly at its new offset.
- Run both suites (`cd bridge && swift test`, `cd firmware && pio test -e native`) — shared
  fixtures keep the two sides locked.

## Out of scope

- **The understated number** ($6.83 vs $51.98). That is upstream: a stale `codexbar serve`
  process not pricing `opus-4-8`. It resolves when the serve daemon is restarted on the updated
  binary. We explicitly chose *not* to add per-record "cost incomplete" flagging in this change
  (YAGNI — the token-ordered models line already reveals the dominant model).
- **Multi-model breakdown with per-model dollars** anywhere on the watch.

## Rollout

Major `CostSnapshot` bump → flash firmware and run the matching bridge together. An old watch
reading a v2 bridge refuses the snapshot (`MajorVersionTooNew`) and shows its no-data state until
reflashed; a v2 watch reading an old bridge likewise. No partial-compatibility window is intended.
