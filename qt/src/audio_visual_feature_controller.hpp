#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <array>
#include "visual_spectrum_analyzer.hpp"
#include "visual_spectrum_features.hpp"
#include "visual_kick_response.hpp"
#include "visual_audio_frame_analyzer.hpp"

struct ag_visual_pcm_snapshot;

class PlaybackController;
class AudioVisualFeatureControllerTest;

class AudioVisualFeatureController final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(int visualKickSensitivity READ visualKickSensitivity WRITE setVisualKickSensitivity NOTIFY visualKickSensitivityChanged)
    Q_PROPERTY(quint64 visualSpectrumUpdateCount READ visualSpectrumUpdateCount NOTIFY visualSpectrumReady)
    Q_PROPERTY(QVariantList bands READ bands NOTIFY featuresChanged)
    Q_PROPERTY(double energy READ energy NOTIFY featuresChanged)
    Q_PROPERTY(double spectralFlux READ spectralFlux NOTIFY featuresChanged)
    Q_PROPERTY(bool kickPulse READ kickPulse NOTIFY featuresChanged)
    Q_PROPERTY(bool snarePulse READ snarePulse NOTIFY featuresChanged)
    Q_PROPERTY(quint64 beatRevision READ beatRevision NOTIFY featuresChanged)
    Q_PROPERTY(double beatStrength READ beatStrength NOTIFY featuresChanged)
    Q_PROPERTY(quint64 impactRevision READ impactRevision NOTIFY featuresChanged)
    Q_PROPERTY(double impactStrength READ impactStrength NOTIFY featuresChanged)
    Q_PROPERTY(bool beatReliable READ beatReliable NOTIFY beatReliableChanged)
    Q_PROPERTY(quint64 derivedUpdateCount READ derivedUpdateCount
                   NOTIFY derivedUpdateCountChanged)
    Q_PROPERTY(double leftPeak READ leftPeak NOTIFY outputLevelsChanged)
    Q_PROPERTY(double rightPeak READ rightPeak NOTIFY outputLevelsChanged)
    Q_PROPERTY(double leftRms READ leftRms NOTIFY outputLevelsChanged)
    Q_PROPERTY(double rightRms READ rightRms NOTIFY outputLevelsChanged)

public:
    explicit AudioVisualFeatureController(PlaybackController* playback = nullptr,
                                          QObject* parent = nullptr);
    ~AudioVisualFeatureController() override;
    const agplayer::VisualSpectrumAnalyzer::Spectrum& visualSpectrum() const noexcept { return visualSpectrum_; }
    quint64 visualSpectrumUpdateCount() const noexcept { return visualSpectrumUpdateCount_; }
    const agplayer::VisualSpectrumFeatures::Features& visualFeatures() const noexcept { return visualFeatures_; }
    const agplayer::visual::KickResponse::Output& visualKick() const noexcept { return visualKick_; }
    int visualKickSensitivity() const noexcept { return visualKickSensitivity_; }
    void setVisualKickSensitivity(int sensitivity);
    agplayer::VisualAudioFrameAnalyzer::Snapshot visualPcmSnapshot() const noexcept;
    agplayer::VisualAudioFrameAnalyzer::Batch visualPcmBatch() const noexcept;
    void acquireRenderFrameAnalysis();
    void releaseRenderFrameAnalysis();

    bool active() const noexcept;
    QVariantList bands() const;
    double energy() const noexcept;
    double spectralFlux() const noexcept;
    bool kickPulse() const noexcept;
    bool snarePulse() const noexcept;
    quint64 beatRevision() const noexcept;
    double beatStrength() const noexcept;
    quint64 impactRevision() const noexcept;
    double impactStrength() const noexcept;
    bool beatReliable() const noexcept;
    quint64 derivedUpdateCount() const noexcept;
    double leftPeak() const noexcept;
    double rightPeak() const noexcept;
    double leftRms() const noexcept;
    double rightRms() const noexcept;

    void setPlaybackController(PlaybackController* playback);
    Q_INVOKABLE void setActive(bool active);
    Q_INVOKABLE void setWaveformTiming(const QString& trackId, double bpm,
                                       qint64 durationMs,
                                       const QVariantList& mixPeaks);
    Q_INVOKABLE void processPlaybackPosition(qint64 positionMs);
    void processSpectrum(const QVariantList& spectrum);

signals:
    void visualSpectrumReady();
    void visualStateReset();
    void visualKickSensitivityChanged();
    void activeChanged();
    void featuresChanged();
    void beatReliableChanged();
    void derivedUpdateCountChanged();
    void outputLevelsChanged();

private:
    friend class AudioVisualFeatureControllerTest;
    void setVisualPcmEnabled(bool enabled);
    void resetVisualPcm();
    void ingestVisualPcm(const ag_visual_pcm_snapshot& snapshot, bool deferAnalysis = false);
    void analyzeVisualPcm();
    void updateVisualPlaybackState();
    agplayer::VisualSpectrumAnalyzer visualAnalyzer_;
    agplayer::VisualSpectrumFeatures visualFeatureAnalyzer_;
    agplayer::visual::KickResponse visualKickResponse_;
    agplayer::VisualSpectrumFeatures::Features visualFeatures_{};
    agplayer::visual::KickResponse::Output visualKick_{};
    int visualKickSensitivity_ = 50;
    std::size_t visualSamplesSinceUpdate_ = 0;
    // Phase accumulator for the reference analyser's requestAnimationFrame
    // cadence. One input sample contributes 60 units; a frame is captured
    // whenever the accumulator reaches the current sample rate.
    std::uint64_t visualFramePhase_ = 0;
    QElapsedTimer visualAnalysisTimer_;
    unsigned int renderFrameConsumers_ = 0;
    quint64 visualPcmEpoch_ = 0;
    agplayer::VisualSpectrumAnalyzer::Window visualPcm_{};
    std::array<agplayer::VisualAudioFrameAnalyzer::Snapshot,
               agplayer::VisualAudioFrameAnalyzer::BatchCapacity> visualPcmFrames_{};
    std::size_t visualPcmFrameCount_ = 0;
    std::size_t visualPcmFrameWriteIndex_ = 0;
    quint64 visualPcmFrameSequence_ = 0;
    agplayer::VisualSpectrumAnalyzer::Spectrum visualSpectrum_{};
    std::size_t visualPcmSize_ = 0;
    quint64 visualGeneration_ = 0;
    quint64 visualNextIndex_ = 0;
    int visualSampleRate_ = 0;
    bool visualPaused_ = false;
    QElapsedTimer visualReleaseTimer_;
    quint64 visualSpectrumUpdateCount_ = 0;
    void connectPlaybackSignals();
    void disconnectPlaybackSignals();
    void updateOutputLevelPolling();
    void pollOutputLevels();
    void applyOutputLevels(double leftPeak, double rightPeak,
                           double leftRms, double rightRms);
    void resetOutputLevels();
    void resetBeatPosition() noexcept;
    void resetTransientHistory() noexcept;
    void resetBandEnvelopes() noexcept;
    static double adaptiveThreshold(const std::array<double, 24>& history,
                                    int sampleCount,
                                    double minimum) noexcept;
    static void appendTransientSample(std::array<double, 24>& history,
                                      int& sampleCount, int& writeIndex,
                                      double value) noexcept;
    void triggerImpact(double strength, bool notify = true);
    void triggerBeat(double strength, bool notify = true);
    static double normalizedValue(const QVariant& value) noexcept;

    QPointer<PlaybackController> playback_;
    QMetaObject::Connection spectrumConnection_;
    QMetaObject::Connection positionConnection_;
    QMetaObject::Connection trackConnection_;
    QMetaObject::Connection playbackDestroyedConnection_;
    bool active_ = false;
    QVariantList bands_{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    QVariantList previousSpectrum_;
    double energy_ = 0.0;
    double spectralFlux_ = 0.0;
    bool audibleSpectrum_ = false;
    bool kickPulse_ = false;
    bool snarePulse_ = false;
    std::array<double, 8> smoothedBands_{};
    bool bandsInitialized_ = false;
    quint64 beatRevision_ = 0;
    double beatStrength_ = 0.0;
    QString timingTrackId_;
    double bpm_ = 0.0;
    qint64 durationMs_ = 0;
    bool beatReliable_ = false;
    qint64 lastPositionMs_ = -1;
    qint64 lastBeatIndex_ = -1;
    qint64 lastBeatEventPositionMs_ = -1;
    double beatPhaseOffsetMs_ = 0.0;
    bool beatPhaseLocked_ = false;
    qint64 lastImpactGroup_ = -1;
    std::array<double, 24> lowFluxHistory_{};
    std::array<double, 24> highFluxHistory_{};
    int transientSampleCount_ = 0;
    int transientWriteIndex_ = 0;
    int fallbackBeatCount_ = 0;
    QElapsedTimer fallbackDebounce_;
    quint64 impactRevision_ = 0;
    double impactStrength_ = 0.0;
    quint64 derivedUpdateCount_ = 0;
    QTimer outputLevelTimer_;
    double leftPeak_ = 0.0;
    double rightPeak_ = 0.0;
    double leftRms_ = 0.0;
    double rightRms_ = 0.0;
};
