# Throwaway spike code (issue #116) — not part of the Sessio build.
"""Real-time load test: N audio tracks, one VAD per track, one shared recognizer, one decode worker.

Each track is fed in real time (100 ms chunks, paced by the wall clock) from its own thread, as
LiveKit would deliver it. Closed phrases go to a common queue; a single worker decodes them in
order with the shared recognizer. Phrase lag is measured against the wall-clock moment the speaker
stopped.
"""
import argparse
import json
import queue
import threading
import time
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from asrbench.metrics import percentile
from asrbench.runner import RESULTS_DIR, RssPeak


@dataclass
class _Phrase:
    track: int
    samples: np.ndarray
    speech_end_wall: float
    arrived_wall: float


def mute_blocks(samples: np.ndarray, sr: int, track: int, n_tracks: int, speakers: int,
                block_s: float = 10.0) -> np.ndarray:
    """Keep only the blocks where `track` is one of `speakers` people talking at the same time."""
    out = np.zeros_like(samples)
    block = int(block_s * sr)
    for b, start in enumerate(range(0, len(samples), block)):
        if ((b - track) % n_tracks) < speakers:
            out[start:start + block] = samples[start:start + block]
    return out


def run_loadtest(recognizer, vad_factory, tracks: list[np.ndarray], sr: int = 16000,
                 feed_s: float = 0.1) -> dict:
    n = len(tracks)
    pending: queue.Queue = queue.Queue()
    records: list[dict] = []
    stop_worker = object()
    cpu0 = time.process_time()
    t0 = time.perf_counter() + 0.2  # common start, leaves time for threads to spin up

    def worker() -> None:
        while True:
            item = pending.get()
            if item is stop_worker:
                return
            started = time.perf_counter()
            stream = recognizer.create_stream()
            stream.accept_waveform(sr, item.samples)
            recognizer.decode_stream(stream)
            done = time.perf_counter()
            records.append({
                "track": item.track,
                "text": stream.result.text.strip(),
                "phrase_lag_s": done - item.speech_end_wall,
                "queue_wait_s": started - item.arrived_wall,
                "decode_s": done - started,
                "audio_s": len(item.samples) / sr,
            })

    def feeder(idx: int) -> None:
        vad, window = vad_factory()
        samples = tracks[idx]
        step = max(1, int(feed_s * sr))
        buf = np.zeros(0, np.float32)

        def drain() -> None:
            while not vad.empty():
                seg = vad.front
                data = np.asarray(seg.samples, dtype=np.float32)
                end_wall = t0 + (seg.start + len(data)) / sr
                vad.pop()
                pending.put(_Phrase(idx, data, end_wall, time.perf_counter()))

        for k, i in enumerate(range(0, len(samples), step)):
            delay = t0 + (k + 1) * feed_s - time.perf_counter()
            if delay > 0:
                time.sleep(delay)
            buf = np.concatenate([buf, samples[i:i + step]])
            while len(buf) >= window:
                vad.accept_waveform(buf[:window])
                buf = buf[window:]
            drain()
        vad.flush()
        drain()

    w = threading.Thread(target=worker)
    w.start()
    feeders = [threading.Thread(target=feeder, args=(i,)) for i in range(n)]
    for f in feeders:
        f.start()
    for f in feeders:
        f.join()
    audio_end_wall = time.perf_counter()
    pending.put(stop_worker)
    w.join()
    wall = time.perf_counter() - t0
    drain_after_audio_s = time.perf_counter() - audio_end_wall

    lags = [r["phrase_lag_s"] for r in records if r["text"]]
    waits = [r["queue_wait_s"] for r in records]
    return {
        "tracks": n,
        "audio_s_per_track": max(len(t) for t in tracks) / sr,
        "phrases": len(records),
        "phrase_lag_p50_s": percentile(lags, 50), "phrase_lag_p95_s": percentile(lags, 95),
        "phrase_lag_max_s": max(lags) if lags else float("nan"),
        "queue_wait_p95_s": percentile(waits, 95),
        "decode_total_s": sum(r["decode_s"] for r in records),
        "backlog_after_audio_s": drain_after_audio_s,
        "cpu_cores_avg": (time.process_time() - cpu0) / wall if wall > 0 else float("nan"),
        "wall_s": wall,
    }


def main() -> None:
    from asrbench.datasets import load_podlodka_long
    from asrbench.fetch import PODLODKA_DIR
    from asrbench.models import DEFAULT_MODELS_DIR, _vad_factory, build

    ap = argparse.ArgumentParser()
    ap.add_argument("--tracks", type=int, required=True)
    ap.add_argument("--seconds", type=float, default=60.0)
    ap.add_argument("--speakers", type=int, default=0,
                    help="0 = everybody talks all the time; k = k people talk at once")
    ap.add_argument("--threads", type=int, default=4)
    args = ap.parse_args()

    longs = load_podlodka_long(PODLODKA_DIR)
    if args.tracks > len(longs):
        raise SystemExit(f"only {len(longs)} distinct recordings available")
    sr = longs[0].sr
    tracks = [u.samples[: int(args.seconds * sr)] for u in longs[: args.tracks]]
    if args.speakers:
        tracks = [mute_blocks(t, sr, i, args.tracks, args.speakers) for i, t in enumerate(tracks)]

    with RssPeak() as rss:
        adapter = build("gigaam-v3-rnnt", DEFAULT_MODELS_DIR, args.threads)
        result = run_loadtest(adapter._rec, _vad_factory(), tracks, sr)
    result["rss_growth_mb"] = rss.delta_mb
    result["speakers_at_once"] = args.speakers or args.tracks
    scenario = "all" if not args.speakers else f"turns{args.speakers}"
    out = Path(RESULTS_DIR) / f"loadtest__{args.tracks}tracks__{scenario}.json"
    out.parent.mkdir(exist_ok=True)
    out.write_text(json.dumps(result, ensure_ascii=False, indent=1), encoding="utf-8")
    print(json.dumps({k: (round(v, 3) if isinstance(v, float) else v) for k, v in result.items()}))


if __name__ == "__main__":
    main()
