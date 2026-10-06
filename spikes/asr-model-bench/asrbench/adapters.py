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
