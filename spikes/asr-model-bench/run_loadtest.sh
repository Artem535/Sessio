#!/usr/bin/env bash
# Throwaway spike code (issue #116) — real-time load test of the shared-recognizer design.
cd "$(dirname "$0")"
mkdir -p results
for n in 1 2 5 10; do
  echo "=== $n tracks, everybody talks $(date +%T)"
  uv run python -m asrbench.loadtest --tracks "$n" --seconds 60 2>>results/stderr.log || echo "FAILED $n"
done
echo "=== 10 tracks, 2 talk at once $(date +%T)"
uv run python -m asrbench.loadtest --tracks 10 --seconds 60 --speakers 2 2>>results/stderr.log || echo "FAILED turns"
echo DONE-LOAD
