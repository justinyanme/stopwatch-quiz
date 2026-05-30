#!/usr/bin/env python3
"""
Direct tests for bump_version.next_version (this repo has no pytest harness).

Run:
  python3 firmware/tools/test_bump_version.py
"""

from __future__ import annotations
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bump_version import next_version

# (current, part, expected)
CASES = [
    ("0.1.1", "patch", "0.1.2"),   # basic patch
    ("0.1.9", "patch", "0.1.10"),  # multi-digit, no string-sort surprise
    ("0.1.1", "minor", "0.2.0"),   # minor resets patch
    ("1.4.2", "minor", "1.5.0"),   # minor resets patch (non-zero start)
    ("0.9.9", "major", "1.0.0"),   # major resets minor + patch
    ("0.1.1", "major", "1.0.0"),   # major resets minor + patch
]

# (current, part) that must raise ValueError
ERROR_CASES = [
    ("0.1", "patch"),     # not three parts
    ("1.x.0", "patch"),   # non-numeric field
    ("0.1.1", "build"),   # unknown part
]


def main() -> int:
    failures = 0

    for current, part, expected in CASES:
        got = next_version(current, part)
        ok = got == expected
        print(f"  {'ok  ' if ok else 'FAIL'}: {current} {part} -> {got}  (expected {expected})")
        if not ok:
            failures += 1

    for current, part in ERROR_CASES:
        try:
            next_version(current, part)
        except ValueError:
            print(f"  ok  : {current!r} {part!r} raised ValueError")
        else:
            print(f"  FAIL: {current!r} {part!r} did not raise ValueError")
            failures += 1

    total = len(CASES) + len(ERROR_CASES)
    print(f"{'PASS' if failures == 0 else 'FAIL'}: {total - failures}/{total} checks")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
