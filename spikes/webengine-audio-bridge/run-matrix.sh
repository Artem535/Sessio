#!/usr/bin/env bash
# Runs the transport matrix sequentially (runs must not overlap: CPU is
# measured per process tree) and appends one JSON line per run to $OUT.
# Usage: run-matrix.sh <path/to/audio_bridge_spike> [seconds] [out.jsonl]
set -euo pipefail
BIN=$1
SECS=${2:-60}
OUT=${3:-results.jsonl}
export QT_QPA_PLATFORM=offscreen

run() {
  echo "== $*" >&2
  timeout $((SECS + 60)) "$BIN" --seconds "$SECS" "$@" 2>/dev/null | tail -1 >>"$OUT"
}

for t in none channel ws scheme; do run --transport "$t" --mode loopback --tracks 3; done
run --transport ws --ws-thread --mode loopback --tracks 3
for t in none channel ws scheme; do run --transport "$t" --mode synthetic --tracks 10; done
for t in channel ws scheme; do run --transport "$t" --mode loopback --tracks 3 --gui-jank-ms 300; done
run --transport ws --ws-thread --mode loopback --tracks 3 --gui-jank-ms 300
