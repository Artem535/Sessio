# Real-time ASR Model Comparison Spike Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Measure Russian recognition quality, latency, and CPU/RAM cost of the shortlisted open ASR models on this machine, and produce a decision document for the in-call live transcription feature.

**Architecture:** An isolated Python benchmark harness under `spikes/asr-model-bench/` drives every model through one runtime, `sherpa-onnx` 1.13.8 (offline recognizers for Parakeet/GigaAM/Whisper, online recognizers for Nemotron 3.5 and T-One). One adapter interface (`Recognizer.transcribe`) hides offline/streaming differences. A runner writes one JSON file per (model, dataset) run in a separate process, so peak RSS is per model; a report script turns the JSON files into a Markdown table. A final C++ smoke program proves the winning streaming model also runs through the sherpa-onnx C++ API that the Qt app would use.

**Tech Stack:** Python 3.12 (via `uv`), `sherpa-onnx==1.13.8`, `soundfile`, `numpy`, `jiwer`, `pytest`; C++20 + CMake against the prebuilt `sherpa-onnx-v1.13.8-linux-x64-shared-lib` release.

## Global Constraints

- All code lives under `spikes/asr-model-bench/`. It is never `add_subdirectory`'d from the main `CMakeLists.txt`, never links into `Sessio_app`, and does not touch `vcpkg.json`. No version bump, `CHANGELOG.md` entry, or translation change: the spike ships nothing to users.
- Linux x64, CPU only. This machine has 16 cores, 27 GB RAM, no NVIDIA GPU. Voxtral Realtime (4B) is excluded for that reason. Every run uses `--threads 4` unless a task says otherwise, to leave cores for a video call.
- `sherpa-onnx` is pinned to `1.13.8`. Model archives come only from `https://github.com/k2-fsa/sherpa-onnx/releases/tag/asr-models`.
- `models/`, `data/`, `results/`, and `.venv/` are gitignored. Only code, tests, `README.md`, and `docs/asr-spike-results.md` are committed.
- Never put real client or session audio, or transcripts of it, in the repo, in logs, or in the results document. Own test recordings come from the team reading text or talking about neutral topics, with every speaker's consent.
- Text is normalized identically for every model before WER/CER: lowercase, `ё`→`е`, punctuation removed, whitespace collapsed. No inverse text normalization is applied, so digit-vs-word differences count as errors for every model equally. Record this limitation in the results document.
- Conversational-style test set: `bond005/podlodka_speech` from Hugging Face (spontaneous Russian podcast speech with IT vocabulary and English loanwords, hand transcripts, 16 kHz; all three splits are pooled). It is test data only, kept under the gitignored `data/` directory, and not redistributed. No licence is declared on the dataset card, so do not commit it or its transcripts.
- Utterances longer than 25 s are excluded (GigaAM's per-clip limit), for every model.
- Every new file starts with a one-line comment saying it is throwaway spike code (issue number added in Task 0).

## File Structure

```
spikes/asr-model-bench/
  pyproject.toml            # uv project, pinned deps
  README.md                 # how to run everything
  .gitignore                # models/ data/ results/ .venv/
  asrbench/__init__.py
  asrbench/textnorm.py      # normalize(text) -> str
  asrbench/metrics.py       # wer, cer, percentile
  asrbench/audio.py         # read_wav, mix_at_snr, babble, first_speech_s
  asrbench/datasets.py      # Utterance, load_fleurs, load_own, with_babble
  asrbench/adapters.py      # Transcript, OfflineAdapter, StreamingAdapter, pick()
  asrbench/models.py        # MODEL_SPECS registry, build(model_id, ...)
  asrbench/fetch.py         # download + unpack model archives, FLEURS
  asrbench/runner.py        # run_benchmark(), CLI
  asrbench/report.py        # aggregate results/*.json -> Markdown
  tests/test_textnorm.py
  tests/test_metrics.py
  tests/test_audio.py
  tests/test_datasets.py
  tests/test_adapters.py
  tests/test_runner.py
  tests/test_report.py
  tests/test_smoke_models.py   # skipped unless models are downloaded
  cpp/CMakeLists.txt
  cpp/nemotron_smoke.cc
docs/asr-spike-results.md     # final decision document (committed)
```

---

### Task 0: Issue and branch (needs the user's go-ahead)

AGENTS.md requires every non-trivial task to start as a GitHub issue with a branch from `main`.

- [ ] **Step 1: Create the issue**

```bash
gh issue create \
  --title "Spike: compare real-time ASR models (Nemotron 3.5, GigaAM v3, Parakeet v3, T-One, Whisper turbo)" \
  --label enhancement --label in-progress \
  --body "Compare Russian WER/CER, latency, RTF and RAM of shortlisted open ASR models on CPU to choose the engine for in-call live transcription. Plan: docs/superpowers/plans/2026-10-06-realtime-asr-spike.md. Output: docs/asr-spike-results.md with a decision."
```

Expected: prints the new issue URL. Note the number as `116`.

- [ ] **Step 2: Create the branch from `main`**

```bash
git switch main && git switch -c spike/116-asr-model-bench
```

Expected: `Switched to a new branch 'spike/116-asr-model-bench'`. The existing uncommitted files (`CONTEXT.md`, other plans) stay untracked; do not stage them.

- [ ] **Step 3: Substitute the issue number**

Every `116` in this plan (file header comments, branch names, commit messages, `gh` commands) means the issue number from Step 1. Use the real number when creating files and running commands.

- [ ] **Step 4: Commit this plan**

```bash
git add docs/superpowers/plans/2026-10-06-realtime-asr-spike.md
git commit -m "docs: implementation plan for #116 real-time ASR comparison spike"
```

---

### Task 1: Scaffold the uv project and text/metric primitives

**Files:**
- Create: `spikes/asr-model-bench/pyproject.toml`
- Create: `spikes/asr-model-bench/.gitignore`
- Create: `spikes/asr-model-bench/asrbench/__init__.py`
- Create: `spikes/asr-model-bench/asrbench/textnorm.py`
- Create: `spikes/asr-model-bench/asrbench/metrics.py`
- Test: `spikes/asr-model-bench/tests/test_textnorm.py`, `spikes/asr-model-bench/tests/test_metrics.py`

**Interfaces:**
- Produces: `normalize(text: str) -> str`; `wer(refs: list[str], hyps: list[str]) -> float` and `cer(refs, hyps) -> float` (fractions, corpus level, inputs normalized inside); `percentile(values: list[float], q: float) -> float` (q in 0..100, linear interpolation, `nan` for empty).

- [ ] **Step 1: Create the project files**

`spikes/asr-model-bench/pyproject.toml`:

```toml
# Throwaway spike code (issue #116) — not part of the Sessio build.
[project]
name = "asrbench"
version = "0.0.0"
requires-python = ">=3.12,<3.13"
dependencies = [
  "sherpa-onnx==1.13.8",
  "sherpa-onnx-core==1.13.8",
  "soundfile>=0.12",
  "numpy>=1.26",
  "jiwer>=3.0",
  "psutil>=5.9",
  "pyarrow>=15",
]

[dependency-groups]
dev = ["pytest>=8"]

[tool.pytest.ini_options]
testpaths = ["tests"]
markers = ["models: needs downloaded model archives (skipped otherwise)"]

[build-system]
requires = ["hatchling"]
build-backend = "hatchling.build"

[tool.hatch.build.targets.wheel]
packages = ["asrbench"]
```

`spikes/asr-model-bench/.gitignore`:

```
models/
data/
results/
.venv/
__pycache__/
```

`spikes/asr-model-bench/asrbench/__init__.py`:

```python
# Throwaway spike code (issue #116) — not part of the Sessio build.
```

- [ ] **Step 2: Install the environment**

Run: `cd spikes/asr-model-bench && uv sync --python 3.12`
Expected: creates `.venv`, installs `sherpa-onnx 1.13.8`. If no wheel is found for 1.13.8, stop and report BLOCKED with the pip error.

- [ ] **Step 3: Write the failing tests**

`tests/test_textnorm.py`:

```python
# Throwaway spike code (issue #116) — not part of the Sessio build.
from asrbench.textnorm import normalize


def test_lowercases_folds_yo_and_strips_punctuation():
    assert normalize("Ёлка, 5 штук!") == "елка 5 штук"


def test_collapses_whitespace_and_dashes():
    assert normalize("  кто-то   пришёл —  вот  ") == "кто то пришел вот"


def test_empty_stays_empty():
    assert normalize("...") == ""
```

`tests/test_metrics.py`:

```python
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
```

- [ ] **Step 4: Run tests to verify they fail**

Run: `cd spikes/asr-model-bench && uv run pytest tests/test_textnorm.py tests/test_metrics.py -q`
Expected: FAIL with `ModuleNotFoundError: No module named 'asrbench.textnorm'`.

- [ ] **Step 5: Implement**

`asrbench/textnorm.py`:

```python
# Throwaway spike code (issue #116) — not part of the Sessio build.
import re

_NON_WORD = re.compile(r"[^\w]+", re.UNICODE)


def normalize(text: str) -> str:
    text = text.lower().replace("ё", "е")
    text = text.replace("_", " ")
    return _NON_WORD.sub(" ", text).strip()
```

`asrbench/metrics.py`:

```python
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
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cd spikes/asr-model-bench && uv run pytest tests/test_textnorm.py tests/test_metrics.py -q`
Expected: `10 passed`.

- [ ] **Step 7: Commit**

```bash
git add spikes/asr-model-bench
git commit -m "feat(spike): scaffold ASR bench project with text normalization and WER/CER"
```

---

### Task 2: Audio helpers and datasets (FLEURS, own recordings, babble noise)

**Files:**
- Create: `spikes/asr-model-bench/asrbench/audio.py`
- Create: `spikes/asr-model-bench/asrbench/datasets.py`
- Test: `spikes/asr-model-bench/tests/test_audio.py`, `spikes/asr-model-bench/tests/test_datasets.py`

**Interfaces:**
- Consumes: nothing from Task 1 except package layout.
- Produces:
  - `read_wav(path: Path) -> tuple[np.ndarray, int]` (mono float32, sample rate)
  - `mix_at_snr(signal: np.ndarray, noise: np.ndarray, snr_db: float) -> np.ndarray`
  - `babble(others: list[np.ndarray], length: int) -> np.ndarray`
  - `first_speech_s(samples: np.ndarray, sr: int) -> float`
  - `@dataclass(frozen=True) Utterance(id: str, samples: np.ndarray, sr: int, ref: str, onset_s: float)` with property `duration_s`
  - `load_fleurs(root: Path, n: int, seed: int = 0, max_seconds: float = 25.0) -> list[Utterance]`
  - `load_podlodka(root: Path, n: int, seed: int = 0, max_seconds: float = 25.0) -> list[Utterance]` (reads `*.parquet` in `root`; each row has `audio.bytes`, `transcription`, `episode`)
  - `load_own(root: Path, max_seconds: float = 25.0) -> list[Utterance]`
  - `with_babble(utts: list[Utterance], snr_db: float, seed: int = 0) -> list[Utterance]`

- [ ] **Step 1: Write the failing tests**

`tests/test_audio.py`:

```python
# Throwaway spike code (issue #116) — not part of the Sessio build.
import numpy as np
import soundfile as sf

from asrbench.audio import babble, first_speech_s, mix_at_snr, read_wav, rms

SR = 16000


def _tone(seconds: float, amp: float = 0.1) -> np.ndarray:
    t = np.arange(int(seconds * SR)) / SR
    return (amp * np.sin(2 * np.pi * 220 * t)).astype(np.float32)


def test_read_wav_returns_first_channel_float32(tmp_path):
    stereo = np.stack([_tone(0.1), _tone(0.1, amp=0.01)], axis=1)
    sf.write(tmp_path / "a.wav", stereo, SR)
    audio, sr = read_wav(tmp_path / "a.wav")
    assert sr == SR
    assert audio.dtype == np.float32 and audio.ndim == 1
    assert abs(rms(audio) - rms(_tone(0.1))) < 1e-3


def test_first_speech_finds_onset_after_silence():
    samples = np.concatenate([np.zeros(SR // 2, np.float32), _tone(0.5)])
    assert abs(first_speech_s(samples, SR) - 0.5) < 0.03


def test_first_speech_of_silence_is_zero():
    assert first_speech_s(np.zeros(SR, np.float32), SR) == 0.0


def test_mix_at_snr_hits_target():
    signal = _tone(1.0, amp=0.1)
    noise = np.random.default_rng(0).standard_normal(SR).astype(np.float32)
    mixed = mix_at_snr(signal, noise, 10.0)
    residual = mixed - signal
    achieved = 20 * np.log10(rms(signal) / rms(residual))
    assert abs(achieved - 10.0) < 0.1


def test_mix_at_snr_tiles_short_noise_and_keeps_length():
    signal = _tone(1.0)
    mixed = mix_at_snr(signal, _tone(0.1, amp=0.5), 5.0)
    assert mixed.shape == signal.shape


def test_babble_has_requested_length():
    out = babble([_tone(0.3), _tone(0.7)], SR)
    assert out.shape == (SR,)
```

`tests/test_datasets.py`:

```python
# Throwaway spike code (issue #116) — not part of the Sessio build.
import numpy as np
import soundfile as sf

from asrbench.datasets import load_fleurs, load_own, with_babble

SR = 16000


def _write(path, seconds, amp=0.1):
    t = np.arange(int(seconds * SR)) / SR
    sf.write(path, (amp * np.sin(2 * np.pi * 220 * t)).astype(np.float32), SR)


def _make_fleurs(root, specs):
    """specs: list of (filename, raw_text, seconds)"""
    audio = root / "test"
    audio.mkdir(parents=True)
    lines = []
    for i, (name, text, seconds) in enumerate(specs):
        _write(audio / name, seconds)
        lines.append(f"{i}\t{name}\t{text}\t{text.lower()}\tchars\t{int(seconds * SR)}\tMALE")
    (root / "test.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8")


def test_load_fleurs_reads_ref_text_and_skips_long_clips(tmp_path):
    _make_fleurs(tmp_path, [("a.wav", "Привет, мир.", 1.0), ("b.wav", "Слишком длинный.", 26.0)])
    utts = load_fleurs(tmp_path, n=10, seed=0, max_seconds=25.0)
    assert [u.ref for u in utts] == ["Привет, мир."]
    assert utts[0].id == "a.wav"
    assert abs(utts[0].duration_s - 1.0) < 1e-3


def test_load_fleurs_sampling_is_deterministic_and_bounded(tmp_path):
    _make_fleurs(tmp_path, [(f"{i}.wav", f"фраза {i}", 0.5) for i in range(8)])
    a = load_fleurs(tmp_path, n=3, seed=1)
    b = load_fleurs(tmp_path, n=3, seed=1)
    assert [u.id for u in a] == [u.id for u in b]
    assert len(a) == 3


def test_load_podlodka_reads_parquet_rows_and_filters_long_clips(tmp_path):
    import io

    import pyarrow as pa
    import pyarrow.parquet as pq

    from asrbench.datasets import load_podlodka

    def wav_bytes(seconds):
        buf = io.BytesIO()
        t = np.arange(int(seconds * SR)) / SR
        sf.write(buf, (0.1 * np.sin(2 * np.pi * 220 * t)).astype(np.float32), SR, format="WAV")
        return buf.getvalue()

    rows = [
        {"audio": {"bytes": wav_bytes(1.0), "path": "a.wav"}, "transcription": "Первая, фраза.", "episode": 1, "title": "t"},
        {"audio": {"bytes": wav_bytes(26.0), "path": "b.wav"}, "transcription": "Длинная.", "episode": 1, "title": "t"},
        {"audio": {"bytes": wav_bytes(2.0), "path": "c.wav"}, "transcription": "  ", "episode": 2, "title": "t"},
    ]
    pq.write_table(pa.Table.from_pylist(rows), tmp_path / "test.parquet")
    utts = load_podlodka(tmp_path, n=10, seed=0, max_seconds=25.0)
    assert [(u.id, u.ref) for u in utts] == [("test-0", "Первая, фраза.")]
    assert abs(utts[0].duration_s - 1.0) < 1e-3


def test_load_own_pairs_wav_with_txt(tmp_path):
    _write(tmp_path / "s1.wav", 1.0)
    (tmp_path / "s1.txt").write_text("Первая фраза\n", encoding="utf-8")
    _write(tmp_path / "orphan.wav", 1.0)  # no .txt -> ignored
    utts = load_own(tmp_path)
    assert [(u.id, u.ref) for u in utts] == [("s1.wav", "Первая фраза")]


def test_with_babble_keeps_refs_and_shapes_and_suffixes_ids(tmp_path):
    _make_fleurs(tmp_path, [(f"{i}.wav", f"фраза {i}", 0.5 + i * 0.1) for i in range(5)])
    clean = load_fleurs(tmp_path, n=5, seed=0)
    noisy = with_babble(clean, snr_db=10.0, seed=0)
    assert [u.ref for u in noisy] == [u.ref for u in clean]
    assert [u.samples.shape for u in noisy] == [u.samples.shape for u in clean]
    assert all(u.id.endswith("@babble10") for u in noisy)
    assert all(u.onset_s == c.onset_s for u, c in zip(noisy, clean))
    assert not np.array_equal(noisy[0].samples, clean[0].samples)
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd spikes/asr-model-bench && uv run pytest tests/test_audio.py tests/test_datasets.py -q`
Expected: FAIL with `ModuleNotFoundError: No module named 'asrbench.audio'`.

- [ ] **Step 3: Implement `audio.py`**

```python
# Throwaway spike code (issue #116) — not part of the Sessio build.
from pathlib import Path

import numpy as np
import soundfile as sf


def read_wav(path: Path) -> tuple[np.ndarray, int]:
    audio, sr = sf.read(str(path), dtype="float32", always_2d=True)
    return audio[:, 0].copy(), int(sr)


def rms(x: np.ndarray) -> float:
    return float(np.sqrt(np.mean(np.square(x)))) if len(x) else 0.0


def _fit(x: np.ndarray, length: int) -> np.ndarray:
    if len(x) == 0:
        return np.zeros(length, dtype=np.float32)
    if len(x) < length:
        x = np.tile(x, int(np.ceil(length / len(x))))
    return x[:length]


def mix_at_snr(signal: np.ndarray, noise: np.ndarray, snr_db: float) -> np.ndarray:
    noise = _fit(noise, len(signal))
    ps, pn = rms(signal), rms(noise)
    if ps == 0.0 or pn == 0.0:
        return signal.copy()
    gain = ps / (pn * 10 ** (snr_db / 20))
    out = signal + gain * noise
    peak = float(np.max(np.abs(out)))
    if peak > 1.0:
        out = out / peak * 0.99  # scales signal and noise together: SNR unchanged
    return out.astype(np.float32)


def babble(others: list[np.ndarray], length: int) -> np.ndarray:
    total = np.zeros(length, dtype=np.float32)
    for other in others:
        total += _fit(other, length)
    return total


def first_speech_s(samples: np.ndarray, sr: int, frame_s: float = 0.02) -> float:
    frame = max(1, int(frame_s * sr))
    n = len(samples) // frame
    if n == 0:
        return 0.0
    frames = samples[: n * frame].reshape(n, frame)
    levels = np.sqrt(np.mean(np.square(frames), axis=1))
    peak = float(levels.max())
    if peak == 0.0:
        return 0.0
    return float(np.argmax(levels >= 0.1 * peak)) * frame_s
```

- [ ] **Step 4: Implement `datasets.py`**

```python
# Throwaway spike code (issue #116) — not part of the Sessio build.
import io
import random
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import soundfile as sf

from asrbench.audio import babble, first_speech_s, mix_at_snr, read_wav


@dataclass(frozen=True)
class Utterance:
    id: str
    samples: np.ndarray
    sr: int
    ref: str
    onset_s: float

    @property
    def duration_s(self) -> float:
        return len(self.samples) / self.sr


def _make(id_: str, path: Path, ref: str, max_seconds: float) -> Utterance | None:
    if not ref.strip() or sf.info(str(path)).duration > max_seconds:
        return None
    samples, sr = read_wav(path)
    return Utterance(id_, samples, sr, ref.strip(), first_speech_s(samples, sr))


def load_fleurs(root: Path, n: int, seed: int = 0, max_seconds: float = 25.0) -> list[Utterance]:
    index = {p.name: p for p in root.rglob("*.wav")}
    rows = []
    for line in (root / "test.tsv").read_text(encoding="utf-8").splitlines():
        cols = line.split("\t")
        if len(cols) >= 3 and cols[1] in index:
            rows.append((cols[1], cols[2]))
    random.Random(seed).shuffle(rows)
    out: list[Utterance] = []
    for name, raw in rows:
        utt = _make(name, index[name], raw, max_seconds)
        if utt is not None:
            out.append(utt)
        if len(out) == n:
            break
    return out


def load_podlodka(root: Path, n: int, seed: int = 0, max_seconds: float = 25.0) -> list[Utterance]:
    import pyarrow.parquet as pq

    rows = []
    for parquet in sorted(root.glob("*.parquet")):
        for i, row in enumerate(pq.read_table(parquet).to_pylist()):
            rows.append((f"{parquet.stem}-{i}", row["audio"]["bytes"], row["transcription"]))
    random.Random(seed).shuffle(rows)
    out: list[Utterance] = []
    for id_, wav_bytes, text in rows:
        if not text.strip():
            continue
        samples, sr = sf.read(io.BytesIO(wav_bytes), dtype="float32", always_2d=True)
        samples = samples[:, 0].copy()
        if len(samples) / sr > max_seconds:
            continue
        out.append(Utterance(id_, samples, int(sr), text.strip(), first_speech_s(samples, int(sr))))
        if len(out) == n:
            break
    return out


def load_own(root: Path, max_seconds: float = 25.0) -> list[Utterance]:
    out: list[Utterance] = []
    for wav in sorted(root.glob("*.wav")):
        txt = wav.with_suffix(".txt")
        if not txt.is_file():
            continue
        utt = _make(wav.name, wav, txt.read_text(encoding="utf-8"), max_seconds)
        if utt is not None:
            out.append(utt)
    return out


def with_babble(utts: list[Utterance], snr_db: float, seed: int = 0) -> list[Utterance]:
    rng = random.Random(seed)
    tag = f"@babble{int(snr_db)}"
    out = []
    for i, utt in enumerate(utts):
        pool = [u for j, u in enumerate(utts) if j != i] or [utt]
        picks = [rng.choice(pool).samples for _ in range(3)]
        noisy = mix_at_snr(utt.samples, babble(picks, len(utt.samples)), snr_db)
        out.append(Utterance(utt.id + tag, noisy, utt.sr, utt.ref, utt.onset_s))
    return out
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cd spikes/asr-model-bench && uv run pytest tests/test_audio.py tests/test_datasets.py -q`
Expected: `11 passed`.

- [ ] **Step 6: Commit**

```bash
git add spikes/asr-model-bench
git commit -m "feat(spike): audio helpers and FLEURS/own/babble datasets"
```

---

### Task 3: Recognizer adapters with the latency model

**Files:**
- Create: `spikes/asr-model-bench/asrbench/adapters.py`
- Test: `spikes/asr-model-bench/tests/test_adapters.py`

**Interfaces:**
- Consumes: `Utterance` from Task 2.
- Produces:
  - `@dataclass(frozen=True) Transcript(text: str, first_text_s: float | None, final_lag_s: float, compute_s: float, cpu_s: float)`. `first_text_s` is `None` for offline models. For streaming models it is the real-time moment the first non-empty partial becomes available, minus the utterance's speech onset. `final_lag_s` is how long after the last audio sample the final text is available. `compute_s` is total decode wall time. `cpu_s` is process CPU time over the same span.
  - `pick(directory: Path, stem: str) -> str`: path of the first `{stem}*.onnx` match (`stem` may contain a wildcard, e.g. `*-encoder`), preferring names containing `int8`; raises `FileNotFoundError` if none.
  - `class OfflineAdapter(recognizer, clock=time.perf_counter, cpu_clock=time.process_time)` with `.kind == "offline"` and `.transcribe(utt: Utterance) -> Transcript`.
  - `class StreamingAdapter(recognizer, *, feed_s=0.1, lead_s=0.0, tail_s=0.0, language: str | None = None, clock=..., cpu_clock=...)` with `.kind == "streaming"` and the same `.transcribe`.
- Latency model (streaming): audio is fed in `feed_s` chunks. Chunk `i` arrives at `audio_end_i`. Processing starts at `max(done, audio_end_i)` and takes the measured compute time `c_i`, so `done = max(done, audio_end_i) + c_i`. The first non-empty partial after chunk `i` is available at `done`. If compute cannot keep up (RTF > 1), `done` falls behind and latency grows, which is the honest result.

- [ ] **Step 1: Write the failing tests**

`tests/test_adapters.py`:

```python
# Throwaway spike code (issue #116) — not part of the Sessio build.
import numpy as np
import pytest

from asrbench.adapters import OfflineAdapter, StreamingAdapter, pick
from asrbench.datasets import Utterance

SR = 16000


class FakeClock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now


def _utt(seconds=1.0, onset=0.2):
    return Utterance("u", np.zeros(int(seconds * SR), np.float32), SR, "ref", onset)


# ---- pick ------------------------------------------------------------------
def test_pick_prefers_int8(tmp_path):
    (tmp_path / "encoder.onnx").touch()
    (tmp_path / "encoder.int8.onnx").touch()
    assert pick(tmp_path, "encoder").endswith("encoder.int8.onnx")


def test_pick_falls_back_to_plain_and_matches_prefix(tmp_path):
    (tmp_path / "tiny-encoder.onnx").touch()
    (tmp_path / "model.onnx").touch()
    assert pick(tmp_path, "model").endswith("model.onnx")


def test_pick_raises_when_missing(tmp_path):
    with pytest.raises(FileNotFoundError):
        pick(tmp_path, "joiner")


# ---- offline ---------------------------------------------------------------
class FakeOfflineStream:
    def __init__(self, rec):
        self.rec = rec
        self.result = type("R", (), {"text": ""})()

    def accept_waveform(self, sr, samples):
        self.rec.clock.now += 0.01
        self.rec.cpu.now += 0.02


class FakeOfflineRecognizer:
    def __init__(self, clock, cpu):
        self.clock, self.cpu = clock, cpu

    def create_stream(self):
        return FakeOfflineStream(self)

    def decode_stream(self, stream):
        self.clock.now += 0.5
        self.cpu.now += 1.5
        stream.result.text = "привет мир"


def test_offline_adapter_reports_decode_time_as_final_lag():
    clock, cpu = FakeClock(), FakeClock()
    ad = OfflineAdapter(FakeOfflineRecognizer(clock, cpu), clock=clock, cpu_clock=cpu)
    t = ad.transcribe(_utt())
    assert t.text == "привет мир"
    assert t.first_text_s is None
    assert t.compute_s == pytest.approx(0.51)
    assert t.final_lag_s == pytest.approx(0.51)
    assert t.cpu_s == pytest.approx(1.52)
    assert ad.kind == "offline"


# ---- streaming -------------------------------------------------------------
class FakeOnlineStream:
    def __init__(self, rec):
        self.rec = rec
        self.chunks = 0
        self.pending = 0
        self.finished = False
        self.options = {}

    def set_option(self, key, value):
        self.options[key] = value

    def accept_waveform(self, sr, samples):
        self.chunks += 1
        self.pending += 1

    def input_finished(self):
        self.finished = True


class FakeOnlineRecognizer:
    """Costs `cost` seconds per decoded chunk; text appears after `text_after` chunks."""

    def __init__(self, clock, cpu, cost, text_after):
        self.clock, self.cpu, self.cost, self.text_after = clock, cpu, cost, text_after
        self.last_stream = None

    def create_stream(self):
        self.last_stream = FakeOnlineStream(self)
        return self.last_stream

    def is_ready(self, s):
        return s.pending > 0

    def decode_stream(self, s):
        s.pending -= 1
        self.clock.now += self.cost
        self.cpu.now += self.cost * 2

    def get_result_all(self, s):
        text = "готово" if s.chunks >= self.text_after else ""
        return type("R", (), {"text": text})()


def test_streaming_first_text_latency_when_compute_keeps_up():
    clock, cpu = FakeClock(), FakeClock()
    rec = FakeOnlineRecognizer(clock, cpu, cost=0.01, text_after=5)
    ad = StreamingAdapter(rec, feed_s=0.1, clock=clock, cpu_clock=cpu)
    t = ad.transcribe(_utt(seconds=1.0, onset=0.2))
    # chunk 5 ends at 0.5 s of audio, +0.01 s compute -> 0.51 s; minus onset 0.2
    assert t.first_text_s == pytest.approx(0.31)
    assert t.text == "готово"
    assert t.final_lag_s == pytest.approx(0.01)  # only the last chunk's compute
    assert ad.kind == "streaming"


def test_streaming_latency_grows_when_compute_cannot_keep_up():
    clock, cpu = FakeClock(), FakeClock()
    rec = FakeOnlineRecognizer(clock, cpu, cost=0.2, text_after=1)  # RTF 2
    ad = StreamingAdapter(rec, feed_s=0.1, clock=clock, cpu_clock=cpu)
    t = ad.transcribe(_utt(seconds=1.0, onset=0.0))
    assert t.first_text_s == pytest.approx(0.3)  # 0.1 audio + 0.2 compute
    assert t.final_lag_s == pytest.approx(1.1)   # done = 0.3 + 9 * 0.2 = 2.1 vs 1.0 audio end


def test_streaming_sets_language_and_pads():
    clock, cpu = FakeClock(), FakeClock()
    rec = FakeOnlineRecognizer(clock, cpu, cost=0.0, text_after=1)
    ad = StreamingAdapter(rec, feed_s=0.5, lead_s=0.3, tail_s=0.66, language="ru",
                          clock=clock, cpu_clock=cpu)
    ad.transcribe(_utt(seconds=1.0))
    s = rec.last_stream
    assert s.options == {"language": "ru"}
    assert s.finished
    assert s.chunks >= 3  # lead pad + 2 audio chunks (+ tail pad)
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd spikes/asr-model-bench && uv run pytest tests/test_adapters.py -q`
Expected: FAIL with `ModuleNotFoundError: No module named 'asrbench.adapters'`.

- [ ] **Step 3: Implement `adapters.py`**

```python
# Throwaway spike code (issue #116) — not part of the Sessio build.
import time
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from asrbench.datasets import Utterance


@dataclass(frozen=True)
class Transcript:
    text: str
    first_text_s: float | None
    final_lag_s: float
    compute_s: float
    cpu_s: float


def pick(directory: Path, stem: str) -> str:
    candidates = sorted(Path(directory).glob(f"{stem}*.onnx"))
    if not candidates:
        raise FileNotFoundError(f"no {stem}*.onnx in {directory}")
    for c in candidates:
        if "int8" in c.name:
            return str(c)
    return str(candidates[0])


def _text(recognizer, stream) -> str:
    getter = getattr(recognizer, "get_result_all", None) or recognizer.get_result
    result = getter(stream)
    return result.text if hasattr(result, "text") else str(result)


class OfflineAdapter:
    kind = "offline"

    def __init__(self, recognizer, clock=time.perf_counter, cpu_clock=time.process_time):
        self._rec = recognizer
        self._clock = clock
        self._cpu = cpu_clock

    def transcribe(self, utt: Utterance) -> Transcript:
        t0, c0 = self._clock(), self._cpu()
        stream = self._rec.create_stream()
        stream.accept_waveform(utt.sr, utt.samples)
        self._rec.decode_stream(stream)
        compute = self._clock() - t0
        return Transcript(stream.result.text, None, compute, compute, self._cpu() - c0)


class StreamingAdapter:
    kind = "streaming"

    def __init__(self, recognizer, *, feed_s=0.1, lead_s=0.0, tail_s=0.0,
                 language: str | None = None,
                 clock=time.perf_counter, cpu_clock=time.process_time):
        self._rec = recognizer
        self._feed_s = feed_s
        self._lead_s = lead_s
        self._tail_s = tail_s
        self._language = language
        self._clock = clock
        self._cpu = cpu_clock

    def transcribe(self, utt: Utterance) -> Transcript:
        rec, sr = self._rec, utt.sr
        stream = rec.create_stream()
        if self._language:
            stream.set_option("language", self._language)

        compute = 0.0
        cpu0 = self._cpu()
        done = 0.0          # real-time moment all fed audio has been processed
        audio_end = 0.0     # real-time moment the current chunk has fully arrived
        first_text: float | None = None

        def feed(chunk: np.ndarray, advances_audio: bool) -> None:
            nonlocal compute, done, audio_end, first_text
            if advances_audio:
                audio_end += len(chunk) / sr
            t0 = self._clock()
            stream.accept_waveform(sr, chunk)
            while rec.is_ready(stream):
                rec.decode_stream(stream)
            c = self._clock() - t0
            compute += c
            done = max(done, audio_end) + c
            if first_text is None and advances_audio and _text(rec, stream).strip():
                first_text = done - utt.onset_s

        if self._lead_s > 0:
            feed(np.zeros(int(self._lead_s * sr), np.float32), advances_audio=False)
        step = max(1, int(self._feed_s * sr))
        for i in range(0, len(utt.samples), step):
            feed(utt.samples[i:i + step], advances_audio=True)
        audio_total = audio_end
        if self._tail_s > 0:
            feed(np.zeros(int(self._tail_s * sr), np.float32), advances_audio=False)

        t0 = self._clock()
        stream.input_finished()
        while rec.is_ready(stream):
            rec.decode_stream(stream)
        c = self._clock() - t0
        compute += c
        done = max(done, audio_total) + c

        return Transcript(_text(rec, stream), first_text, max(0.0, done - audio_total),
                          compute, self._cpu() - cpu0)
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd spikes/asr-model-bench && uv run pytest tests/test_adapters.py -q`
Expected: `7 passed`. If `test_streaming_first_text_latency_when_compute_keeps_up` fails on `final_lag_s`, the cause is `input_finished` cost being added on top; with the fake it is 0, so `final_lag_s` is the last chunk's cost (0.01) because `done` after the last chunk is `1.0 + 0.01`.

- [ ] **Step 5: Commit**

```bash
git add spikes/asr-model-bench
git commit -m "feat(spike): offline/streaming recognizer adapters with real-time latency model"
```

---

### Task 4: Model registry, fetchers, and real-model smoke tests

**Files:**
- Create: `spikes/asr-model-bench/asrbench/models.py`
- Create: `spikes/asr-model-bench/asrbench/fetch.py`
- Test: `spikes/asr-model-bench/tests/test_smoke_models.py`

**Interfaces:**
- Consumes: `OfflineAdapter`, `StreamingAdapter`, `pick` from Task 3.
- Produces:
  - `MODEL_SPECS: dict[str, ModelSpec]` where `ModelSpec(id: str, archive: str, kind: str)`; `archive` is the directory/archive base name.
  - `DEFAULT_MODELS: list[str]` (the ids run by default).
  - `build(model_id: str, models_dir: Path, threads: int) -> OfflineAdapter | StreamingAdapter`.
  - CLI `python -m asrbench.fetch models [ids...]` and `python -m asrbench.fetch fleurs`.

Model ids and archives (all from the `asr-models` release; sizes are download sizes):

| id | archive | kind | size |
|---|---|---|---|
| `parakeet-v3` | `sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8` | offline | 487 MB |
| `gigaam-v3-rnnt` | `sherpa-onnx-nemo-transducer-giga-am-v3-russian-2025-12-16` | offline | 167 MB |
| `gigaam-v3-ctc` | `sherpa-onnx-nemo-ctc-giga-am-v3-russian-2025-12-16` | offline | 163 MB |
| `whisper-turbo` | `sherpa-onnx-whisper-turbo` | offline | 564 MB |
| `nemotron-160ms` | `sherpa-onnx-nemotron-3.5-asr-streaming-0.6b-160ms-int8-2026-06-11` | streaming | 475 MB |
| `nemotron-560ms` | `sherpa-onnx-nemotron-3.5-asr-streaming-0.6b-560ms-int8-2026-06-11` | streaming | 475 MB |
| `nemotron-1120ms` | `sherpa-onnx-nemotron-3.5-asr-streaming-0.6b-1120ms-int8-2026-06-11` | streaming | 475 MB |
| `t-one` | `sherpa-onnx-streaming-t-one-russian-2025-09-08` | streaming | 128 MB |

Total about 3.0 GB, plus FLEURS ru test audio (about 0.5 GB) and Podlodka (about 0.2 GB). The user approved the downloads.

- [ ] **Step 1: Write `models.py`**

```python
# Throwaway spike code (issue #116) — not part of the Sessio build.
from dataclasses import dataclass
from pathlib import Path

from asrbench.adapters import OfflineAdapter, StreamingAdapter, pick

DEFAULT_MODELS_DIR = Path(__file__).resolve().parents[1] / "models"


@dataclass(frozen=True)
class ModelSpec:
    id: str
    archive: str
    kind: str


_NEMOTRON = "sherpa-onnx-nemotron-3.5-asr-streaming-0.6b-{ms}ms-int8-2026-06-11"

MODEL_SPECS: dict[str, ModelSpec] = {s.id: s for s in [
    ModelSpec("parakeet-v3", "sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8", "offline"),
    ModelSpec("gigaam-v3-rnnt", "sherpa-onnx-nemo-transducer-giga-am-v3-russian-2025-12-16", "offline"),
    ModelSpec("gigaam-v3-ctc", "sherpa-onnx-nemo-ctc-giga-am-v3-russian-2025-12-16", "offline"),
    ModelSpec("whisper-turbo", "sherpa-onnx-whisper-turbo", "offline"),
    ModelSpec("nemotron-160ms", _NEMOTRON.format(ms=160), "streaming"),
    ModelSpec("nemotron-560ms", _NEMOTRON.format(ms=560), "streaming"),
    ModelSpec("nemotron-1120ms", _NEMOTRON.format(ms=1120), "streaming"),
    ModelSpec("t-one", "sherpa-onnx-streaming-t-one-russian-2025-09-08", "streaming"),
]}

DEFAULT_MODELS = list(MODEL_SPECS)


def build(model_id: str, models_dir: Path, threads: int):
    import sherpa_onnx  # imported lazily so unit tests do not need the wheel's shared libs

    spec = MODEL_SPECS[model_id]
    d = Path(models_dir) / spec.archive
    if not d.is_dir():
        raise FileNotFoundError(f"{d} missing; run: uv run python -m asrbench.fetch models {model_id}")

    if model_id == "parakeet-v3" or model_id == "gigaam-v3-rnnt":
        return OfflineAdapter(sherpa_onnx.OfflineRecognizer.from_transducer(
            encoder=pick(d, "encoder"), decoder=pick(d, "decoder"), joiner=pick(d, "joiner"),
            tokens=str(d / "tokens.txt"), num_threads=threads,
            decoding_method="greedy_search", model_type="nemo_transducer"))

    if model_id == "gigaam-v3-ctc":
        return OfflineAdapter(sherpa_onnx.OfflineRecognizer.from_nemo_ctc(
            model=pick(d, "model"), tokens=str(d / "tokens.txt"), num_threads=threads))

    if model_id == "whisper-turbo":
        return OfflineAdapter(sherpa_onnx.OfflineRecognizer.from_whisper(
            encoder=pick(d, "*-encoder"), decoder=pick(d, "*-decoder"),
            tokens=sorted(d.glob("*tokens.txt"))[0].as_posix(),
            language="ru", task="transcribe", num_threads=threads))

    if model_id.startswith("nemotron-"):
        rec = sherpa_onnx.OnlineRecognizer.from_transducer(
            encoder=pick(d, "encoder"), decoder=pick(d, "decoder"), joiner=pick(d, "joiner"),
            tokens=str(d / "tokens.txt"), num_threads=threads, decoding_method="greedy_search")
        return StreamingAdapter(rec, feed_s=0.1, language="ru")

    if model_id == "t-one":
        rec = sherpa_onnx.OnlineRecognizer.from_t_one_ctc(
            model=str(d / "model.onnx"), tokens=str(d / "tokens.txt"), num_threads=threads)
        return StreamingAdapter(rec, feed_s=0.1, lead_s=0.3, tail_s=0.66)

    raise KeyError(model_id)
```

- [ ] **Step 2: Write `fetch.py`**

```python
# Throwaway spike code (issue #116) — not part of the Sessio build.
import subprocess
import sys
from pathlib import Path

from asrbench.models import DEFAULT_MODELS_DIR, MODEL_SPECS

RELEASE = "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models"
FLEURS = "https://huggingface.co/datasets/google/fleurs/resolve/main/data/ru_ru"
DATA_DIR = Path(__file__).resolve().parents[1] / "data" / "fleurs-ru"
PODLODKA_DIR = Path(__file__).resolve().parents[1] / "data" / "podlodka"
PODLODKA = "https://huggingface.co/datasets/bond005/podlodka_speech/resolve/main/data"


def _curl(url: str, out: Path) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["curl", "-L", "--fail", "-C", "-", "-o", str(out), url], check=True)


def fetch_models(ids: list[str]) -> None:
    DEFAULT_MODELS_DIR.mkdir(parents=True, exist_ok=True)
    for model_id in ids:
        archive = MODEL_SPECS[model_id].archive
        target = DEFAULT_MODELS_DIR / archive
        if target.is_dir():
            print(f"{model_id}: already present")
            continue
        tarball = DEFAULT_MODELS_DIR / f"{archive}.tar.bz2"
        _curl(f"{RELEASE}/{archive}.tar.bz2", tarball)
        subprocess.run(["tar", "xjf", str(tarball), "-C", str(DEFAULT_MODELS_DIR)], check=True)
        tarball.unlink()


def fetch_fleurs() -> None:
    if (DATA_DIR / "test.tsv").is_file() and any(DATA_DIR.rglob("*.wav")):
        print("fleurs: already present")
        return
    _curl(f"{FLEURS}/test.tsv", DATA_DIR / "test.tsv")
    tarball = DATA_DIR / "test.tar.gz"
    _curl(f"{FLEURS}/audio/test.tar.gz", tarball)
    subprocess.run(["tar", "xzf", str(tarball), "-C", str(DATA_DIR)], check=True)
    tarball.unlink()


def fetch_podlodka() -> None:
    for split in ("test", "validation", "train"):
        target = PODLODKA_DIR / f"{split}.parquet"
        if not target.is_file():
            _curl(f"{PODLODKA}/{split}-00000-of-00001.parquet", target)


if __name__ == "__main__":
    what, *rest = sys.argv[1:] or ["models"]
    if what == "models":
        fetch_models(rest or list(MODEL_SPECS))
    elif what == "fleurs":
        fetch_fleurs()
    elif what == "podlodka":
        fetch_podlodka()
    else:
        sys.exit("usage: python -m asrbench.fetch (models [ids...] | fleurs | podlodka)")
```

- [ ] **Step 3: Download (after the user agrees to about 3.5 GB)**

```bash
cd spikes/asr-model-bench && uv run python -m asrbench.fetch fleurs && uv run python -m asrbench.fetch podlodka && uv run python -m asrbench.fetch models
ls models/*/ | head -60
```

Expected: each model directory exists; file names printed. If a model directory lacks the files `build()` expects (for example no `tokens.txt`, or the Whisper files do not match `*-encoder*.onnx` / `*-decoder*.onnx` / `*tokens.txt`), fix `build()` for that model only, using the real file names.

- [ ] **Step 4: Write the smoke test**

`tests/test_smoke_models.py`:

```python
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
```

- [ ] **Step 5: Run the smoke tests**

Run: `cd spikes/asr-model-bench && uv run pytest tests/test_smoke_models.py -v`
Expected: 8 passed. Triage failures per model:
- `OnlineRecognizer.from_transducer` rejects the Nemotron files, or text is empty: print `help(sherpa_onnx.OnlineRecognizer.from_transducer)` and the `model_type` options, and compare with `cxx-api-examples/streaming-nemotron-cxx-api.cc` in the sherpa-onnx repo (it uses default `model_type` and `stream.SetOption("language", ...)`).
- T-One returns empty text: check lead/tail padding against `python-api-examples/online-t-one-ctc-decode-files.py` (0.3 s lead, 0.66 s tail).
- WER above the floor for one model only: that is a finding, not a bug, unless the text is garbage (wrong tokens file or wrong language).
Do not weaken the assertion; fix the build function or record the model as unusable in the results document.

- [ ] **Step 6: Commit**

```bash
git add spikes/asr-model-bench
git commit -m "feat(spike): model registry, fetchers, and real-model smoke tests"
```

---

### Task 5: Benchmark runner (one process per model and dataset)

**Files:**
- Create: `spikes/asr-model-bench/asrbench/runner.py`
- Test: `spikes/asr-model-bench/tests/test_runner.py`

**Interfaces:**
- Consumes: `Utterance`, `with_babble`, `load_fleurs`, `load_own` (Task 2); `OfflineAdapter`/`StreamingAdapter`/`Transcript` (Task 3); `build`, `DEFAULT_MODELS_DIR`, `MODEL_SPECS` (Task 4).
- Produces:
  - `run_benchmark(adapter, utts: list[Utterance]) -> list[dict]`: one record per utterance with keys `id, ref, hyp, dur_s, compute_s, cpu_s, first_text_s, final_lag_s`.
  - Result file `results/<model>__<dataset>.json` with keys `model, kind, dataset, threads, load_s, peak_rss_mb, utts` (the list above).
  - CLI: `uv run python -m asrbench.runner --model <id> --dataset <fleurs-clean|fleurs-babble10|own> [--n 200] [--threads 4] [--own-dir PATH]`.

- [ ] **Step 1: Write the failing test**

`tests/test_runner.py`:

```python
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
```

- [ ] **Step 2: Run to verify failure**

Run: `cd spikes/asr-model-bench && uv run pytest tests/test_runner.py -q`
Expected: FAIL, `ModuleNotFoundError: No module named 'asrbench.runner'`.

- [ ] **Step 3: Implement `runner.py`**

```python
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
```

- [ ] **Step 4: Run tests, then a real 5-utterance dry run**

Run: `cd spikes/asr-model-bench && uv run pytest tests/test_runner.py -q`
Expected: `1 passed`.

Run: `cd spikes/asr-model-bench && uv run python -m asrbench.runner --model nemotron-560ms --dataset fleurs-clean --n 5`
Expected: `wrote .../results/nemotron-560ms__fleurs-clean.json (5 utterances, peak RSS ... MB)`. Open the JSON and confirm `first_text_s` is a positive number and `hyp` is Russian text.

- [ ] **Step 5: Commit**

```bash
git add spikes/asr-model-bench
git commit -m "feat(spike): benchmark runner producing per-model JSON results"
```

---

### Task 6: Report generator

**Files:**
- Create: `spikes/asr-model-bench/asrbench/report.py`
- Test: `spikes/asr-model-bench/tests/test_report.py`

**Interfaces:**
- Consumes: result JSON schema from Task 5; `wer`, `cer`, `percentile` from Task 1.
- Produces:
  - `summarize(run: dict) -> dict` with keys `model, kind, dataset, n, wer, cer, rtf, first_p50, first_p95, final_p50, final_p95, cpu_cores, peak_rss_mb`. `rtf = sum(compute_s) / sum(dur_s)`. `cpu_cores = sum(cpu_s) / sum(compute_s)` (average cores busy while decoding). Latency percentiles are `nan` when no data (offline models have no `first_text_s`).
  - `render_markdown(summaries: list[dict]) -> str`: one table per dataset, rows sorted by WER ascending.
  - CLI: `uv run python -m asrbench.report` prints Markdown for everything in `results/`.

- [ ] **Step 1: Write the failing test**

`tests/test_report.py`:

```python
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
```

- [ ] **Step 2: Run to verify failure**

Run: `cd spikes/asr-model-bench && uv run pytest tests/test_report.py -q`
Expected: FAIL, `ModuleNotFoundError: No module named 'asrbench.report'`.

- [ ] **Step 3: Implement `report.py`**

```python
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
```

- [ ] **Step 4: Run tests**

Run: `cd spikes/asr-model-bench && uv run pytest tests -q -m "not models"`
Expected: all non-model tests pass (about 28).

- [ ] **Step 5: Commit**

```bash
git add spikes/asr-model-bench
git commit -m "feat(spike): Markdown report generator for benchmark results"
```

---

### Task 7: Run the full benchmark and record results

**Files:**
- Create: `spikes/asr-model-bench/README.md`
- Create: `docs/asr-spike-results.md`

This task is mostly running commands and writing down what happened. Run models one at a time, with nothing else heavy running, so RTF is not distorted.

- [ ] **Step 1: Write the README**

`spikes/asr-model-bench/README.md`:

````markdown
# ASR model bench (throwaway spike, issue #116)

Compares open ASR models on Russian speech. See `docs/superpowers/plans/2026-10-06-realtime-asr-spike.md`.

```bash
uv sync --python 3.12
uv run python -m asrbench.fetch fleurs      # ~0.5 GB
uv run python -m asrbench.fetch podlodka    # ~0.2 GB, spontaneous podcast speech
uv run python -m asrbench.fetch models      # ~3 GB
uv run pytest -m "not models"               # unit tests
uv run pytest -m models                     # real-model smoke tests
uv run python -m asrbench.runner --model nemotron-560ms --dataset fleurs-clean --n 200
uv run python -m asrbench.report            # Markdown table from results/
```

Own recordings: put `name.wav` + `name.txt` pairs (one utterance, <= 25 s, reference text in the txt)
in a directory and pass `--dataset own --own-dir <dir>`. Never use real client sessions.
````

- [ ] **Step 2: Run the matrix**

```bash
cd spikes/asr-model-bench
for m in parakeet-v3 gigaam-v3-rnnt gigaam-v3-ctc whisper-turbo nemotron-160ms nemotron-560ms nemotron-1120ms t-one; do
  for d in fleurs-clean fleurs-babble10 podlodka; do
    uv run python -m asrbench.runner --model $m --dataset $d --n 200 --threads 4 || echo "FAILED $m $d"
  done
done
```

Expected: 24 `wrote ...` lines. Runtime is roughly 5 to 20 minutes per model per dataset, longest for Whisper. Any `FAILED` line: record the error in the results document and continue. If the user supplies own recordings, add `--dataset own --own-dir <dir>` runs for every model.

- [ ] **Step 3: Generate the table**

Run: `cd spikes/asr-model-bench && uv run python -m asrbench.report | tee /tmp/asr-report.md`
Expected: three tables (`fleurs-babble10`, `fleurs-clean`, `podlodka`) with 8 rows each.

- [ ] **Step 4: Write `docs/asr-spike-results.md`**

Structure (fill every section with real numbers from `/tmp/asr-report.md`; no section may stay empty):

```markdown
# Real-time ASR spike results (issue #116)

## Setup
Machine (CPU model from `lscpu`, cores, RAM), sherpa-onnx 1.13.8, threads=4, datasets (FLEURS ru test, 200 utterances ≤ 25 s sampled with seed 0; babble noise at 10 dB SNR from three other utterances), normalization rules and the no-ITN limitation.

## Results
The two tables from the report, verbatim.

## Latency definitions
first text, final lag, RTF, cores (copy the definitions from Task 3).

## Findings
One short paragraph per model: quality, speed, anything odd (failed to load, empty output, unstable).

## Decision
Name the primary live model and the post-session model, with the numbers that justify them.
Apply these gates to the live candidate (change a gate only by writing down why):
- streaming RTF ≤ 0.5 at 4 threads
- first-text latency p95 ≤ 1.5 s
- peak RSS ≤ 2000 MB
- clean-WER within 3 points of the best streaming model
List which gates each streaming model passes. Judge conversational quality mainly by the `podlodka` table (spontaneous speech), FLEURS is read speech.

## Not covered
Dialogue between two speakers over a video call (Podlodka is a podcast: studio-quality microphones, no codec), psychotherapy vocabulary, other OSes, GPU, Whisper via whisper.cpp, Voxtral, INT4/other quantizations, team-recorded `own` recordings (unused).

## Next steps
Concrete follow-up issues (integration into the call pipeline, language auto-detect, model download UX, licence review of OpenMDW/CC-BY-4.0).
```

- [ ] **Step 5: Commit**

```bash
git add spikes/asr-model-bench/README.md docs/asr-spike-results.md
git commit -m "docs: ASR spike results and decision for #116"
```

---

### Task 8: C++ smoke test for the winning streaming model

**Files:**
- Create: `spikes/asr-model-bench/cpp/CMakeLists.txt`
- Create: `spikes/asr-model-bench/cpp/nemotron_smoke.cc`

**Interfaces:**
- Consumes: the model directory under `spikes/asr-model-bench/models/` (use the streaming model chosen in Task 7; the code below targets a Nemotron package, which has `encoder.int8.onnx`, `decoder.int8.onnx`, `joiner.int8.onnx`, `tokens.txt`). If T-One won, use `config.model_config.t_one_ctc.model` instead, following `cxx-api-examples/` in the sherpa-onnx repo.
- Produces: an executable `asr_smoke <model_dir> <wav> [language]` that prints the text, duration, elapsed seconds, and RTF using the sherpa-onnx C++ API. Goal: show the same model and similar RTF through the API the Qt app would use.

- [ ] **Step 1: Download the prebuilt C++ library**

```bash
cd spikes/asr-model-bench && mkdir -p cpp/third_party && cd cpp/third_party
curl -L --fail -o sherpa.tar.bz2 \
  https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.13.8/sherpa-onnx-v1.13.8-linux-x64-shared-lib.tar.bz2
tar xjf sherpa.tar.bz2 && rm sherpa.tar.bz2 && ls
ls */lib */include/sherpa-onnx/c-api | head -30
```

Expected: a directory `sherpa-onnx-v1.13.8-linux-x64-shared-lib` with `lib/` (`libsherpa-onnx-c-api.so`, `libsherpa-onnx-cxx-api.so`, `libonnxruntime.so`) and `include/sherpa-onnx/c-api/cxx-api.h`. If the cxx-api library is named differently, adjust `CMakeLists.txt`. Add `cpp/third_party/` and `cpp/build/` to the spike `.gitignore`.

- [ ] **Step 2: Write `cpp/CMakeLists.txt`**

```cmake
# Throwaway spike code (issue #116) — not part of the Sessio build.
cmake_minimum_required(VERSION 3.28)
project(asr_smoke LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SHERPA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sherpa-onnx-v1.13.8-linux-x64-shared-lib")
add_executable(asr_smoke nemotron_smoke.cc)
target_include_directories(asr_smoke PRIVATE "${SHERPA_ROOT}/include")
target_link_directories(asr_smoke PRIVATE "${SHERPA_ROOT}/lib")
target_link_libraries(asr_smoke PRIVATE sherpa-onnx-cxx-api sherpa-onnx-c-api onnxruntime)
set_target_properties(asr_smoke PROPERTIES BUILD_RPATH "${SHERPA_ROOT}/lib")
```

- [ ] **Step 3: Write `cpp/nemotron_smoke.cc`**

```cpp
// Throwaway spike code (issue #116) — not part of the Sessio build.
// Streams one wav through sherpa-onnx's C++ API in 100 ms chunks and prints RTF.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>

#include "sherpa-onnx/c-api/cxx-api.h"

int main(int argc, char** argv) {
  using namespace sherpa_onnx::cxx;  // NOLINT
  if (argc < 3) {
    std::cerr << "usage: asr_smoke <model_dir> <wav> [language]\n";
    return 2;
  }
  const std::string dir = argv[1];
  const std::string language = argc > 3 ? argv[3] : "ru";

  OnlineRecognizerConfig config;
  config.model_config.transducer.encoder = dir + "/encoder.int8.onnx";
  config.model_config.transducer.decoder = dir + "/decoder.int8.onnx";
  config.model_config.transducer.joiner = dir + "/joiner.int8.onnx";
  config.model_config.tokens = dir + "/tokens.txt";
  config.model_config.num_threads = 4;

  OnlineRecognizer recognizer = OnlineRecognizer::Create(config);
  if (!recognizer.Get()) {
    std::cerr << "Failed to create recognizer; check model_dir\n";
    return 1;
  }
  Wave wave = ReadWave(argv[2]);
  if (wave.samples.empty()) {
    std::cerr << "Failed to read " << argv[2] << "\n";
    return 1;
  }

  OnlineStream stream = recognizer.CreateStream();
  stream.SetOption("language", language.c_str());

  const size_t step = static_cast<size_t>(wave.sample_rate / 10);  // 100 ms
  double compute = 0.0;
  for (size_t i = 0; i < wave.samples.size(); i += step) {
    const size_t n = std::min(step, wave.samples.size() - i);
    const auto t0 = std::chrono::steady_clock::now();
    stream.AcceptWaveform(wave.sample_rate, wave.samples.data() + i, n);
    while (recognizer.IsReady(&stream)) recognizer.Decode(&stream);
    compute += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  }
  const auto t0 = std::chrono::steady_clock::now();
  stream.InputFinished();
  while (recognizer.IsReady(&stream)) recognizer.Decode(&stream);
  compute += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

  const double duration = static_cast<double>(wave.samples.size()) / wave.sample_rate;
  std::cout << "text: " << recognizer.GetResult(&stream).text << "\n";
  std::printf("duration %.2fs  compute %.2fs  RTF %.3f\n", duration, compute, compute / duration);
  return 0;
}
```

- [ ] **Step 4: Build and run**

```bash
cd spikes/asr-model-bench/cpp
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel
WAV=$(find ../data/fleurs-ru -name '*.wav' | head -1)
./build/asr_smoke ../models/sherpa-onnx-nemotron-3.5-asr-streaming-0.6b-560ms-int8-2026-06-11 "$WAV" ru
```

Expected: a Russian `text:` line and an RTF close to the Python RTF for the same model (within about 30%). A large gap, or empty text, is a finding for the results document.

- [ ] **Step 5: Add the C++ result to `docs/asr-spike-results.md`**

Append a short "C++ API check" section: the command, the RTF, and whether it matches the Python run.

- [ ] **Step 6: Commit, push, and report on the issue**

```bash
git diff --check
git add spikes/asr-model-bench docs/asr-spike-results.md
git commit -m "feat(spike): C++ API smoke test for the chosen streaming ASR model"
git push -u origin spike/116-asr-model-bench
gh issue comment 116 --body "Spike finished. Results and decision: docs/asr-spike-results.md on branch spike/116-asr-model-bench."
```

Do not open a MR/PR and do not close the issue: the spike is an investigation, and the user decides what follows from `docs/asr-spike-results.md`.

---

## Self-Review

- **Spec coverage:** models requested (Parakeet v3, GigaAM and its newer v3, newer Nemotron, plus T-One and Whisper baseline) are in the registry (Task 4). Metrics the user asked about are WER, CER, first-text latency, final lag, RTF, CPU cores, and peak RSS (Tasks 3, 5, 6). Quality under noise is covered by babble (Task 2). Real conversational speech is covered only if the user supplies `own` recordings and is listed under "Not covered" otherwise. The C++ requirement is covered by Task 8. The decision gate is in Task 7.
- **Known risks recorded in the plan:** `OnlineRecognizer.from_transducer` for Nemotron and the Whisper file-name globs were checked against sherpa-onnx source examples but not run; Task 4 Step 5 is the explicit place they are verified and fixed. The OpenMDW licence of Nemotron needs a licence review before any shipping decision.
- **Type consistency:** `Utterance`, `Transcript`, `pick`, `OfflineAdapter`, `StreamingAdapter`, `build`, `run_benchmark`, `summarize`, `render_markdown` names and signatures match across tasks. The result JSON keys written in Task 5 match those read in Task 6.

## Deviations from this plan (recorded after execution)

- The conversational test set is `bond005/podlodka_speech` (public, hand-transcribed) instead of team recordings; the `own` loader stays available.
- `sherpa-onnx-core==1.13.8` is a required dependency: the `sherpa-onnx` wheel does not pull the native libraries by itself.
- Peak memory is measured as RSS growth over the level after the dataset was loaded (`RssPeak` in `runner.py`); `ru_maxrss` included dataset decoding.
- All audio is resampled to 16 kHz once at load time (`to_16k`), so recognizers do not pay for resampling inside the timed section.
- Added after the first results: `VadOfflineAdapter` (offline model behind Silero VAD), the `-vad` model ids, and the `podlodka-long` dataset, to measure phrase-mode latency and accuracy on continuous audio.
- Task 8 was done with GigaAM v3 + VAD instead of Nemotron, and found that the prebuilt Linux sherpa-onnx libraries use the old `std::string` ABI. Two working variants are in `cpp/`: the C API against the prebuilt libraries (`asr_smoke.cc`) and the C++ API with sherpa-onnx built through `FetchContent` (`from-source/`).
- The decision document is `docs/asciidoc/15-realtime-asr-spike.adoc` (AsciiDoc, generated tables from `report --asciidoc`) instead of `docs/asr-spike-results.md`.
