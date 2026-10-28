"""Prints run-matrix.sh results as a Markdown table: python3 summarize.py results.jsonl"""
import json
import sys

print("| transport | mode | tracks | GUI jank ms | CPU % (tree) | p50 ms | p99 ms | max ms | missing | gaps |")
print("|---|---|---|---|---|---|---|---|---|---|")
for line in open(sys.argv[1]):
    r = json.loads(line)
    lat = r["latencyMs"]
    gaps = sum(t["seqGaps"] for t in r["tracks"])
    print(f"| {r['transport']} | {r['mode']} | {r['page'].get('tracksAdded', 0)} | {r['guiJankMs']} | "
          f"{r['cpuPercentTree']:.1f} | {lat['p50']:.2f} | {lat['p99']:.2f} | {lat['max']:.1f} | "
          f"{r['missingChunks']} | {gaps} |")
