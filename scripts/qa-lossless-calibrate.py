#!/usr/bin/env python3
"""Fit discrete ordinal-score bins and validate on source-disjoint labelled data.

Standard library only; this writes an OFFLINE DIAGNOSTIC, never a release profile.
Exit 0: evaluated (not a quality/pass claim); 2: ineligible; 1: I/O failure.
Usage: python scripts/qa-lossless-calibrate.py --input dataset.json --output report.json

Input contract (schemaVersion 1):
  domain, target, algorithmVersion, parameterVersion: explicit nonempty strings.
  protocol: {splitLockedBeforePredictions: true, developmentSourcesExcluded: true}
  samples: [{sampleId, sourceGroup, split: calibration|validation, sha256,
    status: accepted|abstained|error|cancelled, score: integer 0..100,
    predictedClass, evidenceFamily, labelKnown: true, label: 0|1,
    truthProvenance: "verifiable provenance for this target"}]

label means correctness of the DECLARED accepted claim, not absence of abstention.
Unknown accepted truth is ineligible, never zero. Abstentions/errors may have
labelKnown=false, label=null; errors/cancellations may also have score=null.
sourceGroup must be the canonical connected group of ALL recording/session and
parent dependencies: cuts, encodings, gain changes and mixtures stay together.
The script detects declared group/hash overlaps, but cannot certify grouping,
provenance, exposure declarations, or the audio hashes (it does not read audio).

Both labels are required per class/evidence stratum in BOTH splits. Bins are
fixed to exact scores before inspecting validation; no interpolation or tuning.
Fitting uses the calibration sample correctness frequency, not score/100.
Wilson intervals assume independent samples and are descriptive. Group-equal
Brier and deterministic source bootstrap are also descriptive, particularly
with few groups. No result establishes population reliability or release fitness.
"""

import argparse
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path
import random
import re
import sys


STATUSES = {"accepted", "abstained", "error", "cancelled"}
SPLITS = ("calibration", "validation")


def nonempty(value):
    return isinstance(value, str) and bool(value.strip())


def mean(values):
    return math.fsum(values) / len(values) if values else None


def wilson(correct, count):
    result = {"numerator": correct, "denominator": count, "estimate": None,
              "lower": None, "upper": None, "confidenceLevel": 0.95,
              "method": "wilson_score", "unit": "sample",
              "interpretation": "descriptive_independence_assumption"}
    if count:
        p = correct / count
        z2 = 1.959963984540054 ** 2
        divisor = 1 + z2 / count
        center = (p + z2 / (2 * count)) / divisor
        margin = math.sqrt(z2 * (p * (1 - p) / count + z2 / (4 * count ** 2))) / divisor
        result.update(estimate=p, lower=max(0.0, center - margin), upper=min(1.0, center + margin))
    return result


def group_bootstrap(values):
    """Percentile bootstrap of equal-weight source means, not file observations."""
    result = {"lower": None, "upper": None, "confidenceLevel": 0.95,
              "sourceGroups": len(values), "method": "source_group_percentile_bootstrap",
              "replicates": 2000, "seed": 20260907, "publicationReady": False,
              "interpretation": "descriptive_few_groups_may_be_uninformative"}
    if len(values) >= 2:
        rng = random.Random(result["seed"])
        draws = sorted(mean([values[rng.randrange(len(values))] for _ in values])
                       for _ in range(result["replicates"]))
        def percentile(q):
            location = q * (len(draws) - 1)
            left = int(location)
            return draws[left] + (draws[min(left + 1, len(draws) - 1)] - draws[left]) * (location - left)
        result.update(lower=percentile(0.025), upper=percentile(0.975))
    return result


def source_key(row):
    return row["sourceGroup"].strip().casefold()


def stratum(row):
    return row["predictedClass"], row["evidenceFamily"]


def bin_key(row):
    return (*stratum(row), row["score"])


def counts(rows):
    result = {}
    for split in SPLITS:
        subset = [r for r in rows if r.get("split") == split]
        accepted = sum(r.get("status") == "accepted" for r in subset)
        abstained = sum(r.get("status") == "abstained" for r in subset)
        operational = sum(r.get("status") in ("error", "cancelled") for r in subset)
        successful = accepted + abstained
        result[split] = {
            "total": len(subset), "sourceGroups": len({source_key(r) for r in subset
                                                       if nonempty(r.get("sourceGroup"))}),
            "accepted": accepted, "abstained": abstained, "operationalFailures": operational,
            "knownAcceptedTruth": sum(r.get("status") == "accepted" and r.get("labelKnown") is True
                                      and type(r.get("label")) is int and r["label"] in (0, 1) for r in subset),
            "acceptedCoverageAmongSuccessful": accepted / successful if successful else None,
            "acceptedCoverageAllInputs": accepted / len(subset) if subset else None,
        }
    return result


def empty_report(input_hash):
    return {"schemaVersion": 1, "status": "ineligible", "probability": None,
            "profile": None, "deploymentEligible": False, "inputSha256": input_hash,
            "datasetSha256": None, "domain": None, "target": None,
            "algorithmVersion": None, "parameterVersion": None,
            "declaredAudioHashesVerified": False, "provenanceAssertionsVerified": False,
            "reasons": [], "counts": {}, "calibrationBins": [], "validation": None, "rows": [],
            "limitations": [
                "Offline descriptive evaluation only; no publishable profile or population guarantee.",
                "Ordinal scores are preserved and are not interpreted as probabilities.",
                "Source groups must include all correlated parents; declarations are not independently verified.",
                "Sample Wilson intervals assume independence; source bootstrap is descriptive, not a release gate.",
                "Unknown truth is not a negative, and abstention is not an accepted correct claim.",
            ]}


def evaluate(data, input_hash):
    report = empty_report(input_hash)
    def reject(code, detail):
        entry = {"code": code, "detail": detail}
        if entry not in report["reasons"]:
            report["reasons"].append(entry)

    if not isinstance(data, dict):
        reject("invalid_schema", "Input must be an object.")
        return report
    report["datasetSha256"] = hashlib.sha256(json.dumps(
        data, sort_keys=True, ensure_ascii=False, separators=(",", ":"), allow_nan=False
    ).encode("utf-8")).hexdigest()
    if type(data.get("schemaVersion")) is not int or data["schemaVersion"] != 1:
        reject("invalid_schema", "schemaVersion must be integer 1.")
    for name in ("domain", "target", "algorithmVersion", "parameterVersion"):
        report[name] = data.get(name)
        if not nonempty(data.get(name)):
            reject("missing_context", name)
    protocol = data.get("protocol")
    if not isinstance(protocol, dict) or any(protocol.get(k) is not True for k in (
            "splitLockedBeforePredictions", "developmentSourcesExcluded")):
        reject("protocol_not_independent", "Split must be locked before predictions and exclude development sources.")
    samples = data.get("samples")
    if not isinstance(samples, list):
        reject("invalid_schema", "samples must be an array.")
        return report
    seen_ids, seen_hashes = set(), set()
    rows = []
    for index, sample in enumerate(samples):
        if not isinstance(sample, dict):
            reject("invalid_sample", f"Row {index} must be an object.")
            continue
        row = dict(sample)
        row["probability"] = None
        rows.append(row)
        identity = row.get("sampleId")
        reference = identity if nonempty(identity) else f"row {index}"
        if not nonempty(identity) or identity.strip().casefold() in seen_ids:
            reject("invalid_sample_id", str(reference))
        else:
            seen_ids.add(identity.strip().casefold())
        if not nonempty(row.get("sourceGroup")):
            reject("missing_source_group", str(reference))
        if row.get("split") not in SPLITS:
            reject("invalid_split", str(reference))
        digest = row.get("sha256")
        if not isinstance(digest, str) or not re.fullmatch(r"[a-fA-F0-9]{64}", digest):
            reject("invalid_audio_hash", str(reference))
        elif digest.lower() in seen_hashes:
            reject("duplicate_audio_hash", str(reference))
        else:
            seen_hashes.add(digest.lower())
        status = row.get("status")
        if not isinstance(status, str) or status not in STATUSES:
            reject("invalid_status", str(reference))
        score = row.get("score")
        if not (type(score) is int and 0 <= score <= 100) and not (
                status in ("error", "cancelled") and score is None):
            reject("invalid_score", str(reference))
        for name in ("algorithmVersion", "parameterVersion"):
            if name in row and row[name] != data.get(name):
                reject("version_mismatch", str(reference))
        if status == "accepted":
            if not all(nonempty(row.get(k)) for k in ("predictedClass", "evidenceFamily")):
                reject("missing_stratum", str(reference))
            if row.get("labelKnown") is not True:
                reject("unknown_accepted_truth", str(reference))
            if type(row.get("label")) is not int or row["label"] not in (0, 1):
                reject("invalid_truth", str(reference))
            if not nonempty(row.get("truthProvenance")):
                reject("missing_truth_provenance", str(reference))
    report["rows"] = rows
    report["counts"] = counts(rows)
    if report["reasons"]:
        return report
    sources = {split: {source_key(r) for r in rows if r["split"] == split} for split in SPLITS}
    if sources["calibration"] & sources["validation"]:
        reject("source_overlap", "Canonical sourceGroup occurs in both splits.")
    accepted = {split: [r for r in rows if r["split"] == split and r["status"] == "accepted"]
                for split in SPLITS}
    strata = {stratum(r) for subset in accepted.values() for r in subset}
    for split, subset in accepted.items():
        if not subset:
            reject("empty_split", split)
        if len({source_key(r) for r in subset}) < 2:
            reject("insufficient_source_groups", split)
        for key in sorted(strata):
            labels = {r["label"] for r in subset if stratum(r) == key}
            if labels != {0, 1}:
                reject("single_label_class", f"{split}: {key}; requires both correctness labels.")
    calibration = defaultdict(list)
    for row in accepted["calibration"]:
        calibration[bin_key(row)].append(row)
    for row in accepted["validation"]:
        if bin_key(row) not in calibration:
            reject("unsupported_validation_bin", row["sampleId"])
    if report["reasons"]:
        return report

    probabilities = {}
    for key, subset in sorted(calibration.items()):
        probability = mean([r["label"] for r in subset])
        probabilities[key] = probability
        report["calibrationBins"].append({
            "predictedClass": key[0], "evidenceFamily": key[1], "score": key[2],
            "probability": probability, "fitUnit": "sample_empirical_frequency",
            "samples": len(subset), "sourceGroups": len({source_key(r) for r in subset}),
            "observedCorrectness": wilson(sum(r["label"] for r in subset), len(subset)),
        })
    for row in accepted["calibration"] + accepted["validation"]:
        row["probability"] = probabilities[bin_key(row)]
    subset = accepted["validation"]
    baseline = {key: mean([r["label"] for r in accepted["calibration"] if stratum(r) == key])
                for key in strata}
    grouped = defaultdict(list)
    reliable = defaultdict(list)
    for row in subset:
        grouped[source_key(row)].append(row)
        reliable[min(9, int(row["probability"] * 10))].append(row)
    source_metrics = [{
        "sourceGroup": key, "samples": len(group),
        "brier": mean([(r["probability"] - r["label"]) ** 2 for r in group]),
        "meanProbability": mean([r["probability"] for r in group]),
        "observedCorrectness": mean([r["label"] for r in group]),
    } for key, group in sorted(grouped.items())]
    reliability = [{
        "lowerInclusive": key / 10, "upper": (key + 1) / 10, "upperInclusive": key == 9,
        "samples": len(group), "sourceGroups": len({source_key(r) for r in group}),
        "meanProbability": mean([r["probability"] for r in group]),
        "observedCorrectness": wilson(sum(r["label"] for r in group), len(group)),
        "absoluteGap": abs(mean([r["probability"] - r["label"] for r in group])),
    } for key, group in sorted(reliable.items())]
    report["validation"] = {
        "samples": len(subset), "sourceGroups": len(grouped),
        "brier": mean([(r["probability"] - r["label"]) ** 2 for r in subset]),
        "constantBaselineBrier": mean([(baseline[stratum(r)] - r["label"]) ** 2 for r in subset]),
        "constantBaseline": "calibration_prevalence_within_class_and_evidence_family",
        "observedCorrectness": wilson(sum(r["label"] for r in subset), len(subset)),
        "reliabilityBins": reliability,
        "ece": math.fsum(b["samples"] * b["absoluteGap"] for b in reliability) / len(subset),
        "sourceEqualBrier": mean([g["brier"] for g in source_metrics]),
        "sourceGroupBrierInterval": group_bootstrap([g["brier"] for g in source_metrics]),
        "perSource": source_metrics,
    }
    report["status"] = "evaluated"
    return report


def reject_constant(value):
    raise ValueError(f"Non-finite JSON constant: {value}")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.input.resolve() == args.output.resolve() or (
            args.input.exists() and args.output.exists() and args.input.samefile(args.output)):
        parser.error("Output must not overwrite input truth.")
    try:
        payload = args.input.read_bytes()
        digest = hashlib.sha256(payload).hexdigest()
        try:
            data = json.loads(payload.decode("utf-8-sig"), parse_constant=reject_constant,
                              object_pairs_hook=unique_object)
            report = evaluate(data, digest)
        except (ValueError, UnicodeError, RecursionError) as error:
            report = empty_report(digest)
            report["reasons"].append({"code": "invalid_json", "detail": str(error)})
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False) + "\n",
                               encoding="utf-8")
    except OSError as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0 if report["status"] == "evaluated" else 2


if __name__ == "__main__":
    sys.exit(main())
