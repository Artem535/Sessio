# Throwaway spike code (issue #116) — not part of the Sessio build.
import numpy as np
import soundfile as sf

from asrbench.audio import babble, first_speech_s, mix_at_snr, read_wav, rms, to_16k

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


def test_to_16k_resamples_48k_and_keeps_duration_and_level():
    sr = 48000
    t = np.arange(sr) / sr
    tone = (0.1 * np.sin(2 * np.pi * 440 * t)).astype(np.float32)
    out, out_sr = to_16k(tone, sr)
    assert out_sr == 16000 and out.shape == (16000,) and out.dtype == np.float32
    assert abs(rms(out) - rms(tone)) < 5e-3


def test_to_16k_is_identity_at_16k():
    x = np.ones(10, np.float32)
    out, out_sr = to_16k(x, 16000)
    assert out is x and out_sr == 16000
