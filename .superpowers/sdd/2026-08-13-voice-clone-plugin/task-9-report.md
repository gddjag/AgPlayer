# Task 9 Report: pinned official model Registry and license collection

## Delivered

- Four downloadable model manifests are pinned to immutable official Hugging Face commits: Qwen3-TTS 0.6B (13 files), Qwen3-TTS 1.7B (13), IndexTTS-2.5 plus its complete auxiliary graph (32), and Fun-CosyVoice3 (20).
- Every file has a real content SHA-256 and byte size. All LFS identities and all sizes were rechecked live against the official pinned Hugging Face APIs on 2026-08-14. The complete graph totals are 2,516,106,051; 4,544,229,700; 10,796,836,165; and 9,747,516,745 bytes respectively.
- Package parsing is fail-closed for unknown fields, mutable revisions, source mismatch, incomplete/altered license collections, duplicate/unsafe targets, hash format, negative size, and total-size mismatch. HEAD, resume metadata, partial completion, and final verification all use the manifest byte size in addition to SHA-256.
- Index includes the four auxiliary identities declared by official upstream code: W2V-BERT 2.0, MaskGCT semantic codec, CAMPPlus, and BigVGAN. No Worker-side first-run download is needed.
- Licenses remain a collection. Index requires separate acceptance of the pinned bilibili model license and MaskGCT CC-BY-NC-4.0. Its MIT/Apache auxiliary notices remain separately identified but do not create gates. Qwen and CosyVoice do not receive a gate.
- Acceptance records preserve exact `modelId`, `adapterId`, `licenseId`, URL, revision, and timestamp. A corrupt or incomplete store blocks both package download and generation.
- The existing license dialog is data-driven and requires one checkbox per restricted license, exposes each immutable source link, and states: “仅非商业；商业用途禁用，除非另获授权”.
- Chinese user guidance documents official capabilities and avoids unsupported mobile, 4–6 GB VRAM, latency, and long-form continuity promises. The extension guide shows data-only local model discovery without a UI change and keeps new executable inference architectures behind signed Adapter Packs.

## Official-source method

The research record is `docs/research/2026-08-14-voice-clone-official-model-sources.md`. Git LFS hashes are the official content SHA-256 values. Small Git-managed files were downloaded from immutable `resolve/<commit>/...` URLs and hashed locally. No community mirror or mutable branch is used.

## RED / GREEN evidence

- Registry RED: no download manifest directory. GREEN: the contract accepts exactly four approved immutable manifests and their complete file counts.
- Package RED: source, size, license collection, and multi-license APIs did not exist. GREEN: strict parsing, download/resume size validation, exact collection validation, and two-license acceptance pass.
- License RED: the old store and controller represented one URL/revision. GREEN: each required license has an exact ID and separate record; accepting only one Index license leaves download/generation blocked.
- QML RED: one generic checkbox accepted the single model license. GREEN: the existing dialog renders the two required Index licenses with separate checkboxes and official links.

## Verification

MSVC Debug, `build/msvc-debug`:

```text
voice_clone_package_manager_test ..... Passed
voice_clone_controller_test .......... Passed
voice_clone_registry_contract_test ... Passed
qml_voice_clone_test standalone ...... Passed (9/9 QTest assertions)
Official pinned API reconciliation ... Passed (78/78 file sizes; every LFS SHA)
git diff --check ...................... Passed
```

A later combined four-test run passed the three deterministic tests and reproduced an intermittent existing QML real-plugin reactivation assertion after all new dual-license assertions passed. A direct standalone rerun immediately before it passed 9/9. Task 8 already records instability in the same Debug QML test process. No model weights or real inference were run; hardware/quality acceptance remains Task 11.
