#!/usr/bin/env bash
# Throwaway spike code (issue #116) — runs the full model x dataset matrix sequentially.
cd "$(dirname "$0")"
mkdir -p results
for m in t-one nemotron-160ms nemotron-560ms nemotron-1120ms parakeet-v3 gigaam-v3-rnnt gigaam-v3-ctc whisper-turbo; do
  for d in podlodka fleurs-clean fleurs-babble10; do
    echo "=== $m $d $(date +%T)"
    uv run python -m asrbench.runner --model "$m" --dataset "$d" --n 200 --threads 4 2>>results/stderr.log || echo "FAILED $m $d"
  done
done
echo DONE
