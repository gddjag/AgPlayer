#include "lossless/lossless_celt.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
constexpr double pi = 3.14159265358979323846;
void require(bool value, const char* message)
{
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
// Independently synthesize sparse lapped transform coefficients, then apply
// decoder de-emphasis. This tests a sample-domain framing signal, not scores.
std::vector<double> lapped(std::size_t hop = 960)
{
    const auto n = 2 * hop;
    const auto zero = (hop - 120) / 2;
    std::vector<double> window(n, 0.0), pcm(61 * hop, 0.0);
    for (std::size_t i = 0; i < 120; ++i) {
        const double s = std::sin(pi * (i + 0.5) / 240.0);
        window[zero + i] = window[n - zero - 1 - i] = std::sin(pi * 0.5 * s * s);
    }
    std::fill(window.begin() + zero + 120, window.end() - zero - 120, 1.0);
    std::uint32_t seed = 6716;
    for (std::size_t block = 0; block < 60; ++block) {
        for (std::size_t coefficient = 0; coefficient < 72; ++coefficient) {
            seed = seed * 1664525U + 1013904223U;
            const auto bin = (seed >> 8U) % hop;
            const double amplitude = (seed & 1) ? 0.003 : -0.003;
            for (std::size_t i = 0; i < n; ++i)
                pcm[block * hop + i] += amplitude * window[i]
                    * std::cos(2.0 * pi / n * (i + 0.5 + n / 4.0) * (bin + 0.5));
        }
    }
    for (std::size_t i = 1; i < pcm.size(); ++i) pcm[i] += 0.8500061035 * pcm[i - 1];
    return pcm;
}
auto scan(const std::vector<double>& pcm, std::size_t chunk, bool multichannel = false, std::size_t frameSamples = 960)
{
    agplayer::lossless::CeltFramingProbe probe(48000, multichannel ? 3 : 1, pcm.size(), frameSamples);
    std::atomic_bool cancel{false};
    for (std::size_t start = 0; start < pcm.size(); start += chunk) {
        const auto count = std::min(chunk, pcm.size() - start);
        if (!multichannel) probe.consume(pcm.data() + start, count, cancel);
        else {
            std::vector<double> channels(count * 3, 0.0);
            for (std::size_t i = 0; i < count; ++i) {
                channels[3 * i + 1] = pcm[start + i]; channels[3 * i + 2] = -pcm[start + i];
            }
            probe.consume(channels.data(), count, cancel);
        }
    }
    return probe.result();
}
bool supports(const agplayer::lossless::CeltFrameEvidence& r)
{
    return r.fullBand.activeBlocks >= 3 && r.lowBand.activeBlocks >= 3
        && r.fullBand.robustZ >= 8 && r.lowBand.robustZ >= 8
        && r.fullBand.halfProminenceWidth <= 8 && r.lowBand.halfProminenceWidth <= 8
        && r.fullBand.anchorPeakCoherence >= 0.9 && r.lowBand.anchorPeakCoherence >= 0.9
        && r.phaseAgreementSamples <= 2;
}
}
int main()
{
    for (const std::size_t frameSamples : {240U, 480U}) {
        const auto shortPcm = lapped(frameSamples);
        const auto shortResult = scan(shortPcm, shortPcm.size(), false, frameSamples);
        require(supports(shortResult), "5/10 ms independent inverse MDCT must be detected");
        const auto chunkedShort = scan(shortPcm, 127, true, frameSamples);
        require(std::abs(chunkedShort.lowBand.robustZ - shortResult.lowBand.robustZ) < 1e-8,
                "short-frame chunking and antiphase preserve evidence");
    }
    const auto pcm = lapped();
    const auto result = scan(pcm, pcm.size());
    require(result.fullBand.activeBlocks == 4, "four complete disjoint anchors");
    require(supports(result), "sparse low-overlap framing must survive decoder de-emphasis");
    const auto chunked = scan(pcm, 127, true);
    require(std::abs(chunked.lowBand.robustZ - result.lowBand.robustZ) < 1e-8
        && chunked.lowBand.peakPhase == result.lowBand.peakPhase,
        "chunking, a silent first channel and antiphase must preserve evidence");
    std::vector<double> noise(pcm.size());
    std::uint32_t random = 123;
    for (auto& value : noise) {
        random = random * 1664525U + 1013904223U;
        value = (static_cast<double>(random) / 4294967296.0 - 0.5) * 0.1;
    }
    for (const std::size_t frameSamples : {240U, 480U, 960U}) {
        require(!supports(scan(noise, 997, false, frameSamples)), "uncompressed noise fails every profile");
        for (const std::size_t period : {120U, 240U, 480U, 960U}) {
            auto periodic = noise;
            for (std::size_t i = 0; i < periodic.size(); ++i) periodic[i] = noise[i % period];
            require(!supports(scan(periodic, 997, false, frameSamples)), "periodic noise fails every profile");
            for (std::size_t i = 0; i < periodic.size(); ++i)
                periodic[i] = 0.2 * std::sin(2.0 * pi * i / period);
            require(!supports(scan(periodic, 997, false, frameSamples)), "periodic tone fails every profile");
        }
    }
    constexpr std::size_t hop = 960;
    std::vector<double> loop(hop);
    std::copy_n(noise.begin(), hop, loop.begin());
    for (std::size_t i = 0; i < noise.size(); ++i) noise[i] = loop[i % hop];
    require(!supports(scan(noise, 997)), "uncompressed periodic noise is not CELT history");
    for (std::size_t i = 0; i < noise.size(); ++i) noise[i] = 0.2 * std::sin(2.0 * pi * i / hop);
    require(!supports(scan(noise, 997)), "a native codec-grid-periodic tone is not CELT history");
    std::fill(noise.begin(), noise.end(), 0.0);
    require(scan(noise, 997).fullBand.activeBlocks == 0, "silence is unmeasured");
    std::fill(noise.begin(), noise.end(), 1e300);
    require(scan(noise, 997).fullBand.activeBlocks == 0, "overflowing power is unmeasured");
    std::atomic_bool cancel{true};
    agplayer::lossless::CeltFramingProbe stopped(48000, 1, pcm.size());
    stopped.consume(pcm.data(), pcm.size(), cancel);
    require(stopped.result().fullBand.activeBlocks == 0, "pre-cancelled probe does no work");
    std::cout << "CELT framing checks passed\n";
}
