# -*- coding: utf-8 -*-
"""Build the rag-core LadybugDB index v2 on Buran (GPU Ollama embeddings).

Schema mirrors src/rag/index_store.cpp exactly so the app can open the file:
Document(+doc_type,+primary_entity), Chunk, Entity(+primary_docs), Faq,
Passport. Enrichment (entities roles, doc_type, FAQ pairs, project passports,
cross-cutting FAQ) comes from enriched.json produced by extract_v2.py.
Vectors: Ollama embeddinggemma-2:270m in the GPU container (:11436); the app
embeds queries with LiteRT (sanity-validated pair). Token never logged.
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
OLLAMA = os.environ.get("OLLAMA_URL", "http://127.0.0.1:11436/api/embed")
MODEL = "embeddinggemma-2:270m"
DB_PATH = os.path.expanduser(os.environ.get("DB_PATH", "~/rag-index/index-v2.lbug"))
ENRICHED = os.path.expanduser(os.environ.get("ENRICHED", "~/rag-entities/enriched.json"))
TOKEN = os.environ["OUTLINE_API_TOKEN"]

QUERY_PREFIX = "task: search result | query: "
DOC_PREFIX = "title: none | text: "


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


def embed(prefixed):
    out = []
    for s in range(0, len(prefixed), 64):
        payload = {"model": MODEL, "input": prefixed[s:s + 64], "keep_alive": "30m"}
        req = urllib.request.Request(OLLAMA, data=json.dumps(payload).encode(),
            headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=900) as r:
            out.extend(json.load(r)["embeddings"])
        print(f"embed {min(s + 64, len(prefixed))}/{len(prefixed)}", flush=True)
    return np.array(out, dtype=np.float32)


def main():
    t0 = time.time()
    enriched = json.load(open(ENRICHED))
    doc_meta = {d["id"]: d for d in enriched["docs"]}
    coll = {c["id"]: c["name"] for c in fetch_all("collections.list")}
    docs = fetch_all("documents.list")
    print(f"fetched {len(docs)} docs, enriched {len(doc_meta)}", flush=True)

    pool, texts, meta = [], [], []
    for d in docs:
        text = d.get("text") or ""
        if not text.strip() or d.get("archivedAt"):
            continue
        chunks, _ = chunk_structural(d.get("title") or "", text)
        url = d.get("url") or ""
        if url.startswith("/"):
            url = BASE + url
        em = doc_meta.get(d["id"], {})
        info = {"id": d["id"], "title": d.get("title") or "", "url": url,
                "collection": coll.get(d.get("collectionId"), ""),
                "revision": int(d.get("revision") or 0),
                "docType": em.get("doc_type") or "other", "primaryEntity": em.get("primary") or ""}
        for c in chunks:
            pool.append((info, c["text"]))
            texts.append(DOC_PREFIX + chunk_text(c, info["title"]))
        meta.append(info)
    print(f"chunked: {len(pool)} chunks from {len(meta)} docs", flush=True)

    faq = enriched.get("faq", []) + [dict(f, docId="", url="") for f in enriched.get("faq_cross", [])]
    # Resolver targets are the compact industry keys, not the long description:
    # gate-calibrated 0.11-0.22 for client-case descriptions vs 0.25+ for absent
    # industries. Passports without an industry are internal pages, not projects.
    passports = [p for p in enriched.get("passports", []) if p["industry"].strip()]
    faq_texts = [QUERY_PREFIX + f["q"] for f in faq]
    passport_texts = [QUERY_PREFIX + ". ".join(x for x in (p["industry"], p["client_type"], p["what_built"]) if x)
                      for p in passports]
    cache = os.path.expanduser("~/rag-index/vecs-v2.npz")
    counts = (len(texts), len(faq), len(passports))
    vecs = None
    if os.path.exists(cache):
        with np.load(cache) as z:
            if tuple(int(x) for x in z["counts"]) == counts:
                vecs = z["vecs"]
                print(f"vectors from cache {cache}", flush=True)
    if vecs is None:
        vecs = embed(texts + faq_texts + passport_texts)
        np.savez(cache, vecs=vecs, counts=np.array(counts))
    chunk_vecs, faq_vecs, passport_vecs = vecs[:len(texts)], vecs[len(texts):len(texts) + len(faq)], vecs[len(texts) + len(faq):]
    print(f"embedded in {time.time() - t0:.0f}s total", flush=True)

    os.makedirs(os.path.dirname(DB_PATH), exist_ok=True)
    db = lb.Database(DB_PATH)
    conn = lb.Connection(db)
    conn.execute("INSTALL fts")
    conn.execute("INSTALL vector")
    conn.execute("LOAD EXTENSION fts")
    conn.execute("LOAD EXTENSION vector")
    conn.execute("CREATE NODE TABLE Document(id STRING PRIMARY KEY, title STRING, url STRING, "
                 "collection STRING, revision INT64, doc_type STRING, primary_entity STRING)")
    conn.execute("CREATE NODE TABLE Chunk(id SERIAL PRIMARY KEY, docId STRING, text STRING, embedding FLOAT[768])")
    conn.execute("CREATE NODE TABLE Entity(name STRING PRIMARY KEY, kind STRING, docs STRING, primary_docs STRING)")
    conn.execute("CREATE NODE TABLE Faq(id SERIAL PRIMARY KEY, docId STRING, question STRING, answer STRING, "
                 "url STRING, embedding FLOAT[768])")
    conn.execute("CREATE NODE TABLE Passport(name STRING PRIMARY KEY, industry STRING, client_type STRING, "
                 "what_built STRING, deployment STRING, description STRING, docId STRING, url STRING, "
                 "aliases STRING, models STRING, embedding FLOAT[768])")
    conn.execute("CALL CREATE_FTS_INDEX('Chunk','chunk_fts',['text'],stemmer := 'russian')")
    conn.execute("CALL CREATE_VECTOR_INDEX('Chunk','chunk_vec','embedding',metric := 'cosine')")
    conn.execute("CALL CREATE_VECTOR_INDEX('Faq','faq_vec','embedding',metric := 'cosine')")
    for info in meta:
        conn.execute("CREATE (d:Document {id:$id, title:$title, url:$url, collection:$collection, "
                     "revision:$revision, doc_type:$docType, primary_entity:$primaryEntity})", info)
    ins = conn.prepare("CREATE (c:Chunk {docId:$docId, text:$text, embedding:$emb})")
    for i, ((info, text), vec) in enumerate(zip(pool, chunk_vecs)):
        conn.execute(ins, {"docId": info["id"], "text": text, "emb": vec.tolist()})
        if (i + 1) % 1000 == 0:
            print(f"inserted {i + 1}/{len(pool)}", flush=True)
    # Entity PK is the name: the same name extracted as project and product
    # must merge into one row (kind priority project > client > product).
    merged, priority = {}, {"project": 0, "client": 1, "product": 2}
    for e in enriched.get("entities", []):
        entry = merged.setdefault(e["name"], {"kind": e["kind"], "docs": set(), "primaryDocs": set()})
        if priority.get(e["kind"], 3) < priority.get(entry["kind"], 3):
            entry["kind"] = e["kind"]
        entry["docs"].update(e["docs"])
        entry["primaryDocs"].update(e["primaryDocs"])
    for name, e in merged.items():
        conn.execute("CREATE (e:Entity {name:$name, kind:$kind, docs:$docs, primary_docs:$primaryDocs})",
                     {"name": name, "kind": e["kind"], "docs": ",".join(sorted(e["docs"])),
                      "primaryDocs": ",".join(sorted(e["primaryDocs"]))})
    fins = conn.prepare("CREATE (f:Faq {docId:$docId, question:$question, answer:$answer, url:$url, embedding:$emb})")
    for f, vec in zip(faq, faq_vecs):
        conn.execute(fins, {"docId": f["docId"], "question": f["q"], "answer": f["a"],
                            "url": f["url"], "emb": vec.tolist()})
    pins = conn.prepare("CREATE (p:Passport {name:$name, industry:$industry, client_type:$clientType, "
                        "what_built:$whatBuilt, deployment:$deployment, description:$description, "
                        "docId:$docId, url:$url, aliases:$aliases, models:$models, embedding:$emb})")
    for p, vec in zip(passports, passport_vecs):
        conn.execute(pins, {"name": p["name"], "industry": p["industry"], "clientType": p["client_type"],
                            "whatBuilt": p["what_built"], "deployment": p["deployment"],
                            "description": p["description"], "docId": p["docId"], "url": p["url"],
                            "aliases": "\n".join(p["aliases"]), "models": "\n".join(p["models"]),
                            "emb": vec.tolist()})
    print(f"DONE {len(pool)} chunks, {len(meta)} docs, {len(enriched.get('entities', []))} entities, "
          f"{len(faq)} faq, {len(passports)} passports, {time.time() - t0:.0f}s -> {DB_PATH}", flush=True)


if __name__ == "__main__":
    sys.exit(main())
