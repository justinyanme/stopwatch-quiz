# Per-provider API usage & spend — design

**Date:** 2026-05-30
**Status:** Approved design, pre-implementation
**Scope:** Add usage/spend detail screens with charts to the API Balances view, drilled into per provider.

## 1. Summary

The API Balances list becomes an overview you can drill into. Tapping a balance row opens a per-provider **detail screen**: balance, 30-day spend/tokens/requests, and a 30-day chart in the provider's color, reusing the SPEND & BURN sparkline and entrance animation. Usage data is fetched lazily on first drill-in and cached, on a separate BLE payload from the fast balance poll so the two never block each other.

## 2. The core constraint: data availability

The three usage-capable providers expose very different surfaces. This unevenness drives the whole design; the feature degrades gracefully per provider rather than pretending parity.

| Provider | Balance (today) | Usage time-series | Auth needed | Confidence |
|---|---|---|---|---|
| **OpenRouter** | `GET /api/v1/credits` | `GET /api/v1/activity` — daily × model: cost, requests, prompt/completion/reasoning tokens, 30-day window | **management key** (not the inference key) | Documented, reliable |
| **AIHubMix** | `GET /api/user/self` | `GET /api/data/self?start_timestamp&end_timestamp` (per-hour × model: cost=quota/500000, count, token_used), ≤1-month span — undocumented `new-api` endpoint | **system access token** (`fd***`, not the model key) | Undocumented; verify in DevTools first |
| **DeepSeek** | `GET /user/balance` | none. Dashboard chart at platform.deepseek.com/usage is browser-only; the only official export is a manual monthly CSV download | session **cookie** replay of the dashboard's internal XHR | Fragile; verify in DevTools; cookie expires |
| Others (Groq, generic) | configured | none | n/a | balance-only |

Confirmed against provider docs on 2026-05-30: OpenRouter analytics docs; DeepSeek API reference lists only Get User Balance under "Others"; AIHubMix runs the open-source `new-api` stack (QuantumNous/new-api) whose `/api/data/self` and `/api/log/self/stat` endpoints serve the dashboard charts.

**DevTools spike (precedes Phase 2/3):** open each dashboard logged-in with the Network tab, identify the exact JSON request that populates the usage chart (URL, method, headers, auth, CSRF), confirm it is replayable with a captured cookie/token. If DeepSeek guards it with a short-lived CSRF token that can't be replayed headlessly, DeepSeek stays balance-only.

## 3. Interaction model

- **Tap** a balance row (a clean tap: press + release with no drag — distinct from the existing scroll drag) → enter that provider's detail screen.
- **Button A** (currently prev-view): inside a detail → **back to the list**. On the list → unchanged (cycles the carousel).
- **Button B** (currently next-view): inside a detail → **toggle the chart** between *daily cost* (default) and *daily tokens*. On the list → unchanged (cycles the carousel).
- The carousel is otherwise untouched: there is still exactly one Balances view in the A/B cycle. The detail is a sub-state of it, not a new carousel stop.
- Every row is tappable. Usage-capable providers show the full chart screen; all others show a clean balance-only detail.
- Chart: 30 daily bars, rising from the baseline left→right on entry via the existing `motion::barRise` (see [[ring-animation]] / `Anim.h`). Hero number counts up via `motion::countUp`.

## 4. Architecture

Mirror the existing **Cost** pipeline, which already solves this exact shape (per-provider records + `history[30]` + a sparkline renderer).

### 4.1 New `BalanceUsage` snapshot (separate, lazy)

A dedicated payload on its own BLE characteristic, carrying **only usage-capable providers**:

- Per record: provider identity, `currency`, `cost[kUsageHistoryDays]`, `tokens[kUsageHistoryDays]`, today's + 30-day totals for cost / tokens / requests, plus a per-array unit scale (mirroring `historyUnitCents`).
- `kUsageHistoryDays = 30` to match cost.
- Lazy: fetched on first drill-in this wake-session (like `ensureCostLoaded` / `ensureBalanceLoaded`), then cached in NVS and reused. Balance polling stays lean and unchanged.
- Versioned independently (`kUsageVersionMajor = 1`); firmware rejects a newer major and falls back to balance-only.

This is **additive**: the existing 584-byte Balance snapshot (`kBalanceSnapshotMaxSize`, v1, 8-byte header + 36×16) is not touched, so no balance-side version bump and no risk to the fast path.

### 4.2 Bridge (Swift)

- A `UsageClient` with one fetch method per provider behind a shared interface returning a normalized `[ProviderUsage]` (daily cost + tokens + request series, currency, totals):
  - `OpenRouterUsage` — management-key API (`/credits` + `/activity`).
  - `AIHubMixUsage` — access-token scrape (`/api/data/self`).
  - `DeepSeekUsage` — cookie replay of the dashboard XHR.
- A `BalanceUsageEncoder` packs the normalized data into the new wire payload (shared unit-scaling like `CostEncoder`).
- Cache/merge layer keeps last-good usage on transient failures (like `BalanceCache`), so a stale cookie shows cached data + a marker, not an empty screen.
- Credentials (OpenRouter management key, AIHubMix access token, DeepSeek cookie) live in the **existing KeyStore** alongside current API keys.

### 4.3 Firmware (C++)

- `BalanceUsageCodec.h/.cpp` — struct + decoder, modeled on `CostCodec`.
- `Protocol.h` — new constants (`kUsageVersionMajor`, `kUsageHistoryDays`, `kUsageMaxRecords`, sizes) and a new characteristic UUID + trigger scope byte (next free after Balances=0x05).
- `BleClient::fetchBalanceUsage(...)` — same trigger-then-read mechanism as `fetchCost` / `fetchBalances`.
- New detail view `drawProviderUsage(renderer, usage, kind, link, anim)` reusing `drawSparkline` and the entrance animation. Renders cost or tokens per the toggle.
- `App` gains detail sub-state: which provider is selected (or none), and the current chart metric (cost/tokens). Tap enters, A exits, B toggles. `main.cpp` loop maps touch-tap → enter and the two buttons → back/toggle while in detail.
- The detail screen owns its own `Entrance` so the chart animates on open.

## 5. Key states (per detail screen)

- **Full** — balance + 30-day chart + cost/token/request totals. (usage-capable, data present)
- **Balance-only** — balance + "usage data unavailable", no chart. (non-usage provider, or first-time before any fetch)
- **Usage-stale** — cached chart shown with a stale marker (cookie/token expired but last-good exists).
- **Loading** — first fetch in progress; brief overlay like `renderRefreshingOverlay`.
- **Error / fallback** — scrape or cookie broke with no cache → degrade to balance-only, never a fake/zero chart (mirrors the cost view's em-dash honesty convention).

## 6. Build order

1. **OpenRouter, end-to-end.** Full vertical slice: bridge `OpenRouterUsage` client → `BalanceUsageEncoder` → new wire payload + characteristic → firmware codec → detail view + chart → tap/back/toggle nav. Proves the entire pipeline on the clean, documented API.
2. **DevTools spike**, then **AIHubMix** `UsageClient` behind the same interface.
3. **DeepSeek** cookie client, with graceful balance-only fallback.
4. **Balance-only detail** polish for all remaining providers.

## 7. Out of scope (YAGNI)

- No new carousel views (rejected approach C); detail is a sub-state of Balances.
- No charting of requests (available as a text total only; cost/tokens cover the chart toggle).
- No headless browser / Puppeteer (rejected in favor of cookie replay).
- No DeepSeek self-metering of the device's own calls (the device isn't the caller).
- No backfill of >30 days history.

## 8. Open questions

None blocking. The AIHubMix/DeepSeek endpoint paths and auth are confirmed-by-spike in Phase 2/3; OpenRouter (Phase 1) is fully specified and unblocked.
