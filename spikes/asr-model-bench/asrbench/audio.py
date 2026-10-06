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
