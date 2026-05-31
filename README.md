# CodexBar StopWatch

Brings [CodexBar](https://codexbar.app/) usage indicators for Codex / Claude Code / Gemini onto an M5Stack StopWatch over Bluetooth LE. No agent credentials live on the device.

> **What's CodexBar?** [CodexBar](https://codexbar.app/) is a macOS menu-bar app that tracks how close you are to your Codex / Claude Code / Gemini usage limits. This project reads CodexBar's data locally on your Mac (via its `codexbar serve` localhost API) and relays a compact snapshot to the watch over Bluetooth LE — so a glance at your wrist replaces opening the menu bar.

## What you get

- Three concentric activity rings — outer Codex, middle Claude, inner Gemini — at a wrist-glance.
- Per-provider drill-down: session ring + week ring + reset countdown + credits.
- Wake-to-glance: press KEYA or KEYB to wake, ~1 s to first paint.
- API credit balances: a scrollable wallet screen of remaining balance per provider (OpenRouter, DeepSeek, and any Bearer-key API), in native currency.

## Requirements

- macOS 14 (Sonoma) or newer.
- [CodexBar](https://codexbar.app/) — the app or its `codexbar` CLI — installed and signed into Codex / Claude Code / Gemini. This is the data source the bridge reads from.
- Swift 6.2+ (Xcode 16.2+).
- PlatformIO Core (`pip install platformio` or `brew install platformio`).
- An [M5Stack StopWatch](https://docs.m5stack.com/en/core/StopWatch) + USB-C cable for flashing.

## Install

```bash
git clone https://github.com/justinyanme/stopwatch-quiz.git
cd stopwatch-quiz
make build              # builds the Swift bridge in release mode
./scripts/install-bridge.sh   # generates random port, writes config, registers a launchd agent
```

The first launch will prompt for Bluetooth permission. Grant it in System Settings → Privacy & Security → Bluetooth.

Tail the bridge log: `tail -f /tmp/stopwatch-bridge.log`

## Flash the watch

Connect the watch over USB-C, then:

```bash
make flash
make monitor   # optional: watch serial output
```

Power-cycle the watch. It will wake into the overview with cached or live data.

If `make flash` fails with `Failed to connect to ESP32-S3: No serial data received`, the running firmware is drowning out esptool's sync on the USB-Serial-JTAG endpoint. Long-press BOOT until the screen goes off and the green LED blinks (firmware-controlled sleep — USB stays enumerated), then re-run `make flash`. `upload_protocol = esp-builtin` is a dead end on macOS because the kernel CDC kext blocks libusb from claiming the JTAG interface.

## Daily use

| Input | Action |
|---|---|
| KEYA short | Previous view |
| KEYB short | Next view |
| KEYA long  | Force refresh from Mac |
| KEYB long  | Sleep display now |
| KEYA + KEYB long | Open/close carousel settings |
| 5 min idle | Sleep display |

## Carousel autoplay

Autoplay is enabled by default while the watch is awake. It advances through the main views every 10 seconds and pauses on any button press, touch scroll, row tap, refresh, loading state, or balance detail. It resumes after the configured quiet period.

Hold KEYA + KEYB to open carousel settings on the watch. KEYB moves between rows, KEYA changes the selected value, KEYA-long resets defaults, and KEYA + KEYB long saves and exits.

Settings:

- Autoplay: On / Off
- Interval: 5s / 10s / 15s / 30s
- Motion: Iris / Fade / Instant
- Resume: 10s / 20s / 30s after input

Status pills under the rings:

- `● no bridge` — the watch can't see the Mac (out of range, BT off, daemon not running)
- `● link error` — found the Mac but couldn't read the snapshot
- `● stale` — bridge has prior data but the latest CodexBar fetch failed
- `● no source` — the `codexbar` CLI isn't installed on the Mac

## API balances

The last view in the carousel is a **scrollable wallet** of prepaid balances from pay-as-you-go API providers. The bridge polls each provider's balance endpoint on a timer and serves it over BLE like the usage/spend data; the watch reads it lazily on entering the screen. Drag to scroll; a balance under its `lowThreshold` turns amber.

Configure providers in `~/Library/Application Support/stopwatch-bridge/providers.json` (`0600`). **API keys live in the macOS Keychain, never in this file.**

```jsonc
[
  { "id": "openrouter", "name": "OpenRouter", "kind": "openrouter", "lowThreshold": 5.0 },
  { "id": "deepseek",   "name": "DeepSeek",   "kind": "deepseek" },
  { "id": "aihubmix",   "name": "AiHubMix",   "kind": "generic",
    "endpoint": "https://aihubmix.com/api/user/self", "auth": "raw",
    "balancePath": "data.quota", "currency": "USD", "scale": 500000, "lowThreshold": 5.0 }
]
```

- **Known kinds** (`openrouter`, `deepseek`) fill in endpoint/paths/currency — just give `id`/`name`/`kind`.
- **Generic providers** need `endpoint`, `balancePath` (dotted/indexed JSON, e.g. `data.balance` or `balance_infos[0].total_balance`), and `currency` (literal `"USD"` or `"path:balance_infos[0].currency"`). Optional: `usagePath` (remaining = balance − usage), `scale` (divide raw units into currency, e.g. `500000` for AiHubMix quota→USD), `auth` (`bearer` default | `raw` token-verbatim | `none`), `currencyDecimals`, `pollSeconds` (default 900), `lowThreshold`.

Manage keys (stored in the Keychain, read from stdin — never argv):

```bash
./bridge/.build/release/stopwatch-bridge set-key <id>     # paste the key at the prompt
./bridge/.build/release/stopwatch-bridge list-keys
./bridge/.build/release/stopwatch-bridge delete-key <id>
```

After adding or changing a key, restart the daemon so it re-reads the Keychain (a running process caches the value):

```bash
launchctl kickstart -k "gui/$(id -u)/dev.stopwatch.bridge"
```

Providers without a key-queryable balance API can't be shown — e.g. MiniMax pay-as-you-go balance is dashboard-only.

## Troubleshooting

- **Watch shows `● no bridge`:** `launchctl print "gui/$(id -u)/dev.stopwatch.bridge"` should say `state = running`. If not: `launchctl kickstart -k "gui/$(id -u)/dev.stopwatch.bridge"`.
- **Bridge logs `Bluetooth not powered on`:** turn Bluetooth on in System Settings, then kickstart the bridge.
- **Bridge logs `codexbar binary not found in known locations`:** install CodexBar.app and run its **Install CLI** action, or symlink the CLI manually to `/opt/homebrew/bin/codexbar`.
- **Watch doesn't wake on button press after sleep:** the GPIO numbers for KEYA/KEYB in `firmware/src/Power.cpp` are placeholders pending M5Stack StopWatch schematic verification. Update `kPinKeyA` and `kPinKeyB` and re-flash.
- **Decode a snapshot off the wire:** `./bridge/.build/release/stopwatch-bridge decode-snapshot <hex>` pretty-prints the bytes.

## Layout

- `bridge/` — Swift Package, the macOS CLI daemon.
- `firmware/` — PlatformIO project, the watch firmware.
- `shared/` — wire-protocol single source of truth (UUIDs, byte layout, test fixtures).
- `scripts/` — install/flash convenience wrappers.
- `docs/superpowers/specs/` — design docs.

## Tests

```bash
cd bridge && swift test                              # Swift bridge tests
cd firmware && pio test -e native                    # firmware native tests
```

Cross-side wire compatibility is locked: both sides round-trip against the same `shared/fixtures/*.hex` files.

## Uninstall

```bash
launchctl bootout "gui/$(id -u)" "$HOME/Library/LaunchAgents/dev.stopwatch.bridge.plist"
rm "$HOME/Library/LaunchAgents/dev.stopwatch.bridge.plist"
rm -rf "$HOME/Library/Application Support/stopwatch-bridge"
```

## License

MIT — see [LICENSE](LICENSE).
