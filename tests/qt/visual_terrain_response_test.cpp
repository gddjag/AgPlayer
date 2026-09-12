#include "visual_terrain_response.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

static void near(double actual, double expected, const char* contract) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > 1e-12)
        throw std::runtime_error(contract);
}

int main() {
    try {
        using Response = agplayer::VisualTerrainResponse;
        agplayer::VisualSpectrumFeatures::Features input;
        Response response;
        Response::EqBands eq{.9,.92,.5,.5,.5,.5,.5,.48};
        Response::EnabledBands enabled{true,true,true,true,true,true,true,true};
        // Captured unchanged original MapScene, low-pulse frame 1, ec8ecbae.
        // Frame 0 has dt=0, so the scene's smoothed bands still start at zero.
        // build/qa/immersive-parity/comparison/reference-s-oracle-green3/
        // low-pulse-frame0001.json. Catches kick normalization, clipping and dt.
        input.bands = {.2606323529411765,.08692279411764706,0,0,0,0,0,0};
        input.energy = .0013581686580882353;
        input.smoothness = .27625195312499995;
        input.density = .06937499999999999;
        input.spectralCentroid = .23624413145539902;
        auto out = response.update(input,.4311521558084681,eq,enabled,1.0/60);
        near(out.bands[0],.48538530992153495,"original subBass frame oracle");
        near(out.bands[1],.46516092200813763,"original bass frame oracle");
        near(out.energy,.0015618939568014705,"original energy oracle");
        near(out.warmth,1,"original warmth oracle");
        near(out.brightness,0,"original brightness oracle");
        near(out.smoothness,input.smoothness,"smoothness passthrough");
        near(out.density,input.density,"density passthrough");
        near(out.spectralCentroid,input.spectralCentroid,"centroid passthrough");

        // One settled frame isolates EQ mapping, including >1 low-band headroom.
        response.reset(); input.bands.fill(1); input.energy=.4; input.sharpness=1.7;
        eq.fill(.5);
        out=response.update(input,.75,eq,enabled,100);
        near(out.bands[0],1.2,"subBass retains shader headroom");
        near(out.bands[1],1.15,"bass retains shader headroom");
        near(out.bands[7],1,"upper bands cap at one");
        near(out.sharpness,1.7,"sharpness must not clamp to one");
        near(out.energy,.4,"neutral EQ energy");

        // Unsaturated kick verifies normalization independently of the capped
        // browser fixture: 0.075 is one tenth of its 0.75 envelope range.
        response.reset(); input.bands.fill(.5);
        out=response.update(input,.075,eq,enabled,100);
        near(out.bands[0],.238,"uncapped kick normalization and subBass base gain");
        near(out.bands[1],.215,"uncapped kick normalization and bass base gain");

        // Attenuation is subtractive then multiplicative, and EQ rounds percent.
        response.reset(); input.bands.fill(.5); eq.fill(0); eq[2]=.5049;
        out=response.update(input,0,eq,enabled,100);
        near(out.bands[0],0,"low band noise suppression");
        near(out.bands[2],.5,"EQ rounding below half percent");
        near(out.bands[3],.0975,"subtractive EQ attenuation");
        eq[2]=.5051;
        out=response.update(input,0,eq,enabled,100);
        near(out.bands[2],.518,"EQ rounding above half percent");

        // highMid is deliberately absent from the original timbre denominator.
        response.reset(); input.bands={0,0,0,1,1,1,0,0}; eq.fill(.5);
        out=response.update(input,0,eq,enabled,100);
        near(out.warmth,.5,"warmth excludes highMid");
        near(out.brightness,.5,"brightness excludes highMid");
        enabled.fill(false);
        out=response.update(input,0,eq,enabled,100);
        for (double band:out.bands) near(band,0,"disabled bands converge to zero");
        near(out.energy,.4,"band enable does not mute overall energy");

        response.reset(); enabled.fill(true); input.bands.fill(1);
        out=response.update(input,0,eq,enabled,1.0/60,0);
        near(out.bands[2],.03600258572898462,"minimum motion response");
        response.reset();
        out=response.update(input,0,eq,enabled,1.0/60,100);
        near(out.bands[2],.6321205588285577,"maximum motion response");
        enabled.fill(false);
        out=response.update(input,0,eq,enabled,1.0/60,100);
        near(out.bands[2],.23254415793482963,"disabled bands decay from previous frame");
        out=response.update(input,0,eq,enabled,-1);
        near(out.bands[2],.23254415793482963,"negative dt cannot rewind smoothing");
        out=response.update(input,0,eq,enabled,std::numeric_limits<double>::quiet_NaN());
        near(out.bands[2],.23254415793482963,"invalid dt cannot poison state");
        std::cout << "visual terrain response passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
