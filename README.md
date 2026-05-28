# CodexBar StopWatch

Brings CodexBar usage indicators for Codex / Claude Code / Gemini onto an M5Stack StopWatch over Bluetooth LE.

See `docs/superpowers/specs/2026-05-28-codexbar-stopwatch-design.md` for design and `docs/superpowers/plans/2026-05-28-codexbar-stopwatch.md` for the implementation plan.

## Quick start

```
make build         # build the Swift bridge
make install       # install as launchd agent (prompts for Bluetooth permission)
make flash         # flash the firmware to a connected watch
```

See `make help` for all targets.
