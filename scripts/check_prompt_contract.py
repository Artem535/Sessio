#!/usr/bin/env python3
"""Check the versioned runtime prompt contract without modifying datasets."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "call_prompter"
PROMPTS = SOURCE / "prompts"
MANIFEST = PROMPTS / "manifest.json"


def check(dataset_manifest=None):
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    expected = manifest["prompts"]
    files = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()[:16]
             for p in PROMPTS.glob("*.txt")}
    prompt_lines = {line.strip() for path in PROMPTS.glob("*.txt")
                    for line in path.read_text(encoding="utf-8").splitlines()
                    if len(line.strip()) >= 40 and not line.lstrip().startswith("<")}
    calls = set()
    for path in SOURCE.rglob("*.cpp"):
        text = path.read_text(encoding="utf-8")
        if (re.search(r'<(?:role|task|input_format|reasoning|output)>', text)
                or re.search(r'\b\w*[Pp]rompt\w*\s*=\s*(?:QStringLiteral\s*\(|R")', text)
                or any(line in text for line in prompt_lines)):
            raise ValueError(f"inline prompt marker in {path.relative_to(ROOT)}")
        calls.update(re.findall(r'\bprompt\s*\(\s*"([^"\n]+\.txt)"\s*\)', text))
    if set(manifest["tasks"]) != {name.removesuffix(".txt") for name in files}:
        raise ValueError("task list differs from prompt files")
    if calls != set(files):
        raise ValueError(f"prompt calls differ from files: calls={sorted(calls)}, files={sorted(files)}")
    if expected != files:
        raise ValueError("prompt hashes differ from manifest")
    if dataset_manifest is not None:
        dataset = json.loads(dataset_manifest.read_text(encoding="utf-8"))
        if (set(dataset["tasks"]) != set(manifest["tasks"]) or dataset["prompts"] != expected
                or type(dataset["seed"]) is not int or type(dataset["volume"]) is not int
                or dataset["volume"] <= 0 or set(dataset["by_task"]) != set(manifest["tasks"])
                or any(type(n) is not int or n <= 0 for n in dataset["by_task"].values())
                or sum(dataset["by_task"].values()) != dataset["volume"]):
            raise ValueError("dataset manifest differs from runtime contract or lacks seed/volume")
    print(f"Prompt contract OK: {len(files)} tasks")


if __name__ == "__main__":
    try:
        parser = argparse.ArgumentParser()
        parser.add_argument("--dataset-manifest", type=Path)
        check(parser.parse_args().dataset_manifest)
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"Prompt contract failed: {error}", file=sys.stderr)
        sys.exit(1)
