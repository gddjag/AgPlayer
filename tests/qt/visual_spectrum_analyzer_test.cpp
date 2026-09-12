#undef NDEBUG
#include "visual_spectrum_analyzer.hpp"
#include <cassert>
#include <cmath>
#include <limits>

using Analyzer = agplayer::VisualSpectrumAnalyzer;

int main()
{
    Analyzer analyzer;
    Analyzer::Window samples{};
    for (auto value : analyzer.process(samples)) assert(value == 0);

    // Periodic Blackman DC coefficients are .42, -.25, .04.
    // Input .1, first-frame smoothing .2: magnitudes .0084, .005, .0008.
    samples.fill(0.1F);
    auto spectrum = analyzer.process(samples);
    assert(spectrum[0] == 189 && spectrum[1] == 164 && spectrum[2] == 74);
    for (std::size_t i = 3; i < spectrum.size(); ++i) assert(spectrum[i] == 0);

    analyzer.reset();
    // Bin-centered sine has half the DC-window magnitudes at k, k +/- 1, k +/- 2.
    for (std::size_t i = 0; i < samples.size(); ++i)
        samples[i] = static_cast<float>(0.1 * std::sin(2.0 * 3.14159265358979323846 * 37.0 * static_cast<double>(i) / 1024.0));
    spectrum = analyzer.process(samples);
    for (std::size_t i = 0; i < spectrum.size(); ++i) {
        const int expected = i == 37 ? 155 : (i == 36 || i == 38) ? 130 : (i == 35 || i == 39) ? 39 : 0;
        assert(spectrum[i] == expected);
    }
    const auto first = spectrum;
    spectrum = analyzer.process(samples);
    assert(spectrum[37] == 184); // .8 * .0042 + .2 * .021 = .00756.
    analyzer.reset();
    assert(analyzer.process(samples) == first);
    samples.fill(0.0F);
    assert(analyzer.process(samples)[37] == 144); // .8 * .0042 = .00336.
    analyzer.reset();
    for (auto value : analyzer.process(samples)) assert(value == 0);

    // Non-finite samples are treated as silence without poisoning future windows.
    samples[20] = std::numeric_limits<float>::quiet_NaN();
    samples[60] = std::numeric_limits<float>::infinity();
    samples[90] = -std::numeric_limits<float>::infinity();
    for (auto value : analyzer.process(samples)) assert(value == 0);
    samples.fill(0.1F);
    assert(analyzer.process(samples)[0] == 189);
    analyzer.reset();
    samples.fill(1.0F);
    assert(analyzer.process(samples)[0] == 255);
    analyzer.reset();
    samples.fill(0.00001F);
    for (auto value : analyzer.process(samples)) assert(value == 0);
}
