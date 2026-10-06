# Throwaway spike code (issue #116) — not part of the Sessio build.
import numpy as np
import pytest

from asrbench.adapters import OfflineAdapter, StreamingAdapter, VadOfflineAdapter, pick
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


# ---- VAD + offline ---------------------------------------------------------
class FakeVad:
    """Emits one segment (start, length in samples) once `trigger` windows were fed."""

    def __init__(self, trigger, start, length):
        self.trigger, self.start, self.length = trigger, start, length
        self.windows = 0
        self.queue = []
        self.flushed = False

    def accept_waveform(self, window):
        self.windows += 1
        if self.windows == self.trigger:
            seg = type("Seg", (), {"start": self.start, "samples": np.ones(self.length, np.float32)})()
            self.queue.append(seg)

    def empty(self):
        return not self.queue

    @property
    def front(self):
        return self.queue[0]

    def pop(self):
        self.queue.pop(0)

    def flush(self):
        self.flushed = True


def test_vad_adapter_reports_phrase_latency_from_end_of_speech():
    clock, cpu = FakeClock(), FakeClock()
    rec = FakeOfflineRecognizer(clock, cpu)
    vad = FakeVad(trigger=10, start=1600, length=3200)  # speech ends at 0.3 s, VAD emits at 0.4 s
    ad = VadOfflineAdapter(rec, lambda: (vad, 512), feed_s=0.1, clock=clock, cpu_clock=cpu)
    t = ad.transcribe(_utt(seconds=2.0, onset=0.2))
    assert t.text == "привет мир"
    assert t.phrase_lags == (pytest.approx(0.61),)   # 0.4 emitted + 0.51 decode - 0.3 end of speech
    assert t.first_text_s == pytest.approx(0.71)     # 0.91 - onset 0.2
    assert t.final_lag_s == pytest.approx(0.0)       # decode finished long before audio ended
    assert vad.flushed and ad.kind == "offline+vad"


def test_vad_adapter_without_segments_returns_empty_text():
    clock, cpu = FakeClock(), FakeClock()
    vad = FakeVad(trigger=10 ** 9, start=0, length=1)
    ad = VadOfflineAdapter(FakeOfflineRecognizer(clock, cpu), lambda: (vad, 512),
                           clock=clock, cpu_clock=cpu)
    t = ad.transcribe(_utt(seconds=1.0))
    assert t.text == "" and t.phrase_lags == () and t.first_text_s is None
