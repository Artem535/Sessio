#!/usr/bin/env bash
# Throwaway spike code (issue #116) — long-form run; waits for run_matrix.sh to finish first.
cd "$(dirname "$0")"
until grep -q '^DONE' results-run.log 2>/dev/null; do sleep 20; done
for m in gigaam-v3-rnnt-vad gigaam-v3-ctc-vad parakeet-v3-vad t-one nemotron-160ms nemotron-560ms nemotron-1120ms; do
  echo "=== $m podlodka-long $(date +%T)"
  uv run python -m asrbench.runner --model "$m" --dataset podlodka-long --threads 4 2>>results/stderr.log || echo "FAILED $m"
done
echo DONE-LONG
