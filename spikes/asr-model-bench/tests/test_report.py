# Throwaway spike code (issue #116) — not part of the Sessio build.
import math

from asrbench.report import render_markdown, summarize


def _run(model, kind, hyps, first=None, final=0.5):
    utts = [
        {"id": f"u{i}", "ref": "а б в г", "hyp": h, "dur_s": 4.0, "compute_s": 1.0,
         "cpu_s": 3.0, "first_text_s": first, "final_lag_s": final}
        for i, h in enumerate(hyps)
    ]
    return {"model": model, "kind": kind, "dataset": "fleurs-clean", "threads": 4,
            "load_s": 1.0, "peak_rss_mb": 900.0, "utts": utts}


def test_summarize_computes_corpus_metrics():
    s = summarize(_run("m", "streaming", ["а б в г", "а б в д"], first=0.4))
    assert s["n"] == 2
    assert s["wer"] == 1 / 8  # one wrong word out of eight reference words
    assert s["rtf"] == 0.25   # 2 s compute / 8 s audio
    assert s["cpu_cores"] == 3.0
    assert s["first_p50"] == 0.4 and s["final_p95"] == 0.5
    assert s["peak_rss_mb"] == 900.0
    assert math.isnan(s["phrase_p50"])  # no VAD phrases for this run


def test_summarize_pools_phrase_lags_across_utterances():
    run = _run("v", "offline+vad", ["а б в г", "а б в г"])
    run["utts"][0]["phrase_lags"] = [0.8, 1.0]
    run["utts"][1]["phrase_lags"] = [1.2]
    s = summarize(run)
    assert s["phrase_p50"] == 1.0


def test_offline_models_have_nan_first_text_latency():
    s = summarize(_run("o", "offline", ["а б в г"], first=None))
    assert math.isnan(s["first_p50"])


def test_render_markdown_sorts_by_wer_and_marks_missing_latency():
    good = summarize(_run("good", "offline", ["а б в г"]))
    bad = summarize(_run("bad", "streaming", ["х х х х"], first=0.3))
    md = render_markdown([bad, good])
    assert md.index("| good |") < md.index("| bad |")
    assert "### fleurs-clean" in md
    assert "| good | offline |" in md
    assert "n/a" in md  # good has no first-text latency


def test_render_asciidoc_emits_sorted_table_per_dataset():
    from asrbench.report import render_asciidoc

    good = summarize(_run("good", "offline", ["а б в г"]))
    bad = summarize(_run("bad", "streaming", ["х х х х"], first=0.3))
    adoc = render_asciidoc([bad, good], captions={"fleurs-clean": "FLEURS ru, clean"})
    assert ".FLEURS ru, clean" in adoc
    assert adoc.count("|===") == 2
    assert adoc.index("|good |offline") < adoc.index("|bad |streaming")
    assert "|model |kind |n |WER %" in adoc
