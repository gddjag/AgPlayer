#include "visual_kick_response.hpp"
#include "visual_pulse_trigger.hpp"
#include "visual_snare_trigger.hpp"
#include "visual_spectrum_features.hpp"
#include "immersive_theme_catalog.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <iostream>
#include <cstdlib>

int main() {
    QFile file(QStringLiteral(SONIC_AUDIO_FIXTURE));
    if (!file.open(QIODevice::ReadOnly)) return 2;
    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    agplayer::VisualSpectrumAnalyzer fft;
    const auto fftFrames=root["webAudioFft"].toArray();
    if(fftFrames.size()!=64) return 8;
    int differingBins=0;
    for(const auto value:fftFrames){
        const auto f=value.toObject();
        const auto pcm=f["pcm"].toArray(), expected=f["spectrum"].toArray();
        if(pcm.size()!=1024 || expected.size()!=512) return 9;
        agplayer::VisualSpectrumAnalyzer::Window window{};
        for(int i=0;i<1024;++i) window[i]=float(pcm[i].toDouble());
        const auto actual=fft.process(window);
        for(int i=0;i<512;++i){
            const int difference=std::abs(int(actual[i])-expected[i].toInt());
            if(difference) ++differingBins;
            if(difference>1){std::cerr<<"Web Audio FFT bin "<<i<<" difference="<<difference<<'\n';return 10;}
        }
    }
    std::cout<<"64 Web Audio FFT windows matched within 1 byte; differing bins="<<differingBins<<'\n';
    const auto palettes = root["palettes"].toObject();
    if (palettes.size() != 13) return 5;
    int referencePalettes = 0;
    for (const auto& theme : agplayer::immersive::builtInThemes()) {
        // User-requested AgPlayer preset replaces Daybreak Lime. It is tested
        // by immersive_theme_catalog_test, not an upstream palette assertion.
        if (theme.id == "violet-heart") continue;
        ++referencePalettes;
        const auto expected = palettes[QString::fromUtf8(theme.id.data(), qsizetype(theme.id.size()))].toObject();
        constexpr const char* roles[] = {"uBaseColor1","uBaseColor2","uFogColor","uCoolCore","uCoolEdge","uWarmCore","uWarmEdge","uRippleColor"};
        for (std::size_t i=0;i<theme.colors.size();++i) {
            const auto actual=agplayer::immersive::toWorkingLinear(theme.colors[i]);
            const auto goal=expected[roles[i]].toArray();
            if(goal.size()!=3 || std::abs(actual.red-goal[0].toDouble())>2e-7
                || std::abs(actual.green-goal[1].toDouble())>2e-7
                || std::abs(actual.blue-goal[2].toDouble())>2e-7) return 6;
        }
        if(std::abs(theme.glowIntensity-expected["uGlowIntensity"].toDouble())>2e-7) return 7;
    }
    if (referencePalettes != 12) return 11;
    int checked = 0;
    for (const auto c : root["cases"].toArray()) {
        agplayer::visual::KickResponse kick(agplayer::visual::KickResponse::Mode::Reference);
        agplayer::VisualPulseTrigger pulse;
        agplayer::VisualSnareTrigger snare, meteor(159,174,.45,241,.5);
        agplayer::VisualSpectrumFeatures descriptors;
        agplayer::VisualSpectrumAnalyzer::Spectrum spectrum{};
        int frame = 0;
        for (const auto v : c.toObject()["frames"].toArray()) {
            const auto f = v.toObject(), expected = f["data"].toObject(), events = f["events"].toObject();
            const bool playing = f["playing"].toBool(), releasing = f["releasing"].toBool();
            const double dt = frame == 0 ? 1.0/60 : f["dt"].toDouble();
            if (playing) {
                const auto bytes = QByteArray::fromBase64(f["spectrum"].toString().toLatin1());
                if (bytes.size() != 512) return 3;
                std::copy(bytes.begin(), bytes.end(), spectrum.begin());
            } else for (auto& b : spectrum) b = releasing ? std::uint8_t(std::floor(b*.94)) : 0;
            const auto k = kick.process(spectrum, dt);
            const auto d = descriptors.update(spectrum, playing, releasing);
            const auto p = playing ? pulse.process(spectrum, f["dt"].toDouble()) : pulse.suspend(f["dt"].toDouble());
            const auto s = playing ? snare.process(spectrum) : snare.suspend();
            const auto m = playing ? meteor.process(spectrum) : meteor.suspend();
            const auto same = [&](const char* name, double actual, double goal) {
                if (std::abs(actual-goal) > 1e-9) {
                    std::cerr << "hz=" << c.toObject()["hz"].toInt() << " frame=" << frame
                        << " " << name << " native=" << actual << " original=" << goal << '\n';
                    std::exit(1);
                }
            };
            for (const auto pair : {std::pair{"kickLevel",k.level}, {"kickFlux",k.flux},
                 {"kickThreshold",k.threshold}, {"kickOnset",k.onset}, {"kickEnvelope",k.envelope},
                 {"kickConfidence",k.confidence}, {"energy",d.energy}, {"warmth",d.warmth},
                 {"brightness",d.brightness}, {"sharpness",d.sharpness}, {"smoothness",d.smoothness},
                 {"density",d.density}, {"spectralCentroid",d.spectralCentroid}})
                same(pair.first, pair.second, expected[pair.first].toDouble());
            constexpr const char* names[] = {"subBass","bass","lowMid","mid","highMid","presence","brilliance","air"};
            for (int i=0;i<8;++i) same(names[i], d.bands[i], expected[names[i]].toDouble());
            same("Pulse event", p.triggered, events.contains("Pulse"));
            same("Pulse strength", p.strength, events["Pulse"].toDouble());
            same("Snare event", s.triggered, events.contains("Snare"));
            same("Snare strength", s.strength, events["Snare"].toDouble());
            same("Meteor event", m.triggered, events.contains("Meteor"));
            same("Meteor strength", m.strength, events["Meteor"].toDouble());
            same("band start", double(p.bandStart), f["band"].toArray()[0].toDouble());
            same("band end", double(p.bandEnd), f["band"].toArray()[1].toDouble());
            ++frame; ++checked;
        }
    }
    std::cout << checked << " original TypeScript frames and 12 retained upstream palettes matched\n";
    return checked == 1680 ? 0 : 4;
}
