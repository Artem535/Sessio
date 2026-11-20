# -*- coding: utf-8 -*-
"""Offline gate for the passport resolver and cross-FAQ on the client case.

Embeds the spoken descriptions/questions of the reference utterance (no
project names, no question mark) with the same recipe as the app and ranks
them against passport/FAQ vectors from enriched.json. Prints top-3 with
cosine distances so the threshold and the top-2 ambiguity rule can be
calibrated before any GUI work. Run on Buran next to the GPU embedder.
"""
import json
import os
import urllib.request

import numpy as np

OLLAMA = os.environ.get("OLLAMA_URL", "http://127.0.0.1:11436/api/embed")
MODEL = "embeddinggemma-2:270m"
ENRICHED = os.path.expanduser(os.environ.get("ENRICHED", "~/rag-entities/enriched.json"))
PREFIX = "task: search result | query: "

DESCRIPTIONS = [
    "производитель удобрений",
    "сельскохозяйственный производитель",
    "производитель отделочных материалов",
    "строительный ретейл",
]
QUESTIONS = [
    "он-прем и облако на проектах",
    "он-прем модели на проектах",
    "какие он-прем модели использовались на проектах",
]
# Industries absent from the wiki: must stay beyond the resolver threshold.
NEGATIVES = [
    "сеть кофеен",
    "авиаперевозки и аэропорты",
    "металлургия и прокат чёрных металлов",
]


def embed(texts):
    out = []
    for s in range(0, len(texts), 64):
        payload = {"model": MODEL, "input": [PREFIX + t for t in texts[s:s + 64]], "keep_alive": "30m"}
        req = urllib.request.Request(OLLAMA, data=json.dumps(payload).encode(),
            headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=900) as r:
            out.extend(json.load(r)["embeddings"])
    return np.array(out, dtype=np.float32)


def distances(query, matrix):
    q = query / np.linalg.norm(query)
    m = matrix / np.linalg.norm(matrix, axis=1, keepdims=True)
    return 1.0 - m @ q


def report(title, queries, names, matrix, limit=0.5):
    print(f"== {title} ==", flush=True)
    vecs = embed(queries)
    for text, vec in zip(queries, vecs):
        d = distances(vec, matrix)
        order = np.argsort(d)[:3]
        line = " | ".join(f"{names[i]} {d[i]:.3f}" for i in order)
        flag = "OK " if d[order[0]] <= limit else "FAR"
        margin = d[order[1]] - d[order[0]] if len(order) > 1 else 1.0
        print(f"{flag} {text!r}: {line} | margin {margin:.3f}", flush=True)


def passport_text(p, target):
    if target == "key":
        return ". ".join(x for x in (p["industry"], p["client_type"], p["what_built"]) if x)
    return p["description"]


def main():
    enriched = json.load(open(ENRICHED))
    passports = enriched.get("passports", [])
    faq = enriched.get("faq", []) + enriched.get("faq_cross", [])
    if not passports:
        print("no passports in enriched.json", flush=True)
        return
    target = os.environ.get("TARGET", "description")
    if os.environ.get("REQUIRE_INDUSTRY"):
        passports = [p for p in passports if p["industry"].strip()]
    print(f"passports: {len(passports)}, target: {target}", flush=True)
    pnames = [p["name"] for p in passports]
    pmat = embed([passport_text(p, target) for p in passports])
    report("descriptions -> passports", DESCRIPTIONS, pnames, pmat)
    report("negatives (expect FAR)", NEGATIVES, pnames, pmat)
    if faq and not os.environ.get("SKIP_FAQ"):
        fnames = [f["q"] for f in faq]
        report("questions -> faq", QUESTIONS, fnames, embed([f["q"] for f in faq]), limit=0.35)


if __name__ == "__main__":
    main()
