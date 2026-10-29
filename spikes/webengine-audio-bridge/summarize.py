"""Prints run-matrix.sh results as a Markdown table: python3 summarize.py results.jsonl"""
import json
import sys
from pathlib import Path

# run-matrix.sh writes into the working directory; refuse paths outside it.
base = Path.cwd().resolve()
results = (base / sys.argv[1]).resolve()
if not results.is_relative_to(base) or results.suffix != ".jsonl":
    sys.exit(f"expected a .jsonl file under {base}")

print("| transport | mode | tracks | GUI jank ms | CPU % (tree) | p50 ms | p99 ms | max ms | missing | gaps |")
print("|---|---|---|---|---|---|---|---|---|---|")
for line in results.open(encoding="utf-8"):
    r = json.loads(line)
    lat = r["latencyMs"]
    gaps = sum(t["seqGaps"] for t in r["tracks"])
    print(f"| {r['transport']} | {r['mode']} | {r['page'].get('tracksAdded', 0)} | {r['guiJankMs']} | "
          f"{r['cpuPercentTree']:.1f} | {lat['p50']:.2f} | {lat['p99']:.2f} | {lat['max']:.1f} | "
          f"{r['missingChunks']} | {gaps} |")
