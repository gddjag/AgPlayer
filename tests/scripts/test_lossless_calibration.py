"""Numerical/contract tests only: synthetic fixtures are NOT product calibration evidence."""

import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "qa-lossless-calibrate.py"


def fixture():
    rows = []
    # Hand-computed bin rates: score 20 -> 1/4; score 80 -> 3/4.
    for split, points in (
        ("calibration", [(20, 0), (20, 0), (20, 0), (20, 1),
                         (80, 1), (80, 1), (80, 1), (80, 0)]),
        ("validation", [(20, 0), (20, 0), (80, 1), (80, 0)]),
    ):
        for i, (score, label) in enumerate(points):
            name = f"{split}-{i}"
            rows.append({
                "sampleId": name, "sourceGroup": name, "split": split,
                "sha256": hashlib.sha256(name.encode()).hexdigest(),
                "score": score, "labelKnown": True, "label": label,
                "truthProvenance": "Synthetic script-unit-test construction only",
                "predictedClass": "suspected_lossy_transcode",
                "evidenceFamily": "unit-test-fixture", "status": "accepted",
            })
    return {
        "schemaVersion": 1, "domain": "synthetic_script_unit_test_only",
        "target": "accepted_lossy_claim_correct",
        "algorithmVersion": "test-only", "parameterVersion": "test-only",
        "protocol": {"splitLockedBeforePredictions": True,
                     "developmentSourcesExcluded": True},
        "samples": rows,
    }


class CalibrationTests(unittest.TestCase):
    def run_script(self, data, expected_exit=0):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "input.json"
            output = Path(directory) / "output.json"
            payload = json.dumps(data, ensure_ascii=False).encode("utf-8")
            source.write_bytes(payload)
            process = subprocess.run(
                [sys.executable, str(SCRIPT), "--input", str(source), "--output", str(output)],
                capture_output=True, text=True, encoding="utf-8",
            )
            self.assertEqual(process.returncode, expected_exit, process.stderr)
            self.assertTrue(output.exists(), "CLI must produce a reviewable JSON report")
            result = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(source.read_bytes(), payload, "Input truth must remain unchanged")
            self.assertEqual(result["inputSha256"], hashlib.sha256(payload).hexdigest())
            return result

    def assert_ineligible(self, data, reason):
        result = self.run_script(data, expected_exit=2)
        self.assertEqual(result["status"], "ineligible")
        self.assertIn(reason, {r["code"] for r in result["reasons"]})
        self.assertIsNone(result["probability"])
        self.assertIsNone(result["profile"])
        self.assertFalse(result["deploymentEligible"])
        self.assertTrue(all(row["probability"] is None for row in result["rows"]))
        return result

    def test_independent_validation_has_hand_computed_brier_and_wilson(self):
        report = self.run_script(fixture())
        self.assertEqual(report["status"], "evaluated")
        self.assertEqual([b["probability"] for b in report["calibrationBins"]], [0.25, 0.75])
        validation = report["validation"]
        self.assertAlmostEqual(validation["brier"], 0.1875)
        self.assertAlmostEqual(validation["constantBaselineBrier"], 0.25)
        self.assertAlmostEqual(validation["ece"], 0.25)
        self.assertEqual(validation["samples"], 4)
        self.assertEqual(validation["sourceGroups"], 4)
        interval = validation["observedCorrectness"]
        self.assertAlmostEqual(interval["lower"], 0.0455872608, places=9)
        self.assertAlmostEqual(interval["upper"], 0.6993581574, places=9)
        self.assertEqual(interval["interpretation"], "descriptive_independence_assumption")
        self.assertEqual([b["samples"] for b in validation["reliabilityBins"]], [2, 2])
        self.assertFalse(report["deploymentEligible"])
        self.assertIsNone(report["profile"])

    def test_validation_labels_never_change_fitted_probabilities(self):
        before = self.run_script(fixture())
        data = fixture()
        for row in data["samples"]:
            if row["split"] == "validation":
                row["label"] = 1 - row["label"]
        after = self.run_script(data)
        self.assertEqual(before["calibrationBins"], after["calibrationBins"])
        self.assertNotEqual(before["validation"]["brier"], after["validation"]["brier"])
        self.assertNotEqual(before["datasetSha256"], after["datasetSha256"])

    def test_raw_ordinal_scores_are_preserved_without_dividing_by_100(self):
        report = self.run_script(fixture())
        validation = [r for r in report["rows"] if r["split"] == "validation"]
        self.assertEqual([r["score"] for r in validation], [20, 20, 80, 80])
        self.assertEqual([r["probability"] for r in validation], [0.25, 0.25, 0.75, 0.75])

    def test_repeated_source_uses_equal_group_error_not_file_count(self):
        data = fixture()
        # The first three validations have loss 1/16, the last has loss 9/16.
        for row in data["samples"][8:11]:
            row["sourceGroup"] = "one-related-source"
        result = self.run_script(data)["validation"]
        self.assertAlmostEqual(result["brier"], 0.1875)
        self.assertAlmostEqual(result["sourceEqualBrier"], 0.3125)
        interval = result["sourceGroupBrierInterval"]
        self.assertAlmostEqual(interval["lower"], 0.0625)
        self.assertAlmostEqual(interval["upper"], 0.5625)
        self.assertEqual(interval["sourceGroups"], 2)
        self.assertFalse(interval["publicationReady"])

    def test_abstention_and_errors_count_for_coverage_not_correctness(self):
        data = fixture()
        for i, status in enumerate(("abstained", "error", "cancelled")):
            row = copy.deepcopy(data["samples"][-1])
            row.update(sampleId=status, sourceGroup=status, status=status,
                       sha256=hashlib.sha256(status.encode()).hexdigest(),
                       labelKnown=False, label=None, score=32 if i == 0 else None)
            data["samples"].append(row)
        report = self.run_script(data)
        coverage = report["counts"]["validation"]
        self.assertEqual(coverage["total"], 7)
        self.assertEqual(coverage["accepted"], 4)
        self.assertEqual(coverage["abstained"], 1)
        self.assertEqual(coverage["operationalFailures"], 2)
        self.assertAlmostEqual(coverage["acceptedCoverageAmongSuccessful"], 0.8)
        self.assertAlmostEqual(coverage["acceptedCoverageAllInputs"], 4 / 7)
        self.assertEqual(report["validation"]["samples"], 4)
        self.assertTrue(all(r["probability"] is None for r in report["rows"][-3:]))

    def test_rejects_source_overlap_even_if_case_or_whitespace_differs(self):
        data = fixture()
        data["samples"][-1]["sourceGroup"] = " CALIBRATION-0 "
        self.assert_ineligible(data, "source_overlap")

    def test_rejects_renamed_audio_hash_duplicate(self):
        data = fixture()
        data["samples"][-1]["sha256"] = data["samples"][0]["sha256"].upper()
        self.assert_ineligible(data, "duplicate_audio_hash")

    def test_rejects_missing_or_invalid_mandatory_values(self):
        for field, value, reason in (
            ("sourceGroup", "", "missing_source_group"),
            ("sha256", "no-hash", "invalid_audio_hash"),
            ("score", None, "invalid_score"),
            ("score", True, "invalid_score"),
            ("score", 101, "invalid_score"),
            ("label", True, "invalid_truth"),
            ("label", None, "invalid_truth"),
            ("truthProvenance", "", "missing_truth_provenance"),
            ("split", "test", "invalid_split"),
            ("evidenceFamily", "", "missing_stratum"),
        ):
            with self.subTest(field=field, value=value):
                data = fixture()
                data["samples"][0][field] = value
                self.assert_ineligible(data, reason)

    def test_explicit_unknown_truth_is_not_automatically_a_negative(self):
        data = fixture()
        data["samples"][0].update(labelKnown=False, label=None)
        report = self.assert_ineligible(data, "unknown_accepted_truth")
        self.assertIsNone(report["rows"][0]["label"])

    def test_rejects_single_label_in_either_split(self):
        for split in ("calibration", "validation"):
            with self.subTest(split=split):
                data = fixture()
                for row in data["samples"]:
                    if row["split"] == split:
                        row["label"] = 1
                self.assert_ineligible(data, "single_label_class")

    def test_rejects_validation_score_without_a_fitted_bin(self):
        data = fixture()
        data["samples"][-1]["score"] = 64
        self.assert_ineligible(data, "unsupported_validation_bin")

    def test_different_predicted_class_cannot_borrow_another_class_probability(self):
        data = fixture()
        data["samples"][-1]["predictedClass"] = "suspected_upsample"
        self.assert_ineligible(data, "single_label_class")

    def test_rejects_unlocked_or_development_exposed_data(self):
        for field in ("splitLockedBeforePredictions", "developmentSourcesExcluded"):
            with self.subTest(field=field):
                data = fixture()
                data["protocol"][field] = False
                self.assert_ineligible(data, "protocol_not_independent")

    def test_rejects_version_mixing_and_unspecified_domain(self):
        data = fixture()
        data["samples"][-1]["algorithmVersion"] = "older-version"
        self.assert_ineligible(data, "version_mismatch")
        data = fixture()
        data["domain"] = ""
        self.assert_ineligible(data, "missing_context")

    def test_empty_data_has_no_probability(self):
        data = fixture()
        data["samples"] = []
        self.assert_ineligible(data, "empty_split")

    def test_invalid_json_is_reviewable_ineligible_output(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "bad.json"
            output = Path(directory) / "out.json"
            source.write_text('{"samples": [NaN]}', encoding="utf-8")
            process = subprocess.run([sys.executable, str(SCRIPT), "--input", str(source),
                                      "--output", str(output)], capture_output=True)
            self.assertEqual(process.returncode, 2)
            self.assertTrue(output.exists())
            self.assertEqual(json.loads(output.read_text())["status"], "ineligible")

    def test_malformed_status_does_not_crash_before_ineligible_report(self):
        for invalid in ([], {}):
            with self.subTest(status=invalid):
                data = fixture()
                data["samples"][0]["status"] = invalid
                self.assert_ineligible(data, "invalid_status")


if __name__ == "__main__":
    unittest.main()
