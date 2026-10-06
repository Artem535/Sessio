# Throwaway spike code (issue #116) — not part of the Sessio build.
from pathlib import Path

import pytest

from asrbench.datasets import load_fleurs
from asrbench.fetch import DATA_DIR
from asrbench.metrics import wer
from asrbench.models import DEFAULT_MODELS_DIR, MODEL_SPECS, build

pytestmark = pytest.mark.models


@pytest.fixture(scope="module")
def sample():
    if not (DATA_DIR / "test.tsv").is_file():
        pytest.skip("FLEURS not downloaded")
    return load_fleurs(DATA_DIR, n=5, seed=0)


@pytest.mark.parametrize("model_id", list(MODEL_SPECS))
def test_model_transcribes_russian_sample(model_id, sample):
    if not (DEFAULT_MODELS_DIR / MODEL_SPECS[model_id].archive).is_dir():
        pytest.skip(f"{model_id} not downloaded")
    adapter = build(model_id, DEFAULT_MODELS_DIR, threads=4)
    hyps = [adapter.transcribe(u).text for u in sample]
    assert any(h.strip() for h in hyps)
    assert wer([u.ref for u in sample], hyps) < 0.6  # sanity floor, not a quality bar
