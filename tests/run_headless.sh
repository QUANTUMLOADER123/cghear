#!/usr/bin/env bash
# Headless logic test: compiles the game against a tiny pure-C++ raylib stub
# (no window, no GPU) and simulates 12,000 frames: menu autopilot must stack
# a perfect tower, then human-like taps must crash it and trigger a retry.
set -euo pipefail
cd "$(dirname "$0")/.."

g++ -std=c++17 -O2 -DHS_HEADLESS -Wall -Wextra -Wno-unused-parameter \
    -I tests/stub src/main.cpp -o /tmp/hyperstack_headless

OUT=$(/tmp/hyperstack_headless)
echo "$OUT"

BEST=$(echo "$OUT" | grep -oP 'best=\K[0-9]+')
if [ "${BEST:-0}" -lt 50 ]; then
    echo "FAIL: autopilot best score too low ($BEST < 50)"; exit 1
fi
echo "HEADLESS TEST OK (best=$BEST)"
