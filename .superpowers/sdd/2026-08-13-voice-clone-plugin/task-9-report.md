# Task 9 Report: pinned official model Registry and license collection

## Delivered

- Four downloadable model manifests are pinned to immutable official Hugging Face commits: Qwen3-TTS 0.6B (13 files), Qwen3-TTS 1.7B (13), IndexTTS-2.5 plus its complete auxiliary graph (32), and Fun-CosyVoice3 (20).
- Every file has a real content SHA-256 and byte size. All LFS identities and all sizes were rechecked live against the official pinned Hugging Face APIs on 2026-08-14. The complete graph totals are 2,516,106,051; 4,544,229,700; 10,796,836,165; and 9,747,516,745 bytes respectively.
- Package parsing is fail-closed for unknown fields, mutable revisions, source mismatch, incomplete/altered license collections, duplicate/unsafe targets, hash format, negative size, and total-size mismatch. HEAD, resume metadata, partial completion, and final verification all use the manifest byte size in addition to SHA-256.
- Every file URL is derived exactly from its declared official repository, immutable revision, and upstream path. Index auxiliary files carry their own pinned official per-file source identity; community repositories, altered revisions/paths, lookalike hosts, userinfo, and custom ports are rejected. Loopback URLs exist only in `BUILD_TESTING` builds for the local HTTP integration fixture.
- `VoiceCloneController::downloadModel()` is the production entry for the four shipped manifests. The asynchronous package manager now atomically installs to `models/voice-clone/<adapter>/<packageId>`, generates a production-valid `agplayer-model.json`, refreshes discovery on completion, and exposes aggregate state/progress to the existing model bar. The old dead `packages/` destination is no longer used.
- Index includes the four auxiliary identities declared by official upstream code: W2V-BERT 2.0, MaskGCT semantic codec, CAMPPlus, and BigVGAN. No Worker-side first-run download is needed.
- Licenses remain a collection. Index requires separate acceptance of the pinned bilibili model license and MaskGCT CC-BY-NC-4.0. Its MIT/Apache auxiliary notices remain separately identified but do not create gates. Qwen and CosyVoice do not receive a gate.
- Any locally added `indextts25` model, regardless of its stable ID, is rejected unless it declares the exact two approved required license identities. Complete identities still require both stored acceptances before load/generate.
- Acceptance records preserve exact `modelId`, `adapterId`, `licenseId`, URL, revision, and timestamp. A corrupt or incomplete store blocks both package download and generation.
- The existing license dialog is data-driven and requires one checkbox per restricted license, exposes each immutable source link, and states: “仅非商业；商业用途禁用，除非另获授权”.
- The dialog uses an explicit Apply action: incomplete acceptance keeps it open and displays an inline prompt. The model bar adds only the existing-flow download action and asynchronous aggregate progress, without a layout redesign.
- Chinese user guidance documents official capabilities and per-model reference-audio evidence while explicitly deferring unknown hard ranges to the Worker/model card. It avoids unsupported mobile, 4–6 GB VRAM, latency, and long-form continuity promises. The extension guide has a copyable two-level production path and parser-valid JSON; a test writes that example to disk and discovers it through the production parser. New executable inference architectures remain behind signed Adapter Packs.

## Official-source method

The research record is `docs/research/2026-08-14-voice-clone-official-model-sources.md`. Git LFS hashes are the official content SHA-256 values. Small Git-managed files were downloaded from immutable `resolve/<commit>/...` URLs and hashed locally. No community mirror or mutable branch is used.

## RED / GREEN evidence

- Registry RED: no download manifest directory. GREEN: the contract accepts exactly four approved immutable manifests and their complete file counts.
- Package RED: source, size, license collection, and multi-license APIs did not exist. GREEN: strict parsing, download/resume size validation, exact collection validation, and two-license acceptance pass.
- License RED: the old store and controller represented one URL/revision. GREEN: each required license has an exact ID and separate record; accepting only one Index license leaves download/generation blocked.
- QML RED: one generic checkbox accepted the single model license. GREEN: the existing dialog renders the two required Index licenses with separate checkboxes and official links.
- Integration RED: the plugin installed packages under an undiscoverable `packages/` tree and had no download entry. GREEN: a real loopback HTTP fixture exercises `downloadModel` → HEAD/GET → SHA/size verify → atomic install → generated manifest → refresh → ready discovery → select while still unloaded.
- Final delegate review RED: a local Index manifest could append a third non-required license, and a partial per-file `source` object could inherit around the auxiliary allowlist. GREEN: local Index accepts exactly the two required identities; generated manifests filter non-gating notices; any per-file source override must provide all three fields and match one of the four exact auxiliary identities.

## Verification

MSVC Debug, `build/msvc-debug`:

```text
voice_clone_manifest_test ............ Passed (32/32)
voice_clone_package_manager_test ..... Passed (31/31)
voice_clone_controller_test .......... Passed (17/17)
voice_clone_registry_contract_test ... Passed
qml_voice_clone_test ................. Passed (10/10)
Official pinned API reconciliation ... Passed (78/78 file sizes; every LFS SHA)
git diff --check ...................... Passed
OCR delegate review .................. 16/16 reviewable files; 9 excluded formats manually reviewed
Ponytail complexity review ........... Lean already; no speculative layer added
```

The four focused CTest executables passed together in one run. The configured OCR remote-LLM path was unavailable, so the installed OCR delegate mode supplied deterministic workspace selection and rules while the host reviewed every selected file; documentation, QML, and CMake formats excluded by OCR were reviewed manually. No production weights or real inference were run; hardware/quality acceptance remains Task 11.
