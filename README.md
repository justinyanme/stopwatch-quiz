# CodexBar StopWatch

Brings CodexBar usage indicators for Codex / Claude Code / Gemini onto an M5Stack StopWatch over Bluetooth LE. No agent credentials live on the device.

## What you get

- Three concentric activity rings — outer Codex, middle Claude, inner Gemini — at a wrist-glance.
- Per-provider drill-down: session ring + week ring + reset countdown + credits.
- Wake-to-glance: press KEYA or KEYB to wake, ~1 s to first paint.

## Requirements

- macOS 14 (Sonoma) or newer.
- CodexBar.app (or the `codexbar` CLI) installed and signed into Codex / Claude Code / Gemini.
- Swift 6.2+ (Xcode 16.2+).
- PlatformIO Core (`pip install platformio` or `brew install platformio`).
- An M5Stack StopWatch + USB-C cable for flashing.

## Install

```bash
git clone <this repo>
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

## Daily use

| Input | Action |
|---|---|
| KEYA short | Previous view |
| KEYB short | Next view |
| KEYA long  | Force refresh from Mac |
| KEYB long  | Sleep display now |
| 15 s idle  | Auto-sleep |

Status pills under the rings:

- `● no bridge` — the watch can't see the Mac (out of range, BT off, daemon not running)
- `● link error` — found the Mac but couldn't read the snapshot
- `● stale` — bridge has prior data but the latest CodexBar fetch failed
- `● no source` — the `codexbar` CLI isn't installed on the Mac

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
- `docs/superpowers/specs/` — design doc.
- `docs/superpowers/plans/` — implementation plan.

## Tests

```bash
cd bridge && swift test                              # 18/18 Swift bridge tests
cd firmware && pio test -e native                    # 10/10 firmware native tests
```

Cross-side wire compatibility is locked: both sides round-trip against the same `shared/fixtures/*.hex` files.

## Uninstall

```bash
launchctl bootout "gui/$(id -u)" "$HOME/Library/LaunchAgents/dev.stopwatch.bridge.plist"
rm "$HOME/Library/LaunchAgents/dev.stopwatch.bridge.plist"
rm -rf "$HOME/Library/Application Support/stopwatch-bridge"
```
