# Throwaway spike code (issue #116) — not part of the Sessio build.
from asrbench.textnorm import normalize


def test_lowercases_folds_yo_and_strips_punctuation():
    assert normalize("Ёлка, 5 штук!") == "елка 5 штук"


def test_collapses_whitespace_and_dashes():
    assert normalize("  кто-то   пришёл —  вот  ") == "кто то пришел вот"


def test_empty_stays_empty():
    assert normalize("...") == ""
