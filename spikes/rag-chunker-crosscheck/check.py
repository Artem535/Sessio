# -*- coding: utf-8 -*-
"""Fidelity check: compare the C++ chunker port against the Python spike on
deterministic synthetic documents. Requires the archived spike file
(outline-chunker-spike.py) and a compiled ./crosscheck binary next to it.

Usage: python3 check.py <spike.py> <workdir>
"""
import importlib.util
import json
import os
import random
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SPIKE = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser(
    "~/.codex/visualizations/2026/11/11/01a1cb80-69dd-7ba2-8a59-359a55c0989a/outline-chunker-spike.py")
WORK = sys.argv[2] if len(sys.argv) > 2 else HERE
spec = importlib.util.spec_from_file_location("spike", SPIKE)
spike = importlib.util.module_from_spec(spec)
spec.loader.exec_module(spike)

rng = random.Random(42)
WORDS = ["клиент", "договор", "статус", "пилот", "кейс", "alpha", "beta", "gamma",
         "интеграция", "отчёт", "встреча", "задача", "срок", "оплата", "этап"]


def words(n):
    return " ".join(rng.choice(WORDS) for _ in range(n))


def gen_block():
    kind = rng.choice(["heading", "para", "table", "list", "code", "quote", "bigpara", "bigtable"])
    if kind == "heading":
        return "#" * rng.randint(1, 4) + " " + words(3).title()
    if kind == "para":
        return words(rng.randint(5, 60))
    if kind == "bigpara":
        return words(rng.randint(300, 500))
    if kind == "table":
        rows = rng.randint(1, 8)
        return "| Колонка A | Колонка B |\n| --- | --- |\n" + "\n".join(
            f"| {words(2)} | {words(2)} |" for _ in range(rows))
    if kind == "bigtable":
        rows = rng.randint(30, 60)
        return "| Name | Value |\n| --- | --- |\n" + "\n".join(
            f"| row{i} | {words(12)} |" for i in range(rows))
    if kind == "list":
        items = []
        for _ in range(rng.randint(1, 6)):
            items.append("- " + words(4))
            if rng.random() < 0.5:
                items.append("  " + words(6))
        return "\n".join(items)
    if kind == "code":
        lines = ["```"]
        for _ in range(rng.randint(1, 5)):
            lines.append(rng.choice(["echo hello", "| pipe | line |", "- dash line", "x = 1"]))
        lines.append("```")
        return "\n".join(lines)
    return "\n".join("> " + words(5) for _ in range(rng.randint(1, 3)))


def gen_doc(i):
    title = f"Synthetic Doc {i} " + words(2)
    blocks = [gen_block() for _ in range(rng.randint(3, 25))]
    return title, "\n\n".join(blocks) + "\n"


def write_str(f, s):
    b = s.encode("utf-8")
    f.write(struct.pack("<I", len(b)))
    f.write(b)


def read_str(f):
    head = f.read(4)
    if len(head) < 4:
        raise EOFError
    (n,) = struct.unpack("<I", head)
    return f.read(n).decode("utf-8")


docs = [gen_doc(i) for i in range(200)]
with open(WORK + "/input.bin", "wb") as f:
    for title, text in docs:
        write_str(f, title)
        write_str(f, text)

subprocess.run([HERE + "/crosscheck", WORK + "/input.bin", WORK + "/output.bin"], check=True)

mismatches = 0
with open(WORK + "/output.bin", "rb") as f:
    for i, (title, text) in enumerate(docs):
        expected, _ = spike.chunk_structural(title, text)
        (n,) = struct.unpack("<I", f.read(4))
        got = []
        for _ in range(n):
            ctext = read_str(f)
            path = read_str(f).split("\n")
            kinds = read_str(f).split(",")
            got.append({"text": ctext, "path": path, "kinds": kinds})
        if got != expected:
            mismatches += 1
            print(f"DOC {i} MISMATCH: {len(got)} vs {len(expected)} chunks")
            for j, (g, e) in enumerate(zip(got, expected)):
                if g != e:
                    print(f"  chunk {j}:\n   got={json.dumps(g, ensure_ascii=False)[:300]}\n"
                          f"   exp={json.dumps(e, ensure_ascii=False)[:300]}")
                    break
print(f"{len(docs)} docs, {mismatches} mismatches")
sys.exit(1 if mismatches else 0)
