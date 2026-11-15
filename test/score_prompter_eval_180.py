"""Score synthetic outputs; lexical matches are screening, not semantic recall."""
import argparse
from collections import Counter
import json
from pathlib import Path


def normal(text):
    return text.casefold().replace("ё", "е")


def ratio(n, d):
    return round(n / d, 4) if d else None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("fixture", type=Path)
    parser.add_argument("results", type=Path)
    args = parser.parse_args()
    fixture = json.loads(args.fixture.read_text(encoding="utf-8"))
    cases = {case["id"]: case for case in fixture["key_points"] + fixture["dialogues"]}
    rows = [json.loads(line) for line in args.results.read_text(encoding="utf-8").splitlines() if line]
    ids = [row["id"] for row in rows]
    if len(ids) != len(set(ids)) or not set(ids) <= set(cases):
        raise ValueError("duplicate or unknown result IDs")
    counts = Counter()
    note_lengths, overview_lengths = [], []
    for row in rows:
        case = cases[row["id"]]
        counts["calls"] += row["calls"]
        counts["json_objects"] += row["json_objects"]
        counts["pipeline_errors"] += int("error" in row)
        answer = row.get("answer", {})
        allowed = {u["id"] for u in case["utterances"]}
        note_lengths += row["leaf_note_lengths_utf16"]
        if row["task"] == "key_points":
            items = answer.get("key_points", [])
            if case["gold"]["abstain"]:
                counts["abstain_cases"] += 1
                counts["abstain_correct"] += int("error" not in row and not items)
            else:
                counts["positive_cases"] += 1
                combined = normal(" ".join(item.get("text", "") for item in items))
                counts["lexical_recalled"] += int("error" not in row and all(normal(t) in combined for t in case["gold"]["terms"]))
        else:
            overview = answer.get("overview", {})
            if overview:
                overview_lengths.append(len(overview.get("text", "").encode("utf-16-le")) // 2)
            items = [overview] + sum((answer.get(k, []) for k in ("key_points", "tasks", "open_questions")), [])
            counts["dialogues_scored"] += 1
            combined = normal(" ".join(item.get("text", "") for item in items))
            counts["final_value_mentioned"] += int("error" not in row and normal(case["gold"]["final_value"]) in combined)
            counts["superseded_value_mentioned"] += int("error" not in row and normal(case["gold"]["superseded_value"]) in combined)
        for item in items:
            if not item or not item.get("text", "").strip():
                continue
            counts["evidence_items"] += 1
            ids = item.get("evidence_ids", [])
            counts["valid_evidence_items"] += int(bool(ids) and len(ids) == len(set(ids)) and set(ids) <= allowed)
    lengths = lambda xs: {"count": len(xs), "min": min(xs), "max": max(xs),
                          "mean": round(sum(xs) / len(xs), 1)} if xs else {"count": 0}
    print(json.dumps({
        "cases_completed": len(rows), "cases_expected": len(cases), "counts": counts,
        "json_object_rate_per_call": ratio(counts["json_objects"], counts["calls"]),
        "valid_evidence_rate_final_items": ratio(counts["valid_evidence_items"], counts["evidence_items"]),
        "lexical_recall_proxy": ratio(counts["lexical_recalled"], counts["positive_cases"]),
        "abstain_accuracy_on_negative_cases": ratio(counts["abstain_correct"], counts["abstain_cases"]),
        "final_value_mention_rate": ratio(counts["final_value_mentioned"], counts["dialogues_scored"]),
        "superseded_value_mention_rate_not_contradiction_rate": ratio(counts["superseded_value_mentioned"], counts["dialogues_scored"]),
        "leaf_note_lengths_utf16": lengths(note_lengths), "overview_lengths_utf16": lengths(overview_lengths),
        "limitations": "Lexical screening is not semantic fact recall; superseded-value mentions are not necessarily contradictions. Evidence checks cover emitted items, not rejected raw replies. Review manually."
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
