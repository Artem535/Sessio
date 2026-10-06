# Throwaway spike code (issue #116) — not part of the Sessio build.
import json
import math
from collections import defaultdict
from pathlib import Path

from asrbench.metrics import cer, percentile, wer
from asrbench.runner import RESULTS_DIR


def summarize(run: dict) -> dict:
    utts = run["utts"]
    first = [u["first_text_s"] for u in utts if u["first_text_s"] is not None]
    final = [u["final_lag_s"] for u in utts]
    compute = sum(u["compute_s"] for u in utts)
    return {
        "model": run["model"], "kind": run["kind"], "dataset": run["dataset"], "n": len(utts),
        "wer": wer([u["ref"] for u in utts], [u["hyp"] for u in utts]),
        "cer": cer([u["ref"] for u in utts], [u["hyp"] for u in utts]),
        "rtf": compute / sum(u["dur_s"] for u in utts),
        "cpu_cores": sum(u["cpu_s"] for u in utts) / compute if compute else math.nan,
        "first_p50": percentile(first, 50), "first_p95": percentile(first, 95),
        "final_p50": percentile(final, 50), "final_p95": percentile(final, 95),
        "peak_rss_mb": run["peak_rss_mb"],
    }


def _fmt(x: float, spec: str) -> str:
    return "n/a" if x is None or (isinstance(x, float) and math.isnan(x)) else format(x, spec)


def render_markdown(summaries: list[dict]) -> str:
    by_dataset: dict[str, list[dict]] = defaultdict(list)
    for s in summaries:
        by_dataset[s["dataset"]].append(s)
    parts = []
    for dataset, rows in sorted(by_dataset.items()):
        parts.append(f"### {dataset}\n")
        parts.append("| model | kind | n | WER % | CER % | RTF | cores | first text p50/p95 s "
                     "| final lag p50/p95 s | peak RSS MB |")
        parts.append("|---|---|---|---|---|---|---|---|---|---|")
        for s in sorted(rows, key=lambda r: (math.isnan(r["wer"]), r["wer"])):
            parts.append(
                f"| {s['model']} | {s['kind']} | {s['n']} | {_fmt(s['wer'] * 100, '.1f')} "
                f"| {_fmt(s['cer'] * 100, '.1f')} | {_fmt(s['rtf'], '.2f')} "
                f"| {_fmt(s['cpu_cores'], '.1f')} "
                f"| {_fmt(s['first_p50'], '.2f')} / {_fmt(s['first_p95'], '.2f')} "
                f"| {_fmt(s['final_p50'], '.2f')} / {_fmt(s['final_p95'], '.2f')} "
                f"| {_fmt(s['peak_rss_mb'], '.0f')} |")
        parts.append("")
    return "\n".join(parts)


def main() -> None:
    runs = [json.loads(p.read_text(encoding="utf-8")) for p in sorted(Path(RESULTS_DIR).glob("*.json"))]
    print(render_markdown([summarize(r) for r in runs]))


if __name__ == "__main__":
    main()
