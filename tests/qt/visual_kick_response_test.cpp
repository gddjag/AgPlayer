#include "visual_kick_response.hpp"
#include "visual_pulse_trigger.hpp"
#include "visual_audio_frame_analyzer.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
#include <chrono>
#include <string>
using agplayer::visual::KickResponse;
void require(bool value, const char* description) {
    if (!value) { std::cerr << description << '\n'; std::exit(1); }
}
bool near(double a, double b) { return std::abs(a-b) < 1e-12; }
int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--benchmark") {
        std::array<agplayer::VisualSpectrumAnalyzer::Window, 64> windows{};
        for (std::size_t frame = 0; frame < windows.size(); ++frame)
            for (std::size_t i = 0; i < windows[frame].size(); ++i) {
                const double t = double(frame * 800 + i) / 48000.0;
                windows[frame][i] = float(.5 * std::sin(6.283185307179586 * 70 * t)
                    * std::exp(-std::fmod(t, .25) * 35) + .1 * std::sin(6.283185307179586 * 110 * t));
            }
        std::array<double, 5> samples{};
        std::uint64_t checksum = 0;
        for (std::size_t run = 0; run < samples.size(); ++run) {
            agplayer::VisualSpectrumAnalyzer analyzer;
            const auto start = std::chrono::steady_clock::now();
            for (int frame = 0; frame < 20000; ++frame) {
                const auto spectrum = analyzer.process(windows[std::size_t(frame) % windows.size()]);
                for (int bin = 0; bin < 8; ++bin)
                    checksum += analyzer.onsetSpectrum()[bin] + spectrum[bin];
            }
            samples[run] = std::chrono::duration<double, std::micro>(
                std::chrono::steady_clock::now() - start).count() / 20000;
        }
        std::sort(samples.begin(), samples.end());
        std::cout << "FFT analysis median_us=" << samples[2] << " checksum=" << checksum << '\n';
        return 0;
    }
    // Independent numeric contract from beatDetector.ts / kickEnvelope.ts.
    // A recovery pulse or a rejected swell must not shift the source clock.
    {
        constexpr double dt = 1.0 / 60.0;
        KickResponse reference(KickResponse::Mode::Reference);
        KickResponse recovered(KickResponse::Mode::Reference);
        KickResponse filtered(KickResponse::Mode::Reference);
        KickResponse::Spectrum step{};
        step.fill(128);
        const double raw = 128.0 / 255.0;
        reference.process(step, dt);
        recovered.process(step, dt, 100, true, true);
        filtered.process(step, dt);
        const auto second = reference.process(step, dt);
        const double flux = raw * .35;
        const double mean = flux / 90;
        const double deviation = std::sqrt(((flux-mean)*(flux-mean) + 89*mean*mean) / 90);
        require(near(second.threshold, std::max(.016, mean + 1.1*deviation)),
                "reference threshold must use mean/std of smoothed flux");
        require(second.onset == 1 && recovered.process(step, dt).onset == 1,
                "recovery must not veto the next original onset");
        const double floor1 = raw * (1-std::exp(-1.15*dt));
        const double level1 = raw-floor1-.025;
        const double breath1 = std::min(.11, level1*.18);
        const double floor2 = floor1+(raw-floor1)*(1-std::exp(-1.15*dt));
        const double target2 = std::max(.48, (raw-floor2-.025)*.95);
        require(near(second.envelope, breath1+(target2-breath1)*(1-std::exp(-42*dt))),
                "lift envelope must retain the original level/floor/attack formula");
        require(filtered.process(step,dt,100,false).onset == 0
                    && filtered.process(step,dt).onset == 0,
                "a rejected source onset must not reappear on its decaying tail");
        reference.reset();
        reference.process(step,dt);
        require(near(reference.process(step,dt).threshold, second.threshold),
                "reset must retain reference mode");
    }
    // A rising full-band arrangement must not veto the reference drum clock.
    {
        agplayer::VisualAudioFrameAnalyzer analyzer;
        KickResponse original(KickResponse::Mode::Reference);
        agplayer::VisualAudioFrameAnalyzer::Snapshot pcm;
        pcm.valid = true; pcm.sampleRate = 48000; pcm.epoch = 1;
        int referenceOnsets = 0;
        for (int frame = 0; frame < 1200; ++frame) {
            pcm.sequence = frame+1; pcm.firstSampleIndex = std::uint64_t(frame)*800;
            for (std::size_t i = 0; i < pcm.pcm.size(); ++i) {
                const double t = double(pcm.firstSampleIndex+i)/48000;
                const double tau = 6.283185307179586;
                const double swell = .15 + .12 * std::sin(tau*.7*t);
                pcm.pcm[i] = float(.3*std::sin(tau*70*t)*std::exp(-std::fmod(t,.25)*35)
                    + swell*(std::sin(tau*600*t)+std::sin(tau*2600*t)));
            }
            const auto& actual = analyzer.process(pcm,1.0/60);
            const auto expected = original.process(actual.spectrum,1.0/60);
            if (expected.onset > 0) {
                ++referenceOnsets;
                require(actual.kick.onset > 0, "full-mix swell must preserve every reference onset on its frame");
            }
        }
        require(referenceOnsets > 30, "mixed-arrangement fixture must exercise repeated source onsets");
    }
    // Original parity is the contract, not a custom drum/melody classifier.
    // Every frame (including quiet low-tone interference) must retain exactly
    // the source onset and envelope, with neither a veto nor a recovery pulse.
    for (int kind = 0; kind < 7; ++kind) {
        agplayer::VisualAudioFrameAnalyzer analyzer;
        KickResponse original(KickResponse::Mode::Reference);
        int originalOnsets = 0;
        agplayer::VisualAudioFrameAnalyzer::Snapshot pcm;
        pcm.valid = true; pcm.sampleRate = 48000; pcm.epoch = 1;
        int onsets = 0;
        double peakEnvelope = 0;
        for (int frame = 0; frame < 1200; ++frame) {
            pcm.sequence = frame + 1; pcm.firstSampleIndex = std::uint64_t(frame) * 800;
            for (std::size_t i = 0; i < pcm.pcm.size(); ++i) {
                const double t = double(pcm.firstSampleIndex + i) / 48000;
                const double tau = 6.283185307179586;
                const double fade = std::min(1.0, t / .5);
                double sample = 0;
                if (kind == 0) // Continuous upper-register chord, no attacks.
                    sample = .18 * (std::sin(tau * 261.63 * t) + std::sin(tau * 329.63 * t)
                        + std::sin(tau * 392 * t));
                if (kind == 1) // Legato low melody with vibrato, no percussion.
                    sample = .35 * std::sin(tau * 146.83 * t + 1.2 * std::sin(tau * 4.7 * t));
                if (kind == 2) // Slow pad swelling, not a kick attack.
                    sample = .25 * (.6 + .4 * std::sin(tau * .7 * t))
                        * (std::sin(tau * 110 * t) + .4 * std::sin(tau * 220 * t));
                if (kind == 3) // Near-silent tonal background.
                    sample = .0003 * (std::sin(tau * 73 * t) + std::sin(tau * 127 * t));
                if (kind == 4) // Sustained bass chord with interfering partials.
                    sample = .18 * (std::sin(tau * 65.41 * t) + std::sin(tau * 82.41 * t)
                        + std::sin(tau * 98 * t));
                if (kind == 5) // Quiet version must not be promoted to loud kicks.
                    sample = .0018 * (std::sin(tau * 65.41 * t) + std::sin(tau * 82.41 * t)
                        + std::sin(tau * 98 * t));
                if (kind == 6) // Soft legato phrase with smooth 250 ms note attacks.
                    sample = .2 * std::pow(std::sin(tau * .5 * t), 2)
                        * (std::sin(tau * 73.42 * t) + .5 * std::sin(tau * 146.84 * t));
                pcm.pcm[i] = float(sample * fade);
            }
            const auto& result = analyzer.process(pcm, 1.0 / 60.0);
            const auto referenceFrame = original.process(result.spectrum, 1.0 / 60.0);
            require(result.kick.onset == referenceFrame.onset && near(result.kick.envelope, referenceFrame.envelope),
                    "PCM analyzer must preserve exact original onset and lift, including quiet melody");
            if (frame >= 60) {
                originalOnsets += referenceFrame.onset > 0 ? 1 : 0;
                onsets += result.kick.onset > 0 ? 1 : 0;
                peakEnvelope = std::max(peakEnvelope, result.kick.envelope);
            }
        }
        std::cout << "no-percussion kind=" << kind << " onsets=" << onsets
                  << " peakEnvelope=" << peakEnvelope << " originalOnsets=" << originalOnsets << std::endl;
        require(onsets == originalOnsets, "PCM output must neither add nor remove original low-tone events");
    }
    // Exercise actual PCM -> windowed/smoothed FFT -> onset, rather than
    // injecting ideal 0/255 spectra that cannot reveal analyzer saturation.
    struct PcmCase { double gain; int seconds; int sampleRate; int beatFrames = 15; };
    for (const auto test : {PcmCase{0.15, 300, 48000}, PcmCase{0.5, 3600, 48000},
                           PcmCase{0.9, 16, 48000}, PcmCase{0.5, 16, 44100},
                           PcmCase{0.5, 16, 96000}, PcmCase{0.5, 20, 48000, 10},
                           PcmCase{0.5, 20, 48000, 30}}) {
        const double gain = test.gain;
        agplayer::VisualAudioFrameAnalyzer analyzer;
        KickResponse original(KickResponse::Mode::Reference);
        agplayer::VisualAudioFrameAnalyzer::Snapshot pcm;
        pcm.valid = true;
        pcm.sampleRate = test.sampleRate;
        pcm.epoch = 1;
        int onsets = 0;
        std::vector<int> beatCounts(std::size_t(test.seconds * 60 / test.beatFrames));
        for (int frame = 0; frame < 60 * test.seconds; ++frame) {
            pcm.sequence = std::uint64_t(frame + 1);
            pcm.firstSampleIndex = std::uint64_t(frame) * test.sampleRate / 60;
            for (std::size_t i = 0; i < pcm.pcm.size(); ++i) {
                const double t = double(pcm.firstSampleIndex + i) / test.sampleRate;
                const double age = std::fmod(t, test.beatFrames / 60.0);
                const double kick = std::exp(-age * 35.0)
                    * std::sin(6.283185307179586 * 70.0 * t);
                const double bass = frame >= 120
                    ? .22 * std::sin(6.283185307179586 * 110.0 * t) : 0;
                pcm.pcm[i] = float(gain * (kick + bass));
            }
            const auto& result = analyzer.process(pcm, 1.0 / 60.0);
            const auto expected = original.process(result.spectrum, 1.0/60);
            require(result.kick.onset == expected.onset && near(result.kick.envelope, expected.envelope),
                    "every frame through the 60-minute tail must match original lift, without extra or missing events");
            onsets += result.kick.onset > 0 ? 1 : 0;
            if (result.kick.onset > 0) ++beatCounts[std::size_t(frame / test.beatFrames)];
        }
        std::cout << "PCM gain=" << gain << " seconds=" << test.seconds
                  << " sampleRate=" << test.sampleRate << " onsets=" << onsets
                  << "/" << beatCounts.size() << std::endl;
        // Frame-exact source equality above replaces the old metronome-count
        // assertion, which encouraged adding events the original never emits.
    }
    {
        agplayer::VisualAudioFrameAnalyzer analyzer;
        agplayer::VisualAudioFrameAnalyzer::Snapshot pcm;
        pcm.valid = true; pcm.sampleRate = 48000; pcm.epoch = 1;
        for (int frame = 0; frame < 600; ++frame) {
            pcm.sequence = frame + 1;
            for (std::size_t i = 0; i < pcm.pcm.size(); ++i)
                pcm.pcm[i] = float(.5 * std::sin(6.283185307179586 * 110.0 * (frame * 800 + i) / 48000.0));
            const auto& output = analyzer.process(pcm, 1.0 / 60.0);
            if (frame > 60) require(output.kick.onset == 0,
                "sustained bass must not manufacture periodic beats");
        }
    }
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

        // At 120 BPM, eighth-note kicks arrive every 250 ms. The native
        // detector runs at a fixed 60 Hz, so a frame-count hold copied from a
        // high-refresh browser must not suppress later identical kicks.
        pulse.reset();
        int pulseOnsets = 0;
        for (int frame = 0; frame < 16 * 15; ++frame) {
            agplayer::VisualSpectrumAnalyzer::Spectrum eighthNotes{};
            if ((frame % 15) < 2)
                eighthNotes[1] = eighthNotes[2] = 255;
            if (pulse.process(eighthNotes, 1.0 / 60.0).triggered)
                ++pulseOnsets;
        }
        require(pulseOnsets == 9,
                "source Pulse holds 15 frames independently of the 120ms terrain kick detector");

        pulse.reset();
        agplayer::VisualPulseTrigger::Output adjacentBand;
        for (int frame = 0; frame < 62; ++frame) {
            agplayer::VisualSpectrumAnalyzer::Spectrum mixed{};
            if ((frame % 15) < 2) {
                mixed[1] = 240;
                mixed[2] = 230;
                mixed[8] = 255;
            }
            adjacentBand = pulse.process(mixed, 1.0 / 60.0);
        }
        require(adjacentBand.bandStart == 1 && adjacentBand.bandEnd == 8,
                "original auto-track selects the strongest two bins even when nonadjacent");

        const auto verifyLongPlayback = [](int minutes) {
            agplayer::VisualPulseTrigger longPulse;
            const int frameCount = minutes * 60 * 60;
            const int expectedOnsets = frameCount / 30;
            int onsets = 0;
            int lastOnsetFrame = -1;
            for (int frame = 0; frame < frameCount; ++frame) {
                agplayer::VisualSpectrumAnalyzer::Spectrum mixed{};
                const int beatPhase = frame % 15;
                if (beatPhase < 2) {
                    mixed[1] = 240;
                    mixed[2] = 230;
                    // A stronger isolated transient must not make auto-track
                    // normalize the kick across bins 1..8 later in the song.
                    mixed[8] = 255;
                }
                if (longPulse.process(mixed, 1.0 / 60.0).triggered) {
                    ++onsets;
                    lastOnsetFrame = frame;
                }
            }
            require(onsets == expectedOnsets,
                    "long playback must preserve every scheduled kick from intro through tail");
            require(lastOnsetFrame >= frameCount - 30,
                    "long playback must retain the source Pulse cadence through the tail");
        };
        verifyLongPlayback(5);
        verifyLongPlayback(60);
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
