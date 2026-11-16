#!/usr/bin/env python3
"""Validate an exported JSONL dataset against the runtime prompt contract."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile

from check_prompt_contract import PROMPTS, check


def build_manifest(path: Path, seed: int):
    contract = json.loads((PROMPTS / "manifest.json").read_text(encoding="utf-8"))
    counts = Counter()
    with path.open(encoding="utf-8") as rows:
        for line_no, line in enumerate(rows, 1):
            if not line.strip():
                continue
            row = json.loads(line)
            task = row["task"]
            if task not in contract["tasks"]:
                raise ValueError(f"line {line_no}: unknown task {task}")
            messages = row["messages"]
            if not messages or messages[0]["role"] != "system":
                raise ValueError(f"line {line_no}: missing system message")
            digest = hashlib.sha256(messages[0]["content"].encode("utf-8")).hexdigest()[:16]
            if digest != contract["prompts"][task + ".txt"]:
                raise ValueError(f"line {line_no}: prompt differs for {task}")
            counts[task] += 1
    if set(counts) != set(contract["tasks"]):
        raise ValueError("dataset does not cover every task")
    return {"tasks": contract["tasks"], "prompts": contract["prompts"],
            "seed": seed, "volume": sum(counts.values()), "by_task": dict(counts)}


def validate_generator_source(tasks, source: Path):
    """Fail before teacher calls or writes if a generator uses an old contract."""
    check()
    contract = json.loads((PROMPTS / "manifest.json").read_text(encoding="utf-8"))
    if set(tasks) != set(contract["tasks"]):
        raise ValueError("generator task set differs from runtime contract")
    rows = json.loads(source.read_text(encoding="utf-8"))
    if not isinstance(rows, list) or not rows:
        raise ValueError("generator source must be a nonempty JSON array")
    seen = set()
    for row in rows:
        task = row["task"]
        if task not in contract["tasks"] or row["messages"][0]["role"] != "system":
            raise ValueError("generator source has unknown task or missing system message")
        digest = hashlib.sha256(row["messages"][0]["content"].encode("utf-8")).hexdigest()[:16]
        if digest != contract["prompts"][task + ".txt"]:
            raise ValueError(f"generator source prompt differs for {task}")
        seen.add(task)
    if seen != set(contract["tasks"]):
        raise ValueError("generator source does not cover every task")


def export_dataset(rows, destination: Path, seed: int):
    """Validate a temporary export before replacing data and its manifest."""
    check()
    destination = Path(destination)
    manifest_path = destination.with_name("manifest.json")
    if destination == manifest_path:
        raise ValueError("dataset and manifest must have different names")
    temp_path = None
    temp_manifest = None
    try:
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=destination.parent,
                                         prefix=".dataset-", delete=False) as temp:
            temp_path = Path(temp.name)
            for row in rows:
                temp.write(json.dumps(row, ensure_ascii=False) + "\n")
        manifest = build_manifest(temp_path, seed)
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=destination.parent,
                                         prefix=".manifest-", delete=False) as temp:
            temp_manifest = Path(temp.name)
            temp.write(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
        # Never leave an old manifest claiming to describe newly replaced data.
        manifest_path.unlink(missing_ok=True)
        os.replace(temp_path, destination)
        temp_path = None
        os.replace(temp_manifest, manifest_path)
        temp_manifest = None
    finally:
        if temp_path is not None:
            temp_path.unlink(missing_ok=True)
        if temp_manifest is not None:
            temp_manifest.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("dataset", type=Path)
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    check()
    manifest = build_manifest(args.dataset, args.seed)
    args.output.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, IndexError, TypeError) as error:
        print(f"Dataset manifest failed: {error}", file=sys.stderr)
        sys.exit(1)
