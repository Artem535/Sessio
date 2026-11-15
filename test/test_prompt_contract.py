import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import importlib.util
import sys

ROOT = Path(__file__).resolve().parents[1]


class PromptContractTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "scripts").mkdir()
        (self.root / "src").mkdir()
        shutil.copy(ROOT / "scripts/check_prompt_contract.py", self.root / "scripts")
        shutil.copy(ROOT / "scripts/write_dataset_manifest.py", self.root / "scripts")
        shutil.copytree(ROOT / "src/call_prompter", self.root / "src/call_prompter")

    def run_check(self):
        return subprocess.run(
            ["python3", str(self.root / "scripts/check_prompt_contract.py")],
            capture_output=True, text=True, check=False
        )

    def test_accepts_current_contract(self):
        self.assertEqual(self.run_check().returncode, 0)

    def test_rejects_changed_prompt(self):
        path = self.root / "src/call_prompter/prompts/extract.txt"
        path.write_bytes(path.read_bytes() + b"\nchange\n")
        self.assertNotEqual(self.run_check().returncode, 0)

    def test_rejects_missing_prompt(self):
        (self.root / "src/call_prompter/prompts/extract.txt").unlink()
        self.assertNotEqual(self.run_check().returncode, 0)

    def test_rejects_reference_to_missing_file(self):
        path = self.root / "src/call_prompter/extra.cpp"
        path.write_text('void f() { prompt("missing.txt"); }')
        self.assertNotEqual(self.run_check().returncode, 0)

    def test_rejects_wrong_task_list(self):
        path = self.root / "src/call_prompter/prompts/manifest.json"
        manifest = json.loads(path.read_text())
        manifest["tasks"].pop()
        path.write_text(json.dumps(manifest))
        self.assertNotEqual(self.run_check().returncode, 0)

    def test_rejects_inline_prompt(self):
        path = self.root / "src/call_prompter/inline.cpp"
        path.write_text('const auto kSuggestPrompt = QStringLiteral("Hello");')
        self.assertNotEqual(self.run_check().returncode, 0)

    def test_rejects_copied_prompt_text_with_unrelated_variable_name(self):
        line = (self.root / "src/call_prompter/prompts/extract.txt").read_text().splitlines()[1]
        (self.root / "src/call_prompter/inline.cpp").write_text('const char *text = "' + line + '";')
        self.assertNotEqual(self.run_check().returncode, 0)

    def test_dataset_manifest_rejects_mismatch_and_records_seed_and_volume(self):
        prompts = self.root / "src/call_prompter/prompts"
        rows = [{"task": path.stem, "messages": [{"role": "system", "content": path.read_text()}]}
                for path in prompts.glob("*.txt")]
        dataset = self.root / "dataset.jsonl"
        output = self.root / "dataset-manifest.json"
        def run_writer():
            return subprocess.run(
                ["python3", str(self.root / "scripts/write_dataset_manifest.py"), str(dataset),
                 "--seed", "17", "--output", str(output)],
                capture_output=True, text=True, check=False
            )
        dataset.write_text("".join(json.dumps(row) + "\n" for row in rows))
        self.assertEqual(run_writer().returncode, 0)
        manifest = json.loads(output.read_text())
        self.assertEqual(manifest["seed"], 17)
        self.assertEqual(manifest["volume"], 6)
        checker = self.root / "scripts/check_prompt_contract.py"
        self.assertEqual(subprocess.run(
            ["python3", str(checker), "--dataset-manifest", str(output)], capture_output=True
        ).returncode, 0)
        manifest["prompts"]["extract.txt"] = "0" * 16
        output.write_text(json.dumps(manifest))
        self.assertNotEqual(subprocess.run(
            ["python3", str(checker), "--dataset-manifest", str(output)], capture_output=True
        ).returncode, 0)
        rows[0]["messages"][0]["content"] += "changed"
        dataset.write_text("".join(json.dumps(row) + "\n" for row in rows))
        output.unlink()
        self.assertNotEqual(run_writer().returncode, 0)
        self.assertFalse(output.exists())

    def test_generator_export_is_guarded_and_writes_matching_manifest(self):
        sys.modules.pop("check_prompt_contract", None)
        sys.path.insert(0, str(self.root / "scripts"))
        self.addCleanup(sys.path.remove, str(self.root / "scripts"))
        spec = importlib.util.spec_from_file_location("dataset_manifest_test", self.root / "scripts/write_dataset_manifest.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        prompts = self.root / "src/call_prompter/prompts"
        rows = [{"task": p.stem, "messages": [{"role": "system", "content": p.read_text()}]}
                for p in prompts.glob("*.txt")]
        source = self.root / "source.json"
        source.write_text(json.dumps(rows))
        module.validate_generator_source([row["task"] for row in rows], source)
        wrong = [dict(row) for row in rows]
        wrong[0] = {"task": rows[0]["task"], "messages": [{"role": "system", "content": "wrong"}]}
        source.write_text(json.dumps(wrong))
        with self.assertRaisesRegex(ValueError, "prompt differs"):
            module.validate_generator_source([row["task"] for row in rows], source)
        output = self.root / "sft.jsonl"
        output.write_text("old dataset")
        manifest = self.root / "manifest.json"
        manifest.write_text("old manifest")
        with self.assertRaises(ValueError):
            module.export_dataset(rows[:-1], output, seed=17)
        self.assertEqual(output.read_text(), "old dataset")
        self.assertEqual(manifest.read_text(), "old manifest")
        module.export_dataset(rows, output, seed=17)
        self.assertEqual(len(output.read_text().splitlines()), 6)
        self.assertEqual(json.loads(manifest.read_text())["volume"], 6)

    def test_legacy_generator_is_blocked_before_source_access(self):
        sys.modules.pop("check_prompt_contract", None)
        sys.path.insert(0, str(self.root / "scripts"))
        self.addCleanup(sys.path.remove, str(self.root / "scripts"))
        spec = importlib.util.spec_from_file_location("dataset_preflight_test", self.root / "scripts/write_dataset_manifest.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with self.assertRaisesRegex(ValueError, "task"):
            module.validate_generator_source(["extract", "summary_leaf", "live_points"], self.root / "missing.json")


if __name__ == "__main__":
    unittest.main()
