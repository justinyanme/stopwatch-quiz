#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
echo "==> building release binary"
cd bridge && swift build -c release
echo "==> installing"
.build/release/stopwatch-bridge install
