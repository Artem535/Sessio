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
