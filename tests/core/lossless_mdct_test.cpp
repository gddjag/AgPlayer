#include "lossless/lossless_mdct.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
constexpr double pi = 3.14159265358979323846;
void require(bool value, const char* message)
{
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
// Independent inverse-transform synthesis: a lapped sine-window signal with
// sparse coefficients, the structural effect being detected. No encoder/name
// hints are provided to the probe. Direct cosine synthesis is an FFT oracle.
std::vector<double> sparseLappedSignal(std::size_t n = 2048)
{
    const auto h = n / 2;
    constexpr std::size_t blocks = 40;
    std::vector<double> samples((blocks + 1) * h, 0.0);
    std::uint32_t state = 1907;
    for (std::size_t block = 0; block < blocks; ++block) {
        for (std::size_t bin = 0; bin < 48; ++bin) {
            state = state * 1664525U + 1013904223U;
            const auto k = static_cast<double>((state >> 8U) % h);
            const double amplitude = (state & 1U) ? 0.006 : -0.006;
            for (std::size_t index = 0; index < n; ++index) {
                const double x = static_cast<double>(index);
                samples[block * h + index] += amplitude
                    * std::cos(2.0 * pi / n * (x + 0.5 + n / 4.0) * (k + 0.5))
                    * std::sin(pi / n * (x + 0.5));
            }
        }
    }
    return samples;
}
auto scan(const std::vector<double>& mono, std::size_t chunk, bool opposite = false)
{
    const int channels = opposite ? 2 : 1;
    agplayer::lossless::MdctFramingProbe probe(44100, channels, mono.size());
    std::atomic_bool cancel{false};
    for (std::size_t start = 0; start < mono.size(); start += chunk) {
        const auto count = std::min(chunk, mono.size() - start);
        if (!opposite) probe.consume(mono.data() + start, count, cancel);
        else {
            std::vector<double> stereo(count * 2);
            for (std::size_t i = 0; i < count; ++i) {
                stereo[2 * i] = mono[start + i]; stereo[2 * i + 1] = -mono[start + i];
            }
            probe.consume(stereo.data(), count, cancel);
        }
    }
    return probe.result();
}
}
int main()
{
    const auto signal = sparseLappedSignal();
    const auto whole = scan(signal, signal.size());
    require(whole[0].activeBlocks == 4, "four independent active blocks");
    require(whole[0].coherentPeakDb > 6.0 && whole[0].meanPeakZ > 12.0,
            "lapped sparse MDCT coefficients must reveal framing");
    const auto chunked = scan(signal, 997, true);
    for (std::size_t i = 0; i < whole.size(); ++i) {
        require(std::abs(whole[i].coherentPeakDb - chunked[i].coherentPeakDb) < 1e-9,
                "decoder chunking and antiphase channels must preserve framing");
    }
    const auto shortFrames = scan(sparseLappedSignal(1152), 127);
    require(shortFrames[3].coherentPeakDb >= 2.0 && shortFrames[3].meanPeakZ >= 5.0
            && shortFrames[3].alignedBlocks >= 3,
            "1152-frame evidence must retain precise phase alignment across chunks");
    std::vector<double> noise(signal.size());
    std::uint32_t random = 71;
    for (auto& value : noise) {
        random = random * 1664525U + 1013904223U;
        value = (static_cast<double>(random) / 4294967296.0 - 0.5) * 0.2;
    }
    const auto negative = scan(noise, 1024);
    for (const auto& value : negative)
        require(value.coherentPeakDb < 6.0 || value.meanPeakZ < 12.0,
                "uncompressed random PCM must not produce codec framing");
    for (const int codedChannel : {0, 1}) {
        agplayer::lossless::MdctFramingProbe stereo(44100, 2, signal.size());
        std::atomic_bool cancel{false};
        std::vector<double> samples(signal.size() * 2);
        for (std::size_t i = 0; i < signal.size(); ++i) {
            samples[2 * i + codedChannel] = signal[i] * 0.03;
            samples[2 * i + 1 - codedChannel] = noise[i];
        }
        for (std::size_t start = 0; start < signal.size(); start += 997)
            stereo.consume(samples.data() + start * 2, std::min<std::size_t>(997, signal.size() - start), cancel);
        require(stereo.result()[0].coherentPeakDb < 6.0,
                "fixture must reproduce louder-channel masking");
        cancel = true;
        stereo.refineStereo(cancel);
        require(stereo.channelResult(codedChannel)[0].activeBlocks == 0,
                "cancel must prevent deferred stereo work");
        cancel = false;
        stereo.refineStereo(cancel);
        const auto coded = stereo.channelResult(codedChannel)[0];
        const auto clean = stereo.channelResult(1 - codedChannel)[0];
        require(coded.activeBlocks == 4 && coded.coherentPeakDb > 6.0 && coded.meanPeakZ > 12.0,
                "quiet transform-coded channel must retain independent framing evidence");
        require(clean.coherentPeakDb < 6.0 || clean.meanPeakZ < 12.0,
                "coded-channel evidence must not leak into clean-channel result");
        stereo.refineStereo(cancel);
        require(stereo.channelResult(codedChannel)[0].activeBlocks == coded.activeBlocks,
                "repeated refinement must not count excerpts twice");
        require(stereo.channelResult(2)[0].activeBlocks == 0,
                "invalid channel must not alias a real channel");
    }
    {
        agplayer::lossless::MdctFramingProbe stereo(44100, 2, noise.size());
        std::atomic_bool cancel{false};
        std::vector<double> samples(noise.size() * 2);
        for (std::size_t i = 0; i < noise.size(); ++i) {
            samples[2 * i] = noise[i];
            samples[2 * i + 1] = noise[noise.size() - 1 - i] * 0.03;
        }
        stereo.consume(samples.data(), noise.size(), cancel);
        stereo.refineStereo(cancel);
        for (int channel = 0; channel < 2; ++channel)
            for (const auto& value : stereo.channelResult(channel)) {
                const bool mp3 = std::string_view(value.window) == "sine1152";
                require(mp3 ? (value.coherentPeakDb < 2.0 || value.meanPeakZ < 5.0 || value.alignedBlocks < 3)
                            : (value.coherentPeakDb < 6.0 || value.meanPeakZ < 12.0),
                        "independent native stereo PCM must not be labelled transform-coded");
            }
    }
    for (std::size_t index = 0; index < noise.size(); ++index)
        noise[index] = 0.2 * std::sin(2.0 * pi * 997.0 * index / 44100.0);
    const auto tone = scan(noise, 4096);
    for (const auto& value : tone)
        require(value.coherentPeakDb < 6.0 || value.meanPeakZ < 12.0,
                "a stationary natural tone is not proof of codec framing");
    for (const double period : {576.0, 1024.0}) {
        for (std::size_t index = 0; index < noise.size(); ++index)
            noise[index] = 0.2 * std::sin(2.0 * pi * index / period);
        const auto periodic = scan(noise, 997);
        require(periodic[3].coherentPeakDb < 2.0 || periodic[3].meanPeakZ < 5.0
                || periodic[3].alignedBlocks < 3,
                "an uncompressed codec-grid-periodic tone must not imply MP3 framing");
        for (std::size_t i = 0; i < 3; ++i)
            require(periodic[i].coherentPeakDb < 6.0 || periodic[i].meanPeakZ < 12.0,
                    "an uncompressed codec-grid-periodic tone must not imply MDCT coding");
    }
    std::vector<double> loop(576);
    for (auto& value : loop) {
        random = random * 1664525U + 1013904223U;
        value = (static_cast<double>(random) / 4294967296.0 - 0.5) * 0.2;
    }
    for (std::size_t index = 0; index < noise.size(); ++index) noise[index] = loop[index % loop.size()];
    const auto repeated = scan(noise, 997);
    require(repeated[3].coherentPeakDb < 2.0 || repeated[3].meanPeakZ < 5.0
            || repeated[3].alignedBlocks < 3,
            "an exact uncompressed periodic noise loop must not imply MP3 history");
    std::atomic_bool cancel{true};
    agplayer::lossless::MdctFramingProbe stopped(44100, 1, signal.size());
    stopped.consume(signal.data(), signal.size(), cancel);
    require(stopped.result()[0].activeBlocks == 0, "cancel must prevent work");
    agplayer::lossless::MdctFramingProbe unsupported(96000, 1, signal.size());
    cancel = false;
    unsupported.consume(signal.data(), signal.size(), cancel);
    require(unsupported.result()[0].activeBlocks == 0, "no native 96k codec assumption");
    std::vector<double> overflowing(signal.size(), 1e300);
    const auto rejected = scan(overflowing, 1024);
    for (const auto& value : rejected)
        require(value.activeBlocks == 0 && std::isfinite(value.coherentPeakDb),
                "finite input whose power overflows must not create evidence");
    std::cout << "MDCT framing checks passed\n";
}
