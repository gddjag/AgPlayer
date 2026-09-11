#include "visual_kick_response.hpp"
#include "visual_pulse_trigger.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
using agplayer::visual::KickResponse;
void require(bool value, const char* description) {
    if (!value) { std::cerr << description << '\n'; std::exit(1); }
}
bool near(double a, double b) { return std::abs(a-b) < 1e-12; }
int main() {
    {
        agplayer::VisualPulseTrigger pulse;
        agplayer::VisualSpectrumAnalyzer::Spectrum silence{}, low{};
        low[1] = low[2] = 255;
        require(!pulse.process(silence, 1.0 / 60.0).triggered,
                "reference pulse stays idle in silence");
        require(!pulse.process(low, 1.0 / 60.0).triggered,
                "reference pulse waits for the falling-flux peak");
        const auto wave = pulse.process(low, 1.0 / 60.0);
        require(wave.triggered && near(wave.strength, 2.4),
                "reference pulse preserves Auto Beat strength and timing");
        agplayer::VisualSpectrumAnalyzer::Spectrum mid{};
        mid[40] = 255;
        require(!pulse.process(mid, 1.0 / 60.0).triggered,
                "midrange alone does not manufacture a kick wave");

        pulse.reset();
        agplayer::VisualPulseTrigger::Output tracked;
        for (int frame = 0; frame < 62; ++frame) {
            agplayer::VisualSpectrumAnalyzer::Spectrum candidate{};
            if ((frame % 12) < 2) candidate[8] = candidate[9] = 255;
            tracked = pulse.process(candidate, 1.0 / 60.0);
        }
        require(tracked.bandStart == 8 && tracked.bandEnd == 9,
                "reference pulse auto-track follows the two strongest transient bins");
    }

    KickResponse detector;
    KickResponse::Spectrum silence{}, full{};
    full.fill(255);
    auto quiet = detector.process(silence, 1.0/60);
    require(near(quiet.threshold, .016) && quiet.windowIndex == 1 && quiet.envelope == 0, "silence uses default sensitivity and initial window");
    auto rise = detector.process(full, 1.0/60);
    require(near(rise.flux, .35) && rise.onset == 0 && rise.windowIndex == 0, "rising broadband transient selects focused window but awaits peak");
    auto peak = detector.process(full, 1.0/60);
    require(peak.onset == 1 && near(peak.flux, .35) && peak.envelope > .4, "falling flux confirms kick peak and drives envelope");
    for (int i=0;i<20;++i) {
        auto held = detector.process(i%2 ? full : silence, 0);
        require(held.onset == 0, "zero elapsed time must not expire refractory interval");
    }
    detector.reset();
    auto again = detector.process(full, 0);
    KickResponse fresh;
    auto timed = fresh.process(full, 1.0/60);
    require(near(again.envelope, timed.envelope) && near(again.level,timed.level), "zero dt still advances envelope by fallback frame");
    detector.reset();
    require(near(detector.process(silence, 0, -50).threshold,.05), "negative sensitivity clamps to strict setting");
    detector.reset();
    require(near(detector.process(silence,0,50).threshold,.028), "midpoint sensitivity has intermediate threshold");
    detector.reset();
    auto invalid = detector.process(full, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN());
    require(near(invalid.level,timed.level) && near(invalid.threshold,.016), "nonfinite inputs use safe time and default sensitivity");
    detector.reset();
    require(near(detector.process(silence,-1,150).threshold,.016), "negative time and excess sensitivity remain bounded");

    // A 64-beat arrangement starts with eight isolated kicks, then adds dense
    // melody, vocal and high-frequency content outside the four kick windows.
    // Those layers must neither mask a low-frequency onset nor create a second
    // onset for the same beat.
    detector.reset();
    int mixedOnsets = 0;
    for (int beat = 0; beat < 64; ++beat) {
        int beatOnsets = 0;
        for (int frame = 0; frame < 30; ++frame) {
            KickResponse::Spectrum arrangement{};
            if (beat >= 8) {
                for (std::size_t bin = 0; bin < 7; ++bin)
                    arrangement[bin] = static_cast<unsigned char>(
                        38 + ((beat * 3 + frame / 6 + int(bin) * 5) % 26));
            }
            if (frame < 2)
                for (std::size_t bin = 0; bin < 7; ++bin)
                    arrangement[bin] = 255;
            if (beat >= 8) {
                for (std::size_t bin = 8; bin < arrangement.size(); ++bin)
                    arrangement[bin] = static_cast<unsigned char>(
                        48 + ((beat * 19 + frame * 11 + int(bin) * 7) % 176));
            }
            const auto result = detector.process(arrangement, 1.0 / 60.0);
            if (result.onset > 0) {
                ++beatOnsets;
                ++mixedOnsets;
            }
        }
        require(beatOnsets == 1,
                "every pure or mixed-arrangement beat must produce exactly one kick onset");
    }
    require(mixedOnsets == 64,
            "64-beat mixed arrangement must have zero missed or duplicate kick onsets");
    std::cout << "kick response tests passed\n";
}
