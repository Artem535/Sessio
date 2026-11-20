# -*- coding: utf-8 -*-
"""Enriched wiki extraction via the VSellm teacher router (external API).

One pass produces: entities with roles (primary = the document is ABOUT it,
mention = merely referenced), doc_type, primary entity per document, grounded
FAQ pairs, project passports (a description collection for the
description->project resolver) and cross-cutting FAQ over the passport set.
Output enriched.json:
  {"entities": [{name, kind, docs, primaryDocs}],
   "docs": [{id, title, url, doc_type, primary}],
   "faq": [{q, a, docId, url}],
   "passports": [{name, aliases, industry, client_type, what_built, deployment,
                  models, description, docId, url}],
   "faq_cross": [{q, a}]}

VSellm quirks: response_format json_object only (no json_schema), thinking is
a native top-level param and MUST be disabled (thinking tokens eat max_tokens).
The wiki holds client names, so every outbound text is pseudonymized with the
local NER dictionary and restored afterwards; the mapping never leaves memory.
Outline token from OUTLINE_API_TOKEN env; VSellm key from VSELLM_API_KEY env or
the local dataset notebook. Neither is ever logged.
"""
import concurrent.futures as cf
import json
import os
import re
import time
import urllib.request

BASE = os.environ.get("OUTLINE_BASE", "").rstrip("/")
if not BASE:
    raise SystemExit("set OUTLINE_BASE to the Outline instance URL")
API = BASE + "/api/"
VLLM = os.environ.get("VLLM_URL", "https://api.vsellm.ru/v1/chat/completions")
MODEL = os.environ.get("VLLM_MODEL", "deepseek/deepseek-v4.1-flash")
TOKEN = os.environ["OUTLINE_API_TOKEN"]
OUT = os.path.expanduser("~/rag-entities/enriched.json")
NER_DICT = os.path.expanduser(os.environ.get("NER_DICT", "~/rag-entities/entities.json"))


def vsellm_key():
    key = os.environ.get("VSELLM_API_KEY")
    if key:
        return key
    notebook = os.path.expanduser("~/Projects/sessio-dataset/sessio_v5.ipynb")
    with open(notebook) as f:
        text = f.read()
    match = re.search(r"VSELLM_API_KEY[\"']?\s*[:=]\s*[\"']([A-Za-z0-9_\-\.]{20,})", text)
    if not match:
        match = re.search(r"sk-[A-Za-z0-9_\-]{20,}", text)
    if not match:
        raise RuntimeError("vsellm key not found")
    return match.group(1) if match.lastindex else match.group(0)


KEY = vsellm_key()

SYSTEM = (
    "Analyze a corporate wiki document (Russian sales/presale wiki). Reply with JSON only, keys: "
    '"projects" (list of project/code names), "clients" (list of client company names), '
    '"products" (list of product/service names), '
    '"doc_type" (exactly one of: prd, kp, case, meeting, faq, process, other), '
    '"primary" (the single project or client name the document is ABOUT; empty string if none), '
    '"faq" (up to 3 objects {"q", "a"}: REAL questions a client could ask that this document answers, '
    "with short concrete answers grounded strictly in the document text, in Russian; never use ellipsis "
    'or placeholder text; if the document answers nothing, return an empty list). '
    "The text may contain placeholders like [P12] or [C3]: these ARE company/project names, "
    "copy them exactly as written. Names exactly as written. No duplicates."
)

PASSPORT_SYSTEM = (
    "Write a project passport for a corporate sales/presale wiki (Russian). Reply with JSON only, keys: "
    '"aliases" (other names/code names of this project in the texts), '
    '"industry" (short Russian phrase: what the client company does, e.g. "производитель удобрений"), '
    '"client_type" (short Russian: производитель / ретейл / дистрибьютор / банк / ... or ""), '
    '"what_built" (short Russian: what system was built for them), '
    '"deployment" (exactly one of: on-prem, cloud, mixed, unknown), '
    '"models" (list of model names used, empty if not stated), '
    '"description" (2-4 dense Russian sentences combining industry, client type, what was built, '
    "deployment and models; this text is matched against spoken descriptions like "
    '"производитель удобрений" or "строительный ретейл"). '
    "Ground every field strictly in the provided texts; unknown fields stay empty or \"unknown\", "
    "never invent. Placeholders like [P12] or [C3] ARE names: copy them exactly."
)

CROSS_SYSTEM = (
    "You get one line per project: name | industry | client_type | deployment | models. "
    "Reply with JSON only: {\"faq\": [{\"q\", \"a\"}]} with up to 6 cross-project questions a sales "
    "person could ask (e.g. \"на каких проектах было он-прем развёртывание моделей\", "
    "\"где обращались к облачным моделям\", \"какие он-прем модели использовались на проектах\"). "
    "Answers in Russian: a short summary listing the projects and the fact per project, strictly "
    "from the lines; if no line carries the fact, answer \"нет данных в wiki\". No invented facts. "
    "Placeholders like [P12] or [C3] ARE project names: copy them exactly."
)


def build_pseudonymizer():
    entries = json.load(open(NER_DICT))
    pairs, seen = [], set()
    counters = {"project": 0, "client": 0, "product": 0}
    prefixes = {"project": "P", "client": "C", "product": "R"}
    for entry in sorted(entries, key=lambda e: -len(e["name"])):
        name, kind = entry["name"], entry.get("kind", "project")
        if kind not in counters or len(name) < 3 or name in seen:
            continue
        if re.fullmatch(r"[\d\+\-\(\) ]{6,}", name) or "@" in name:
            continue
        seen.add(name)
        counters[kind] += 1
        pairs.append((name, f"[{prefixes[kind]}{counters[kind]}]"))
    patterns = [(re.compile(r"(?<!\w)" + re.escape(name) + r"(?!\w)", re.IGNORECASE), token)
                for name, token in pairs]
    reverse = {token: name for name, token in pairs}

    def apply(text):
        for pattern, token in patterns:
            text = pattern.sub(token, text)
        return text

    def restore(text):
        for token, name in reverse.items():
            text = text.replace(token, name)
        return text

    return apply, restore


pseud, unpseud = build_pseudonymizer()


def vllm(system, user, max_tokens):
    payload = {
        "model": MODEL,
        "messages": [{"role": "system", "content": system},
                     {"role": "user", "content": pseud(user.replace("\t", " "))}],
        "temperature": 0,
        "max_tokens": max_tokens,
        "response_format": {"type": "json_object"},
        "thinking": {"type": "disabled"},
    }
    for attempt in range(4):
        req = urllib.request.Request(VLLM, data=json.dumps(payload).encode(),
            headers={"Content-Type": "application/json", "Authorization": "Bearer " + KEY}, method="POST")
        try:
            with urllib.request.urlopen(req, timeout=300) as r:
                choice = json.load(r)["choices"][0]
            break
        except urllib.error.HTTPError as e:
            if e.code not in (429, 500, 502, 503) or attempt == 3:
                raise
            time.sleep(float(e.headers.get("Retry-After", 5)) * (attempt + 1))
        except urllib.error.URLError:
            if attempt == 3:
                raise
            time.sleep(2 * (attempt + 1))
    content = choice["message"]["content"] or ""
    try:
        return json.loads(content)
    except json.JSONDecodeError:
        start, end = content.find("{"), content.rfind("}")
        if start >= 0 and end > start:
            return json.loads(content[start:end + 1])
        raise


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
        except urllib.error.URLError:
            if attempt == 3:
                raise
            time.sleep(2 * (attempt + 1))


def fetch_all(method):
    items, offset = [], 0
    while True:
        data = call(method, {"limit": 100, "offset": offset})
        batch = data["data"]
        items.extend(batch)
        offset += len(batch)
        if not batch or offset >= data["pagination"]["total"]:
            return items


def names(value):
    out = []
    for item in value or []:
        name = item.get("name") if isinstance(item, dict) else item
        if isinstance(name, str) and name.strip():
            out.append(unpseud(name.strip()))
    return out


def analyze(doc):
    found = vllm(SYSTEM, "Document title: " + (doc.get("title") or "")
                 + "\nDocument text:\n" + (doc.get("text") or "")[:3000], 1200)
    url = doc.get("url") or ""
    if url.startswith("/"):
        url = BASE + url
    meta = {"id": doc["id"], "title": unpseud(doc.get("title") or ""), "url": url,
            "doc_type": found.get("doc_type") or "other",
            "primary": unpseud((found.get("primary") or "").strip())}
    if meta["doc_type"] not in ("prd", "kp", "case", "meeting", "faq", "process", "other"):
        meta["doc_type"] = "other"
    entities = []
    for kind, key in (("project", "projects"), ("client", "clients"), ("product", "products")):
        for name in names(found.get(key)):
            entities.append({"name": name, "kind": kind, "doc": doc["id"],
                             "role": "primary" if name == meta["primary"] else "mention"})
    faq = []
    for item in found.get("faq") or []:
        if isinstance(item, dict) and item.get("q") and item.get("a"):
            q, a = unpseud(str(item["q"]).strip()), unpseud(str(item["a"]).strip())
            if q in ("...", "…") or a in ("...", "…"):
                continue
            faq.append({"q": q, "a": a, "docId": doc["id"], "url": url})
    return meta, entities, faq[:3]


def passport(entity, docs_by_id):
    sources = []
    for doc_id in entity["primaryDocs"][:2]:
        doc = docs_by_id.get(doc_id)
        if doc:
            sources.append("[" + (doc.get("title") or "") + "]\n" + (doc.get("text") or "")[:2500])
    if not sources:
        return None
    found = vllm(PASSPORT_SYSTEM, "Project name: " + entity["name"]
                 + "\nSource documents:\n" + "\n\n".join(sources), 900)
    description = unpseud((found.get("description") or "").strip())
    # A placeholder or stub description is useless as a resolver target.
    if len(description) < 40 or description in ("...", "…"):
        return None
    doc_id = entity["primaryDocs"][0]
    url = (docs_by_id.get(doc_id) or {}).get("url") or ""
    if url.startswith("/"):
        url = BASE + url
    return {"name": entity["name"],
            "aliases": names(found.get("aliases")),
            "industry": unpseud((found.get("industry") or "").strip()),
            "client_type": unpseud((found.get("client_type") or "").strip()),
            "what_built": unpseud((found.get("what_built") or "").strip()),
            "deployment": found.get("deployment") if found.get("deployment") in
                          ("on-prem", "cloud", "mixed", "unknown") else "unknown",
            "models": names(found.get("models")),
            "description": description,
            "docId": doc_id, "url": url}


def cross_faq(passports):
    lines = [" | ".join([p["name"], p["industry"], p["client_type"], p["deployment"],
                         ", ".join(p["models"])]) for p in passports]
    found = vllm(CROSS_SYSTEM, "\n".join(lines), 1600)
    out = []
    for item in found.get("faq") or []:
        if isinstance(item, dict) and item.get("q") and item.get("a"):
            q, a = unpseud(str(item["q"]).strip()), unpseud(str(item["a"]).strip())
            if q in ("...", "…") or a in ("...", "…"):
                continue
            out.append({"q": q, "a": a})
    return out[:6]


def main():
    t0 = time.time()
    docs = [d for d in fetch_all("documents.list")
            if (d.get("text") or "").strip() and not d.get("archivedAt")]
    docs_by_id = {d["id"]: d for d in docs}
    print(f"fetched {len(docs)} docs", flush=True)
    metas, entity_rows, faqs = [], [], []
    failed = 0
    with cf.ThreadPoolExecutor(max_workers=8) as pool:
        futures = {pool.submit(analyze, d): d for d in docs}
        for i, future in enumerate(cf.as_completed(futures)):
            try:
                meta, entities, faq = future.result()
            except Exception as e:
                failed += 1
                print(f"doc failed: {type(e).__name__}", flush=True)
                continue
            metas.append(meta)
            entity_rows.extend(entities)
            faqs.extend(faq)
            if (i + 1) % 50 == 0:
                print(f"processed {i + 1}/{len(docs)}, entities {len(entity_rows)}, faq {len(faqs)}, "
                      f"{time.time() - t0:.0f}s", flush=True)
    entities = {}
    for row in entity_rows:
        entry = entities.setdefault((row["name"], row["kind"]),
                                    {"name": row["name"], "kind": row["kind"], "docs": [], "primaryDocs": []})
        if row["doc"] not in entry["docs"]:
            entry["docs"].append(row["doc"])
        if row["role"] == "primary" and row["doc"] not in entry["primaryDocs"]:
            entry["primaryDocs"].append(row["doc"])
    projects = [e for e in entities.values() if e["kind"] == "project" and e["primaryDocs"]]
    print(f"passports: {len(projects)} projects with primary docs", flush=True)
    passports, pfailed = [], 0
    with cf.ThreadPoolExecutor(max_workers=8) as pool:
        futures = {pool.submit(passport, e, docs_by_id): e for e in projects}
        for i, future in enumerate(cf.as_completed(futures)):
            try:
                p = future.result()
            except Exception as e:
                pfailed += 1
                print(f"passport failed: {type(e).__name__}", flush=True)
                continue
            if p:
                passports.append(p)
            if (i + 1) % 25 == 0:
                print(f"passports {i + 1}/{len(projects)}, {time.time() - t0:.0f}s", flush=True)
    passports.sort(key=lambda p: p["name"])
    faq_cross = cross_faq(passports) if passports else []
    out = {"entities": sorted(entities.values(), key=lambda e: (e["kind"], e["name"])),
           "docs": metas, "faq": faqs, "passports": passports, "faq_cross": faq_cross}
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
    print(f"DONE {len(out['entities'])} entities, {len(metas)} docs, {len(faqs)} faq, "
          f"{len(passports)} passports, {len(faq_cross)} cross-faq, "
          f"{failed} doc failed, {pfailed} passport failed -> {OUT} in {time.time() - t0:.0f}s", flush=True)


if __name__ == "__main__":
    main()
