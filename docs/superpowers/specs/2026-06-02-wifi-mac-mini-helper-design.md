# WiFi Mac Mini Helper Architecture — Design

**Status:** Approved design
**Date:** 2026-06-02
**Owner:** Justin Yan
**Builds on:** `docs/superpowers/specs/2026-05-28-codexbar-stopwatch-design.md`

## 1. Summary

Move the stopwatch's primary data path from a nearby BLE bridge that shells out to
CodexBar into an independent Mac mini service reachable over WiFi through
Cloudflare Tunnel.

The watch keeps its current screens, binary codecs, and snapshot cache behavior.
The architectural change is:

- The Mac mini helper fetches provider usage/cost/balance data directly.
- The helper no longer depends on `CodexBar.app`, `codexbar serve`, or a spawned
  CodexBar CLI process.
- The helper serves the same binary snapshot bytes over authenticated HTTP.
- The watch adds a user-selected transport mode: `WiFi` or `BLE`.

BLE remains available as a manually selected fallback mode, not an automatic
fallback. In WiFi mode the watch does not scan for BLE.

## 2. Goals

- Reduce `stale` / `no bridge` failures by letting the watch fetch from the
  always-on home Mac mini whenever it has WiFi.
- Preserve the existing binary protocol and firmware rendering code.
- Serve every existing watch data type over HTTP:
  - `UsageSnapshot`
  - `CostSnapshot`
  - `BalanceSnapshot`
  - `BalanceUsage`
- Keep provider credentials on the Mac mini, not in the firmware.
- Store only network/API access credentials on the watch, provisioned over USB
  serial into NVS.
- Avoid normal-mode provider fetch paths that can hang or require user
  interaction on a 24/7 machine.

## 3. Non-goals

- No automatic WiFi-to-BLE fallback. The transport is explicit in watch settings.
- No JSON parsing on the watch for the existing snapshot payloads.
- No direct Cloudflare client or provider secrets on the watch beyond the tunnel
  access credentials and origin API token.
- No on-watch text entry for WiFi/API provisioning.
- No dependency on CodexBar's runtime, localhost server, or CLI command output.
- No broad provider expansion beyond what existing screens already support.
- No OTA firmware update path.

## 4. Chosen Approach

Use an independent `stopwatch-bridge` data helper with a binary HTTP API.

Rejected alternatives:

| Approach | Reason rejected |
|---|---|
| Thin proxy over `codexbar serve` | Keeps the dependency that is currently failing and inherits CodexBar's localhost server limitations. |
| JSON-first remote API | Adds firmware parsing/model complexity and throws away the tested shared binary fixtures. |
| Automatic BLE fallback | Makes failures ambiguous and can hide a bad WiFi/API configuration. Manual transport mode is easier to reason about. |

## 5. Architecture

The helper has two roles:

1. **Data collector** — polls Codex, Claude, Gemini, API balances, and
   balance-usage sources directly.
2. **Transport server** — exposes cached binary frames through HTTP and, when
   BLE mode is selected on the watch, may continue to advertise the existing
   GATT service.

Data flow:

```text
Provider files/APIs/logs
  -> Mac mini stopwatch-bridge collectors
  -> NormalizedUsage / NormalizedCost / NormalizedBalance / NormalizedUsageSpend
  -> existing binary encoders
  -> HTTP binary endpoints through Cloudflare Tunnel
  -> watch WiFi client
  -> existing firmware codecs and views
```

The existing bridge encoder/cache classes stay valuable. The `CodexbarClient`
surface should be replaced or bypassed by direct provider collectors.

## 6. Cloudflare Tunnel

The Mac mini helper binds only to loopback, for example `127.0.0.1:8787`.
`cloudflared` maps the public hostname to that local service.

This matches Cloudflare's tunnel model: `cloudflared` creates outbound-only
connections from the origin machine to Cloudflare, so the helper does not need a
public listener or inbound firewall rule.

Cloudflare references:

- Tunnel overview: <https://developers.cloudflare.com/cloudflare-one/networks/connectors/cloudflare-tunnel/>
- Tunnel configuration/ingress: <https://developers.cloudflare.com/tunnel/advanced/local-management/configuration-file/>
- Access service tokens: <https://developers.cloudflare.com/cloudflare-one/access-controls/service-credentials/service-tokens/>

Example local tunnel intent:

```yaml
ingress:
  - hostname: stopwatch.example.com
    service: http://127.0.0.1:8787
  - service: http_status:404
```

## 7. HTTP API

All snapshot endpoints return raw binary bytes with
`Content-Type: application/octet-stream`.

| Method | Path | Behavior |
|---|---|---|
| `GET` | `/v1/health` | JSON health/config/status summary. |
| `GET` | `/v1/snapshot` | Cached `UsageSnapshot` bytes. |
| `GET` | `/v1/cost` | Cached `CostSnapshot` bytes. |
| `GET` | `/v1/balances` | Cached `BalanceSnapshot` bytes. |
| `GET` | `/v1/balance-usage` | Cached `BalanceUsage` bytes. |
| `POST` | `/v1/refresh?scope=<byte>` | Schedule refresh for the same scope byte used by BLE. Returns immediately. |

`GET` endpoints must be fast cache reads. They do not block on provider network
calls or local log scanning.

`POST /v1/refresh` schedules work and returns the current status. The watch can
issue refresh on long-press, show its existing loading overlay, then re-read the
relevant binary endpoint after a short delay.

## 8. HTTP Auth

HTTP access uses two layers:

1. **Cloudflare Access service token headers**
   - `CF-Access-Client-Id`
   - `CF-Access-Client-Secret`
2. **Origin API token checked by the helper**
   - `Authorization: Bearer <stopwatch-api-token>`

The extra origin token is intentional. It keeps the local helper protected if
the Cloudflare Access application is misconfigured.

All HTTP routes except `/v1/health` should require origin auth. `/v1/health`
may support a minimal unauthenticated response for local debugging, but the
tunneled route should still be protected by Cloudflare Access.

## 9. Watch Provisioning

Provisioning is over USB serial and writes to NVS. The values are never compiled
into source.

Required NVS values:

- WiFi SSID
- WiFi password
- API base URL
- Cloudflare Access client ID
- Cloudflare Access client secret
- Origin API token

Serial command shape:

```text
wifi ssid <value>
wifi password <value>
api base-url <https://...>
api cf-client-id <value>
api cf-client-secret <value>
api token <value>
config show
config clear
```

`config show` must redact secrets and show only presence/length/last-updated
metadata.

## 10. Firmware Behavior

Add `NetworkClient` alongside `BleClient`.

Transport mode is stored in NVS:

| Mode | Behavior |
|---|---|
| `WiFi` | Connect to WiFi, call authenticated HTTP endpoints, decode returned binary bytes. |
| `BLE` | Existing BLE/GATT behavior. |

The watch settings view gets a row:

```text
Transport  WiFi / BLE
```

Useful read-only status rows can show whether WiFi and API credentials are
configured. They should not show secret values.

WiFi-mode link/status states:

| State | Meaning |
|---|---|
| `wifi missing` | SSID/password not provisioned. |
| `api missing` | API base URL or auth values not provisioned. |
| `wifi offline` | Cannot join the configured WiFi network. |
| `api auth` | HTTP 401/403, Cloudflare Access rejection, or origin token rejection. |
| `api error` | DNS/timeout/5xx/malformed binary response. |
| `stale` | Server has last-good data but latest background poll failed. |

Existing view rendering should remain unchanged except for status labels needed
to communicate WiFi/API failures.

## 11. Provider Collection

The helper adapts CodexBar's non-interactive provider logic into this repository.

Primary sources:

| Provider/data | Source |
|---|---|
| Codex usage | Read `~/.codex/auth.json`, refresh OAuth token when needed, call Codex/ChatGPT usage API. |
| Codex cost | Scan local Codex and supported pi session logs. |
| Claude usage | Prefer OAuth credentials, Admin/API key, or configured web-cookie source that can run without prompts. |
| Claude cost | Scan local Claude and supported pi session logs. |
| Gemini usage | Read Gemini CLI OAuth credentials and call Gemini quota APIs. |
| API balances | Keep existing `providers.json` + Keychain model. |
| Balance usage | Keep existing provider usage clients, beginning with OpenRouter. |

Normal 24/7 mode excludes:

- `codex app-server`
- bare `claude` PTY
- any provider path that can launch an interactive auth flow

Those can be added later behind explicit debug config if needed.

## 12. Cache And Refresh

The helper polls in the background and serves cache reads.

Suggested cadence:

| Data type | Cadence |
|---|---|
| `UsageSnapshot` | Every 60 seconds. |
| `CostSnapshot` | After a successful full usage refresh and on a slower independent cadence. |
| `BalanceSnapshot` | Existing per-provider `pollSeconds`. |
| `BalanceUsage` | Lazy/background once configured; cache-backed. |

Cache semantics match the current bridge:

- Success updates last-good bytes.
- Failure preserves last-good bytes and marks stale/error.
- No last-good emits a well-formed empty/error frame.
- Narrow refresh scopes do not trigger unrelated heavy refreshes.

## 13. Files And Components

Likely bridge-side additions or replacements:

| Component | Responsibility |
|---|---|
| `HTTPServer` | Loopback HTTP server, routing, binary responses, auth checks. |
| `RefreshScheduler` | Scope-based background refresh queue; deduplicates in-flight work. |
| `ProviderCollectors/*` | Direct Codex/Claude/Gemini collectors adapted from CodexBar logic. |
| `SnapshotRepository` | Owns last-good bytes and current status for all four payloads. |
| `ProvisioningConfig` | API token/server config for the helper. |

Likely firmware-side additions:

| Component | Responsibility |
|---|---|
| `NetworkClient` | WiFi connect + authenticated HTTP GET/POST. |
| `DeviceConfig` | NVS-backed WiFi/API/transport settings. |
| `SerialProvisioning` | USB serial command parser and secret-safe config display. |
| `TransportClient` facade | Calls `NetworkClient` or `BleClient` based on selected mode. |

Existing codec and view files should not need structural changes.

## 14. Testing

Bridge tests:

- HTTP routing and method validation.
- Cloudflare/origin auth header validation.
- Binary content type and exact byte response.
- Refresh scheduling behavior.
- Cache read behavior while refresh is in flight.
- Collector tests with stubbed file/API inputs, reusing CodexBar fixtures where
  practical.

Firmware native tests:

- NVS config model serialization boundaries.
- Serial command parsing and redaction.
- Transport mode selection.
- HTTP status/error mapping.
- Binary length/major-version validation before decode.

Compatibility tests:

- Existing shared hex fixtures remain the guard for wire compatibility.
- Any wire-format change still updates `shared/PROTOCOL.md`, Swift encoder tests,
  firmware codec tests, and fixtures together.

Manual validation:

1. Run helper on localhost and verify all endpoints return expected byte lengths.
2. Configure Cloudflare Tunnel to the loopback service.
3. Provision the watch over USB serial.
4. Test WiFi mode through the tunnel.
5. Switch to BLE mode and confirm current behavior still works.
6. Break auth/network/provider inputs and verify honest status labels.

## 15. Rollout Plan

1. Add HTTP server that serves existing cached binary frames.
2. Add helper auth and refresh scheduling.
3. Add firmware config/provisioning and manual transport mode.
4. Add firmware WiFi HTTP client that consumes the binary endpoints.
5. Replace CodexBar-dependent collectors with direct Codex/Claude/Gemini sources.
6. Move API balance and balance-usage polling fully under the independent helper.
7. Update README/install docs for Mac mini + Cloudflare Tunnel setup.

This order creates a working transport path before the provider-source rewrite,
but the final architecture does not depend on CodexBar.

## 16. Open Risks

- Provider private APIs may change. Mitigation: isolated collectors, last-good
  cache, and explicit stale/error flags.
- Cloudflare Access service-token credentials live on the watch. Mitigation:
  use a dedicated Access app/token, rotateable origin token, and serial
  provisioning with redacted display.
- WiFi association may add wake latency. Mitigation: cached first paint remains
  local NVS; HTTP fetch updates after connection.
- Porting CodexBar log scanners may be non-trivial. Mitigation: start with
  copied/adapted narrow scanner logic and fixture tests, not a broad dependency.

