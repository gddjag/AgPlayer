#include "audio_visual_feature_controller.hpp"
#include "playback_controller.hpp"
#include "visual_spectrum_features.hpp"
#include "visual_kick_response.hpp"
#include "visual_audio_frame_analyzer.hpp"
#include "visual_terrain_response.hpp"
#include "visual_snare_trigger.hpp"

#include <agplayer/c_api.h>

#include <QSignalSpy>
#include <QTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

#include <cmath>
#include <limits>
#include <memory>

namespace {
const QStringList traceFields{
    "subBass", "bass", "lowMid", "mid", "highMid", "presence", "brilliance", "air",
    "energy", "warmth", "brightness", "sharpness", "smoothness", "density", "spectralCentroid",
    "kickLevel", "kickFlux", "kickThreshold", "kickOnset", "kickEnvelope", "kickConfidence"};

QString validateAudioTrace(const QJsonObject& root)
{
    if (root.value("sampleRate").toInt() <= 0) return "sampleRate must be positive";
    const auto frames = root.value("frames").toArray();
    if (frames.isEmpty()) return "frames must be nonempty";
    double previousTime = -1;
    double previousReads = 0;
    for (int i = 0; i < frames.size(); ++i) {
        const auto frame = frames[i].toObject();
        const auto fail = [i](const QString& detail) { return QString("frame %1: %2").arg(i).arg(detail); };
        for (const auto& key : {"time", "sampleIndex", "frequencyReads"})
            if (!frame[key].isDouble() || !std::isfinite(frame[key].toDouble()) || frame[key].toDouble() < 0)
                return fail(QString("invalid %1").arg(key));
        const double time = frame["time"].toDouble();
        const double reads = frame["frequencyReads"].toDouble();
        if (time < previousTime || reads < previousReads || reads < 1 || reads != std::floor(reads))
            return fail("nonmonotonic time/frequencyReads");
        previousTime = time;
        previousReads = reads;
        for (const auto& key : {"timeDomain", "spectrum"}) {
            const auto values = frame[key].toArray();
            if (values.size() != (QString(key) == "timeDomain" ? 1024 : 512))
                return fail(QString("invalid %1 length").arg(key));
            for (const auto& value : values) {
                if (!value.isDouble() || !std::isfinite(value.toDouble())) return fail("nonfinite array value");
                if (QString(key) == "spectrum" && (value.toDouble() < 0 || value.toDouble() > 255
                    || value.toDouble() != std::floor(value.toDouble()))) return fail("invalid spectrum byte");
            }
        }
        const auto descriptors = frame["descriptors"].toObject();
        for (const auto& key : traceFields)
            if (!descriptors[key].isDouble() || !std::isfinite(descriptors[key].toDouble()))
                return fail(QString("missing/nonfinite descriptor %1").arg(key));
    }
    return {};
}
}

class AudioVisualFeatureControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void snareMatchesOriginalBoundaryEvents();
    void snareMatchesOriginalRealTrace();
    void referenceAudioTraceSchemaRejectsMalformedInput();
    void referenceAudioTraceMatchesOriginal();
    void referenceTerrainTraceMatchesOriginal();
    void renderFrameAnalysisOwnsCadenceAndResets();
    void pausedRenderFrameUsesOriginalVisualRelease();
    void renderFrameConsumersSharePcmWithoutGuiAnalysis();
    void visualResetNotifiesOnceAfterClearing();
    void visualKickSensitivityControlsDetector();
    void visualPcmUpdatesDescriptorsBeforeReadyAndSkipsEmptyReads();
    void visualPcmDescriptorHistoryResets_data();
    void visualPcmDescriptorHistoryResets();
    void visualPcmAssemblesAndResets();
    void visualPcmLifecycle();
    void visualPcmPauseRetainsWindowForRelease();
    void visualPcmDelayedPollDrainsContiguousAudioWithoutReset();
    void visualPcmBatchingPreservesAnalysisAndEpochReset();
    void renderFrameBatchPreservesDelayedPcmWindows();
    void reliableBpmEmitsOnceEveryEightBeats();
    void reliableBpmEmitsRegularBeatPulseAndEightBeatImpact();
    void reliableBpmUsesTransientPhaseAndDeduplicatesGridBeat();
    void reliableBpmLocksFutureGridToTheObservedTransientPhase();
    void rhythmSensitivityRecoversModerateRepeatedDrumOnsets();
    void seeksAndTrackChangesDoNotEmitDuplicateImpacts();
    void unreliableTimingUsesDebouncedTransientFallbackOnly();
    void outputLevelsHaveFastAttackAndVisibleDecay();
    void outputLevelsPollCoreAtPlaybackCadence();
    void adaptiveTransientFloorRejectsRepeatedBackgroundPulses();
    void perceptualBandsKeepNarrowBassEnergyLocalized();
    void narrowBassTransientsTriggerKickWithoutMidrangeLeakage();
    void perceptualBandsUseFastAttackAndProgressiveRelease();
    void perceptualBandsPreserveSparseMidAndHighTones();
    void perceptualBandsReleaseWithinEightFrames();
    void silentWaveformDoesNotEnableSyntheticBeatGrid();
    void reliableBeatGridKeepsCountingWithoutLightingSilentPassages();
    void fadeOutPublicationResidueDoesNotLightBeatGrid_data();
    void fadeOutPublicationResidueDoesNotLightBeatGrid();
};

void AudioVisualFeatureControllerTest::snareMatchesOriginalBoundaryEvents()
{
    // Actual fixed ec8 AudioEngine.getAudioData/onFreqTrigger output, exported
    // with snare-oracle.cjs (external study). These are not native-computed expectations.
    const std::array<int,4> eventFrames{6,38,81,113};
    const std::array<double,4> strengths{3.6091482352941178,3.6000476710532605,
        3.6000000010394704,3.6000002865118};
    agplayer::VisualSnareTrigger trigger;
    // Repeat after invalidation to catch retained flux/hold/history state.
    for (int pass = 0; pass < 2; ++pass) {
        int event = 0;
        for (int frame = 0; frame < 160; ++frame) {
            agplayer::VisualSpectrumAnalyzer::Spectrum spectrum{};
            const int amplitude = frame == 0 ? 2 : frame == 2 ? 3
                : (frame == 5 || frame == 15 || frame == 37 || frame == 80 || frame == 112) ? 255 : 0;
            for (int bin = 47; bin <= 120; ++bin) spectrum[bin] = amplitude;
            spectrum[46] = spectrum[121] = frame % 2 ? 255 : 0;
            const auto actual = trigger.process(spectrum);
            const bool expected = event < 4 && eventFrames[event] == frame;
            QVERIFY2(actual.triggered == expected, qPrintable(QString("Snare frame %1").arg(frame)));
            if (expected) {
                QVERIFY(std::abs(actual.strength - strengths[event]) < 1e-12);
                ++event;
            } else QCOMPARE(actual.strength, 0.0);
        }
        QCOMPARE(event, 4);
        agplayer::VisualSpectrumAnalyzer::Spectrum loud;
        loud.fill(255);
        QVERIFY(!trigger.process(loud, false).triggered);
    }
}

void AudioVisualFeatureControllerTest::snareMatchesOriginalRealTrace()
{
    const auto path = qEnvironmentVariable("AGPLAYER_REFERENCE_SNARE_ORACLE");
    if (path.isEmpty()) QSKIP("Optional fixed-original Snare oracle not provided");
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    const auto root = document.object();
    QCOMPARE(root["sourceSha256"].toString(),
        QString("0d96a161a4da5559189cd0d35f5774b9717867522d6e544c534ab68362fe60ea"));
    const auto frames = root["real"].toArray();
    QVERIFY(!frames.isEmpty());
    agplayer::VisualSnareTrigger trigger;
    int events = 0;
    for (int i = 0; i < frames.size(); ++i) {
        const auto frame = frames[i].toObject();
        const auto values = frame["spectrum"].toArray();
        QCOMPARE(values.size(), 512);
        agplayer::VisualSpectrumAnalyzer::Spectrum spectrum{};
        for (int bin = 0; bin < 512; ++bin) {
            QVERIFY(values[bin].isDouble());
            const double value = values[bin].toDouble();
            QVERIFY(value >= 0 && value <= 255 && value == std::floor(value));
            spectrum[bin] = std::uint8_t(value);
        }
        const auto expected = frame["events"].toArray();
        QVERIFY(expected.size() <= 1);
        const auto actual = trigger.process(spectrum);
        QVERIFY2(actual.triggered == !expected.isEmpty(), qPrintable(QString("Original Snare frame %1").arg(i)));
        if (!expected.isEmpty()) {
            const auto event = expected[0].toObject();
            QCOMPARE(event["action"].toString(), QString("Snare"));
            QVERIFY(event["strength"].isDouble());
            QVERIFY2(std::abs(actual.strength - event["strength"].toDouble()) < 1e-12,
                qPrintable(QString("Original Snare strength frame %1").arg(i)));
            ++events;
        } else QCOMPARE(actual.strength, 0.0);
    }
    QVERIFY(events > 0);
}

void AudioVisualFeatureControllerTest::referenceAudioTraceSchemaRejectsMalformedInput()
{
    // Parser-only shape fixture: deliberately never fed to the analyzer as an oracle.
    QJsonObject root{{"sampleRate", 48000}};
    QVERIFY(!validateAudioTrace(root).isEmpty());
    QJsonArray pcm, spectrum;
    for (int i = 0; i < 1024; ++i) pcm.append(0);
    for (int i = 0; i < 512; ++i) spectrum.append(0);
    QJsonObject descriptors;
    for (const auto& field : traceFields) descriptors[field] = 0;
    QJsonObject frame{{"time", .016}, {"sampleIndex", 768}, {"frequencyReads", 1},
                      {"timeDomain", pcm}, {"spectrum", spectrum}, {"descriptors", descriptors}};
    root["frames"] = QJsonArray{frame};
    QVERIFY(validateAudioTrace(root).isEmpty());
    spectrum.removeLast();
    frame["spectrum"] = spectrum;
    root["frames"] = QJsonArray{frame};
    QCOMPARE(validateAudioTrace(root), QString("frame 0: invalid spectrum length"));
    spectrum.append(256);
    frame["spectrum"] = spectrum;
    root["frames"] = QJsonArray{frame};
    QCOMPARE(validateAudioTrace(root), QString("frame 0: invalid spectrum byte"));
}

void AudioVisualFeatureControllerTest::referenceAudioTraceMatchesOriginal()
{
    const auto path = qEnvironmentVariable("AGPLAYER_REFERENCE_AUDIO_TRACE");
    if (path.isEmpty()) QSKIP("Original audio trace not supplied; no parity claim");
    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    QVERIFY2(parseError.error == QJsonParseError::NoError, qPrintable(parseError.errorString()));
    QVERIFY(document.isObject());
    const auto root = document.object();
    const auto error = validateAudioTrace(root);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    agplayer::VisualAudioFrameAnalyzer analyzer;
    agplayer::VisualAudioFrameAnalyzer::Snapshot snapshot;
    snapshot.sampleRate = root["sampleRate"].toInt();
    snapshot.epoch = 1;
    snapshot.valid = true;
    agplayer::VisualAudioFrameAnalyzer::Frame actual;
    double lastAnalysisTime = 0;
    double lastReads = 0;
    const auto frames = root["frames"].toArray();
    for (int i = 0; i < frames.size(); ++i) {
        const auto frame = frames[i].toObject();
        const auto reads = frame["frequencyReads"].toDouble();
        const auto time = frame["time"].toDouble();
        if (reads != lastReads) {
            QVERIFY2(reads == lastReads + 1, "Trace omitted frequency-analysis frames");
            const auto pcm = frame["timeDomain"].toArray();
            for (int sample = 0; sample < 1024; ++sample) snapshot.pcm[sample] = float(pcm[sample].toDouble());
            actual = analyzer.process(snapshot, time - lastAnalysisTime);
            lastAnalysisTime = time;
            lastReads = reads;
        }
        const auto bytes = frame["spectrum"].toArray();
        for (int bin = 0; bin < 512; ++bin) {
            const auto detail = QString("frame %1 sampleIndex %2 bin %3: native %4 original %5")
                .arg(i).arg(frame["sampleIndex"].toDouble(), 0, 'f', 0).arg(bin)
                .arg(actual.spectrum[bin]).arg(bytes[bin].toInt());
            QVERIFY2(actual.spectrum[bin] == bytes[bin].toInt(), qPrintable(detail));
        }
        const auto& d = actual.descriptors;
        const auto& k = actual.kick;
        const std::array<double, 21> values{d.bands[0],d.bands[1],d.bands[2],d.bands[3],
            d.bands[4],d.bands[5],d.bands[6],d.bands[7],d.energy,d.warmth,d.brightness,
            d.sharpness,d.smoothness,d.density,d.spectralCentroid,k.level,k.flux,k.threshold,k.onset,k.envelope,k.confidence};
        const auto expected = frame["descriptors"].toObject();
        for (int field = 0; field < traceFields.size(); ++field) {
            const auto goal = expected[traceFields[field]].toDouble();
            const auto detail = QString("frame %1 sampleIndex %2 %3: native %4 original %5")
                .arg(i).arg(frame["sampleIndex"].toDouble(), 0, 'f', 0).arg(traceFields[field])
                .arg(values[field], 0, 'g', 17).arg(goal, 0, 'g', 17);
            QVERIFY2(std::abs(values[field] - goal) <= 1e-6, qPrintable(detail));
        }
    }
}

void AudioVisualFeatureControllerTest::referenceTerrainTraceMatchesOriginal()
{
    const auto path = qEnvironmentVariable("AGPLAYER_REFERENCE_TERRAIN_TRACE");
    if (path.isEmpty()) QSKIP("Original per-frame terrain response trace not supplied");
    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    QVERIFY2(parseError.error == QJsonParseError::NoError, qPrintable(parseError.errorString()));
    QVERIFY(document.isObject());
    const auto root = document.object();
    const auto error = validateAudioTrace(root);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const auto settings = root["terrainSettings"].toObject();
    const auto bands = settings["bands"].toArray();
    const auto enabled = settings["enabledBands"].toArray();
    QCOMPARE(bands.size(), 8);
    QCOMPARE(enabled.size(), 8);
    QVERIFY(settings["motionSpeed"].isDouble());
    agplayer::VisualTerrainResponse response;
    agplayer::VisualTerrainResponse::EqBands eq;
    agplayer::VisualTerrainResponse::EnabledBands on;
    for (int i = 0; i < 8; ++i) {
        QVERIFY(bands[i].isDouble());
        QVERIFY(enabled[i].isBool());
        eq[i] = bands[i].toDouble() / 100.0;
        on[i] = enabled[i].toBool();
    }
    const QStringList fields{"uSubBass", "uBass", "uLowMid", "uMid", "uHighMid", "uPresence",
        "uBrilliance", "uAir", "uEnergy", "uWarmth", "uBrightness", "uSharpness", "uSmoothness",
        "uDensity", "uSpectralCentroid"};
    const auto frames = root["frames"].toArray();
    for (int frameIndex = 0; frameIndex < frames.size(); ++frameIndex) {
        const auto frame = frames[frameIndex].toObject();
        const auto original = frame["descriptors"].toObject();
        // Isolate the response layer using upstream's captured input; do not
        // regenerate its expected output with native FFT/features helpers.
        agplayer::VisualSpectrumFeatures::Features input;
        for (int i = 0; i < 8; ++i) input.bands[i] = original[traceFields[i]].toDouble();
        input.energy = original["energy"].toDouble();
        input.sharpness = original["sharpness"].toDouble();
        input.smoothness = original["smoothness"].toDouble();
        input.density = original["density"].toDouble();
        input.spectralCentroid = original["spectralCentroid"].toDouble();
        const auto terrain = frame["terrainResponse"].toObject();
        QVERIFY2(terrain["deltaSeconds"].isDouble(), qPrintable(QString("frame %1: missing render delta").arg(frameIndex)));
        const auto& out = response.update(input, original["kickEnvelope"].toDouble(), eq, on,
            terrain["deltaSeconds"].toDouble(), settings["motionSpeed"].toDouble());
        const std::array<double, 15> actual{out.bands[0],out.bands[1],out.bands[2],out.bands[3],
            out.bands[4],out.bands[5],out.bands[6],out.bands[7],out.energy,out.warmth,out.brightness,
            out.sharpness,out.smoothness,out.density,out.spectralCentroid};
        const auto expected = terrain["uniforms"].toObject();
        for (int field = 0; field < fields.size(); ++field) {
            const auto goal = expected[fields[field]];
            const auto detail = QString("terrain frame %1 sampleIndex %2 %3: native %4 original %5")
                .arg(frameIndex).arg(frame["sampleIndex"].toDouble(), 0, 'f', 0).arg(fields[field])
                .arg(actual[field], 0, 'g', 17).arg(goal.toDouble(), 0, 'g', 17);
            QVERIFY2(goal.isDouble() && std::isfinite(goal.toDouble()), qPrintable("Missing/nonfinite " + detail));
            QVERIFY2(std::abs(actual[field] - goal.toDouble()) <= 1e-6, qPrintable(detail));
        }
    }
}

void AudioVisualFeatureControllerTest::renderFrameAnalysisOwnsCadenceAndResets()
{
    using Analyzer = agplayer::VisualAudioFrameAnalyzer;
    Analyzer analyzer;
    Analyzer::Snapshot pcm;
    pcm.sampleRate = 48000;
    pcm.epoch = 1;
    pcm.valid = true;
    pcm.pcm.fill(.1f);
    const auto first = analyzer.process(pcm, .2);
    QVERIFY(first.valid);
    QVERIFY(first.descriptors.energy > 0);
    agplayer::visual::KickResponse kick(agplayer::visual::KickResponse::Mode::Reference);
    agplayer::VisualSpectrumAnalyzer spectrum;
    const auto display = spectrum.process(pcm.pcm);
    const auto expected = kick.process(display, 1.0 / 60.0);
    QCOMPARE(first.kick.envelope, expected.envelope);
    // No new audio callback is needed for the next actual display frame.
    const auto second = analyzer.process(pcm, 1.0 / 60.0);
    QVERIFY(second.descriptors.energy > first.descriptors.energy);
    ++pcm.epoch;
    const auto afterSeek = analyzer.process(pcm, .2);
    QCOMPARE(afterSeek.descriptors.energy, first.descriptors.energy);
    QCOMPARE(afterSeek.kick.envelope, first.kick.envelope);
    pcm.valid = false;
    const auto cleared = analyzer.process(pcm, .016);
    QVERIFY(!cleared.valid);
    QCOMPARE(cleared.descriptors.energy, 0.0);
    QCOMPARE(cleared.kick.envelope, 0.0);
    pcm.valid = true;
    QCOMPARE(analyzer.process(pcm, .016).descriptors.energy, first.descriptors.energy);
}

void AudioVisualFeatureControllerTest::pausedRenderFrameUsesOriginalVisualRelease()
{
    using Analyzer = agplayer::VisualAudioFrameAnalyzer;
    Analyzer analyzer;
    Analyzer::Snapshot pcm;
    pcm.sampleRate = 48000;
    pcm.epoch = 1;
    pcm.valid = true;
    pcm.pcm.fill(.18f);

    const auto playing = analyzer.process(pcm, 1.0 / 60.0);
    const double playingEnergy = playing.descriptors.energy;
    agplayer::visual::KickResponse original(agplayer::visual::KickResponse::Mode::Reference);
    original.process(playing.spectrum, 1.0/60);
    QVERIFY(playingEnergy > 0.0);

    pcm.valid = false;
    pcm.paused = true;
    pcm.releasing = true;
    const auto released = analyzer.process(pcm, 1.0 / 60.0);
    QVERIFY(released.valid);
    QVERIFY(released.descriptors.energy > 0.0);
    QVERIFY(released.descriptors.energy < playingEnergy);
    QCOMPARE(released.kick.onset, original.process(released.spectrum, 1.0/60).onset);
    QCOMPARE(released.descriptors.energy, playingEnergy * .965);
    double releaseEnergy = released.descriptors.energy;
    for (int frame = 0; frame < 11; ++frame)
        releaseEnergy = analyzer.process(pcm, 1.0 / 60.0).descriptors.energy;
    QVERIFY(std::abs(releaseEnergy - playingEnergy * std::pow(.965,12)) < 1e-12);

    pcm.paused = false;
    pcm.releasing = false;
    const auto hardReset = analyzer.process(pcm, 1.0 / 60.0);
    QVERIFY(!hardReset.valid);
    QCOMPARE(hardReset.descriptors.energy, 0.0);
}

void AudioVisualFeatureControllerTest::renderFrameConsumersSharePcmWithoutGuiAnalysis()
{
    AudioVisualFeatureController controller;
    controller.setActive(true);
    controller.acquireRenderFrameAnalysis();
    controller.acquireRenderFrameAnalysis();
    ag_visual_pcm_snapshot pcm{};
    pcm.generation = 1;
    pcm.sample_rate = 48000;
    pcm.sample_count = 1024;
    std::fill(std::begin(pcm.samples), std::end(pcm.samples), .1f);
    controller.ingestVisualPcm(pcm);
    const auto first = controller.visualPcmSnapshot();
    QVERIFY(first.valid);
    QCOMPARE(first.sampleRate, 48000);
    QCOMPARE(first.pcm[400], .1f);
    QCOMPARE(controller.visualSpectrumUpdateCount(), 0);
    controller.releaseRenderFrameAnalysis();
    pcm.first_sample_index = 1024;
    controller.ingestVisualPcm(pcm);
    QCOMPARE(controller.visualSpectrumUpdateCount(), 0);
    controller.setActive(false);
    const auto cleared = controller.visualPcmSnapshot();
    QVERIFY(!cleared.valid);
    QVERIFY(cleared.epoch != first.epoch);
    controller.releaseRenderFrameAnalysis();
    controller.setActive(true);
    controller.ingestVisualPcm(pcm);
    QCOMPARE(controller.visualSpectrumUpdateCount(), 1);
}

void AudioVisualFeatureControllerTest::visualPcmBatchingPreservesAnalysisAndEpochReset()
{
    AudioVisualFeatureController whole, split;
    whole.setActive(true);
    split.setActive(true);
    ag_visual_pcm_snapshot pcm{};
    pcm.generation = 1;
    pcm.sample_rate = 48000;
    pcm.sample_count = 1024;
    const auto fillSignal = [](ag_visual_pcm_snapshot& block) {
        constexpr double tau = 6.2831853071795864769;
        for (std::size_t i = 0; i < block.sample_count; ++i) {
            const double index = double(block.first_sample_index + i);
            const double seconds = index / 48000.0;
            const double envelope = .12 + .24 * index / 2304.0;
            block.samples[i] = float(envelope * (
                .55 * std::sin(tau * 93.0 * seconds)
                + .30 * std::sin(tau * 713.0 * seconds + .4)
                + .15 * std::sin(tau * 3101.0 * seconds + .9)));
        }
    };
    fillSignal(pcm);
    whole.ingestVisualPcm(pcm, true);
    pcm.first_sample_index = 1024;
    fillSignal(pcm);
    whole.ingestVisualPcm(pcm, true);
    pcm.sample_count = 256;
    for (int i = 0; i < 8; ++i) {
        pcm.first_sample_index = i * 256;
        fillSignal(pcm);
        split.ingestVisualPcm(pcm, true);
    }
    whole.analyzeVisualPcm();
    split.analyzeVisualPcm();
    QVERIFY(std::any_of(whole.visualSpectrum().begin(), whole.visualSpectrum().end(),
                        [](auto value) { return value != 0; }));
    QCOMPARE(whole.visualSpectrum(), split.visualSpectrum());
    QCOMPARE(whole.visualFeatures().bands, split.visualFeatures().bands);
    QCOMPARE(whole.visualFeatures().energy, split.visualFeatures().energy);
    QCOMPARE(whole.visualKick().level, split.visualKick().level);
    QCOMPARE(whole.visualKick().envelope, split.visualKick().envelope);
    QCOMPARE(whole.visualKick().onset, split.visualKick().onset);
    QSignalSpy ready(&split, &AudioVisualFeatureController::visualSpectrumReady);
    pcm.first_sample_index = 2048;
    fillSignal(pcm);
    split.ingestVisualPcm(pcm, true);
    ag_visual_pcm_snapshot empty{};
    empty.generation = 2;
    split.ingestVisualPcm(empty, true);
    split.analyzeVisualPcm();
    QCOMPARE(ready.count(), 0);
    QCOMPARE(split.visualFeatures().energy, 0.0);
    QCOMPARE(split.visualKick().envelope, 0.0);
}

void AudioVisualFeatureControllerTest::visualPcmDelayedPollDrainsContiguousAudioWithoutReset()
{
    const auto fixture = qgetenv("AGPLAYER_TEST_AUDIO");
    QVERIFY(!fixture.isEmpty());
    ag_player* player = nullptr;
    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4096U};
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    {
        PlaybackController playback(player);
        AudioVisualFeatureController features(&playback);
        features.setActive(true);
        QCOMPARE(ag_player_load(player, fixture.constData()), AG_OK);
        QCOMPARE(ag_player_play(player), AG_OK);
        QTRY_VERIFY_WITH_TIMEOUT(features.visualSpectrumUpdateCount() > 0, 1000);
        QSignalSpy resets(&features, &AudioVisualFeatureController::visualStateReset);
        QSignalSpy ready(&features, &AudioVisualFeatureController::visualSpectrumReady);
        const auto before = features.visualSpectrumUpdateCount();
        const auto beforeIndex = features.visualNextIndex_;
        // A GUI scheduling delay is not an audio discontinuity. The NULL
        // output continues producing real PCM while no GUI timer can consume it.
        QTest::qSleep(60);
        features.pollOutputLevels();
        QCOMPARE(resets.count(), 0);
        QVERIFY2(features.visualNextIndex_ > beforeIndex + 1024,
                 "One GUI poll must drain more than one queued PCM window");
        QCOMPARE(features.visualSpectrumUpdateCount(), before + 1);
        QCOMPARE(ready.count(), 1);
        QCOMPARE(ag_player_pause(player), AG_OK);
        features.pollOutputLevels();
        // Pause is a visual release, not a seek/track discontinuity. Keep the
        // last complete PCM window so the renderer can decay it smoothly.
        QCOMPARE(resets.count(), 0);
        const auto paused = features.visualPcmSnapshot();
        QVERIFY(paused.paused);
        QVERIFY(paused.releasing);
        QVERIFY(!paused.valid);
        QCOMPARE(paused.epoch, features.visualPcmEpoch_);
    }
    ag_player_destroy(player);
}

void AudioVisualFeatureControllerTest::visualResetNotifiesOnceAfterClearing()
{
    AudioVisualFeatureController controller;
    QSignalSpy reset(&controller, SIGNAL(visualStateReset()));
    QVERIFY(reset.isValid());
    controller.setActive(true);
    QCOMPARE(reset.count(), 0);
    ag_visual_pcm_snapshot pcm{};
    pcm.generation = 1;
    pcm.sample_rate = 48000;
    pcm.sample_count = 1024;
    std::fill(std::begin(pcm.samples), std::end(pcm.samples), .1f);
    controller.ingestVisualPcm(pcm);
    QCOMPARE(reset.count(), 0);
    QVERIFY(controller.visualFeatures().energy > 0);
    controller.setActive(false);
    QCOMPARE(reset.count(), 1);
    QCOMPARE(controller.visualFeatures().energy, 0.0);
    QCOMPARE(controller.visualKick().envelope, 0.0);
    controller.resetVisualPcm();
    QCOMPARE(reset.count(), 1);
    controller.setActive(true);
    controller.ingestVisualPcm(pcm);
    ag_visual_pcm_snapshot empty{};
    empty.generation = 2;
    controller.ingestVisualPcm(empty);
    QCOMPARE(reset.count(), 2);
    QCOMPARE(controller.visualFeatures().energy, 0.0);
    controller.ingestVisualPcm(empty);
    QCOMPARE(reset.count(), 2);
}

void AudioVisualFeatureControllerTest::visualKickSensitivityControlsDetector()
{
    AudioVisualFeatureController controller;
    QCOMPARE(controller.property("visualKickSensitivity").toInt(), 50);
    QVERIFY(controller.setProperty("visualKickSensitivity", -20));
    QCOMPARE(controller.property("visualKickSensitivity").toInt(), 0);
    controller.setActive(true);
    ag_visual_pcm_snapshot pcm{};
    pcm.generation = 1;
    pcm.sample_rate = 48000;
    pcm.sample_count = 1024;
    controller.ingestVisualPcm(pcm);
    QCOMPARE(controller.visualKick().threshold, .05);
    QVERIFY(controller.setProperty("visualKickSensitivity", 120));
    QCOMPARE(controller.property("visualKickSensitivity").toInt(), 100);
    pcm.first_sample_index = 1024;
    controller.ingestVisualPcm(pcm);
    QCOMPARE(controller.visualKick().threshold, .016);
}

void AudioVisualFeatureControllerTest::visualPcmUpdatesDescriptorsBeforeReadyAndSkipsEmptyReads()
{
    AudioVisualFeatureController controller;
    controller.setActive(true);
    ag_visual_pcm_snapshot pcm{};
    pcm.sample_rate = 48000;
    pcm.generation = 1;
    pcm.sample_count = 512;
    std::fill(std::begin(pcm.samples), std::end(pcm.samples), .1f);
    int readyCount = 0;
    connect(&controller, &AudioVisualFeatureController::visualSpectrumReady, this, [&] {
        ++readyCount;
        QVERIFY(controller.visualFeatures().energy > 0);
        QVERIFY(controller.visualKick().level > 0);
    });
    controller.ingestVisualPcm(pcm);
    QCOMPARE(readyCount, 0);
    QCOMPARE(controller.visualFeatures().energy, 0.0);
    pcm.first_sample_index = 512;
    controller.ingestVisualPcm(pcm);
    QCOMPARE(readyCount, 1);
    const auto first = controller.visualFeatures();
    const auto kick = controller.visualKick();
    QVERIFY(first.energy > 0);
    // The independently tested detector is an oracle for the controller's dt wiring.
    agplayer::visual::KickResponse detector;
    const auto firstExpected = detector.process(controller.visualSpectrum(), 1.0 / 60.0);
    QCOMPARE(kick.level, firstExpected.level);
    QCOMPARE(kick.envelope, firstExpected.envelope);
    ag_visual_pcm_snapshot empty{};
    empty.generation = 1;
    for (int i = 0; i < 10; ++i) controller.ingestVisualPcm(empty);
    QCOMPARE(readyCount, 1);
    QCOMPARE(controller.visualFeatures().energy, first.energy);
    QCOMPARE(controller.visualFeatures().smoothness, first.smoothness);
    QCOMPARE(controller.visualKick().envelope, kick.envelope);
    QCOMPARE(controller.visualKick().flux, kick.flux);
    pcm.first_sample_index = 1024;
    pcm.sample_count = 128;
    QTest::qSleep(30);
    const double lowerDt = std::min(.25, controller.visualAnalysisTimer_.nsecsElapsed() / 1e9);
    QElapsedTimer callTimer;
    callTimer.start();
    controller.ingestVisualPcm(pcm);
    const double upperDt = std::min(.25, lowerDt + callTimer.nsecsElapsed() / 1e9 + .001);
    QCOMPARE(readyCount, 2);
    // Elapsed analysis time, not 128/sample_rate, drives the envelope.
    auto upperDetector = detector;
    const auto lower = detector.process(controller.visualSpectrum(), lowerDt);
    const auto upper = upperDetector.process(controller.visualSpectrum(), upperDt);
    const auto within = [](double value, double a, double b) {
        return value >= std::min(a, b) - 1e-9 && value <= std::max(a, b) + 1e-9;
    };
    QVERIFY(within(controller.visualKick().level, lower.level, upper.level));
    QVERIFY(within(controller.visualKick().envelope, lower.envelope, upper.envelope));
    QVERIFY(controller.visualFeatures().energy > first.energy);
    QCOMPARE(controller.energy(), 0.0);
    QCOMPARE(controller.beatRevision(), 0);
    QCOMPARE(controller.derivedUpdateCount(), 0);
}

void AudioVisualFeatureControllerTest::visualPcmDescriptorHistoryResets_data()
{
    QTest::addColumn<int>("interruption");
    QTest::newRow("generation") << 0;
    QTest::newRow("sample-rate") << 1;
    QTest::newRow("sample-index") << 2;
    QTest::newRow("empty-generation") << 3;
    QTest::newRow("inactive") << 4;
}

void AudioVisualFeatureControllerTest::visualPcmDescriptorHistoryResets()
{
    QFETCH(int, interruption);
    AudioVisualFeatureController controller;
    controller.setActive(true);
    ag_visual_pcm_snapshot pcm{};
    pcm.generation = 1;
    pcm.sample_rate = 48000;
    pcm.sample_count = 1024;
    std::fill(std::begin(pcm.samples), std::end(pcm.samples), .2f);
    controller.ingestVisualPcm(pcm);
    pcm.first_sample_index = 1024;
    controller.ingestVisualPcm(pcm);
    QVERIFY(controller.visualFeatures().energy > 0);
    QVERIFY(controller.visualKick().envelope > 0);
    pcm.first_sample_index = 2048;
    pcm.sample_count = 256;
    if (interruption == 0) ++pcm.generation;
    if (interruption == 1) pcm.sample_rate = 44100;
    if (interruption == 2) ++pcm.first_sample_index;
    if (interruption == 3) { ++pcm.generation; pcm.sample_count = 0; }
    if (interruption == 4) controller.setActive(false);
    controller.ingestVisualPcm(pcm);
    const auto reset = controller.visualFeatures();
    QCOMPARE(reset.energy, 0.0);
    QCOMPARE(reset.warmth, 0.0);
    QCOMPARE(reset.brightness, 0.0);
    QCOMPARE(reset.sharpness, 0.0);
    QCOMPARE(reset.smoothness, 0.0);
    QCOMPARE(reset.density, 0.0);
    QCOMPARE(reset.spectralCentroid, 0.0);
    for (double band : reset.bands) QCOMPARE(band, 0.0);
    const auto kick = controller.visualKick();
    QCOMPARE(kick.level, 0.0);
    QCOMPARE(kick.flux, 0.0);
    QCOMPARE(kick.threshold, 0.0);
    QCOMPARE(kick.onset, 0.0);
    QCOMPARE(kick.envelope, 0.0);
    QCOMPARE(kick.confidence, 0.0);
    QCOMPARE(kick.windowIndex, std::size_t(0));
    // Same post-interruption samples must behave exactly like a fresh stream.
    controller.setActive(true);
    if (pcm.sample_count) pcm.first_sample_index += pcm.sample_count;
    pcm.sample_count = (interruption < 3) ? 768 : 1024;
    controller.ingestVisualPcm(pcm);
    AudioVisualFeatureController fresh;
    fresh.setActive(true);
    pcm.sample_count = 1024;
    fresh.ingestVisualPcm(pcm);
    QCOMPARE(controller.visualFeatures().energy, fresh.visualFeatures().energy);
    QCOMPARE(controller.visualFeatures().smoothness, fresh.visualFeatures().smoothness);
    QCOMPARE(controller.visualKick().level, fresh.visualKick().level);
    QCOMPARE(controller.visualKick().flux, fresh.visualKick().flux);
    QCOMPARE(controller.visualKick().envelope, fresh.visualKick().envelope);
}

void AudioVisualFeatureControllerTest::visualPcmLifecycle()
{
    ag_player* player = nullptr;
    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4096U};
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    const auto fixture = qgetenv("AGPLAYER_TEST_AUDIO");
    QCOMPARE(ag_player_load(player, fixture.constData()), AG_OK);
    QCOMPARE(ag_player_play(player), AG_OK);
    auto playback = std::make_unique<PlaybackController>(player);
    ag_visual_pcm_snapshot pcm{};
    auto expectDisabled = [&] {
        QTest::qWait(40);
        QCOMPARE(ag_player_read_visual_pcm(player, &pcm), AG_OK);
        QCOMPARE(pcm.sample_count, std::size_t(0));
    };
    {
        AudioVisualFeatureController features(playback.get());
        expectDisabled();
        features.setActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(features.visualSpectrumUpdateCount() > 0, 1000);
        features.setPlaybackController(nullptr);
        expectDisabled();
        features.setPlaybackController(playback.get());
        const auto count = features.visualSpectrumUpdateCount();
        QTRY_VERIFY_WITH_TIMEOUT(features.visualSpectrumUpdateCount() > count, 1000);
    }
    expectDisabled();
    {
        AudioVisualFeatureController features(playback.get());
        features.setActive(true);
        playback.reset();
        QVERIFY(features.playback_.isNull());
        QVERIFY(!features.outputLevelTimer_.isActive());
        QCOMPARE(features.visualPcmSize_, std::size_t(0));
        features.pollOutputLevels();
        expectDisabled();
    }
    ag_player_destroy(player);
}

void AudioVisualFeatureControllerTest::visualPcmPauseRetainsWindowForRelease()
{
    ag_player* player = nullptr;
    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4096U};
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    const auto cleanup = qScopeGuard([&] { ag_player_destroy(player); });
    const auto fixture = qgetenv("AGPLAYER_TEST_AUDIO");
    QCOMPARE(ag_player_load(player, fixture.constData()), AG_OK);
    QCOMPARE(ag_player_play(player), AG_OK);

    PlaybackController playback(player);
    AudioVisualFeatureController features(&playback);
    features.setActive(true);
    features.acquireRenderFrameAnalysis();
    QTRY_VERIFY_WITH_TIMEOUT(features.visualPcmSnapshot().valid, 1000);
    const auto playing = features.visualPcmSnapshot();

    QCOMPARE(ag_player_pause(player), AG_OK);
    features.pollOutputLevels();
    const auto paused = features.visualPcmSnapshot();
    QVERIFY(!paused.valid);
    QVERIFY(paused.paused);
    QVERIFY(paused.releasing);
    QCOMPARE(paused.epoch, playing.epoch);
    QVERIFY(std::equal(paused.pcm.cbegin(), paused.pcm.cend(),
                       playing.pcm.cbegin()));
    QTest::qWait(550);
    QVERIFY(features.visualPcmSnapshot().releasing);
    QTest::qWait(1100);
    QVERIFY(!features.visualPcmSnapshot().releasing);

    QCOMPARE(ag_player_play(player), AG_OK);
    QCOMPARE(ag_player_set_muted(player, 1), AG_OK);
    features.pollOutputLevels();
    // Original analyzer is before the user's volume/mute gain.
    QVERIFY(!features.visualPcmSnapshot().paused);
    QCOMPARE(ag_player_set_muted(player, 0), AG_OK);
    features.pollOutputLevels();
    QVERIFY(!features.visualPcmSnapshot().paused);
    QCOMPARE(ag_player_stop(player), AG_OK);
    features.pollOutputLevels();
    QVERIFY(features.visualPcmSnapshot().paused);
}

void AudioVisualFeatureControllerTest::renderFrameBatchPreservesDelayedPcmWindows()
{
    for (const int sampleRate : {44100, 48000, 96000}) {
        AudioVisualFeatureController controller;
        controller.setActive(true);
        controller.acquireRenderFrameAnalysis();

        ag_visual_pcm_snapshot pcm{};
        pcm.generation = 1;
        pcm.sample_rate = sampleRate;
        pcm.sample_count = 1024;
        for (int block = 0; block < 3; ++block) {
            pcm.first_sample_index = std::uint64_t(block) * pcm.sample_count;
            std::fill_n(pcm.samples, pcm.sample_count, 0.05F + 0.10F * float(block));
            controller.ingestVisualPcm(pcm, true);
        }

        const auto batch = controller.visualPcmBatch();
        const std::uint64_t hop = std::uint64_t(sampleRate) / 60;
        const std::size_t expectedCount = sampleRate == 96000 ? 2 : 3;
        QCOMPARE(batch.count, expectedCount);
        for (std::size_t index = 0; index < batch.count; ++index) {
            QVERIFY(batch.frames[index].valid);
            QCOMPARE(batch.frames[index].sampleRate, sampleRate);
            QCOMPARE(batch.frames[index].firstSampleIndex,
                     std::uint64_t(index) * hop);
            QCOMPARE(batch.frames[index].sequence, std::uint64_t(index + 1));
        }
        QCOMPARE(batch.frames[0].pcm.front(), 0.05F);
        if (sampleRate == 96000) {
            QCOMPARE(batch.frames[1].pcm.front(), 0.15F);
            QCOMPARE(batch.frames[1].pcm.back(), 0.25F);
        } else {
            QCOMPARE(batch.frames[1].pcm.front(), 0.05F);
            QCOMPARE(batch.frames[1].pcm.back(), 0.15F);
            QCOMPARE(batch.frames[2].pcm.back(), 0.25F);
        }
    }
}

void AudioVisualFeatureControllerTest::visualPcmAssemblesAndResets()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    QSignalSpy ready(&features, &AudioVisualFeatureController::visualSpectrumReady);
    ag_visual_pcm_snapshot pcm{};
    pcm.sample_rate = 48000;
    pcm.generation = 1;
    pcm.sample_count = 512;
    std::fill(std::begin(pcm.samples), std::end(pcm.samples), 0.1f);
    features.ingestVisualPcm(pcm);
    QCOMPARE(features.visualSpectrumUpdateCount(), 0);
    ag_visual_pcm_snapshot empty{};
    empty.generation = pcm.generation;
    features.ingestVisualPcm(empty);
    QCOMPARE(features.visualPcmSize_, std::size_t(512));
    pcm.first_sample_index = 512;
    features.ingestVisualPcm(pcm);
    QCOMPARE(features.visualSpectrumUpdateCount(), 1);
    QCOMPARE(features.visualPcmSize_, std::size_t(1024));
    QCOMPARE(ready.count(), 1);
    agplayer::VisualSpectrumAnalyzer reference;
    agplayer::VisualSpectrumAnalyzer::Window window;
    window.fill(0.1f);
    QVERIFY(features.visualSpectrum() == reference.process(window));
    features.ingestVisualPcm(empty);
    QCOMPARE(features.visualPcmSize_, std::size_t(1024));
    pcm.first_sample_index = 1024;
    pcm.sample_count = 256;
    std::fill(std::begin(pcm.samples), std::end(pcm.samples), 0.2f);
    features.ingestVisualPcm(pcm);
    QCOMPARE(features.visualSpectrumUpdateCount(), 2);
    std::fill(window.end() - 256, window.end(), 0.2f);
    QVERIFY(features.visualSpectrum() == reference.process(window));
    pcm.sample_count = 0;
    features.ingestVisualPcm(pcm);
    QCOMPARE(features.visualSpectrumUpdateCount(), 2);
    QCOMPARE(ready.count(), 2);
    pcm.sample_count = 1024;
    pcm.first_sample_index = 4000;
    std::fill(std::begin(pcm.samples), std::end(pcm.samples), 0.0f);
    features.ingestVisualPcm(pcm);
    QCOMPARE(features.visualPcmSize_, std::size_t(1024));
    QVERIFY(std::all_of(features.visualSpectrum().begin(), features.visualSpectrum().end(),
                        [](auto v) { return v == 0; }));
    pcm.sample_count = 200;
    pcm.first_sample_index = 5024;
    features.ingestVisualPcm(pcm);
    ++pcm.generation;
    pcm.first_sample_index = 0;
    features.ingestVisualPcm(pcm);
    QCOMPARE(features.visualPcmSize_, std::size_t(200));
    pcm.sample_rate = 44100;
    pcm.first_sample_index = 200;
    features.ingestVisualPcm(pcm);
    QCOMPARE(features.visualPcmSize_, std::size_t(200));
    empty.generation = pcm.generation + 1;
    features.ingestVisualPcm(empty);
    QCOMPARE(features.visualPcmSize_, std::size_t(0));
    QVERIFY(std::all_of(features.visualSpectrum().begin(), features.visualSpectrum().end(),
                        [](auto v) { return v == 0; }));
    features.setActive(false);
    QCOMPARE(features.visualPcmSize_, std::size_t(0));
    const auto count = features.visualSpectrumUpdateCount();
    features.ingestVisualPcm(pcm);
    QCOMPARE(features.visualSpectrumUpdateCount(), count);
}

namespace {
QVariantList spectrum(double value, bool lowOnly = false)
{
    QVariantList values;
    values.reserve(128);
    for (int index = 0; index < 128; ++index) {
        values.append(!lowOnly || index < 32 ? value : 0.0);
    }
    return values;
}
}

void AudioVisualFeatureControllerTest::reliableBpmEmitsOnceEveryEightBeats()
{
    AudioVisualFeatureController features;
    QSignalSpy impactSpy(&features, &AudioVisualFeatureController::featuresChanged);
    QVERIFY(impactSpy.isValid());
    features.setActive(true);
    features.setWaveformTiming(QStringLiteral("track-a"), 120.0, 120000,
                               QVariantList{0.2, 0.6, 0.4});
    QVERIFY(features.beatReliable());
    features.processSpectrum(spectrum(0.3));
    impactSpy.clear();

    features.processPlaybackPosition(0);
    features.processPlaybackPosition(3999);
    QCOMPARE(features.impactRevision(), 0);
    features.processPlaybackPosition(4000);
    QCOMPARE(features.impactRevision(), 1);
    features.processPlaybackPosition(4017);
    QCOMPARE(features.impactRevision(), 1);
    features.processPlaybackPosition(7999);
    features.processPlaybackPosition(8000);
    QCOMPARE(features.impactRevision(), 2);
    QCOMPARE(impactSpy.count(), 2);
    QVERIFY(features.impactStrength() > 0.0);
}

void AudioVisualFeatureControllerTest::reliableBpmEmitsRegularBeatPulseAndEightBeatImpact()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    features.setWaveformTiming(QStringLiteral("track-beat"), 120.0, 120000,
                               QVariantList{0.2, 0.6, 0.4});
    features.processSpectrum(spectrum(0.3));

    features.processPlaybackPosition(0);
    features.processPlaybackPosition(499);
    QCOMPARE(features.beatRevision(), 0);
    QCOMPARE(features.impactRevision(), 0);

    features.processPlaybackPosition(500);
    QCOMPARE(features.beatRevision(), 1);
    QCOMPARE(features.impactRevision(), 0);
    QVERIFY(features.beatStrength() >= 0.35);
    QVERIFY(features.beatStrength() < 0.8);

    for (qint64 position = 1000; position <= 4000; position += 500) {
        features.processPlaybackPosition(position);
    }
    QCOMPARE(features.beatRevision(), 8);
    QCOMPARE(features.impactRevision(), 1);
}

void AudioVisualFeatureControllerTest::reliableBpmUsesTransientPhaseAndDeduplicatesGridBeat()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    features.setWaveformTiming(QStringLiteral("track-phase"), 120.0, 120000,
                               QVariantList{0.2, 0.6, 0.4});
    features.processPlaybackPosition(0);
    features.processSpectrum(spectrum(0.0));

    features.processPlaybackPosition(450);
    features.processSpectrum(spectrum(0.30, true));
    QCOMPARE(features.beatRevision(), 1);
    features.processPlaybackPosition(500);
    QCOMPARE(features.beatRevision(), 1);

    features.processSpectrum(spectrum(0.0));
    features.processPlaybackPosition(950);
    features.processSpectrum(spectrum(0.30, true));
    QCOMPARE(features.beatRevision(), 2);
    features.processPlaybackPosition(1000);
    QCOMPARE(features.beatRevision(), 2);

    features.processSpectrum(spectrum(0.0));
    features.processPlaybackPosition(1500);
    QCOMPARE(features.beatRevision(), 3);
}

void AudioVisualFeatureControllerTest::reliableBpmLocksFutureGridToTheObservedTransientPhase()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    features.setWaveformTiming(QStringLiteral("track-phase-lock"), 120.0,
                               120000, QVariantList{0.2, 0.6, 0.4});
    features.processPlaybackPosition(0);
    features.processSpectrum(spectrum(0.0));

    features.processPlaybackPosition(300);
    features.processSpectrum(spectrum(0.30, true));
    QCOMPARE(features.beatRevision(), 1);

    features.processPlaybackPosition(500);
    features.processPlaybackPosition(799);
    QCOMPARE(features.beatRevision(), 1);
    features.processPlaybackPosition(800);
    QCOMPARE(features.beatRevision(), 2);
}

void AudioVisualFeatureControllerTest::rhythmSensitivityRecoversModerateRepeatedDrumOnsets()
{
    AudioVisualFeatureController conservative;
    conservative.setVisualKickSensitivity(0);
    conservative.setActive(true);
    conservative.processSpectrum(spectrum(0.0));
    conservative.processSpectrum(spectrum(0.08, true));
    QVERIFY(!conservative.kickPulse());

    AudioVisualFeatureController sensitive;
    sensitive.setVisualKickSensitivity(100);
    sensitive.setActive(true);
    sensitive.processSpectrum(spectrum(0.0));
    sensitive.processSpectrum(spectrum(0.08, true));
    QVERIFY(sensitive.kickPulse());
}

void AudioVisualFeatureControllerTest::seeksAndTrackChangesDoNotEmitDuplicateImpacts()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    features.setWaveformTiming(QStringLiteral("track-a"), 120.0, 120000,
                               QVariantList{0.2, 0.6, 0.4});
    features.processPlaybackPosition(0);
    features.processPlaybackPosition(3999);
    features.processPlaybackPosition(4000);
    QCOMPARE(features.impactRevision(), 1);

    features.processPlaybackPosition(20000);
    QCOMPARE(features.impactRevision(), 1);
    features.processPlaybackPosition(4000);
    QCOMPARE(features.impactRevision(), 1);

    features.setWaveformTiming(QStringLiteral("track-b"), 120.0, 120000,
                               QVariantList{0.4, 0.3});
    features.processPlaybackPosition(4000);
    QCOMPARE(features.impactRevision(), 1);

    features.setWaveformTiming(QStringLiteral("track-c"), 120.0, 120000,
                               QVariantList{0.4, 0.3});
    features.processPlaybackPosition(3200);
    features.processPlaybackPosition(4000);
    QCOMPARE(features.impactRevision(), 1);
}

void AudioVisualFeatureControllerTest::unreliableTimingUsesDebouncedTransientFallbackOnly()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    features.setWaveformTiming(QStringLiteral("track-a"), 120.0, 120000, {});
    QVERIFY(!features.beatReliable());

    features.processSpectrum(spectrum(0.0));
    features.processSpectrum(spectrum(0.25, true));
    QCOMPARE(features.beatRevision(), 1);
    QCOMPARE(features.impactRevision(), 0);

    features.processSpectrum(spectrum(0.0));
    features.processSpectrum(spectrum(0.25, true));
    QCOMPARE(features.beatRevision(), 1);
    QCOMPARE(features.impactRevision(), 0);
    QTest::qWait(190);
    features.processSpectrum(spectrum(0.0));
    features.processSpectrum(spectrum(0.25, true));
    QCOMPARE(features.beatRevision(), 2);
    QCOMPARE(features.impactRevision(), 0);

    features.setWaveformTiming(QStringLiteral("track-a"), 120.0, 120000,
                               QVariantList{0.2, 0.6, 0.4});
    features.processSpectrum(spectrum(0.0));
    QTest::qWait(190);
    features.processSpectrum(spectrum(0.25, true));
    QCOMPARE(features.beatRevision(), 2);
    QCOMPARE(features.impactRevision(), 0);
}

void AudioVisualFeatureControllerTest::outputLevelsHaveFastAttackAndVisibleDecay()
{
    AudioVisualFeatureController features;
    QVERIFY(features.metaObject()->indexOfProperty("leftPeak") >= 0);
    QVERIFY(features.metaObject()->indexOfProperty("rightPeak") >= 0);
    QVERIFY(features.metaObject()->indexOfProperty("leftRms") >= 0);
    QVERIFY(features.metaObject()->indexOfProperty("rightRms") >= 0);
    QSignalSpy levelsSpy(
        &features, &AudioVisualFeatureController::outputLevelsChanged);
    QVERIFY(levelsSpy.isValid());
    features.setActive(true);

    features.applyOutputLevels(0.80, 0.40, 0.50, 0.25);
    QCOMPARE(features.leftPeak(), 0.80);
    QCOMPARE(features.rightPeak(), 0.40);
    QCOMPARE(features.leftRms(), 0.50);
    QCOMPARE(features.rightRms(), 0.25);

    features.applyOutputLevels(0.0, 0.0, 0.0, 0.0);
    QVERIFY(features.leftPeak() > 0.0);
    QVERIFY(features.leftPeak() < 0.80);
    QVERIFY(features.rightRms() > 0.0);
    QVERIFY(features.rightRms() < 0.25);

    for (int tick = 0; tick < 100; ++tick) {
        features.applyOutputLevels(0.0, 0.0, 0.0, 0.0);
    }
    QCOMPARE(features.leftPeak(), 0.0);
    QCOMPARE(features.rightPeak(), 0.0);
    QCOMPARE(features.leftRms(), 0.0);
    QCOMPARE(features.rightRms(), 0.0);

    features.applyOutputLevels(
        std::numeric_limits<double>::quiet_NaN(), 2.0, -1.0, 0.4);
    QCOMPARE(features.leftPeak(), 0.0);
    QCOMPARE(features.rightPeak(), 1.0);
    QCOMPARE(features.leftRms(), 0.0);
    QCOMPARE(features.rightRms(), 0.4);
    QVERIFY(levelsSpy.count() > 0);

    features.setActive(false);
    QCOMPARE(features.leftPeak(), 0.0);
    QCOMPARE(features.rightPeak(), 0.0);
    QCOMPARE(features.leftRms(), 0.0);
    QCOMPARE(features.rightRms(), 0.0);

    PlaybackController playback;
    AudioVisualFeatureController detached(&playback);
    detached.setActive(true);
    detached.applyOutputLevels(0.6, 0.5, 0.4, 0.3);
    detached.setPlaybackController(nullptr);
    QCOMPARE(detached.leftPeak(), 0.0);
    QCOMPARE(detached.rightPeak(), 0.0);

    PlaybackController replacement;
    detached.setPlaybackController(&playback);
    detached.applyOutputLevels(0.7, 0.6, 0.5, 0.4);
    detached.setPlaybackController(&replacement);
    QCOMPARE(detached.leftPeak(), 0.0);
    QCOMPARE(detached.rightPeak(), 0.0);

    auto ownedPlayback = std::make_unique<PlaybackController>();
    AudioVisualFeatureController destroyed(ownedPlayback.get());
    destroyed.setActive(true);
    destroyed.applyOutputLevels(0.7, 0.6, 0.5, 0.4);
    QVERIFY(destroyed.outputLevelTimer_.isActive());
    ownedPlayback.reset();
    QVERIFY(destroyed.playback_.isNull());
    QVERIFY(!destroyed.outputLevelTimer_.isActive());
    QCOMPARE(destroyed.leftPeak(), 0.0);
    QCOMPARE(destroyed.rightPeak(), 0.0);
}

void AudioVisualFeatureControllerTest::outputLevelsPollCoreAtPlaybackCadence()
{
    const QByteArray fixture = qgetenv("AGPLAYER_TEST_AUDIO");
    QVERIFY2(!fixture.isEmpty(), "AGPLAYER_TEST_AUDIO is required");
    ag_player* player = nullptr;
    const ag_player_config config{AG_AUDIO_BACKEND_NULL, 4'096U};
    QCOMPARE(ag_player_create_with_config(&config, &player), AG_OK);
    QVERIFY(player != nullptr);
    {
        PlaybackController playback(player);
        AudioVisualFeatureController features(&playback);
        features.setActive(true);
        QCOMPARE(ag_player_load(player, fixture.constData()), AG_OK);
        QCOMPARE(ag_player_play(player), AG_OK);
        QTRY_VERIFY_WITH_TIMEOUT(features.leftPeak() > 0.0, 3'000);
        QTRY_VERIFY_WITH_TIMEOUT(features.rightRms() > 0.0, 3'000);
        QTRY_VERIFY_WITH_TIMEOUT(features.visualSpectrumUpdateCount() > 0, 3'000);
        QVERIFY(std::any_of(features.visualSpectrum().begin(), features.visualSpectrum().end(),
                            [](auto v) { return v > 0; }));

        QCOMPARE(ag_player_set_muted(player, 1), AG_OK);
        const double beforeDecay = features.leftPeak();
        QTRY_VERIFY_WITH_TIMEOUT(features.leftPeak() < beforeDecay, 500);
        QTRY_COMPARE_WITH_TIMEOUT(features.leftPeak(), 0.0, 2'000);
        QTRY_COMPARE_WITH_TIMEOUT(features.rightPeak(), 0.0, 2'000);
    }
    ag_player_destroy(player);
}

void AudioVisualFeatureControllerTest::adaptiveTransientFloorRejectsRepeatedBackgroundPulses()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    features.setWaveformTiming(QStringLiteral("track-noisy"), 0.0, 120000, {});
    QVERIFY(!features.beatReliable());

    for (int cycle = 0; cycle < 3; ++cycle) {
        features.processSpectrum(spectrum(0.10, true));
        features.processSpectrum(spectrum(0.18, true));
        QTest::qWait(190);
    }
    QVERIFY2(features.beatRevision() <= 1,
             "A repeating low-level noise floor must not be classified as a new beat");

    const quint64 beforeAccent = features.beatRevision();
    features.processSpectrum(spectrum(0.10, true));
    features.processSpectrum(spectrum(0.82, true));
    QCOMPARE(features.beatRevision(), beforeAccent + 1);
    QCOMPARE(features.impactRevision(), 0);
}

void AudioVisualFeatureControllerTest::perceptualBandsKeepNarrowBassEnergyLocalized()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    features.processSpectrum(spectrum(0.0));

    QVariantList narrowBass(128, 0.0);
    for (int index = 0; index < 3; ++index) narrowBass[index] = 1.0;
    features.processSpectrum(narrowBass);

    const QVariantList bands = features.bands();
    QCOMPARE(bands.size(), 8);
    QVERIFY2(bands.at(0).toDouble() > 0.70,
             "A narrow bass transient must retain useful visual energy");
    for (int band = 1; band < bands.size(); ++band) {
        QVERIFY2(bands.at(band).toDouble() < 0.05,
                 "A narrow bass transient leaked into another visual band");
    }
    QVERIFY(features.energy() > 0.12);
}

void AudioVisualFeatureControllerTest::narrowBassTransientsTriggerKickWithoutMidrangeLeakage()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    features.processSpectrum(spectrum(0.0));

    QVariantList bass(128, 0.0);
    for (int index = 0; index < 3; ++index) bass[index] = 0.30;
    features.processSpectrum(bass);
    QVERIFY2(features.kickPulse(),
             "A localized audible bass onset must not be diluted by silent midrange bins");
    QCOMPARE(features.beatRevision(), 1);
    QCOMPARE(features.impactRevision(), 0);

    // A sustained bass bed plus a new midrange note is not another kick.
    QVariantList bassAndMid = bass;
    for (int index = 13; index < 22; ++index) bassAndMid[index] = 0.8;
    features.processSpectrum(bassAndMid);
    QVERIFY2(!features.kickPulse(),
             "Midrange flux must not trigger a kick over sustained bass");
}

void AudioVisualFeatureControllerTest::perceptualBandsUseFastAttackAndProgressiveRelease()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    features.processSpectrum(spectrum(0.0));

    QVariantList midPulse(128, 0.0);
    for (int index = 13; index < 22; ++index) midPulse[index] = 1.0;
    features.processSpectrum(midPulse);
    const double attack = features.bands().at(3).toDouble();
    QVERIFY2(attack > 0.70, "Visual bands must rise within one spectrum frame");

    features.processSpectrum(spectrum(0.0));
    const double release = features.bands().at(3).toDouble();
    QVERIFY2(release > 0.25 && release < attack,
             "Visual bands must decay instead of snapping to zero");
}

void AudioVisualFeatureControllerTest::perceptualBandsPreserveSparseMidAndHighTones()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    features.processSpectrum(spectrum(0.0));

    QVariantList sparseMid(128, 0.0);
    sparseMid[14] = 1.0;
    features.processSpectrum(sparseMid);
    QVERIFY2(features.bands().at(3).toDouble() > 0.20,
             "A single active mid bin must remain visible after band reduction");

    features.processSpectrum(spectrum(0.0));
    QVariantList sparseHigh(128, 0.0);
    sparseHigh[40] = 1.0;
    features.processSpectrum(sparseHigh);
    QVERIFY2(features.bands().at(5).toDouble() > 0.15,
             "A single active high bin must remain visible after band reduction");
}

void AudioVisualFeatureControllerTest::perceptualBandsReleaseWithinEightFrames()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    features.processSpectrum(spectrum(0.0));

    QVariantList midPulse(128, 0.0);
    for (int index = 13; index < 22; ++index) midPulse[index] = 1.0;
    features.processSpectrum(midPulse);
    const double attack = features.bands().at(3).toDouble();
    QVERIFY(attack > 0.70);

    for (int frame = 0; frame < 8; ++frame) {
        features.processSpectrum(spectrum(0.0));
    }
    QVERIFY2(features.bands().at(3).toDouble() < 0.10,
             "A stopped tone must settle within roughly eight spectrum frames");
}

void AudioVisualFeatureControllerTest::silentWaveformDoesNotEnableSyntheticBeatGrid()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    features.setWaveformTiming(QStringLiteral("silent-track"), 120.0, 120000,
                               QVariantList{0.0, 0.0, 0.0});

    QVERIFY(!features.beatReliable());
    features.processPlaybackPosition(0);
    features.processPlaybackPosition(4000);
    QCOMPARE(features.beatRevision(), 0);
    QCOMPARE(features.impactRevision(), 0);
}

void AudioVisualFeatureControllerTest::reliableBeatGridKeepsCountingWithoutLightingSilentPassages()
{
    AudioVisualFeatureController features;
    features.setActive(true);
    features.setWaveformTiming(QStringLiteral("track-with-silence"), 120.0, 120000,
                               QVariantList{0.2, 0.6, 0.4});
    features.processSpectrum(spectrum(0.3));
    features.processPlaybackPosition(0);
    features.processPlaybackPosition(500);
    QVERIFY(features.beatStrength() > 0.0);

    // The envelope still has a release tail, but the current audio is silent.
    features.processSpectrum(spectrum(0.0));
    QVERIFY(features.energy() > 0.0);
    for (qint64 position = 1000; position <= 4000; position += 500)
        features.processPlaybackPosition(position);
    QCOMPARE(features.beatRevision(), 8);
    QCOMPARE(features.impactRevision(), 1);
    QCOMPARE(features.beatStrength(), 0.0);
    QCOMPARE(features.impactStrength(), 0.0);

    features.processSpectrum(spectrum(0.3));
    for (qint64 position = 4500; position <= 8000; position += 500)
        features.processPlaybackPosition(position);
    QCOMPARE(features.beatRevision(), 16);
    QCOMPARE(features.impactRevision(), 2);
    QVERIFY(features.beatStrength() > 0.0);
    QVERIFY(features.impactStrength() > 0.0);
}

void AudioVisualFeatureControllerTest::fadeOutPublicationResidueDoesNotLightBeatGrid_data()
{
    QTest::addColumn<double>("residue");
    QTest::newRow("below-publication-threshold") << double(0.0015F);
    QTest::newRow("at-publication-threshold") << double(0.002F);
}

void AudioVisualFeatureControllerTest::fadeOutPublicationResidueDoesNotLightBeatGrid()
{
    QFETCH(double, residue);
    AudioVisualFeatureController features;
    features.setActive(true);
    features.setWaveformTiming(QStringLiteral("fading-track"), 120.0, 120000,
                               QVariantList{0.2, 0.6, 0.4});
    features.processSpectrum(spectrum(0.3));
    features.processPlaybackPosition(0);
    features.processPlaybackPosition(500);
    QVERIFY(features.beatStrength() > 0.0);

    // This is the last published fade-out sample. pollSpectrum does not
    // publish the subsequent zero bins because their delta is <= 0.002F.
    features.processSpectrum(spectrum(residue));
    QVERIFY(features.energy() > residue);
    for (qint64 position = 1000; position <= 8000; position += 500) {
        features.processPlaybackPosition(position);
        QCOMPARE(features.beatStrength(), 0.0);
        QCOMPARE(features.impactStrength(), 0.0);
    }
    QCOMPARE(features.beatRevision(), 16);
    QCOMPARE(features.impactRevision(), 2);

    // A returning signal whose delta clears the publication threshold restores
    // visual pulses without resetting the eight-beat count.
    features.processSpectrum(spectrum(0.0041F));
    for (qint64 position = 8500; position <= 12000; position += 500)
        features.processPlaybackPosition(position);
    QCOMPARE(features.beatRevision(), 24);
    QCOMPARE(features.impactRevision(), 3);
    QVERIFY(features.beatStrength() > 0.0);
    QVERIFY(features.impactStrength() > 0.0);
}

QTEST_GUILESS_MAIN(AudioVisualFeatureControllerTest)

#include "audio_visual_feature_controller_test.moc"
