import importlib.util
import json
import subprocess
import tempfile
from pathlib import Path
import unittest

DATA = Path(__file__).parent / "data"
spec = importlib.util.spec_from_file_location("fixture", DATA / "build_prompter_eval_180.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PrompterEvaluationFixtureTest(unittest.TestCase):
    def test_frozen_fixture_matches_generator_and_has_expected_cases(self):
        data = json.loads((DATA / "prompter_eval_180.json").read_text(encoding="utf-8"))
        self.assertEqual(data, module.build())
        self.assertEqual(len(data["key_points"]), 40)
        self.assertEqual(len(data["dialogues"]), 30)
        self.assertEqual(sum(case["gold"]["abstain"] for case in data["key_points"]), 8)
        ids = set()
        for case in data["key_points"] + data["dialogues"]:
            self.assertNotIn(case["id"], ids)
            ids.add(case["id"])
            sources = {entry["id"] for entry in case["utterances"]}
            self.assertTrue(set(case["gold"]["evidence_ids"]) <= sources)
            for entry in case["utterances"]:
                self.assertLessEqual(entry["start_ms"], entry["end_ms"])

    def test_scoring_counts_errors_as_missed_and_checks_evidence(self):
        rows = [
            {"id": "kp-01", "task": "key_points", "calls": 1, "json_objects": 1,
             "leaf_note_lengths_utf16": [], "answer": {"key_points": [
                 {"text": "запуск в понедельник", "evidence_ids": ["foreign"]}]}},
            {"id": "kp-33", "task": "key_points", "calls": 1, "json_objects": 0,
             "leaf_note_lengths_utf16": [], "error": "inference failed"},
        ]
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "results.jsonl"
            path.write_text("".join(json.dumps(row) + "\n" for row in rows))
            output = subprocess.check_output(
                ["python3", str(DATA.parent / "score_prompter_eval_180.py"),
                 str(DATA / "prompter_eval_180.json"), str(path)], text=True
            )
        report = json.loads(output)
        self.assertEqual(report["lexical_recall_proxy"], 1.0)
        self.assertEqual(report["abstain_accuracy_on_negative_cases"], 0.0)
        self.assertEqual(report["valid_evidence_rate_final_items"], 0.0)
        self.assertEqual(report["json_object_rate_per_call"], 0.5)


if __name__ == "__main__":
    unittest.main()
