# Throwaway spike code (issue #116) — not part of the Sessio build.
import numpy as np

from asrbench.adapters import Transcript
from asrbench.datasets import Utterance
from asrbench.runner import run_benchmark

SR = 16000


class StubAdapter:
    kind = "offline"

    def transcribe(self, utt):
        return Transcript(text=f"hyp-{utt.id}", first_text_s=None, final_lag_s=0.4,
                          compute_s=0.4, cpu_s=1.2)


def test_run_benchmark_emits_one_record_per_utterance():
    utts = [Utterance(f"u{i}", np.zeros(SR * 2, np.float32), SR, f"ref {i}", 0.1) for i in range(3)]
    records = run_benchmark(StubAdapter(), utts)
    assert [r["id"] for r in records] == ["u0", "u1", "u2"]
    first = records[0]
    assert first["hyp"] == "hyp-u0" and first["ref"] == "ref 0"
    assert first["dur_s"] == 2.0
    assert first["compute_s"] == 0.4 and first["cpu_s"] == 1.2
    assert first["first_text_s"] is None and first["final_lag_s"] == 0.4
    assert first["phrase_lags"] == []


def test_rss_peak_reports_growth_over_baseline():
    from asrbench.runner import RssPeak

    with RssPeak(interval_s=0.01) as rss:
        hog = np.ones(20 * 1024 * 1024, dtype=np.uint8) + 1  # ~20 MB touched
        import time as _t
        _t.sleep(0.05)
    assert rss.delta_mb >= 10
    del hog
