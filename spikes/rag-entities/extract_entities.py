# -*- coding: utf-8 -*-
"""NER over the Outline wiki with a local GPU model (no cloud: the wiki holds
client names, anonymization would destroy the entities we need).

Runs on Buran against Ollama :11435 (qwen3.5:9b-bf16). Output entities.json:
[{"name": ..., "kind": "project|client|product", "docs": [docId, ...]}].
Token from OUTLINE_API_TOKEN env, never logged.
"""
import json
import os
import time
import urllib.request

BASE = os.environ.get("OUTLINE_BASE", "").rstrip("/")
if not BASE:
    raise SystemExit("set OUTLINE_BASE to the Outline instance URL")
API = BASE + "/api/"
OLLAMA = os.environ.get("OLLAMA_URL", "http://127.0.0.1:11435/api/generate")
MODEL = "lfm3:latest"
TOKEN = os.environ["OUTLINE_API_TOKEN"]
OUT = os.path.expanduser("~/rag-entities/entities.json")

SYSTEM = (
    "Extract named entities from a corporate wiki document. Reply with JSON "
    '{"projects": [...], "clients": [...], "products": [...]} containing exact '
    "names as written in the document (project/code names, client companies, "
    "products/services). Empty arrays when none. No duplicates."
)


def call(method, payload):
    req = urllib.request.Request(API + method, data=json.dumps(payload).encode(),
        headers={"Authorization": "Bearer " + TOKEN, "Content-Type": "application/json"}, method="POST")
    for attempt in range(4):
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                return json.load(r)
        except urllib.error.HTTPError as e:
            if e.code != 429 or attempt == 3:
                raise
            time.sleep(float(e.headers.get("Retry-After", 5)))


def fetch_all(method):
    items, offset = [], 0
    while True:
        data = call(method, {"limit": 100, "offset": offset})
        batch = data["data"]
        items.extend(batch)
        offset += len(batch)
        if not batch or offset >= data["pagination"]["total"]:
            return items


def parse_reply(reply):
    # lfm3 may wrap JSON in prose or fences despite format=json.
    try:
        return json.loads(reply)
    except json.JSONDecodeError:
        pass
    start, end = reply.find("{"), reply.rfind("}")
    if start >= 0 and end > start:
        try:
            return json.loads(reply[start:end + 1])
        except json.JSONDecodeError:
            pass
    raise json.JSONDecodeError("no json object", reply, 0)


def extract(title, text):
    payload = {
        "model": MODEL,
        "system": SYSTEM,
        "prompt": "Document title: " + title + "\nDocument text:\n" + text[:3000],
        "format": "json",
        "stream": False,
        "keep_alive": "30m",
        "options": {"temperature": 0, "num_ctx": 4096},
    }
    req = urllib.request.Request(OLLAMA, data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=300) as r:
        reply = json.load(r)["response"]
    return parse_reply(reply)


def main():
    t0 = time.time()
    docs = fetch_all("documents.list")
    print(f"fetched {len(docs)} docs", flush=True)
    entities = {}
    for i, d in enumerate(docs):
        text = d.get("text") or ""
        if not text.strip() or d.get("archivedAt"):
            continue
        try:
            found = extract(d.get("title") or "", text)
        except Exception as e:
            print(f"doc {i} extract failed: {type(e).__name__}", flush=True)
            continue
        for kind, key in (("project", "projects"), ("client", "clients"), ("product", "products")):
            for item in found.get(key, []):
                # lfm3 returns objects {"name", "description"}; others strings.
                name = item.get("name") if isinstance(item, dict) else item
                if not isinstance(name, str) or not name.strip():
                    continue
                name = name.strip()
                entry = entities.setdefault((name, kind), {"name": name, "kind": kind, "docs": []})
                if d["id"] not in entry["docs"]:
                    entry["docs"].append(d["id"])
        if (i + 1) % 50 == 0:
            print(f"processed {i + 1}/{len(docs)}, entities {len(entities)}, {time.time() - t0:.0f}s", flush=True)
    out = sorted(entities.values(), key=lambda e: (e["kind"], e["name"]))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
    print(f"DONE {len(out)} entities -> {OUT} in {time.time() - t0:.0f}s", flush=True)


if __name__ == "__main__":
    main()
