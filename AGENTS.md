# Repository Guidelines

## Project Structure & Module Organization

This repository has two buildable parts. `bridge/` is a Swift Package for the macOS `stopwatch-bridge` CLI and launchd daemon; source lives in `bridge/Sources/StopwatchBridge/` and tests in `bridge/Tests/StopwatchBridgeTests/`. `firmware/` is the PlatformIO Arduino/C++17 project for the M5Stack StopWatch; firmware source is in `firmware/src/` and native Unity tests are in `firmware/test/`. Shared protocol documentation and compatibility fixtures live in `shared/`. Use `scripts/` for install helpers and `docs/superpowers/` for design notes and implementation plans.

## Build, Test, and Development Commands

- `make build` builds the Swift bridge in release mode.
- `make test` runs Swift tests, firmware native tests, and the firmware version bump script test.
- `cd bridge && swift test` runs only bridge tests.
- `cd firmware && pio test -e native` runs only firmware unit tests.
- `make flash` uploads firmware to a connected watch; `make monitor` opens the serial monitor at 115200 baud.
- `make clean` clears Swift Package and PlatformIO build artifacts.

## Coding Style & Naming Conventions

Use four-space indentation in Swift and C++. Keep Swift files focused on one command, service, or model. C++ code should stay under the `stopwatch` namespace, use `PascalCase` for types, `camelCase` for functions, and trailing underscores for private member fields where existing code does so. Prefer small protocol/codec changes with matching fixture updates in `shared/fixtures/`.

## Testing Guidelines

Bridge tests use Swift Testing (`@Suite`, `@Test`, `#expect`). Firmware tests use Unity with `test_*` functions and explicit `RUN_TEST(...)` registration. Add or update tests when changing encoding, decoding, configuration, carousel state, formatting, or protocol behavior. For wire-format changes, update both Swift and firmware tests against the shared hex/JSON fixtures.

## Commit & Pull Request Guidelines

Recent commits use scoped, imperative subjects such as `firmware: pause carousel during active touch` and `docs: document carousel autoplay`. Follow that pattern: `bridge:`, `firmware:`, `shared:`, `docs:`, or `scripts:`. Pull requests should describe the user-visible change, list test commands run, link related issues or design docs, and include screenshots or photos when watch UI rendering changes.

## Security & Configuration Tips

Do not commit API keys, generated user config, launchd plists, or local logs. Provider API keys belong in the macOS Keychain via `stopwatch-bridge set-key`; `providers.json` should contain only provider metadata.
