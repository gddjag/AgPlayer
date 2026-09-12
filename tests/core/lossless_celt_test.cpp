#include "lossless/lossless_celt.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr double pi = 3.14159265358979323846;
void require(bool value, const char* message)
{
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
// Independently synthesize sparse lapped transform coefficients, then apply
// decoder de-emphasis. This tests a sample-domain framing signal, not scores.
std::vector<double> lapped(std::size_t hop = 960, std::size_t blocks = 60)
{
    const auto n = 2 * hop;
    const auto zero = (hop - 120) / 2;
    std::vector<double> window(n, 0.0), pcm((blocks + 1) * hop, 0.0);
    for (std::size_t i = 0; i < 120; ++i) {
        const double s = std::sin(pi * (i + 0.5) / 240.0);
        window[zero + i] = window[n - zero - 1 - i] = std::sin(pi * 0.5 * s * s);
    }
    std::fill(window.begin() + zero + 120, window.end() - zero - 120, 1.0);
    std::uint32_t seed = 6716;
    for (std::size_t block = 0; block < blocks; ++block) {
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
auto scan(const std::vector<double>& pcm, std::size_t chunk, bool multichannel = false, std::size_t frameSamples = 960, std::size_t depth = 12)
{
    agplayer::lossless::CeltFramingProbe probe(48000, multichannel ? 3 : 1, pcm.size(), frameSamples, depth);
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
    if (depth > 12) {
        require(probe.result().fullBand.activeBlocks == 0, "deep capture must be lazy");
        probe.refine(cancel);
        const auto first = probe.result();
        probe.refine(cancel);
        require(probe.result().lowBand.robustZ == first.lowBand.robustZ, "refine must be idempotent");
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
int main(int argc, char** argv)
{
    if (argc == 2) {
        const std::string mode = argv[1];
        if (mode == "--extended-192" || mode == "--extended-768") {
            const std::size_t depth = mode == "--extended-192" ? 192U : 768U;
            const auto pcm = lapped(120, 4 * (depth + 2) + 1);
            const auto result = scan(pcm, pcm.size(), false, 120, depth);
            require(result.fullBand.activeBlocks == 4 && supports(result), "extended120 independent IMDCT evidence");
            const auto chunks = scan(pcm, 127, true, 120, depth);
            require(std::abs(result.lowBand.robustZ - chunks.lowBand.robustZ) < 1e-8,
                    "extended120 chunking and antiphase preserve evidence");
            std::atomic_bool cancelled{true};
            agplayer::lossless::CeltFramingProbe stopped(48000, 1, pcm.size(), 120, depth);
            stopped.consume(pcm.data(), pcm.size(), cancelled); stopped.refine(cancelled);
            require(stopped.result().fullBand.activeBlocks == 0, "extended pre-cancel publishes nothing");
            const std::vector<double> shortPcm(pcm.begin(), pcm.begin() + (depth + 1) * 120);
            require(scan(shortPcm, 127, false, 120, depth).fullBand.activeBlocks == 0,
                    "extended incomplete anchor is unmeasured");
            std::vector<double> tone(pcm.size());
            for (std::size_t i = 0; i < tone.size(); ++i)
                tone[i] = 0.2 * std::sin(2 * pi * i / 120.0);
            require(!supports(scan(tone, 127, false, 120, depth)), "extended native tone does not imply codec history");
            agplayer::lossless::CeltFramingProbe unsupported(48000, 1, pcm.size(), 960, depth);
            cancelled = false; unsupported.consume(pcm.data(), pcm.size(), cancelled); unsupported.refine(cancelled);
            require(unsupported.result().fullBand.activeBlocks == 0, "long frames cannot request short-frame-only depth");
            std::cout << "CELT extended120 " << depth << " checks passed\n";
            return 0;
        }
        if (mode == "--cancel-running" || mode == "--cancel-extended") {
            const std::size_t cancelHop = mode == "--cancel-extended" ? 120U : 960U;
            const std::size_t cancelDepth = mode == "--cancel-extended" ? 768U : 48U;
            const auto pcm = lapped(cancelHop, 4 * (cancelDepth + 2) + 20);
            std::atomic_bool cancel{false}, aboutToRefine{false}, returned{false};
            agplayer::lossless::CeltFramingProbe probe(48000, 1, pcm.size(), cancelHop, cancelDepth);
            probe.consume(pcm.data(), pcm.size(), cancel);
            require(probe.result().fullBand.activeBlocks == 0, "cancel fixture captures lazily");
            std::chrono::steady_clock::time_point requestedAt;
            bool observedBeforeReturn = false;
            std::thread requestCancel([&] {
                while (!aboutToRefine.load(std::memory_order_acquire)) std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                requestedAt = std::chrono::steady_clock::now();
                observedBeforeReturn = !returned.load(std::memory_order_acquire);
                cancel.store(true, std::memory_order_release);
            });
            // This external handshake brackets the call, not an internal FFT
            // hook: a scheduler pause before function entry remains possible.
            // The measured bound is a software cancellation checkpoint target,
            // not decoder/end-to-end or hard realtime latency certification.
            aboutToRefine.store(true, std::memory_order_release);
            probe.refine(cancel);
            const auto returnedAt = std::chrono::steady_clock::now();
            returned.store(true, std::memory_order_release);
            requestCancel.join();
            const double latencyMs = std::chrono::duration<double, std::milli>(returnedAt - requestedAt).count();
            const auto partial = probe.result();
            require(observedBeforeReturn, "cancel request must precede observed refine return");
            require(latencyMs >= 0.0 && latencyMs < 500.0, "CELT software cancellation checkpoint exceeds500ms");
            require(partial.fullBand.activeBlocks < 4 && partial.lowBand.activeBlocks < 4,
                    "cancelled refine must not publish all anchors");
            cancel = false;
            probe.refine(cancel);
            const auto resumed = probe.result();
            const auto reference = scan(pcm, pcm.size(), false, cancelHop, cancelDepth);
            require(resumed.fullBand.activeBlocks == 4 && supports(resumed), "cancelled probe resumes independent IMDCT evidence");
            require(std::abs(resumed.lowBand.robustZ - reference.lowBand.robustZ) < 1e-8
                    && std::abs(resumed.fullBand.robustZ - reference.fullBand.robustZ) < 1e-8
                    && resumed.lowBand.peakPhase == reference.lowBand.peakPhase,
                    "resume must match uninterrupted independent fixture");
            std::cout << "CELT cancellation software checkpoint passed: request-to-return-ms="
                      << latencyMs << ", published-anchors=" << partial.fullBand.activeBlocks
                      << "; external call handshake, internal FFT entry not instrumented\n";
            return 0;
        }
        require(mode == "--deep-120" || mode == "--deep-240" || mode == "--deep-960", "unknown CELT test mode");
        const std::size_t frameSamples = mode == "--deep-120" ? 120U : mode == "--deep-240" ? 240U : 960U;
        const auto deepPcm = lapped(frameSamples, 220);
        const auto deep = scan(deepPcm, deepPcm.size(), false, frameSamples, 48);
        require(deep.fullBand.activeBlocks == 4 && supports(deep), "48-frame independent IMDCT evidence");
        const auto deepChunks = scan(deepPcm, 127, true, frameSamples, 48);
        require(std::abs(deep.lowBand.robustZ - deepChunks.lowBand.robustZ) < 1e-8,
                "deep chunking and antiphase preserve evidence");
        std::atomic_bool stop{false};
        agplayer::lossless::CeltFramingProbe deferred(48000, 1, deepPcm.size(), frameSamples, 48);
        deferred.consume(deepPcm.data(), deepPcm.size(), stop);
        stop = true; deferred.refine(stop);
        require(deferred.result().fullBand.activeBlocks == 0, "cancelled refinement publishes no anchors");
        stop = false; deferred.refine(stop);
        require(std::abs(deferred.result().lowBand.robustZ - deep.lowBand.robustZ) < 1e-8,
                "cancelled refinement can resume complete evidence");
        agplayer::lossless::CeltFramingProbe shortDeep(48000, 1, 47 * frameSamples, frameSamples, 48);
        shortDeep.consume(deepPcm.data(), 47 * frameSamples, stop); shortDeep.refine(stop);
        require(shortDeep.result().fullBand.activeBlocks == 0, "incomplete deep anchor is unmeasured");
        agplayer::lossless::CeltFramingProbe invalid(48000, 1, deepPcm.size(), frameSamples, 13);
        invalid.consume(deepPcm.data(), deepPcm.size(), stop); invalid.refine(stop);
        require(invalid.result().fullBand.activeBlocks == 0, "unsupported depth is unavailable");
        if (frameSamples == 120) {
            const auto regular = scan(deepPcm, deepPcm.size(), false, 120);
            require(supports(regular), "2.5 ms independent inverse MDCT must be detected");
            const auto regularChunks = scan(deepPcm, 127, true, 120);
            require(std::abs(regular.lowBand.robustZ - regularChunks.lowBand.robustZ) < 1e-8,
                    "2.5 ms regular chunking and antiphase preserve evidence");
            std::vector<double> periodic(deepPcm.size());
            std::uint32_t seed = 123;
            for (std::size_t i = 0; i < 120; ++i) {
                seed = seed * 1664525U + 1013904223U;
                periodic[i] = (static_cast<double>(seed) / 4294967296.0 - 0.5) * 0.1;
            }
            for (std::size_t i = 120; i < periodic.size(); ++i) periodic[i] = periodic[i % 120];
            for (const auto depth : {12U, 48U})
                require(!supports(scan(periodic, 127, false, 120, depth)), "native120-period noise is not CELT evidence");
            for (std::size_t i = 0; i < periodic.size(); ++i)
                periodic[i] = 0.2 * std::sin(2.0 * pi * i / 120.0);
            for (const auto depth : {12U, 48U})
                require(!supports(scan(periodic, 127, false, 120, depth)), "native120-period tone is not CELT evidence");
            for (std::size_t i = 0; i < periodic.size(); ++i)
                for (std::size_t harmonic = 2; harmonic <= 16; ++harmonic)
                    periodic[i] += 0.2 / harmonic * std::sin(2.0 * pi * i * harmonic / 120.0);
            for (const auto depth : {12U, 48U})
                require(!supports(scan(periodic, 127, false, 120, depth)), "native120-period harmonics are not CELT evidence");
        }
        std::cout << "CELT deep " << frameSamples << " framing checks passed\n";
        return 0;
    }
    require(argc == 1, "unexpected CELT test arguments");

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
