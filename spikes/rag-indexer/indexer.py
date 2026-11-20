# -*- coding: utf-8 -*-
"""Build the rag-core LadybugDB index on Buran (GPU Ollama embeddings).

Schema mirrors src/rag/index_store.cpp exactly so the app can open the file.
Vectors come from Ollama embeddinggemma-2:270m (GGUF Q8) — the same recipe as
the archived spike; the app embeds queries with the LiteRT runtime (the pair
is sanity-validated). Token comes from OUTLINE_API_TOKEN env, never logged.
"""
import json
import os
import sys
import time
import urllib.request

import ladybug as lb
import numpy as np

from outline_chunker_spike import chunk_structural

BASE = os.environ.get("OUTLINE_BASE", "").rstrip("/")
if not BASE:
    raise SystemExit("set OUTLINE_BASE to the Outline instance URL")
API = BASE + "/api/"
OLLAMA = "http://127.0.0.1:11435/api/embed"
MODEL = "embeddinggemma-2:270m"
DB_PATH = os.path.expanduser("~/rag-index/index.lbug")
TOKEN = os.environ["OUTLINE_API_TOKEN"]


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


def chunk_text(c, title):
    return title + "\n" + " > ".join(c["path"][1:] if len(c["path"]) > 1 else []) + "\n" + c["text"]


def embed(texts):
    out = []
    for s in range(0, len(texts), 64):
        payload = {"model": MODEL, "input": ["title: none | text: " + t for t in texts[s:s + 64]], "keep_alive": "30m"}
        req = urllib.request.Request(OLLAMA, data=json.dumps(payload).encode(),
            headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=900) as r:
            out.extend(json.load(r)["embeddings"])
        print(f"embed {min(s + 64, len(texts))}/{len(texts)}", flush=True)
    return np.array(out, dtype=np.float32)


def main():
    t0 = time.time()
    coll = {c["id"]: c["name"] for c in fetch_all("collections.list")}
    docs = fetch_all("documents.list")
    print(f"fetched {len(docs)} docs in {time.time() - t0:.0f}s", flush=True)

    pool, texts, meta = [], [], []
    for d in docs:
        text = d.get("text") or ""
        if not text.strip() or d.get("archivedAt"):
            continue
        chunks, _ = chunk_structural(d.get("title") or "", text)
        url = d.get("url") or ""
        if url.startswith("/"):
            url = BASE + url
        info = {"id": d["id"], "title": d.get("title") or "", "url": url,
                "collection": coll.get(d.get("collectionId"), ""), "revision": int(d.get("revision") or 0)}
        for c in chunks:
            pool.append((info, c["text"]))
            texts.append(chunk_text(c, info["title"]))
        meta.append(info)
    print(f"chunked: {len(pool)} chunks from {len(meta)} docs", flush=True)

    vecs = embed(texts)
    print(f"embedded in {time.time() - t0:.0f}s total", flush=True)

    os.makedirs(os.path.dirname(DB_PATH), exist_ok=True)
    db = lb.Database(DB_PATH)
    conn = lb.Connection(db)
    conn.execute("INSTALL fts")
    conn.execute("INSTALL vector")
    conn.execute("LOAD EXTENSION fts")
    conn.execute("LOAD EXTENSION vector")
    conn.execute("CREATE NODE TABLE Document(id STRING PRIMARY KEY, title STRING, url STRING, collection STRING, revision INT64)")
    conn.execute("CREATE NODE TABLE Chunk(id SERIAL PRIMARY KEY, docId STRING, text STRING, embedding FLOAT[768])")
    conn.execute("CALL CREATE_FTS_INDEX('Chunk','chunk_fts',['text'],stemmer := 'russian')")
    conn.execute("CALL CREATE_VECTOR_INDEX('Chunk','chunk_vec','embedding',metric := 'cosine')")
    for info in meta:
        conn.execute("CREATE (d:Document {id:$id, title:$title, url:$url, collection:$collection, revision:$revision})",
                     info)
    ins = conn.prepare("CREATE (c:Chunk {docId:$docId, text:$text, embedding:$emb})")
    for i, ((info, text), vec) in enumerate(zip(pool, vecs)):
        conn.execute(ins, {"docId": info["id"], "text": text, "emb": vec.tolist()})
        if (i + 1) % 1000 == 0:
            print(f"inserted {i + 1}/{len(pool)}", flush=True)
    print(f"DONE {len(pool)} chunks, {len(meta)} docs, {time.time() - t0:.0f}s -> {DB_PATH}", flush=True)


if __name__ == "__main__":
    sys.exit(main())
