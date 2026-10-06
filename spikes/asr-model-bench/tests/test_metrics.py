# Throwaway spike code (issue #116) — not part of the Sessio build.
import math

from asrbench.metrics import cer, percentile, wer


def test_wer_one_substitution_in_four_words():
    assert wer(["а б в г"], ["а б в д"]) == 0.25


def test_wer_is_corpus_level_not_mean_of_utterances():
    # 1 error in 1 word + 0 errors in 3 words = 1/4, not (1.0 + 0.0) / 2
    assert wer(["а", "б в г"], ["х", "б в г"]) == 0.25


def test_metrics_normalize_inputs_first():
    assert wer(["Привет, мир!"], ["привет мир"]) == 0.0


def test_cer_counts_characters():
    assert cer(["кот"], ["кит"]) == 1 / 3


def test_wer_skips_pairs_with_empty_reference():
    assert wer(["...", "а б"], ["что-то", "а б"]) == 0.0


def test_percentile_linear_interpolation():
    assert percentile([1.0, 2.0, 3.0, 4.0], 50) == 2.5
    assert percentile([1.0, 2.0, 3.0, 4.0], 100) == 4.0


def test_percentile_empty_is_nan():
    assert math.isnan(percentile([], 50))
