# Throwaway spike code (issue #116) — not part of the Sessio build.
import argparse
import json
import resource
import time
from pathlib import Path

from asrbench.datasets import Utterance, load_fleurs, load_own, load_podlodka, with_babble
from asrbench.fetch import DATA_DIR, PODLODKA_DIR
from asrbench.models import DEFAULT_MODELS_DIR, MODEL_SPECS, build

RESULTS_DIR = Path(__file__).resolve().parents[1] / "results"


def run_benchmark(adapter, utts: list[Utterance]) -> list[dict]:
    records = []
    for utt in utts:
        t = adapter.transcribe(utt)
        records.append({
            "id": utt.id, "ref": utt.ref, "hyp": t.text, "dur_s": utt.duration_s,
            "compute_s": t.compute_s, "cpu_s": t.cpu_s,
            "first_text_s": t.first_text_s, "final_lag_s": t.final_lag_s,
        })
    return records


def _dataset(name: str, n: int, own_dir: Path | None) -> list[Utterance]:
    if name == "fleurs-clean":
        return load_fleurs(DATA_DIR, n=n, seed=0)
    if name == "fleurs-babble10":
        return with_babble(load_fleurs(DATA_DIR, n=n, seed=0), snr_db=10.0, seed=0)
    if name == "podlodka":
        return load_podlodka(PODLODKA_DIR, n=n, seed=0)
    if name == "own":
        if own_dir is None:
            raise SystemExit("--own-dir is required for dataset 'own'")
        return load_own(own_dir)
    raise SystemExit(f"unknown dataset {name}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True, choices=list(MODEL_SPECS))
    ap.add_argument("--dataset", required=True)
    ap.add_argument("--n", type=int, default=200)
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--own-dir", type=Path)
    args = ap.parse_args()

    utts = _dataset(args.dataset, args.n, args.own_dir)
    if not utts:
        raise SystemExit("dataset is empty")

    t0 = time.perf_counter()
    adapter = build(args.model, DEFAULT_MODELS_DIR, args.threads)
    load_s = time.perf_counter() - t0

    # Warm-up on one utterance so first-call allocation does not skew compute time.
    adapter.transcribe(utts[0])
    records = run_benchmark(adapter, utts)

    out = {
        "model": args.model, "kind": adapter.kind, "dataset": args.dataset,
        "threads": args.threads, "load_s": load_s,
        "peak_rss_mb": resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024.0,
        "utts": records,
    }
    RESULTS_DIR.mkdir(exist_ok=True)
    path = RESULTS_DIR / f"{args.model}__{args.dataset}.json"
    path.write_text(json.dumps(out, ensure_ascii=False), encoding="utf-8")
    print(f"wrote {path} ({len(records)} utterances, peak RSS {out['peak_rss_mb']:.0f} MB)")


if __name__ == "__main__":
    main()
