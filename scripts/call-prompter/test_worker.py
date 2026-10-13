import importlib.util
import json
from pathlib import Path
import unittest


class WorkerTest(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location("worker", Path(__file__).with_name("worker.py"))
        self.worker = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.worker)
        self.sources = [
            {"id": "1", "speaker": "Анна", "text": "Отправлю документ в пятницу."},
            {"id": "2", "speaker": "Борис", "text": "Хорошо, жду в пятницу."},
        ]
        self.request = {"protocol_version": 1, "request_id": 7, "session_generation": 3,
                        "ledger_revision": 4, "utterances": self.sources, "candidates": []}

    def run_responses(self, responses):
        iterator = iter(responses)
        return self.worker.process_request(self.request, lambda prompt, payload: next(iterator))

    def event(self, **changes):
        value = {"kind": "accept", "action": "Анна отправит документ в пятницу",
                 "participants": ["Анна", "Борис"], "evidence_ids": ["1", "2"]}
        value.update(changes)
        return value

    def test_events_remain_proposals_and_response_echoes_revision(self):
        out = self.run_responses([{"events": [self.event()]}, {"suggestions": []}])
        self.assertEqual(out["request_id"], 7)
        self.assertEqual(out["session_generation"], 3)
        self.assertEqual(out["ledger_revision"], 4)
        self.assertEqual(out["events"][0]["target_id"], "")
        self.assertEqual(self.request["candidates"], [])

    def test_foreign_evidence_and_speaker_are_rejected(self):
        for event in (self.event(evidence_ids=["foreign"]), self.event(participants=["Unknown"])):
            out = self.run_responses([{"events": [event]}, {"suggestions": []}])
            self.assertEqual(out["events"], [])
            self.assertTrue(out["rejected"])

    def test_ambiguous_cancellation_remains_unresolved(self):
        self.request["candidates"] = [
            {"id": "a", "text": "Документ в пятницу", "participants": ["Анна", "Борис"]},
            {"id": "b", "text": "Документ в субботу", "participants": ["Анна", "Борис"]}]
        out = self.run_responses([{"events": [self.event(kind="cancel")]},
                                  {"matches": ["a", "b"]}, {"suggestions": []}])
        self.assertTrue(out["events"][0]["unresolved"])
        self.assertEqual(out["events"][0]["target_id"], "")
        self.assertEqual(len(self.request["candidates"]), 2)

    def test_foreign_matching_target_cannot_be_applied(self):
        self.request["candidates"] = [{"id": "a", "text": "Документ", "participants": ["Анна"]}]
        out = self.run_responses([{"events": [self.event(kind="cancel")]},
                                  {"matches": ["foreign"]}, {"suggestions": []}])
        self.assertTrue(out["events"][0]["unresolved"])

    def test_exact_match_avoids_second_model_call(self):
        self.request["candidates"] = [{"id": "a", "text": self.event()["action"],
                                        "participants": ["Анна", "Борис"]}]
        out = self.run_responses([{"events": [self.event(kind="cancel")]}, {"suggestions": []}])
        self.assertEqual(out["events"][0]["target_id"], "a")

    def test_invalid_suggestions_are_not_shown(self):
        out = self.run_responses([{"events": [self.event()]}, {"suggestions": [
            {"text": "К какому дню?", "evidence_ids": ["foreign"]}]}])
        self.assertEqual(out["suggestions"], [])

    def test_neutral_window_skips_the_guidance_model_call(self):
        out = self.run_responses([{"events": []}])
        self.assertEqual(out["suggestions"], [])

    def test_deadline_check_creates_a_fixed_question_with_source_ids(self):
        out = self.run_responses([{"events": [self.event()]}, {"missing_deadline": True, "evidence_ids": ["1", "2"]}])
        self.assertEqual(len(out["suggestions"]), 1)
        self.assertEqual(out["suggestions"][0]["evidence_ids"], ["1", "2"])

    def test_stated_deadline_or_foreign_source_does_not_create_a_question(self):
        for hint in [{"missing_deadline": False, "evidence_ids": ["1"]},
                     {"missing_deadline": True, "evidence_ids": ["foreign"]},
                     {"missing_deadline": "true", "evidence_ids": ["1"]}]:
            out = self.run_responses([{"events": [self.event()]}, hint])
            self.assertEqual(out["suggestions"], [])

    def test_missing_protocol_fields_and_excess_input_fail_closed(self):
        for field in ("request_id", "ledger_revision", "session_generation"):
            request = dict(self.request)
            del request[field]
            with self.assertRaises(ValueError):
                self.worker.process_request(request, lambda *args: {})
        self.request["utterances"] = [{"id": "1", "speaker": "A", "text": "x" * 20000}]
        with self.assertRaises(ValueError):
            self.run_responses([])

    def test_invalid_extraction_fails_instead_of_reporting_processed(self):
        with self.assertRaises(ValueError):
            self.run_responses([{"events": "not an array"}])


if __name__ == "__main__":
    unittest.main()
