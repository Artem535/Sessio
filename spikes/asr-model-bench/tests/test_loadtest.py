# Throwaway spike code (issue #116) — not part of the Sessio build.
import time

import numpy as np

from asrbench.loadtest import mute_blocks, run_loadtest

SR = 16000


class FakeStream:
    def __init__(self):
        self.result = type("R", (), {"text": ""})()

    def accept_waveform(self, sr, samples):
        pass


class FakeRecognizer:
    def create_stream(self):
        return FakeStream()

    def decode_stream(self, stream):
        time.sleep(0.01)
        stream.result.text = "слова"


class FakeVad:
    """Emits one 0.1 s segment once `trigger` windows have been fed; another one on flush."""

    def __init__(self, trigger=4):
        self.trigger, self.windows, self.queue = trigger, 0, []

    def accept_waveform(self, window):
        self.windows += 1
        if self.windows == self.trigger:
            self._push(0)

    def _push(self, start):
        self.queue.append(type("Seg", (), {"start": start, "samples": np.ones(1600, np.float32)})())

    def empty(self):
        return not self.queue

    @property
    def front(self):
        return self.queue[0]

    def pop(self):
        self.queue.pop(0)

    def flush(self):
        self._push(8000)


def test_run_loadtest_shares_one_recognizer_across_tracks():
    tracks = [np.zeros(SR // 2, np.float32) for _ in range(3)]
    result = run_loadtest(FakeRecognizer(), lambda: (FakeVad(), 512), tracks, SR)
    assert result["tracks"] == 3
    assert result["phrases"] == 6                       # two phrases per track
    assert result["phrase_lag_p50_s"] > 0
    assert result["phrase_lag_p95_s"] >= result["phrase_lag_p50_s"]
    assert result["decode_total_s"] >= 0.06 - 1e-6      # 6 decodes of 10 ms
    assert result["cpu_cores_avg"] >= 0


def test_mute_blocks_keeps_only_assigned_blocks():
    x = np.ones(SR * 40, np.float32)                    # four 10 s blocks
    out = mute_blocks(x, SR, track=1, n_tracks=4, speakers=1)
    assert out[SR * 10: SR * 20].all() and not out[:SR * 10].any()
    assert not out[SR * 20:].any()
