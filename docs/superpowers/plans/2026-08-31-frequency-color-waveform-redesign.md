# Frequency Color Waveform Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build AgPlayer's real four-layer Frequency Color Waveform with one-pass on-demand analysis, an independent FCW1 cache, Luminous Glaze dark/light colors, per-layer custom color and opacity controls, and a static-geometry SceneGraph renderer.

**Architecture:** Keep the existing AGWF main waveform and all current interaction/time mapping contracts. Add an analysis-only mono decoder mode and a streaming frequency-envelope module, store only three `uint8_t` band arrays in FCW1, combine them lazily in `WaveformProvider`, and render mix/low/mid/high through four static geometry nodes whose progress, colors and fade are material uniforms.

**Tech Stack:** C++17, FFmpeg/libswresample, Qt 6.7 Quick/SceneGraph, QSGMaterialShader/QSB, QSettings, QML, CMake/Ninja, Qt Test/Quick Test, PowerShell QA scripts.

**Spec:** `docs/development/2026-08-31-frequency-color-waveform-redesign.md`

## Global Constraints

- Preserve visual modes 0–2, playback behavior, waveform time mapping, seek, scrub, zoom, Cue, selection, drag-out, hover, fullscreen and list-thumbnail behavior.
- Do not add a third-party dependency, playback-time FFT, playback-thread DSP, blur, persistent bloom animation, library-wide analysis, or an installer artifact.
- Frequency analysis opens one FFmpeg decoder once, processes interleaved PCM once, runs on one low-priority cancellable worker, and never falls back to a hidden second full decode.
- Stereo analysis mono is `(L + R) × 0.70710678`; multichannel downmix uses the FFmpeg channel layout and L2-normalized role weights.
- Crossovers are LR4 at 180 Hz and 2.8 kHz; the high ceiling is `min(20 kHz, 0.45 × sampleRate)`; cache algorithm version changes when these values or processing semantics change.
- Each bucket is `0.70 × peak + 0.30 × rms`, with low 0 dB, mid -1 dB and high +3 dB compensation, one shared 99.5-percentile reference, -72 dBFS silence, exponent 0.62, cap 0.98, and 7/5/3 band smoothing.
- FCW1 stores exactly three equal-length `uint8_t` bands and validated metadata/CRC; colors and opacity never enter the analysis cache; 2000-point files must remain below 8192 bytes.
- Luminous Glaze colors are exact: dark mix/low/mid/high `#7A8490/#269A8E/#C66B55/#B5A4C6`; light `#59636D/#146B64/#9D4938/#6E5A7D`; focus dark/light `#F2E7D4/#26313A`.
- Default fill opacity is dark mix/low/mid/high `0.18/0.44/0.38/0.46` and light `0.14/0.36/0.32/0.40`; user controls are independent per layer, 0–100%, 1% steps.
- Rendering uses premultiplied SourceOver. Unplayed Alpha is `0.72` of played Alpha and saturation loss is no more than 8%; colors never become a left-to-right frequency gradient.
- Do not reset, clean, checkout, delete or overwrite the pre-existing dirty worktree. Stage only named feature files; leave `$resultPath`, `.obj` files and unrelated text files untracked.
- A focused test, Release-only smoke, or one screenshot is partial evidence. Completion requires the full verification matrix that is executable on this host, plus an explicit list of hardware-only gaps.

---

## File Structure

| Responsibility | Files |
|---|---|
| Decoder output timeline and analysis mono | `core/src/decoder.hpp`, `core/src/decoder.cpp` |
| LR4 and streaming envelope algorithm | `core/src/waveform_analyzer_filters.hpp`, new `core/src/frequency_color_waveform_analyzer.hpp/.cpp` |
| Public native call and result ownership | `core/include/agplayer/c_api.h`, `core/src/c_api.cpp` |
| Independent FCW1 persistence | new `core/src/frequency_color_waveform_cache.hpp/.cpp` |
| Core build and tests | `core/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/core/decoder_test.cpp`, new `tests/core/frequency_color_waveform_analyzer_test.cpp`, new `tests/core/frequency_color_waveform_cache_test.cpp` |
| Lazy request lifecycle | `qt/src/waveform_provider.hpp/.cpp`, `app/qml/AgPlayer/components/WaveformSession.qml`, `tests/qt/waveform_provider_test.cpp` |
| Palette, custom colors, opacity and migration | new `qt/src/frequency_color_waveform_settings.hpp/.cpp`, `qt/src/settings_controller.hpp/.cpp`, `tests/qt/settings_controller_test.cpp` |
| Static geometry and shader material | new `qt/src/waveform_layer_material.hpp/.cpp`, new `qt/shaders/waveform_layer.vert/.frag`, `qt/src/waveform_item.hpp/.cpp`, `qt/CMakeLists.txt`, `tests/qt/waveform_item_test.cpp` |
| User-facing integration | `app/qml/AgPlayer/SettingsPage.qml`, `app/qml/AgPlayer/components/PlayerPane.qml`, `MiniPlayerControls.qml`, `IntegratedPlayerShell.qml`, `SharedWaveformView.qml`, relevant `tests/qml/tst_*.qml` |
| Quality and acceptance evidence | new `tests/stress/frequency_color_waveform_stress_test.cpp`, new `scripts/qa-frequency-color-waveform.ps1`, `docs/development/2026-08-31-frequency-waveform-acceptance.md`, existing Open Color acceptance record |

### Task 1: Analysis-mono decoder contract and high-precision timeline

**Files:**
- Modify: `core/src/decoder.hpp`
- Modify: `core/src/decoder.cpp`
- Modify: `tools/fixture_generator.cpp`
- Modify: `tests/core/decoder_test.cpp`

**Interfaces:**
- Consumes: existing `Decoder::open(path)`, `Decoder::open(path, rate, channels)`, `DecodedAudioBlock::timestamp_frame`.
- Produces:

```cpp
enum class DecoderDownmix { Preserve, AnalysisMono };

struct DecoderOpenOptions final {
    int output_sample_rate = 0;
    int output_channels = 0;
    DecoderDownmix downmix = DecoderDownmix::Preserve;
};

struct DecodedAudioFormat final {
    int sample_rate = 0;
    int channels = 0;
    std::uint64_t timeline_frames = 0;
    bool has_timeline = false;
};

ag_result Decoder::open(const std::string& path,
                        const DecoderOpenOptions& options) noexcept;
const DecodedAudioFormat& Decoder::output_format() const noexcept;
```

- Existing `open` overloads delegate to `DecoderOpenOptions` with `Preserve`; no existing caller changes behavior.

- [ ] **Step 1: Extend deterministic decoder fixtures**

Add fixture-generator modes that write stereo channels independently and WAVEFORMATEXTENSIBLE 5.1 with channel mask `0x3f`. Use fixed samples so tests can distinguish `L`, `R`, `C`, `LFE`, `SL`, and `SR`; do not generate them by duplicating one mono stream.

```cpp
enum class FixtureLayout { Mono, StereoIndependent, Surround51Independent };
// stereo frame: L=0.25F, R=0.50F
// 5.1 frame: FL=.10F, FR=.20F, FC=.30F, LFE=.40F, BL=.50F, BR=.60F
```

- [ ] **Step 2: Write failing decoder tests**

Add assertions that legacy opens preserve channels, `AnalysisMono` returns one channel, stereo produces `(0.25 + 0.50) / sqrt(2)`, antiphase stereo produces zero, 5.1 samples are finite and bounded, and `timeline_frames` equals the stream duration rescaled to the selected output sample rate.

```cpp
agplayer::DecoderOpenOptions options;
options.downmix = agplayer::DecoderDownmix::AnalysisMono;
assert(decoder.open(stereo_fixture, options) == AG_OK);
assert(decoder.output_format().channels == 1);
assert(decoder.output_format().has_timeline);
assert(decoder.read(block) == AG_OK);
assert(std::abs(block.samples.front() - 0.75F / std::sqrt(2.0F)) < 1.0e-4F);
```

- [ ] **Step 3: Run the red test**

Run:

```powershell
cmake --build build/frequency-clean-release --target decoder_test fixture_generator -j 2
ctest --test-dir build/frequency-clean-release -R '^decoder_test$' --output-on-failure
```

Expected: compile failure because `DecoderOpenOptions`, `DecoderDownmix` and `output_format()` do not exist.

- [ ] **Step 4: Implement timeline extraction and rematrix**

Rescale `AVStream::duration` from `stream->time_base` to output frames with `av_rescale_q`; set `has_timeline=false` for `AV_NOPTS_VALUE`, non-positive duration or overflow. Before `swr_init()`, create the analysis-mono matrix from `input_layout_`: mono identity; stereo coefficients `sqrt(0.5)` each; known multichannel roles use explicit coefficients and then L2-normalize; unknown layouts use equal-energy `1/sqrt(N)` coefficients. Call `swr_set_matrix()` while FFmpeg still owns the channel layout.

```cpp
if (options.downmix == DecoderDownmix::AnalysisMono) {
    output_channels_ = 1;
    output_layout_ = AV_CHANNEL_LAYOUT_MONO;
    const std::vector<double> matrix = analysisMonoMatrix(input_layout_);
    if (swr_set_matrix(swr_.get(), matrix.data(), input_layout_.nb_channels) < 0)
        return AG_UNSUPPORTED_FORMAT;
}
```

- [ ] **Step 5: Run decoder and compatibility tests**

Run:

```powershell
cmake --build build/frequency-clean-release --target decoder_test waveform_analyzer_test audio_engine_test -j 2
ctest --test-dir build/frequency-clean-release -R '^(decoder_test|waveform_analyzer_test|audio_engine_test)$' --output-on-failure
```

Expected: all pass; legacy channel-count assertions remain unchanged.

- [ ] **Step 6: Commit the decoder contract**

```powershell
git add -- core/src/decoder.hpp core/src/decoder.cpp tools/fixture_generator.cpp tests/core/decoder_test.cpp
git diff --cached --check
git commit -m "feat: add analysis mono decoder mode"
```

### Task 2: LR4 streaming Frequency Color analyzer

**Files:**
- Modify: `core/src/waveform_analyzer_filters.hpp`
- Create: `core/src/frequency_color_waveform_analyzer.hpp`
- Create: `core/src/frequency_color_waveform_analyzer.cpp`
- Modify: `core/CMakeLists.txt`
- Create: `tests/core/frequency_color_waveform_analyzer_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `DecoderOpenOptions{.downmix = AnalysisMono}` and `DecodedAudioFormat::timeline_frames` from Task 1.
- Produces:

```cpp
struct FrequencyColorWaveformData final {
    std::vector<float> mix;
    std::vector<float> low;
    std::vector<float> mid;
    std::vector<float> high;
    std::uint64_t timeline_frames = 0;
    std::uint64_t decoded_frames = 0;
    std::uint64_t duration_ms = 0;
    std::uint32_t sample_rate = 0;
};

struct FrequencyColorAnalysisDiagnostics final {
    std::uint32_t decoder_open_count = 0;
    std::uint64_t decoded_block_count = 0;
    std::size_t accumulator_bytes = 0;
};

class FrequencyColorWaveformAnalyzer final {
public:
    static constexpr std::uint32_t kAlgorithmVersion = 1;
    static constexpr std::size_t kDefaultPointCount = 2000;
    static ag_result analyze(const std::string& utf8_path,
                             std::size_t point_count,
                             const std::atomic_bool* cancelled,
                             ag_progress_callback progress,
                             void* user_data,
                             FrequencyColorWaveformData& output,
                             FrequencyColorAnalysisDiagnostics* diagnostics = nullptr) noexcept;
};
```

- [ ] **Step 1: Write pure filter and accumulator tests**

Generate in-memory mono signals for sample rates 44100, 48000, 88200, 96000 and 192000. Verify finite coefficients, LR4 crossover magnitude near -6 dB with a measured tolerance of ±0.8 dB, block-boundary invariance, 100 Hz low dominance, 1 kHz mid dominance and 8 kHz high dominance.

```cpp
const auto low100 = analyzeTone(48000, 100.0, 2.0);
assert(max(low100.low) > 2.0F * max(low100.mid));
assert(max(low100.low) > 4.0F * max(low100.high));
```

Add exact bucket tests for three frames into 2000 buckets, 2001 frames into 2000 buckets, timestamp gaps, PTS overlap rejection, cancellation at a block boundary and one shared normalization reference.

Run the same dominance/finite-output assertions through generated 16-bit PCM, 24-bit PCM and 32-bit float WAV; mono, stereo and 5.1; and the repository's lossless/lossy/VBR format fixtures. Use deterministic synthetic mixtures representing sub-bass-heavy, vocal-band-heavy, transient-heavy, classical-dynamic and fade/clip/silence cases so no copyrighted music enters the repository.

- [ ] **Step 2: Run the red analyzer target**

Run:

```powershell
cmake -S . -B build/frequency-clean-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/frequency-clean-release --target frequency_color_waveform_analyzer_test -j 2
ctest --test-dir build/frequency-clean-release -R '^frequency_color_waveform_analyzer_test$' --output-on-failure
```

Expected: configure or compile failure because the new target and analyzer do not exist.

- [ ] **Step 3: Add LR4 filters**

Implement two identical Q=`1/sqrt(2)` biquads per low/high branch, preserving state across calls. Build low as 20 Hz high-pass followed by 180 Hz low-pass, mid as 180 Hz high-pass followed by 2.8 kHz low-pass, and high as 2.8 kHz high-pass followed by a low-pass at `min(20000, 0.45*sampleRate)`.

```cpp
class LinkwitzRiley4 final {
public:
    enum class Type { LowPass, HighPass };
    LinkwitzRiley4(Type type, float sample_rate, float cutoff_hz) noexcept;
    float process(float sample) noexcept {
        return second_.process(first_.process(sample));
    }
private:
    BiquadFilter first_;
    BiquadFilter second_;
};
```

- [ ] **Step 4: Implement fixed-timeline streaming buckets**

Allocate exactly `point_count` accumulators for mix/low/mid/high. Map `absoluteTimestampFrame+i` directly into the bucket; track peak, sum of squares and count; reject non-finite samples and backward/overlapping PTS. Do not allocate by song duration and do not retain PCM blocks.

```cpp
const auto bucket = std::min<std::uint64_t>(
    pointCount - 1,
    frame * static_cast<std::uint64_t>(pointCount) / timelineFrames);
stats[bucket].peak = std::max(stats[bucket].peak, std::abs(sample));
stats[bucket].sumSquares += static_cast<double>(sample) * sample;
++stats[bucket].count;
```

- [ ] **Step 5: Implement post-processing**

For every bucket calculate `0.7*peak + 0.3*rms`; apply gains `{1.0, 10^(-1/20), 10^(3/20)}` to low/mid/high; calculate one 99.5-percentile reference across mix and all bands; apply -72 dBFS gate, symmetric moving averages 7/5/3, `pow(clamp(v/reference), 0.62)` and `min(v, 0.98)`. Empty buckets stay zero.

- [ ] **Step 6: Prove one-pass and bounded memory**

Use `FrequencyColorAnalysisDiagnostics` to assert `decoder_open_count == 1`, `decoded_block_count > 0`, and `accumulator_bytes <= pointCount * 4 * sizeof(BucketStats) + 4096`. Test cancellation and missing timeline return without partially populated output.

- [ ] **Step 7: Run analyzer regression set**

Run:

```powershell
cmake --build build/frequency-clean-release --target frequency_color_waveform_analyzer_test waveform_analyzer_test -j 2
ctest --test-dir build/frequency-clean-release -R '^(frequency_color_waveform_analyzer_test|waveform_analyzer_test|format_matrix_test)$' --output-on-failure
```

Expected: both pass; the legacy analyzer remains behavior-compatible.

- [ ] **Step 8: Commit analyzer**

```powershell
git add -- core/src/waveform_analyzer_filters.hpp core/src/frequency_color_waveform_analyzer.hpp core/src/frequency_color_waveform_analyzer.cpp core/CMakeLists.txt tests/core/frequency_color_waveform_analyzer_test.cpp tests/CMakeLists.txt
git diff --cached --check
git commit -m "feat: analyze frequency color envelopes in one pass"
```

### Task 3: Native result API and FCW1 cache

**Files:**
- Modify: `core/include/agplayer/c_api.h`
- Modify: `core/src/c_api.cpp`
- Create: `core/src/frequency_color_waveform_cache.hpp`
- Create: `core/src/frequency_color_waveform_cache.cpp`
- Modify: `core/CMakeLists.txt`
- Create: `tests/core/frequency_color_waveform_cache_test.cpp`
- Modify: `tests/core/waveform_analyzer_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `FrequencyColorWaveformAnalyzer::analyze()` from Task 2 and existing `ag_waveform` layer getters/ownership.
- Produces:

```c
ag_result ag_track_frequency_color_analysis(
    const char* utf8_path,
    size_t target_points,
    const ag_cancel_token* cancel_token,
    ag_progress_callback progress_callback,
    void* user_data,
    ag_waveform** out_waveform);

void ag_cancel_token_set_paused(ag_cancel_token* token, int paused);
```

```cpp
struct FrequencyColorCacheData final {
    std::vector<std::uint8_t> low;
    std::vector<std::uint8_t> mid;
    std::vector<std::uint8_t> high;
    std::uint32_t point_count = 0;
    std::uint32_t sample_rate = 0;
    std::uint64_t timeline_frames = 0;
    std::uint16_t crossover_low_hz = 180;
    std::uint16_t crossover_high_hz = 2800;
    std::uint32_t algorithm_version = 1;
};

class FrequencyColorWaveformCache final {
public:
    static bool load(const std::filesystem::path& cache,
                     const std::filesystem::path& source,
                     std::uint32_t expected_algorithm,
                     FrequencyColorCacheData& out) noexcept;
    static bool save_atomic(const std::filesystem::path& cache,
                            const std::filesystem::path& source,
                            const FrequencyColorCacheData& data) noexcept;
};
```

- [ ] **Step 1: Write failing C API and cache tests**

Assert the new call validates null/empty/zero-point arguments, returns 2000 equal-length layers, exposes finite timeline metadata through existing getters, owns/frees results with `ag_waveform_destroy`, and a paused cancel token blocks progress until resumed or cancelled. Add cache cases for round trip, source-size/mtime/path-fingerprint mismatch, bad magic, header truncation, payload truncation, CRC corruption, wrong algorithm, mismatched lengths and atomic replacement.

```cpp
assert(file_size(cache_path) < 8192U);
assert(FrequencyColorWaveformCache::load(
    cache_path, source_path, FrequencyColorWaveformAnalyzer::kAlgorithmVersion,
    loaded));
assert(loaded.low == original.low);
```

- [ ] **Step 2: Run red tests**

Run:

```powershell
cmake --build build/frequency-clean-release --target frequency_color_waveform_cache_test waveform_analyzer_test -j 2
ctest --test-dir build/frequency-clean-release -R '^(frequency_color_waveform_cache_test|waveform_analyzer_test)$' --output-on-failure
```

Expected: compile failure for the missing cache and C function.

- [ ] **Step 3: Implement the C API without BPM re-decode**

Call only `FrequencyColorWaveformAnalyzer::analyze`, move its float vectors into `ag_waveform`, set duration/timeline/sample rate, and do not invoke `analyze_bpm`. Keep `ag_track_analysis_with_aggregation` unchanged for ABI and non-frequency behavior. Extend the opaque cancel token with a paused flag and condition variable; the frequency call's internal progress bridge waits at block boundaries and wakes on resume or cancel, so pausing does not spin.

- [ ] **Step 4: Implement the 64-byte FCW1 header and CRC**

Write little-endian fields in this exact order: magic `FCW1`, format version `1`, header size `64`, point count, sample rate, crossover low/high, algorithm version, source size, source mtime ns, timeline frames, 64-bit FNV-1a fingerprint of the normalized canonical UTF-8 source path, payload CRC32, header CRC32. Calculate the header CRC with its own field zeroed. Payload is `low || mid || high`, exactly `3*pointCount` bytes. Validate all arithmetic before allocation and cap point count at 1,000,000.

- [ ] **Step 5: Implement crash-safe persistence**

Write to `<target>.tmp-<pid>-<counter>`, flush/close, re-open and validate, then replace with the same atomic rename helper/pattern used by `WaveformCache`. On every failure remove only the exact temporary file; never remove the source audio or AGWF cache.

- [ ] **Step 6: Run cache/API tests**

Run:

```powershell
cmake --build build/frequency-clean-release --target frequency_color_waveform_cache_test waveform_analyzer_test waveform_cache_test -j 2
ctest --test-dir build/frequency-clean-release -R '^(frequency_color_waveform_cache_test|waveform_analyzer_test|waveform_cache_test)$' --output-on-failure
```

Expected: pass, with a reported 2000-point cache size below 8192 bytes.

- [ ] **Step 7: Commit cache/API**

```powershell
git add -- core/include/agplayer/c_api.h core/src/c_api.cpp core/src/frequency_color_waveform_cache.hpp core/src/frequency_color_waveform_cache.cpp core/CMakeLists.txt tests/core/frequency_color_waveform_cache_test.cpp tests/core/waveform_analyzer_test.cpp tests/CMakeLists.txt
git diff --cached --check
git commit -m "feat: add validated FCW1 waveform cache"
```

### Task 4: Lazy provider orchestration and safe fallback

**Files:**
- Modify: `qt/src/waveform_provider.hpp`
- Modify: `qt/src/waveform_provider.cpp`
- Modify: `qt/src/cache_janitor.cpp`
- Modify: `app/main.cpp`
- Modify: `app/qml/AgPlayer/components/WaveformSession.qml`
- Modify: `tests/qt/waveform_provider_test.cpp`
- Modify: `tests/qml/tst_waveform.qml`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: existing AGWF mix cache and Task 3's `ag_track_frequency_color_analysis`/FCW1 cache.
- Produces:

```cpp
Q_INVOKABLE qulonglong loadForTrack(const QString& trackId,
                                    const QString& path,
                                    bool frequencyColor = false);
Q_INVOKABLE void cancelFrequencyForTrack(const QString& path);
void setAudioResourcePressure(bool pressured);
```

Published layer metadata:

```text
mix, bass, mid, high
_trackId, _generation, _durationMs, _totalSamples, _sampleRate
_frequencyRequested, _frequencyReady, _frequencyCacheVersion
```

- [ ] **Step 1: Write failing provider lifecycle tests**

Cover these exact sequences: non-frequency load never opens FCW; frequency request with AGWF hit publishes mix immediately and bands later; FCW hit publishes all layers without an analyzer job; both caches missing use one frequency analysis job and save AGWF mix plus FCW; corruption rebuilds FCW; cancellation/rapid switch/destruction never publishes stale bands; pause/resume makes progress stop and continue without a busy loop; prefetch remains mix-only.

```cpp
QSignalSpy ready(&provider, &WaveformProvider::waveformReady);
provider.loadForTrack(QStringLiteral("a"), fixture, true);
QTRY_VERIFY(ready.count() >= 1);
QVERIFY(!ready.at(0).at(1).toMap().value("_frequencyReady").toBool());
QTRY_VERIFY(ready.count() >= 2);
QVERIFY(ready.last().at(1).toMap().value("_frequencyReady").toBool());
```

- [ ] **Step 2: Run the red provider tests**

Run:

```powershell
cmake --build build/frequency-clean-release --target waveform_provider_test qml_waveform_test -j 2
ctest --test-dir build/frequency-clean-release -R '^(waveform_provider_test|qml_waveform_test)$' --output-on-failure
```

Expected: compile/QML failure because the request flag and readiness metadata do not exist.

- [ ] **Step 3: Split jobs into MixOnly and FrequencyColor**

Add `enum class JobKind { MixOnly, FrequencyColor }` to the provider job. Keep one `QThreadPool` with `setMaxThreadCount(1)` and set its worker priority to the lowest supported Qt priority. A frequency job uses the new C API and quantizes `round(clamp(value/0.98,0,1)*255)` for FCW. A mix-only job preserves the current aggregation path.

- [ ] **Step 4: Publish mix before bands**

On a frequency request, load AGWF mix first and emit it with `_frequencyReady=false`. Load FCW next; if valid, merge and emit `_frequencyReady=true`. If AGWF is absent, run the one-pass frequency job, publish/save its mix, then atomically save and publish all three bands. A missing/unsupported/cancelled frequency result leaves the already published mix intact.

- [ ] **Step 5: Enforce identity and cancellation**

Accept results only when path, track id, generation, job kind and current request mode all match. `cancelFrequencyForTrack` cancels only an active frequency job; switching to a non-frequency request for the same track cancels the band job and republishes mix-only layers.

Extend the existing cache cleanup enumeration to include FCW1 files under the same byte/age budget as AGWF. Deleting a stale FCW1 file must never delete its AGWF peer or source audio.

Connect `PlaybackController::deviceLostChanged` in `app/main.cpp` to `setAudioResourcePressure(playback.deviceLost())`. While a frequency job exists, check Windows `GetSystemPowerStatus().SystemStatusFlag` at most once every five seconds on the already-running progress timer and pause the token during energy saver. No timer or native power query exists when no frequency job is active.

- [ ] **Step 6: Gate requests in WaveformSession**

Bind the third argument to `SettingsController.waveformMode === 3 && active`. Store `_frequencyReady` separately from the layer arrays, reset it on track/generation changes, and release the frequency job when the session becomes inactive. Do not call `prefetchTracks` for bands.

- [ ] **Step 7: Run provider/QML regression**

Run:

```powershell
cmake --build build/frequency-clean-release --target waveform_provider_test qml_waveform_test qml_main_window_test -j 2
ctest --test-dir build/frequency-clean-release -R '^(waveform_provider_test|qml_waveform_test|qml_main_window_test)$' --output-on-failure
```

Expected: pass; test logs show a maximum of one frequency job and no FCW access in modes 0–2.

- [ ] **Step 8: Commit provider orchestration**

```powershell
git add -- qt/src/waveform_provider.hpp qt/src/waveform_provider.cpp qt/src/cache_janitor.cpp app/main.cpp app/qml/AgPlayer/components/WaveformSession.qml tests/qt/waveform_provider_test.cpp tests/qml/tst_waveform.qml tests/CMakeLists.txt
git diff --cached --check
git commit -m "feat: load frequency color bands on demand"
```

### Task 5: Luminous Glaze settings, OKLCH adaptation and migration

**Files:**
- Create: `qt/src/frequency_color_waveform_settings.hpp`
- Create: `qt/src/frequency_color_waveform_settings.cpp`
- Modify: `qt/src/settings_controller.hpp`
- Modify: `qt/src/settings_controller.cpp`
- Modify: `qt/CMakeLists.txt`
- Modify: `tests/qt/settings_controller_test.cpp`

**Interfaces:**
- Consumes: existing `SettingsController::beginEdit/commitEdit/cancelEdit`, `QSettings` appearance group, and legacy frequency color/strength keys.
- Produces:

```cpp
class FrequencyColorWaveformSettings final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString preset READ preset NOTIFY changed)
    Q_PROPERTY(QString mixDarkColor READ mixDarkColor WRITE setMixDarkColor NOTIFY changed)
    Q_PROPERTY(QString lowDarkColor READ lowDarkColor WRITE setLowDarkColor NOTIFY changed)
    Q_PROPERTY(QString midDarkColor READ midDarkColor WRITE setMidDarkColor NOTIFY changed)
    Q_PROPERTY(QString highDarkColor READ highDarkColor WRITE setHighDarkColor NOTIFY changed)
    Q_PROPERTY(QString mixLightColor READ mixLightColor WRITE setMixLightColor NOTIFY changed)
    Q_PROPERTY(QString lowLightColor READ lowLightColor WRITE setLowLightColor NOTIFY changed)
    Q_PROPERTY(QString midLightColor READ midLightColor WRITE setMidLightColor NOTIFY changed)
    Q_PROPERTY(QString highLightColor READ highLightColor WRITE setHighLightColor NOTIFY changed)
    Q_PROPERTY(double mixDarkOpacity READ mixDarkOpacity WRITE setMixDarkOpacity NOTIFY changed)
    Q_PROPERTY(double lowDarkOpacity READ lowDarkOpacity WRITE setLowDarkOpacity NOTIFY changed)
    Q_PROPERTY(double midDarkOpacity READ midDarkOpacity WRITE setMidDarkOpacity NOTIFY changed)
    Q_PROPERTY(double highDarkOpacity READ highDarkOpacity WRITE setHighDarkOpacity NOTIFY changed)
    Q_PROPERTY(double mixLightOpacity READ mixLightOpacity WRITE setMixLightOpacity NOTIFY changed)
    Q_PROPERTY(double lowLightOpacity READ lowLightOpacity WRITE setLowLightOpacity NOTIFY changed)
    Q_PROPERTY(double midLightOpacity READ midLightOpacity WRITE setMidLightOpacity NOTIFY changed)
    Q_PROPERTY(double highLightOpacity READ highLightOpacity WRITE setHighLightOpacity NOTIFY changed)
    Q_PROPERTY(bool playFocus READ playFocus WRITE setPlayFocus NOTIFY changed)
public:
    Q_INVOKABLE void resetToLuminousGlaze();
    void load(QSettings& settings);
    void save(QSettings& settings) const;
};
```

`SettingsController` exposes `Q_PROPERTY(FrequencyColorWaveformSettings* frequencyColorWaveform READ frequencyColorWaveform CONSTANT)` and retains legacy properties as forwarding aliases for one compatibility cycle.

- [ ] **Step 1: Write failing defaults, edit and migration tests**

Assert all 8 fixed colors, all 8 opacity defaults, focus true, 0.01 opacity quantization, finite/clamped values, invalid-color fallback, reset, persistence, edit cancel/commit, and no writes during an edit until commit. Cover every known old built-in tuple and two custom tuples.

```cpp
auto* fcw = settings.frequencyColorWaveform();
QCOMPARE(fcw->property("mixDarkColor").toString(), QStringLiteral("#7a8490"));
QCOMPARE(fcw->property("highLightOpacity").toDouble(), 0.40);
QVERIFY(fcw->setProperty("lowDarkOpacity", 0.437));
QCOMPARE(fcw->property("lowDarkOpacity").toDouble(), 0.44);
```

For custom dark colors, assert generated light colors retain OKLCH hue within 3 degrees, reduce excessive chroma, and reach at least 3:1 contrast against `#F5F3EF`; manually editing light stops future dark edits from overwriting that light role.

- [ ] **Step 2: Run the red settings test**

Run:

```powershell
cmake --build build/frequency-clean-release --target settings_controller_test -j 2
ctest --test-dir build/frequency-clean-release -R '^settings_controller_test$' --output-on-failure
```

Expected: compile failure because `FrequencyColorWaveformSettings` does not exist.

- [ ] **Step 3: Implement palette state and setters**

Store colors normalized as lowercase `#rrggbb`; store opacity as `round(clamp(value,0,1)*100)/100`. Any valid user edit changes preset to `custom`. Reset writes the exact Luminous Glaze values. Emit one `changed()` signal per semantic edit.

- [ ] **Step 4: Implement sRGB–OKLab–OKLCH conversion**

Use the published OKLab matrices as local arithmetic, not a dependency. Preserve hue, cap chroma at `0.16`, target dark-theme lightness in `[0.68,0.82]` and light-theme lightness in `[0.34,0.48]`, then binary-search lightness until the full-opacity outline color reaches 3:1 against `#0B1017` or `#F5F3EF`. Clamp gamut by reducing chroma before altering hue.

- [ ] **Step 5: Implement schema migration exactly once**

Use `appearance/waveformFrequencyColorSchema = 1`. When absent, map recognized historical default triples plus their matching legacy strengths to full Luminous Glaze defaults. Preserve any unknown/partially edited triple as custom dark low/mid/high, generate light partners, derive each band opacity as `round(clamp(oldStrength * roleDefault / 0.62,0,1)*100)/100`, and use the Luminous Glaze mix defaults. Keep old keys stored, but read the new object after schema 1 exists.

- [ ] **Step 6: Join SettingsController edit lifecycle**

Call child `load/save/reset` inside the same appearance group and make `cancelEdit()` reload it with all other settings. Legacy getters/setters forward to the dark palette and derived band opacity without becoming persistence truth.

- [ ] **Step 7: Run settings regression**

Run:

```powershell
cmake --build build/frequency-clean-release --target settings_controller_test qml_main_window_test -j 2
ctest --test-dir build/frequency-clean-release -R '^(settings_controller_test|qml_main_window_test)$' --output-on-failure
```

Expected: pass, including old settings files and custom-value preservation.

- [ ] **Step 8: Commit settings model**

```powershell
git add -- qt/src/frequency_color_waveform_settings.hpp qt/src/frequency_color_waveform_settings.cpp qt/src/settings_controller.hpp qt/src/settings_controller.cpp qt/CMakeLists.txt tests/qt/settings_controller_test.cpp
git diff --cached --check
git commit -m "feat: add per-layer frequency waveform settings"
```

### Task 6: Static four-layer SceneGraph renderer and play focus

**Files:**
- Create: `qt/src/waveform_layer_material.hpp`
- Create: `qt/src/waveform_layer_material.cpp`
- Create: `qt/shaders/waveform_layer.vert`
- Create: `qt/shaders/waveform_layer.frag`
- Modify: `qt/src/waveform_item.hpp`
- Modify: `qt/src/waveform_item.cpp`
- Modify: `qt/CMakeLists.txt`
- Modify: `tests/qt/waveform_item_test.cpp`

**Interfaces:**
- Consumes: existing `WaveformItem::layers`, duration/position/cursor/visible-range properties and Task 5 palette values.
- Produces these `WaveformItem` properties while retaining the old three-color/global-strength aliases:

```cpp
Q_PROPERTY(QColor frequencyMixColor READ frequencyMixColor WRITE setFrequencyMixColor NOTIFY frequencyStyleChanged)
Q_PROPERTY(double frequencyMixOpacity READ frequencyMixOpacity WRITE setFrequencyMixOpacity NOTIFY frequencyStyleChanged)
Q_PROPERTY(double frequencyLowOpacity READ frequencyLowOpacity WRITE setFrequencyLowOpacity NOTIFY frequencyStyleChanged)
Q_PROPERTY(double frequencyMidOpacity READ frequencyMidOpacity WRITE setFrequencyMidOpacity NOTIFY frequencyStyleChanged)
Q_PROPERTY(double frequencyHighOpacity READ frequencyHighOpacity WRITE setFrequencyHighOpacity NOTIFY frequencyStyleChanged)
Q_PROPERTY(double frequencyBandFade READ frequencyBandFade WRITE setFrequencyBandFade NOTIFY frequencyStyleChanged)
Q_PROPERTY(bool frequencyPlayFocus READ frequencyPlayFocus WRITE setFrequencyPlayFocus NOTIFY frequencyStyleChanged)
Q_PROPERTY(QColor frequencyFocusColor READ frequencyFocusColor WRITE setFrequencyFocusColor NOTIFY frequencyStyleChanged)
Q_PROPERTY(int effectiveFrequencyQuality READ effectiveFrequencyQuality NOTIFY effectiveFrequencyQualityChanged)
```

- [ ] **Step 1: Replace test helpers and write failing renderer tests**

Change tests from “root is one geometry node” to a typed root-container helper. Assert fixed order baseline/mix/low/mid/high/focus, one geometry node per waveform layer, triangle geometry with fill and two outline ribbons, exact palette/alpha uniforms, explicit blending, and premultiplied shader output contract.

```cpp
auto* root = asFrequencyRoot(item.updatePaintNode(nullptr, nullptr));
QCOMPARE(root->layer(NodeRole::Mix)->geometry()->drawingMode(),
         QSGGeometry::DrawTriangles);
QVERIFY(root->layer(NodeRole::Low)->material()->flags().testFlag(QSGMaterial::Blending));
```

Snapshot all four vertex buffers, change position/cursor/colors/opacities/fade, call `updatePaintNode` again and assert node/geometry pointers, vertex counts and coordinates are identical. Assert only material state and the focus transform change.

- [ ] **Step 2: Run the red item test**

Run:

```powershell
cmake --build build/frequency-clean-release --target waveform_item_test -j 2
ctest --test-dir build/frequency-clean-release -R '^waveform_item_test$' --output-on-failure
```

Expected: compile/assertion failure because the root container, material and properties do not exist.

- [ ] **Step 3: Add one-draw layer geometry**

Use one custom vertex buffer per layer with `{x, y, alphaRole}`. Each adjacent sample pair contributes a fill quad and 0.8 dp upper/lower outline ribbons as indexed triangles. `alphaRole=0` selects fill Alpha and `1` selects outline Alpha. Rebuild only for snapshot revision, size, effective DPR, density, visible range, amplitude, quality or layer-presence changes.

Derive outline Alpha from the user fill opacity by the fixed Luminous Glaze ratio for that role/theme, clamped to 1.0; at user opacity 0 both fill and outline are zero. Thus one control scales the complete layer proportionally and never leaves a hidden fill with a visible outline.

- [ ] **Step 4: Add lightweight material and shaders**

Register QSB resources with prefix `/agplayer/shaders`. The material uniform block contains matrix, inherited opacity, premultiplied base color, fill/outline Alpha, layer fade, progress x, feather width, unplayed Alpha `0.72` and desaturation `0.08`. The fragment shader computes the progress transition from item-local x and returns `vec4(rgb * alpha, alpha)`.

```glsl
float played = 1.0 - smoothstep(progressX - featherPx, progressX + featherPx, localX);
float gain = mix(0.72, 1.0, played);
vec3 unplayedRgb = mix(rgb, vec3(dot(rgb, vec3(0.2126, 0.7152, 0.0722))), 0.08);
vec3 outRgb = mix(unplayedRgb, rgb, played);
float alpha = mix(fillAlpha, outlineAlpha, alphaRole) * layerFade * gain * qt_Opacity;
fragColor = vec4(outRgb * alpha, alpha);
```

- [ ] **Step 5: Add focus nodes and DPR alignment**

Use `effectiveDevicePixelRatio()` capped at 4.0. Put the focus under a `QSGTransformNode`; build a 1 dp core, two low-alpha halo ribbons (4 and 8 dp) and a 6 dp dot. Snap x to `round(x*dpr)/dpr`; update only the transform for playback and hide the focus when disabled or outside the visible interval.

- [ ] **Step 6: Add software fallback and height policy**

Detect `QSGRendererInterface::Software`. In software or effective Low quality, use bounded `QSGVertexColorMaterial` lines with `min(512, ceil(width*dpr*density/2))` points, no fill and no custom shader. Height 64–119 selects Balanced with smaller focus halo; 32–63 selects Low-style reduced fill; below 32 displays simplified mix plus combined band outline.

- [ ] **Step 7: Run renderer tests on hardware and software backends**

Run:

```powershell
cmake --build build/frequency-clean-release --target waveform_item_test -j 2
ctest --test-dir build/frequency-clean-release -R '^waveform_item_test$' --output-on-failure
$env:QT_QUICK_BACKEND='software'; ctest --test-dir build/frequency-clean-release -R '^waveform_item_test$' --output-on-failure; Remove-Item Env:QT_QUICK_BACKEND
```

Expected: both pass; software assertions select the bounded line path.

- [ ] **Step 8: Commit renderer**

```powershell
git add -- qt/src/waveform_layer_material.hpp qt/src/waveform_layer_material.cpp qt/shaders/waveform_layer.vert qt/shaders/waveform_layer.frag qt/src/waveform_item.hpp qt/src/waveform_item.cpp qt/CMakeLists.txt tests/qt/waveform_item_test.cpp
git diff --cached --check
git commit -m "feat: render layered frequency waveform geometry"
```

### Task 7: QML settings and all player surfaces

**Files:**
- Modify: `app/qml/AgPlayer/SettingsPage.qml`
- Modify: `app/qml/AgPlayer/components/PlayerPane.qml`
- Modify: `app/qml/AgPlayer/components/MiniPlayerControls.qml`
- Modify: `app/qml/AgPlayer/components/IntegratedPlayerShell.qml`
- Modify: `app/qml/AgPlayer/components/SharedWaveformView.qml`
- Modify: `tests/qml/tst_main_window.qml`
- Modify: `tests/qml/tst_mini_player.qml`
- Modify: `tests/qml/tst_integrated_theme.qml`
- Modify: `tests/qml/tst_audio_visual_impact.qml`
- Modify: `tests/qml/tst_immersive_integration.qml`

**Interfaces:**
- Consumes: `SettingsController.frequencyColorWaveform`, `WaveformSession.frequencyReady`, and all Task 6 `WaveformItem` properties.
- Produces: one consistent binding block used by every production Frequency Color `WaveformItem` and a 150 ms band fade.

```qml
frequencyMixColor: darkSurface ? fcw.mixDarkColor : fcw.mixLightColor
frequencyLowColor: darkSurface ? fcw.lowDarkColor : fcw.lowLightColor
frequencyMidColor: darkSurface ? fcw.midDarkColor : fcw.midLightColor
frequencyHighColor: darkSurface ? fcw.highDarkColor : fcw.highLightColor
frequencyMixOpacity: darkSurface ? fcw.mixDarkOpacity : fcw.mixLightOpacity
frequencyLowOpacity: darkSurface ? fcw.lowDarkOpacity : fcw.lowLightOpacity
frequencyMidOpacity: darkSurface ? fcw.midDarkOpacity : fcw.midLightOpacity
frequencyHighOpacity: darkSurface ? fcw.highDarkOpacity : fcw.highLightOpacity
frequencyFocusColor: darkSurface ? "#F2E7D4" : "#26313A"
frequencyPlayFocus: fcw.playFocus
```

- [ ] **Step 1: Write failing QML contracts**

For settings, find controls by stable object names and assert four rows, separate dark/light edit targets, color propagation, opacity 0/44/100%, focus toggle, preset change and reset. For each player surface, assert mode 3 has one native canvas, receives the same four colors/opacities, does not add a QML focus line, and maintains click/scrub/zoom signal behavior.

```qml
verify(findChild(settingsPage, "frequencyMixColorField") !== null)
compare(findChild(settingsPage, "frequencyLowOpacitySlider").value, 44)
compare(String(waveform.frequencyMixColor), "#7a8490")
compare(waveform.frequencyLowOpacity, 0.44)
```

- [ ] **Step 2: Run the red QML set**

Run:

```powershell
cmake --build build/frequency-clean-release --target qml_main_window_test qml_waveform_test -j 2
ctest --test-dir build/frequency-clean-release -R '^(qml_waveform_test|qml_main_window_test|qml_mini_player_test|qml_integrated_theme_test|qml_audio_visual_impact_test|qml_immersive_integration_test)$' --output-on-failure
```

Expected: failures for missing settings controls/properties and old three-color expectations.

- [ ] **Step 3: Build the compact settings section**

Keep the existing waveform mode selector. Under mode 3 add preset label, dark/light preview selector, four rows ordered mix/low/mid/high, each with the existing `ColorField`, an accessible opacity slider/spin display from 0 to 100 step 1, focus switch, and “恢复曜釉默认”. Bind rows to the selected preview theme without switching the application theme.

- [ ] **Step 4: Implement live preview and counterpart behavior**

Every valid color/opacity edit updates the child settings object immediately, changes the preset label to “自定义”, and repaints the preview only. `beginEdit/cancelEdit/commitEdit` controls persistence. Color changes invoke the child object's automatic counterpart logic; manual edits in the other theme remain authoritative.

- [ ] **Step 5: Bind all production surfaces**

Use the exact binding block above in `PlayerPane`, `MiniPlayerControls`, `IntegratedPlayerShell` and `SharedWaveformView`. Bind `frequencyBandFade` to `waveformSession.frequencyReady ? 1 : 0` with `NumberAnimation { duration: 150; easing.type: Easing.OutCubic }`. Remove duplicate QML focus lines for mode 3 while retaining non-frequency playback guides and overlays.

- [ ] **Step 6: Preserve interaction layering**

Leave MouseArea/PointerHandler, time mapper, Cue, selection and drag-out items above the native waveform. Selection uses its existing neutral overlay and never alters band colors. Keep list thumbnails on the existing lightweight provider/item.

- [ ] **Step 7: Run QML and interaction regression**

Run:

```powershell
cmake --build build/frequency-clean-release --target qml_main_window_test qml_waveform_test -j 2
ctest --test-dir build/frequency-clean-release -R '^(qml_waveform_test|qml_main_window_test|qml_mini_player_test|qml_integrated_theme_test|qml_audio_visual_impact_test|qml_immersive_integration_test|waveform_coordinate_mapper_test)$' --output-on-failure
```

Expected: pass in offscreen mode; no old Open Color token remains in production QML.

- [ ] **Step 8: Commit QML integration**

```powershell
git add -- app/qml/AgPlayer/SettingsPage.qml app/qml/AgPlayer/components/PlayerPane.qml app/qml/AgPlayer/components/MiniPlayerControls.qml app/qml/AgPlayer/components/IntegratedPlayerShell.qml app/qml/AgPlayer/components/SharedWaveformView.qml tests/qml/tst_main_window.qml tests/qml/tst_mini_player.qml tests/qml/tst_integrated_theme.qml tests/qml/tst_audio_visual_impact.qml tests/qml/tst_immersive_integration.qml
git diff --cached --check
git commit -m "feat: expose luminous glaze waveform controls"
```

### Task 8: Automatic quality hysteresis and performance evidence

**Files:**
- Modify: `qt/src/waveform_item.hpp`
- Modify: `qt/src/waveform_item.cpp`
- Create: `tests/stress/frequency_color_waveform_stress_test.cpp`
- Modify: `tests/CMakeLists.txt`
- Create: `scripts/qa-frequency-color-waveform.ps1`

**Interfaces:**
- Consumes: Task 6's `effectiveFrequencyQuality` and geometry/material revision counters.
- Produces: a no-timer frame-pressure tracker and a reproducible JSON/Markdown measurement run.

```cpp
struct FrequencyFramePressure final {
    int slow_frames = 0;
    int stable_frames = 0;
    int quality_penalty = 0;
    void record(std::chrono::milliseconds interval) noexcept;
};
```

- [ ] **Step 1: Write failing hysteresis and stress tests**

Feed 60 intervals at 25 ms and assert one demotion after 45 slow samples; feed mixed intervals and assert no oscillation; feed 240 intervals below 18 ms and assert one promotion. Stress 3840 logical pixels at DPR 2 with four 2000-point bands, 10,000 progress updates and 1,000 zoom/resize rebuilds. Assert progress never changes layer geometry revisions and resident test-process growth settles within 8 MiB after warm-up.

- [ ] **Step 2: Run the red stress target**

Run:

```powershell
cmake --build build/frequency-clean-release --target frequency_color_waveform_stress_test -j 2
ctest --test-dir build/frequency-clean-release -R '^frequency_color_waveform_stress_test$' --output-on-failure
```

Expected: configure/compile failure because the target and tracker do not exist.

- [ ] **Step 3: Implement hysteresis without polling**

Record intervals only when the item is visible, mode 3 is active and position/cursor changes. At 45 of the last 60 intervals over 22 ms, increment penalty to a maximum of 2 and reset counters. At 240 consecutive intervals below 18 ms, decrement penalty once. Combine penalty with height and software-backend policy; a quality change invalidates geometry once, while stable playback remains material-only.

- [ ] **Step 4: Add a reproducible QA script**

The script accepts `-BuildDirectory`, `-Fixture`, and `-OutputDirectory`; validates all resolved paths stay under the repo/output directory; records executable/resources size, five cold frequency analyses, five FCW cache hits, FCW file sizes, 10-minute progress-update CPU/memory, and focused CTest output. It emits `frequency-color-metrics.json` plus `frequency-color-summary.md` and returns nonzero when cache ≥8192 bytes, size delta >1 MiB, non-frequency median regression >3%, analyzer opens !=1, or tests fail.

- [ ] **Step 5: Capture baseline and candidate measurements**

Use the current integration base commit as baseline in a temporary `codex/` worktree only for measurement; never copy its files over the dirty main checkout. Compare the same Release configuration, fixture and five-run median. If creating the baseline worktree is unsafe, record the exact reason and compare against the pre-change executable/metrics captured before Task 1.

- [ ] **Step 6: Run stress and QA**

Run:

```powershell
cmake --build build/frequency-clean-release --target frequency_color_waveform_stress_test AgPlayer -j 2
ctest --test-dir build/frequency-clean-release -R '^(frequency_color_waveform_stress_test|waveform_item_test|waveform_provider_test)$' --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/qa-frequency-color-waveform.ps1 -BuildDirectory build/frequency-clean-release -Fixture build/frequency-clean-release/tests/fixtures/sine-440hz.wav -OutputDirectory artifacts/frequency-color-waveform
```

Expected: pass; JSON contains measured—not estimated—cache, CPU, memory, open-count and size-delta fields.

- [ ] **Step 7: Commit quality and measurement harness**

```powershell
git add -- qt/src/waveform_item.hpp qt/src/waveform_item.cpp tests/stress/frequency_color_waveform_stress_test.cpp tests/CMakeLists.txt scripts/qa-frequency-color-waveform.ps1
git diff --cached --check
git commit -m "test: add frequency waveform performance gates"
```

### Task 9: Full build, visual matrix, documentation and final review

**Files:**
- Create: `docs/development/2026-08-31-frequency-waveform-acceptance.md`
- Modify: `docs/development/2026-08-30-frequency-waveform-overlay-acceptance.md`
- Modify if evidence requires it: `scripts/qa-final-ui-matrix.ps1`
- Review: every file named by Tasks 1–8

**Interfaces:**
- Consumes: completed implementation and `artifacts/frequency-color-waveform` measurements.
- Produces: traceable acceptance evidence and an honest residual-risk statement.

- [ ] **Step 1: Reconfigure and build the whole Release application**

Run:

```powershell
cmake -S . -B build/frequency-clean-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/frequency-clean-release --target all -j 2
```

Expected: no compile/link/QML-cache/shader-bake error.

- [ ] **Step 2: Run focused functional suites**

Run:

```powershell
ctest --test-dir build/frequency-clean-release -R '^(decoder_test|waveform_analyzer_test|frequency_color_waveform_analyzer_test|waveform_cache_test|frequency_color_waveform_cache_test|settings_controller_test|waveform_provider_test|waveform_coordinate_mapper_test|waveform_item_test|qml_waveform_test|qml_audio_visual_impact_test|qml_main_window_test|qml_integrated_theme_test|qml_mini_player_test|qml_immersive_integration_test|frequency_color_waveform_stress_test)$' --output-on-failure
```

Expected: all pass.

- [ ] **Step 3: Run the complete automated suite**

Run:

```powershell
ctest --test-dir build/frequency-clean-release --output-on-failure -j 2
```

Expected: all non-hardware tests pass. Record any skip with its CTest name and skip reason; do not recast a failure as a hardware gap.

- [ ] **Step 4: Run application smoke and UI matrix**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/qa-main-smoke.ps1 -BuildDirectory build/frequency-clean-release
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/qa-final-ui-matrix.ps1 -BuildDirectory build/frequency-clean-release -OutputDirectory artifacts/frequency-color-waveform/ui-matrix
```

Exercise dark, light and system themes; 100/125/150/200% DPI; 720p/1080p/2K/4K-sized windows; narrow/wide/fullscreen; zoom and rapid scrub. Capture PNGs that include both waveform and settings controls. If the existing script cannot set a matrix dimension, add an explicit parameter and a deterministic QA flag instead of manually renaming screenshots.

- [ ] **Step 5: Inspect visual evidence**

Open every dark/light/DPI reference image and verify: four layers are distinct; mix is subordinate; overlap is neither muddy brown nor blown white; focus is crisp and stable; 0% hides exactly one layer; 100% does not clip; played/unplayed keep hue; geometry aligns with the time ruler. Add grayscale and deuteranopia simulations as derived evidence, clearly labeled simulation.

- [ ] **Step 6: Run diff and repository hygiene review**

Run:

```powershell
git diff --check
git status --short
git diff --stat
git diff -- core/src qt/src app/qml tests scripts docs/development docs/superpowers/plans
rg -n '#1098ad|#f59f00|#ae3ec9|Open Color' core qt app tests scripts docs/development/2026-08-31-frequency-waveform-acceptance.md
```

Expected: no whitespace errors, no obsolete production palette, no placeholder acceptance text, and no unrelated object/text files staged.

- [ ] **Step 7: Write the acceptance record**

Include a requirement-to-evidence table for analysis, cache, provider, render, settings, migration, interactions, accessibility, performance and fallback. Copy exact commands, exit codes, test counts, cache bytes, five-run medians, memory peak, executable/resource delta and screenshot paths. Separate `已验证`, `未验证`, and `硬件环境限制` sections.

At the top of the 2026-08-30 Open Color record add:

```markdown
> 已由 `2026-08-31-frequency-waveform-acceptance.md` 的“曜釉”完整实现取代；本文仅保留为历史证据，不再代表当前默认设计。
```

- [ ] **Step 8: Independent standards and correctness review**

Review the final diff against the input specification and design document. Reject changes that touch playback DSP, analyze bands outside mode 3, persist colors in FCW, rebuild four geometries on progress, overwrite custom migration values, omit one player surface, or report an unmeasured performance claim.

- [ ] **Step 9: Commit documentation only after evidence exists**

```powershell
git add -- docs/development/2026-08-31-frequency-color-waveform-redesign.md docs/development/2026-08-31-frequency-waveform-acceptance.md docs/development/2026-08-30-frequency-waveform-overlay-acceptance.md docs/superpowers/plans/2026-08-31-frequency-color-waveform-redesign.md
git diff --cached --check
git commit -m "docs: record frequency color waveform acceptance"
```

## Completion Gate

The feature is complete only when Tasks 1–9 are checked, the Release build succeeds, focused and complete automated suites have no unexplained failure, FCW1 is below 8192 bytes at 2000 points, one-pass count is 1, non-frequency medians remain within 3%, size delta is within 1 MiB, visual evidence covers both themes and available DPI/window states, and the acceptance record identifies any real-device audio/GPU/color-display checks that this host cannot prove.
