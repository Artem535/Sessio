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
