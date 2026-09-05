#include "lossless/lossless_resampled_mdct.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
namespace {
constexpr double pi = 3.14159265358979323846;
void require(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
// Independent direct-cosine inverse MDCT, then a windowed sinc interpolator.
// Neither synthesis stage calls Swr or the forward transform under test.
std::vector<double> makeHighRateSignal() {
    constexpr std::size_t n = 2048, h = n / 2, blocks = 40;
    std::vector<double> low((blocks + 1) * h, 0.0);
    std::uint32_t state = 1907;
    for (std::size_t block = 0; block < blocks; ++block) {
        for (std::size_t bin = 0; bin < 48; ++bin) {
            state = state * 1664525U + 1013904223U;
            const double k = static_cast<double>((state >> 8U) % 800);
            const double amplitude = (state & 1U) ? 0.006 : -0.006;
            for (std::size_t i = 0; i < n; ++i) {
                const double x = static_cast<double>(i);
                low[block * h + i] += amplitude
                    * std::cos(2.0 * pi / n * (x + .5 + n / 4.0) * (k + .5))
                    * std::sin(pi / n * (x + .5));
            }
        }
    }
    std::vector<double> high(low.size() * 2);
    for (std::size_t i = 0; i < low.size(); ++i) {
        high[2 * i] = low[i];
        double sum = 0.0, weight = 0.0;
        for (int tap = -63; tap <= 64; ++tap) {
            const double distance = .5 - tap;
            const double coefficient = std::sin(pi * distance) / (pi * distance)
                * (.5 + .5 * std::cos(pi * distance / 64.0));
            const auto index = static_cast<std::int64_t>(i) + tap;
            if (index >= 0 && static_cast<std::size_t>(index) < low.size())
                sum += coefficient * low[static_cast<std::size_t>(index)];
            weight += coefficient;
        }
        high[2 * i + 1] = sum / weight;
    }
    return high;
}
auto scan(const std::vector<double>& mono, std::size_t chunk, bool stereo = false) {
    agplayer::lossless::ResampledMdctProbe probe(88200, stereo ? 2 : 1, mono.size(), 44100);
    require(probe.available(), "valid inverse-rate probe must initialize");
    std::atomic_bool cancel{false};
    for (std::size_t i = 0; i < mono.size(); i += chunk) {
        const auto count = std::min(chunk, mono.size() - i);
        if (!stereo) probe.consume(mono.data() + i, count, cancel);
        else {
            std::vector<double> channels(count * 2);
            for (std::size_t j = 0; j < count; ++j) {
                channels[2 * j] = mono[i + j]; channels[2 * j + 1] = -mono[i + j];
            }
            probe.consume(channels.data(), count, cancel);
        }
    }
    probe.finish(cancel);
    const auto first = probe.result();
    probe.finish(cancel);
    const auto second = probe.result();
    for (std::size_t i = 0; i < first.size(); ++i)
        require(first[i].coherentPeakDb == second[i].coherentPeakDb
                && first[i].activeBlocks == second[i].activeBlocks, "flush must be idempotent");
    return first;
}
}
int main() {
    const auto high = makeHighRateSignal();
    const auto whole = scan(high, high.size());
    require(whole[0].activeBlocks == 4, "flush must deliver final complete analysis anchor");
    require(whole[0].coherentPeakDb >= 6.0 && whole[0].meanPeakZ >= 12.0,
            "independently interpolated sparse lapped signal must recover framing");
    const auto chunked = scan(high, 127, true);
    for (std::size_t i = 0; i < whole.size(); ++i)
        require(std::abs(whole[i].coherentPeakDb - chunked[i].coherentPeakDb) < 1e-9
                && whole[i].activeBlocks == chunked[i].activeBlocks,
                "chunking and antiphase must preserve inverse-rate framing");
    std::vector<double> native(high.size());
    for (std::size_t i = 0; i < native.size(); ++i)
        native[i] = .1 * std::sin(2.0 * pi * 997.0 * static_cast<double>(i) / 88200.0);
    const auto tone = scan(native, 997);
    for (std::size_t i = 0; i < tone.size(); ++i) {
        const auto& v = tone[i];
        require(i == 3 ? (v.coherentPeakDb < 2.0 || v.meanPeakZ < 5.0 || v.alignedBlocks < 3)
                       : (v.coherentPeakDb < 6.0 || v.meanPeakZ < 12.0),
                "resampling a native tone must not manufacture lossy framing");
    }
    std::atomic_bool cancel{true};
    agplayer::lossless::ResampledMdctProbe stopped(88200, 1, high.size(), 44100);
    stopped.consume(high.data(), high.size(), cancel); stopped.finish(cancel);
    require(stopped.result()[0].activeBlocks == 0, "pre-cancel must prevent conversion and flush");
    agplayer::lossless::ResampledMdctProbe interrupted(88200, 1, high.size(), 44100);
    cancel = false;
    interrupted.consume(high.data(), 127, cancel);
    cancel = true;
    interrupted.consume(high.data() + 127, high.size() - 127, cancel);
    interrupted.finish(cancel);
    require(interrupted.result()[0].activeBlocks == 0, "mid-stream cancel must prevent later anchors");
    for (int rate : {0, 44100, 48000, 800000}) {
        agplayer::lossless::ResampledMdctProbe invalid(rate, 1, 0, 44100);
        require(!invalid.available(), "unsupported source rate must be unavailable");
    }
    for (int channels : {0, 65}) {
        agplayer::lossless::ResampledMdctProbe invalid(96000, channels, 0, 44100);
        require(!invalid.available(), "invalid channel count must be unavailable");
    }
    agplayer::lossless::ResampledMdctProbe invalidTarget(96000, 1, 0, 32000);
    require(!invalidTarget.available(), "only established source-rate hypotheses are accepted");
    agplayer::lossless::ResampledMdctProbe overflow(96000, 1,
        std::numeric_limits<std::uint64_t>::max(), 44100);
    require(!overflow.available(), "metadata overflow must not wrap frame positions");
    agplayer::lossless::ResampledMdctProbe badPcm(96000, 1, 100, 48000);
    cancel = false;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    bool threw = false;
    try { badPcm.consume(&nan, 1, cancel); } catch (const std::runtime_error&) { threw = true; }
    require(threw, "invalid source samples must not silently create evidence");
    agplayer::lossless::ResampledMdctProbe empty(96000, 1, 0, 48000);
    empty.finish(cancel); empty.finish(cancel);
    require(empty.result()[0].activeBlocks == 0, "empty stream is not measured evidence");
    std::vector<double> silence(400, 0.0);
    const auto silent = scan(silence, 127);
    require(silent[0].activeBlocks == 0, "short silence must remain unavailable evidence");
    std::cout << "Resampled MDCT checks passed\n";
}
