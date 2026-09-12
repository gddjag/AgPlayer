#include "decoder.hpp"
#include "visual_audio_frame_analyzer.hpp"
#include <QCoreApplication>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>

// Opt-in local-music diagnostic, not a beat-accuracy oracle. No audio or
// personal paths are uploaded, and no player library/settings are changed.
int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (application.arguments().size() != 2) return 2;
    agplayer::Decoder decoder;
    const auto path = application.arguments().at(1).toUtf8();
    if (decoder.open(path.constData(), 44100, 1) != AG_OK) return 3;
    agplayer::VisualAudioFrameAnalyzer analyzer;
    agplayer::visual::KickResponse reference(agplayer::visual::KickResponse::Mode::Reference);
    int referenceOnsets = 0, suppressedReference = 0;
    std::vector<std::uint64_t> originalFrames, nativeFrames;
    agplayer::VisualAudioFrameAnalyzer::Snapshot snapshot;
    snapshot.valid = true; snapshot.epoch = 1; snapshot.sampleRate = 44100;
    std::array<float, 1024> ring{};
    std::uint64_t samples = 0, nextEnd = ring.size();
    std::array<int, 5> segmentOnsets{};
    double lastOnset = 0, maximumGap = 0, lastAnalyzed = 0;
    const double duration = double(decoder.metadata().duration_ms) / 1000;
    agplayer::DecodedAudioBlock block;
    for (;;) {
        if (decoder.read(block) != AG_OK) return 4;
        for (const float sample : block.samples) {
            ring[std::size_t(samples++ % ring.size())] = sample;
            if (samples < nextEnd) continue;
            snapshot.firstSampleIndex = samples - ring.size();
            ++snapshot.sequence;
            for (std::size_t i = 0; i < ring.size(); ++i)
                snapshot.pcm[i] = ring[std::size_t((snapshot.firstSampleIndex + i) % ring.size())];
            const auto& frame = analyzer.process(snapshot, 1.0 / 60.0);
            const auto original = reference.process(frame.spectrum, 1.0 / 60.0);
            if (original.onset > 0) {
                originalFrames.push_back(snapshot.sequence);
                ++referenceOnsets;
                if (frame.kick.onset == 0) {
                    ++suppressedReference;
                    std::cout << "filtered_reference seconds=" << double(samples) / 44100
                              << " brightness=" << frame.descriptors.brightness
                              << " warmth=" << frame.descriptors.warmth
                              << " smoothness=" << frame.descriptors.smoothness << '\n';
                }
            }
            lastAnalyzed = double(samples) / 44100;
            if (!std::isfinite(frame.kick.envelope) || frame.kick.onset != original.onset
                || std::abs(frame.kick.envelope-original.envelope)>1e-12) return 5;
            if (frame.kick.onset > 0) {
                nativeFrames.push_back(snapshot.sequence);
                const int segment = std::clamp(int(lastAnalyzed / std::max(1.0, duration) * 5), 0, 4);
                ++segmentOnsets[std::size_t(segment)];
                maximumGap = std::max(maximumGap, lastAnalyzed - lastOnset);
                lastOnset = lastAnalyzed;
            }
            nextEnd += 735;
        }
        if (block.end_of_stream) break;
    }
    const double decodedSeconds = double(samples) / 44100;
    if (decodedSeconds <= 0 || decodedSeconds - lastAnalyzed > .05) return 6;
    std::cout << "decoded_seconds=" << decodedSeconds << " analyzed_windows=" << snapshot.sequence
              << " last_window=" << lastAnalyzed << " last_onset=" << lastOnset
              << " longest_onset_gap=" << maximumGap << " segment_onsets=";
    for (const auto count : segmentOnsets) std::cout << count << ',';
    std::cout << " (diagnostic only; no annotated ground truth)\n";
    std::cout << "reference_onsets=" << referenceOnsets
              << " reference_frames_without_native_onset=" << suppressedReference << '\n';
    std::size_t native = 0, matched = 0;
    for (const auto expected : originalFrames) {
        while (native < nativeFrames.size() && nativeFrames[native] + 3 < expected) ++native;
        if (native < nativeFrames.size() && nativeFrames[native] <= expected + 3) { ++matched; ++native; }
    }
    std::cout << "reference_matches_within_50ms=" << matched << '/' << originalFrames.size()
              << " (one-to-one; algorithm comparison, not annotated drum recall)\n";
    return 0;
}
