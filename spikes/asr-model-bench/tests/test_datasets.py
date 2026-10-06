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
