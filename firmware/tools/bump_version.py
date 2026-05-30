#!/usr/bin/env python3
"""
Bump the firmware release version (semver) held in firmware/src/Version.h.

Edits only that one file — no git side effects (commit/tag stays manual).
Prints the transition, e.g. `0.1.1 -> 0.1.2`.

Usage:
  python3 firmware/tools/bump_version.py {major|minor|patch}
"""

from __future__ import annotations
import re, sys
from pathlib import Path

ROOT      = Path(__file__).resolve().parent.parent
VERSION_H = ROOT / "src" / "Version.h"
PARTS     = ("major", "minor", "patch")

# Matches: inline constexpr char kFirmwareVersion[] = "0.1.1";
# Group 2 is the three-part numeric semver we rewrite in place.
VERSION_RE = re.compile(r'(kFirmwareVersion\[\]\s*=\s*")(\d+\.\d+\.\d+)(")')


def next_version(current: str, part: str) -> str:
    """Return the semver after bumping `part` of `current`.

    e.g. next_version("0.1.1", "patch") == "0.1.2".
    minor resets patch to 0; major resets minor and patch to 0.
    Raises ValueError on an unknown part or a non-numeric / non-three-part version.
    """
    if part not in PARTS:
        raise ValueError(f"unknown part {part!r}; expected one of {', '.join(PARTS)}")
    fields = current.split(".")
    if len(fields) != 3 or not all(f.isdigit() for f in fields):
        raise ValueError(f"not a three-part numeric semver: {current!r}")
    major, minor, patch = (int(f) for f in fields)
    if part == "major":
        major, minor, patch = major + 1, 0, 0
    elif part == "minor":
        minor, patch = minor + 1, 0
    else:  # patch
        patch += 1
    return f"{major}.{minor}.{patch}"


def main(argv: list[str]) -> int:
    if len(argv) != 1 or argv[0] not in PARTS:
        sys.stderr.write(f"usage: python3 {Path(__file__).name} {{{'|'.join(PARTS)}}}\n")
        return 2
    if not VERSION_H.exists():
        sys.stderr.write(f"error: {VERSION_H} not found\n")
        return 1
    text = VERSION_H.read_text()
    m = VERSION_RE.search(text)
    if m is None:
        sys.stderr.write(f"error: no kFirmwareVersion line in {VERSION_H}\n")
        return 1
    current = m.group(2)
    new = next_version(current, argv[0])
    VERSION_H.write_text(text[: m.start(2)] + new + text[m.end(2) :])
    print(f"{current} -> {new}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
