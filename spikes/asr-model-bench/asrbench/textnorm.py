# Throwaway spike code (issue #116) — not part of the Sessio build.
import re

_NON_WORD = re.compile(r"[^\w]+", re.UNICODE)


def normalize(text: str) -> str:
    text = text.lower().replace("ё", "е")
    text = text.replace("_", " ")
    return _NON_WORD.sub(" ", text).strip()
