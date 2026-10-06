# Throwaway spike code (issue #116) — not part of the Sessio build.
import math

import jiwer

from asrbench.textnorm import normalize


def _pairs(refs: list[str], hyps: list[str]) -> tuple[list[str], list[str]]:
    kept = [(normalize(r), normalize(h)) for r, h in zip(refs, hyps, strict=True)]
    kept = [(r, h) for r, h in kept if r]
    return [r for r, _ in kept], [h for _, h in kept]


def wer(refs: list[str], hyps: list[str]) -> float:
    r, h = _pairs(refs, hyps)
    return float(jiwer.wer(r, h)) if r else math.nan


def cer(refs: list[str], hyps: list[str]) -> float:
    r, h = _pairs(refs, hyps)
    return float(jiwer.cer(r, h)) if r else math.nan


def percentile(values: list[float], q: float) -> float:
    if not values:
        return math.nan
    ordered = sorted(values)
    pos = (len(ordered) - 1) * q / 100.0
    lo = math.floor(pos)
    hi = math.ceil(pos)
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (pos - lo)
