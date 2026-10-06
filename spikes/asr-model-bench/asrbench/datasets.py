# Throwaway spike code (issue #116) — not part of the Sessio build.
import io
import random
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import soundfile as sf

from asrbench.audio import babble, first_speech_s, mix_at_snr, read_wav, to_16k


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
        samples, sr = to_16k(samples, int(sr))
        out.append(Utterance(id_, samples, sr, text.strip(), first_speech_s(samples, sr)))
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
