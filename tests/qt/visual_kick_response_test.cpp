#include "visual_kick_response.hpp"
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
    std::cout << "kick response tests passed\n";
}
