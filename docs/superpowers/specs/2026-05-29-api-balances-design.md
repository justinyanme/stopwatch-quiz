# CodexBar StopWatch — API Balances — Design

**Status:** Draft (awaiting review)
**Date:** 2026-05-29
**Owner:** Justin Yan
**Builds on:** `docs/superpowers/specs/2026-05-28-codexbar-stopwatch-design.md`, `docs/superpowers/specs/2026-05-29-codexbar-spend-views-design.md`

## 1. Summary

Add a third data type to the watch: **prepaid API credit balances** from pay-as-you-go provider consoles (OpenRouter, DeepSeek, and any similar Bearer-key API such as Together, Fireworks, Groq, SiliconFlow, Moonshot, Zhipu). This is distinct from the existing two types — usage rings and spend/burn — both of which come from the local `codexbar` CLI. Balances instead come from **remote, authenticated HTTPS calls** the bridge makes directly to each provider.

This design adds:

- A single **scrollable "API Balances" wallet screen** — one row per provider (name + remaining balance in its native currency), touch-scrollable, with a low-balance highlight.
- A **config-driven provider model** with built-in branding (brand-colored dot) for known providers and a generic initials-chip fallback for the rest, so new providers are added by editing config — no firmware change.
- **First-class touch input** on the watch (the StopWatch's CST820 panel), used to scroll the wallet list.
- Bridge-side **API key storage in the macOS Keychain** and a **periodic background poll** that keeps balances cached so the glance never waits on a network round-trip.

The rings hot path (`UsageSnapshot`) and the spend path (`CostSnapshot`) are untouched. Wake-to-rings ≤ 1 s is preserved.

## 2. Goals / non-goals

**Goals**

- Show all balances at a glance, in each provider's native currency, exactly as the provider's own console shows them — no FX conversion, nothing that can drift.
- Keep remote latency off the glance: the bridge polls on a timer and caches; the watch reads the cache instantly and lazily (only on entering the Balances view).
- Make adding a provider a config-only operation (no recompile/reflash) for any provider with a Bearer-key balance endpoint.
- Keep API keys encrypted at rest (Keychain), never on the watch and never in a plaintext config file.
- Reuse the existing 2-button navigation between views; add touch **only** for scrolling within the wallet list.

**Non-goals (this iteration)**

- **MiniMax** and any provider whose balance requires a browser cookie/session rather than an API key. (MiniMax's `coding_plan/remains` needs a session cookie; pay-as-you-go balance is dashboard-only. Deferred — see §11.)
- FX conversion or a single cross-currency grand total. Summaries, if any, group per currency.
- Per-provider drill-down detail screens, balance history, or burn sparklines (balance is a single number).
- Manual / static (no-API) provider entries. (Considered; deferred with MiniMax.)
- Editing keys or provider config from the watch.

## 3. Data sources (remote, per provider)

Each provider is a direct authenticated GET. Verified shapes:

### 3.1 OpenRouter
- `GET https://openrouter.ai/api/v1/credits`, header `Authorization: Bearer <key>`.
- Response: `{ "data": { "total_credits": <number>, "total_usage": <number> } }`. **Remaining = `total_credits − total_usage`**. Currency USD.
- Caveat: `/credits` is documented as requiring a **management** key. `GET /api/v1/key` → `{ "data": { "limit_remaining": <number|null>, "usage": <number>, … } }` works with a **normal inference key** and yields `limit_remaining` directly. The OpenRouter adapter tries `/credits` and falls back to `/key` (or the config pins one); `limit_remaining: null` (unlimited) → the unlimited sentinel on the wire (renders `∞`; see §7.2).

### 3.2 DeepSeek
- `GET https://api.deepseek.com/user/balance`, header `Authorization: Bearer <key>`.
- Response: `{ "is_available": bool, "balance_infos": [ { "currency": "CNY"|"USD", "total_balance": "110.00", "granted_balance": "10.00", "topped_up_balance": "100.00" } ] }`. **Remaining = `balance_infos[i].total_balance`** (a **string** → parse to number). Currency from the same object.
- May return **multiple** currency entries. v1 takes the first (or the entry matching a configured `currency`, if set); multi-row emission is a documented refinement (§11). `is_available: false` → `depleted` status. HTTP 402 (insufficient balance) likewise → `depleted`.

### 3.3 Generic (any other Bearer-key provider)
- `GET <endpoint>`, header `Authorization: Bearer <key>` (or `auth: "none"` for keyless).
- `balancePath` is a dotted/indexed JSON selector (e.g. `data.total_credits`, `balance_infos[0].total_balance`, `data.balance`) yielding a number or numeric string. Optional `usagePath`: when present, **remaining = balance − usage**; when absent, **remaining = balance**.
- `currency` is a literal (`"USD"`) or a selector (`path:balance_infos[0].currency`) so providers that report currency in-body work without hardcoding.

## 4. Credentials & config (bridge / macOS)

### 4.1 `providers.json` (new)
- Path: `~/Library/Application Support/stopwatch-bridge/providers.json`, written `0600` with the same umask + atomic-write + chmod belt-and-suspenders as `Config.save`. **Separate file from `config.json`** so it cannot break existing-config decode (the spend-views design avoided touching `Config` for the same reason).
- Shape: a top-level array of provider entries. `kind` supplies defaults (endpoint, auth, paths, currency) so known providers are terse; any field can be overridden.

```jsonc
[
  { "id": "openrouter", "name": "OpenRouter", "kind": "openrouter", "lowThreshold": 5.0 },
  { "id": "deepseek",   "name": "DeepSeek",   "kind": "deepseek" },
  { "id": "myllm",      "name": "MyLLM",      "kind": "generic",
    "endpoint": "https://api.myllm.com/v1/billing",
    "auth": "bearer", "balancePath": "data.balance", "currency": "USD",
    "currencyDecimals": 2, "pollSeconds": 600, "lowThreshold": 10.0 }
]
```

Fields: `id` (Keychain account + stable key), `name` (≤15 chars rendered; the wire label), `kind` (branding + adapter defaults), `endpoint`, `auth` (`bearer` | `none`), `balancePath`, `usagePath?`, `currency` (literal or `path:…`), `currencyDecimals?` (default 2), `pollSeconds?` (default 900), `lowThreshold?` (native-currency number; omitted → no highlight).

### 4.2 Keychain (keys only)
- A `KeychainStore` (Security framework, already linked in `Config.swift`) stores one generic-password item per provider: `service = "dev.stopwatch.bridge"`, `account = <id>`, value = the API key. `kSecAttrAccessibleAfterFirstUnlock` so the launchd daemon can read it unattended after the first login.
- CLI additions (dispatched in `Bridge.swift`'s `switch`): `set-key <id>` (reads the secret from stdin, never argv, to keep it out of shell history/ps), `list-keys` (ids only, never values), `delete-key <id>`.
- Because the `set-key` writer and the `run` daemon are the **same binary**, the item's default ACL admits the daemon without a prompt. Caveat: Keychain ACLs key on the code identity/path; if the release binary is rebuilt to a different path or re-signed, re-run `set-key`. Documented in README + §11.

## 5. Navigation & view set

Flat carousel, KeyB = next / KeyA = prev (looping), KeyA-long = refresh, KeyB-long = sleep — all unchanged. **Balances is appended last:**

```
Overview → TotalSpend → Codex → CodexCost → Claude → ClaudeCost → Gemini → Balances → ↺
```

`ViewId` (firmware) gains `Balances = 7`; `nextView`/`prevView` extend the cycle (Gemini → Balances → Overview). A new `isBalanceView(v)` helper mirrors `isSpendView`.

**Touch** is active only on the Balances view and only scrolls the list. Buttons still switch views everywhere. A touch counts as activity (resets the 5-min idle timer). KeyA-long on Balances writes `RefreshTrigger 0x05` (force an immediate poll) then re-reads.

## 6. View layout (466 × 466 round)

```
   ╭───────────────────╮
  ╱     API · BALANCES    ╲
 │  ● OpenRouter   $42.10  │   brand dot + name + native balance
 │  ● DeepSeek     ¥318.50 │
 │  ● Groq          $2.80  │   ← amber when < lowThreshold
 │  ● Together     $15.30  │
 │  ▣ SiliconFlow  ¥96.00  │   ▣ = grey initials chip (no built-in color)
 │  ▣ Moonshot     ¥40.00 ⋮│   right-edge scroll indicator
  ╲     ● updated 4m       ╱   freshness pill (from capturedAt)
   ╰───────────────────╯
```

- **Row:** a 11 px **dot** in the provider's brand color for known `kind`s, else an 18 px grey **chip** with 2 uppercase initials of `name`. Name left (Font4 / `kFontTitle`); remaining balance right (Font4, brand color, tabular). A bottom fade indicates more rows below the fold.
- **Currency formatting:** integer-minor → decimal via `currencyDecimals`. The symbol (`$`, `¥`, `€`, `£`) is chosen from `currencyCode`. As with the existing `$` workaround (Font7 has no currency glyph; Font4's `0x24` is `£`), all currency marks are drawn from `kFontDollar` (Font2). **¥ is a known glyph risk** (§11): if Font2 lacks `0x00A5`, the watch falls back to rendering the 3-letter code (`CNY`) instead of a symbol. `0xFFFFFFFF` balance → `—` (unknown); `0xFFFFFFFE` → `∞` (unlimited).
- **Per-row status:** `status != ok` dims the row and appends a marker (`auth`, `stale`, `offline`, `empty`); the value still shows last-known when available.
- **Scroll:** content taller than the visible band scrolls 1:1 with a vertical drag, with light kinetic momentum that decays in `loop()`. Offset clamps to `[0, contentHeight − viewHeight]`. A thin right-arc indicator shows position. Scroll offset is per-view state, reset when leaving Balances.
- **Empty/zero providers:** `recordCount == 0` → muted `no providers configured` + the relevant pill.

## 7. Wire protocol — `BalanceSnapshot` (new characteristic)

`UsageSnapshot`, `CostSnapshot`, `RefreshTrigger` are unchanged. We add **one** characteristic, versioned independently. All integers little-endian.

### 7.1 GATT

| Name | UUID | Properties | Direction |
|---|---|---|---|
| `BalanceSnapshot` | `4D9E8F21-7C3A-4B6D-8E15-9A2F6C3B0D74` | Read + Notify | bridge → watch |

Watch reads it **lazily** — only on entering the Balances view — and caches it. Bridge notifies on change while connected. Because reads use ATT offset/read-blob (the existing `handleRead` already serves `subdata(offset..<count)`), the payload may exceed one MTU and is read in chunks; notifications that exceed MTU-3 are truncated, so the watch always does a full **read** on entering the view rather than relying on the notify body.

`RefreshTrigger` gains `0x05 = balances` (poll all providers now). Existing `0x00–0x04` unchanged.

### 7.2 Payload

Total size = `8 + 36 × recordCount`.

**Header (8 bytes)**

| Off | Type | Field | Meaning |
|---|---|---|---|
| 0 | uint8 | `versionMajor` | `0x01`. Watch refuses a greater major. |
| 1 | uint8 | `versionMinor` | `0x00`. Additive (new `kind`/`status` enum values, new flag bits). |
| 2 | uint8 | `recordCount` | 0–16. |
| 3 | uint8 | `flags` | bit0 stale, bit1 bridge_error. |
| 4 | uint32 | `capturedAt` | Unix seconds the snapshot was assembled (drives the freshness pill). |

**Per record (36 bytes, repeated `recordCount` times)**

| Off | Type | Field | Meaning |
|---|---|---|---|
| 0 | uint8 | `kind` | 0 generic, 1 openrouter, 2 deepseek, 3 groq, 4 together, 5 fireworks, 6 siliconflow, 7 moonshot, 8 zhipu (extensible by minor bump). Unknown → render as generic. |
| 1 | uint8 | `status` | 0 ok, 1 stale, 2 auth_error, 3 unreachable, 4 depleted. Unknown → ok. |
| 2 | uint8 | `recordFlags` | bit0 = low_balance (balance < configured `lowThreshold`). |
| 3 | char[3] | `currencyCode` | ASCII, e.g. `USD`, `CNY`. All-zero = unknown. |
| 6 | uint8 | `currencyDecimals` | Minor-unit exponent (2 for USD/CNY, 0 for JPY). |
| 7 | uint8 | `reserved` | `0`. |
| 8 | uint32 | `balanceMinor` | remaining × 10^decimals. `0xFFFFFFFF` = unknown; `0xFFFFFFFE` = unlimited. |
| 12 | uint32 | `usageMinor` | spent/used × 10^decimals if known (e.g. OpenRouter `total_usage`); else `0xFFFFFFFF`. (Reserved for a future detail view; not shown in v1.) |
| 16 | uint32 | `updatedAt` | Unix seconds this provider was last polled OK; `0` = never. |
| 20 | char[16] | `name` | UTF-8, null-padded display label. |

### 7.3 Versioning

Same major/minor rules as `UsageSnapshot`/`CostSnapshot` (`PROTOCOL.md §3.3`): per-record stride is fixed per major; new `kind`/`status` values, new flag bits, and additional records are minor; any stride/header change is major. Versions independently of the other two characteristics.

## 8. Bridge changes (Swift)

| File | Change |
|---|---|
| `Protocol.swift` | Add `balanceSnapshotUUID`, balance version/size constants (`balanceHeaderSize=8`, `balanceRecordSize=36`, `balanceMaxRecords=16`), `triggerScopeBalances: UInt8 = 0x05`, `BalanceKind`/`BalanceStatus` enums, `BalanceFlags`/`BalanceRecordFlags` option sets, and a `NormalizedBalance` model (per-provider: kind, name, status, currency code+decimals, remaining, usage?, updatedAt, isLow). |
| `ProvidersConfig.swift` (new) | `Codable` model for `providers.json` + loader (returns `[]` when absent, never throws on missing file) + `kind`-defaults table. |
| `KeychainStore.swift` (new) | get/set/delete generic-password via Security; behind a `KeyStore` protocol so tests inject a fake (CI has no Keychain). |
| `KeyCommand.swift` (new) | `set-key` (stdin) / `list-keys` / `delete-key`; wired into `Bridge.swift`'s command `switch` + usage text. |
| `BalanceClient.swift` (new) | Actor, mirrors `CodexbarClient`. `fetchAll(_ providers:) async -> [Result]` running per-provider GETs concurrently (TaskGroup), each: resolve key from `KeyStore`, GET with timeout, parse JSON, apply `balancePath`/`usagePath`/`currency` selectors, compute remaining, map errors → `status` (401/403 → auth_error, 402/`is_available:false` → depleted, transport/timeout → unreachable, missing key → auth_error). Known-`kind` adapters (openrouter `/credits`→`/key` fallback, deepseek) layered over the generic path. A tiny dotted-path JSON evaluator. |
| `BalanceCache.swift` (new) | **Per-provider** last-good (unlike whole-snapshot `CostCache`): merges this poll's successes with retained last-good for any failed/missing provider, marking those `stale`; emits the encoded snapshot. |
| `BalanceEncoder.swift` (new) | `[NormalizedBalance] → Data` per §7 + `staleEmpty()` / `markStale()` / `errorEmpty()`, mirroring `CostEncoder`. Clamps to `balanceMaxRecords`; logs if truncated (no silent cap). |
| `GATTPeripheral.swift` | Add `balanceChar` (Read+Notify) to the service, `updateBalanceSnapshot(_:)` + `currentBalance` init = `BalanceEncoder.staleEmpty()`, a `pendingBalanceNotify` slot, serve it in `handleRead`, flush in `flushPendingNotify`. |
| `BridgeService.swift` | Load `providers.json` at start; add `balanceCache`; spawn a `balancePollLoop` that polls each provider when its `pollSeconds` elapses (staggered) and on `0x05`; `handleBalanceRefresh()` mirrors `handleCostRefresh()`. Balance polling is **independent** of the 60 s usage prewarm so remote latency never gates the rings/cost path. |

`CodexbarSupervisor` unaffected.

## 9. Firmware changes (C++)

| File | Change |
|---|---|
| `Protocol.h` | Add `kBalanceSnapshotUUID`, balance version/size constants, `kBalanceMaxRecords=16`, `kTriggerScopeBalances=0x05`, `kBalanceFlag*` / `kBalanceRecordFlagLow`, `enum class BalanceKind`, `enum class BalanceStatus`. |
| `BalanceCodec.{h,cpp}` (new) | `decodeBalanceSnapshot(bytes,len,out) → BalanceSnapshot { header; BalanceRecord records[16]; count }`. Mirrors `CostCodec`; rejects greater major; tolerates trailing unknown bytes (minor forward-compat). |
| `Theme.h` | `balanceColorFor(BalanceKind)` brand-color table (generic → `kTextMuted`); helpers to map `currencyCode` → symbol. |
| `Views/Balances.{h,cpp}` (new) | `drawBalances(renderer, snap, link, scrollOffset)` — title, clipped scrolling row list (dot/chip + name + native balance, amber on low, dim on status≠ok), right-arc scroll indicator, bottom fade, freshness pill. Currency rendering reuses the Font2 currency-glyph approach; `¥` falls back to code text if the glyph is absent. |
| `TouchScroll.{h,cpp}` (new) | Wraps `M5.Touch.getDetail()`: tracks drag delta → scroll offset, captures fling velocity, exposes `tick()` to decay momentum each `loop()`; clamps to content bounds; reports "was touched" for idle-timer reset. Pure-logic core (offset/velocity math) unit-testable in the native env. |
| `App.{h,cpp}` | `ViewId::Balances = 7`; extend `nextView`/`prevView`; add `isBalanceView`; hold scroll offset + a `wantsBalanceFetch_` (lazy first-read) flag, cleared on wake like cost. |
| `BleClient.{h,cpp}` | `fetchBalances(out,bufSize,outLen)` → `fetchInto(kBalanceSnapshotUUID, kTriggerScopeBalances, …)`; NimBLE read handles read-long for >MTU payloads. |
| `SnapshotStore` | New NVS key `"bal"` (no API/struct change — `save`/`load` are already keyed). |
| `main.cpp` | `g_balance` + `g_balanceLoaded`; `ensureBalanceLoaded()` lazy-reads on first entry to Balances this wake-session (mirrors `ensureCostLoaded`); `drawCurrentView` adds the `Balances` case passing scroll offset; `loop()` services `M5.Touch` while on Balances (scroll + momentum, `noteActivity()` on touch); KeyA-long on Balances writes `0x05` then re-reads; load cached `"bal"` at boot for instant first paint. |

**Lazy-read flow:** wake still reads only `UsageSnapshot`; rings paint unchanged. The first time the user reaches Balances, the watch reads `BalanceSnapshot` once and caches it for the session; the bridge already polled in the background so no fetch is induced beyond the read.

## 10. Error handling

| Condition | Bridge | Watch |
|---|---|---|
| Missing API key for a provider | that record `auth_error`, others unaffected | row dimmed + `auth` marker |
| 401/403 | record `auth_error` | as above |
| 402 / `is_available:false` | record `depleted`, `balanceMinor = 0` | row shows `0` + `empty` marker |
| Timeout / DNS / transport | keep last-good for that provider, mark `stale`/`unreachable` | last-known value + `offline`/`stale` marker |
| `balancePath` missing/non-numeric | record `unreachable` (parse fail), logged | dimmed + marker |
| No providers configured | `recordCount = 0` | `no providers configured` |
| Whole poll cycle errors | header `bridge_error` + `stale` | amber `stale` pill |
| `BalanceSnapshot.versionMajor` too new | n/a | decode rejected; keep last-known/empty; rings/cost unaffected |
| Unknown `kind`/`status`/`currency` | written as-is | render generic / ok / code-as-symbol |

The rings and spend paths keep their existing pills unchanged.

## 11. Open questions / risks

- **Touch on this board.** M5GFX 0.2.7 already carries the driver: it probes I²C (SDA 47 / SCL 48) for the CST820 at `0x15` and instantiates `Touch_CST816S` during display init (`M5GFX.cpp:1622,1725`), so `M5.Touch` should be live after `M5.begin()`. But this firmware uses a **custom board JSON + `fallback_board = board_M5StopWatch`**; the first implementation step is to **confirm on hardware** that `M5.Touch.getDetail()` reports points. If autodetect doesn't run on the fallback path, force the StopWatch touch setup (or instantiate `Touch_CST816S` directly at `0x15`). Low likelihood, but it gates the scroll UX.
- **¥ / non-`$` currency glyph.** Same family of issue as the documented `$` workaround. Mitigation: draw currency marks from Font2; if `0x00A5` (¥) / `0x20AC` (€) is absent, fall back to the 3-letter code. Confirm glyph availability when wiring `Views/Balances`.
- **OpenRouter `/credits` vs `/key`.** `/credits` may require a management key; `/key` works with a normal key. The adapter tries `/credits`, falls back to `/key` (`limit_remaining`); config can pin the choice. `limit_remaining: null` (unlimited) → the unlimited sentinel (`0xFFFFFFFE`, renders `∞`).
- **DeepSeek multi-currency.** v1 emits one row (first `balance_infos`, or the configured `currency`). Emitting one row per currency is a clean minor-version follow-up.
- **Keychain ACL / rebuilt binary.** Unattended daemon reads rely on the item ACL admitting the same binary. If the release binary path/identity changes, re-run `set-key`. Documented in README.
- **Payload size.** 16 providers = 584 bytes; read-blob handles it. If a real need exceeds 16, raise the cap (and confirm NimBLE read-long) rather than chunking manually.
- **Currency minor-unit exponent.** Defaults to 2 for unknown currencies; `currencyDecimals` overrides (e.g. 0 for JPY).
- **Poll stagger.** Providers polled on independent `pollSeconds`; stagger initial offsets to avoid a thundering herd at startup.

## 12. Testing

- **Bridge `BalanceEncoderTests`** — fixture `NormalizedBalance[] → expected hex` for: two providers, single provider, generic dotted-path, low-balance flag set, unknown currency, `auth_error`/`stale`/`depleted` statuses, truncation past 16, unknown/unlimited (`0xFFFFFFFF`).
- **Bridge `BalanceClient` tests** — `URLProtocol` stubs returning canned bodies: OpenRouter `/credits` and `/key` fallback, DeepSeek (incl. multi-currency + `is_available:false`), generic path, plus 401→auth_error, 402→depleted, timeout→unreachable, missing-key→auth_error. `KeyStore` faked.
- **Bridge `ProvidersConfig` / `KeychainStore`** — `kind`-defaults resolution; loader returns `[]` on absent file; KeychainStore round-trip behind the fake; `set-key` reads stdin not argv.
- **Firmware `test_balance_codec`** (native) — decode the **same** `shared/fixtures/balances-*.hex` the bridge emits (cross-side wire lock, per `PROTOCOL.md §4`); major-too-new rejection; trailing-bytes tolerance.
- **Firmware `test_touch_scroll`** (native) — offset clamps at both ends; momentum decays to zero; a tap (no drag) doesn't scroll.
- **Firmware `test_state_machine`** — extend for the 8-view cycle and lazy balance-fetch on entering Balances.
- **Shared fixtures** — add `shared/fixtures/balances-*.{json,hex}` loaded by both sides.
- **Manual** — `make flash` + glance: rings still ≤ 1 s on wake; Balances populates on first visit; drag scrolls; KeyA-long forces a poll; a wrong key shows `auth`, not a crash.

## 13. Implementation order (preview, not the plan)

1. `shared/PROTOCOL.md`: add `BalanceSnapshot` schema, UUID, `0x05` trigger scope.
2. Bridge: `NormalizedBalance` + `BalanceEncoder` + fixtures + `BalanceEncoderTests` (no BLE yet).
3. Bridge: `ProvidersConfig` + `KeychainStore`/`KeyStore` + `set-key`/`list-keys`/`delete-key` + tests.
4. Bridge: `BalanceClient` (generic + openrouter/deepseek adapters) + `URLProtocol` tests.
5. Bridge: `GATTPeripheral` balance characteristic + `BalanceCache` (per-provider merge) + `BridgeService` poll loop + `0x05`.
6. Firmware: `Protocol.h` consts + `BalanceCodec` + `test_balance_codec` (mirrored fixtures).
7. Firmware: `Views/Balances.cpp` + `kind`→color + currency formatting, against a hard-coded snapshot.
8. Firmware: `App` ViewId expansion + nav + lazy-fetch flag + scroll state; `test_state_machine`.
9. Firmware: `TouchScroll` + `M5.Touch` wiring + idle reset + `test_touch_scroll`; **verify touch on hardware**.
10. Firmware: `BleClient.fetchBalances` + NVS `"bal"` slot + `main.cpp` wiring.
11. Manual flash + glance validation.

The detailed step-by-step plan with review checkpoints will be written by the `writing-plans` skill next.
