#!/usr/bin/env bash
# Headless logic test: compiles the game against a tiny pure-C++ raylib stub
# (no window, no GPU) and simulates 12,000 frames:
#   * menu autopilot must stack a demo tower and chain perfect combos
#   * human-like taps must then crash the run and trigger the retry loop
set -euo pipefail
cd "$(dirname "$0")/.."

CXX_BIN="${CXX:-}"
if [ -z "$CXX_BIN" ]; then
    if command -v g++ >/dev/null; then CXX_BIN=g++
    elif command -v clang++ >/dev/null; then CXX_BIN=clang++
    else echo "SKIP: no C++ compiler found"; exit 0; fi
fi

"$CXX_BIN" -std=c++17 -O2 -DHS_HEADLESS -Wall -Wextra -Wno-unused-parameter \
    -I tests/stub src/main.cpp -o /tmp/hyperstack_headless

OUT=$(/tmp/hyperstack_headless)
echo "$OUT"

# menu phase: the autopilot must reach at least floor 8
MAX_LVL=$(echo "$OUT" | grep -oE 'level=[0-9]+' | cut -d= -f2 | sort -n | tail -1)
# final line: the game must be in a real run/crash cycle (not stuck in menu)
BEST=$(echo "$OUT" | grep -oE 'best=[0-9]+' | cut -d= -f2 | tail -1)

if [ "${MAX_LVL:-0}" -lt 8 ]; then
    echo "FAIL: autopilot never stacked a tower (max level $MAX_LVL < 8)"; exit 1
fi
if ! echo "$OUT" | grep -q "state=2"; then
    echo "FAIL: never reached the DEAD state (crash/retry loop untested)"; exit 1
fi
if [ -z "$BEST" ] || [ "$BEST" -lt 1 ]; then
    echo "FAIL: best score was never saved"; exit 1
fi
echo "HEADLESS TEST OK (max level=$MAX_LVL, best=$BEST)"
