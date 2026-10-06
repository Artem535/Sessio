# Throwaway spike code (issue #116) — not part of the Sessio build.
import argparse
import json
import threading
import time
from pathlib import Path

import psutil

from asrbench.datasets import (Utterance, load_fleurs, load_own, load_podlodka,
                               load_podlodka_long, with_babble)
from asrbench.fetch import DATA_DIR, PODLODKA_DIR
from asrbench.models import DEFAULT_MODELS_DIR, MODEL_SPECS, build

RESULTS_DIR = Path(__file__).resolve().parents[1] / "results"


class RssPeak:
    """Samples this process's RSS in a thread; reports peak growth over the starting level."""

    def __init__(self, interval_s: float = 0.05):
        self._proc = psutil.Process()
        self._interval = interval_s
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self.baseline = 0
        self.peak = 0

    def _loop(self) -> None:
        while not self._stop.is_set():
            self.peak = max(self.peak, self._proc.memory_info().rss)
            self._stop.wait(self._interval)

    def __enter__(self) -> "RssPeak":
        self.baseline = self.peak = self._proc.memory_info().rss
        self._thread.start()
        return self

    def __exit__(self, *exc) -> None:
        self._stop.set()
        self._thread.join()
        self.peak = max(self.peak, self._proc.memory_info().rss)

    @property
    def delta_mb(self) -> float:
        return (self.peak - self.baseline) / (1024 * 1024)


def run_benchmark(adapter, utts: list[Utterance]) -> list[dict]:
    records = []
    for utt in utts:
        t = adapter.transcribe(utt)
        records.append({
            "id": utt.id, "ref": utt.ref, "hyp": t.text, "dur_s": utt.duration_s,
            "compute_s": t.compute_s, "cpu_s": t.cpu_s,
            "first_text_s": t.first_text_s, "final_lag_s": t.final_lag_s,
            "phrase_lags": list(t.phrase_lags),
        })
    return records


def _dataset(name: str, n: int, own_dir: Path | None) -> list[Utterance]:
    if name == "fleurs-clean":
        return load_fleurs(DATA_DIR, n=n, seed=0)
    if name == "fleurs-babble10":
        return with_babble(load_fleurs(DATA_DIR, n=n, seed=0), snr_db=10.0, seed=0)
    if name == "podlodka":
        return load_podlodka(PODLODKA_DIR, n=n, seed=0)
    if name == "podlodka-long":
        return load_podlodka_long(PODLODKA_DIR)
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

    # Dataset is already in memory here, so the sampler measures model load + decode only.
    with RssPeak() as rss:
        t0 = time.perf_counter()
        adapter = build(args.model, DEFAULT_MODELS_DIR, args.threads)
        load_s = time.perf_counter() - t0

        # Warm-up on one utterance so first-call allocation does not skew compute time.
        adapter.transcribe(utts[0])
        records = run_benchmark(adapter, utts)

    out = {
        "model": args.model, "kind": adapter.kind, "dataset": args.dataset,
        "threads": args.threads, "load_s": load_s,
        "peak_rss_mb": rss.delta_mb,
        "utts": records,
    }
    RESULTS_DIR.mkdir(exist_ok=True)
    path = RESULTS_DIR / f"{args.model}__{args.dataset}.json"
    path.write_text(json.dumps(out, ensure_ascii=False), encoding="utf-8")
    print(f"wrote {path} ({len(records)} utterances, peak RSS {out['peak_rss_mb']:.0f} MB)")


if __name__ == "__main__":
    main()
