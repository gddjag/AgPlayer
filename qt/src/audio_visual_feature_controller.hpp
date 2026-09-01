#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QPointer>
#include <QString>
#include <QVariantList>

#include <array>

class PlaybackController;

class AudioVisualFeatureController final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QVariantList bands READ bands NOTIFY featuresChanged)
    Q_PROPERTY(double energy READ energy NOTIFY featuresChanged)
    Q_PROPERTY(double spectralFlux READ spectralFlux NOTIFY featuresChanged)
    Q_PROPERTY(bool kickPulse READ kickPulse NOTIFY featuresChanged)
    Q_PROPERTY(bool snarePulse READ snarePulse NOTIFY featuresChanged)
    Q_PROPERTY(quint64 impactRevision READ impactRevision NOTIFY featuresChanged)
    Q_PROPERTY(double impactStrength READ impactStrength NOTIFY featuresChanged)
    Q_PROPERTY(bool beatReliable READ beatReliable NOTIFY beatReliableChanged)
    Q_PROPERTY(quint64 derivedUpdateCount READ derivedUpdateCount
                   NOTIFY derivedUpdateCountChanged)

public:
    explicit AudioVisualFeatureController(PlaybackController* playback = nullptr,
                                          QObject* parent = nullptr);

    bool active() const noexcept;
    QVariantList bands() const;
    double energy() const noexcept;
    double spectralFlux() const noexcept;
    bool kickPulse() const noexcept;
    bool snarePulse() const noexcept;
    quint64 impactRevision() const noexcept;
    double impactStrength() const noexcept;
    bool beatReliable() const noexcept;
    quint64 derivedUpdateCount() const noexcept;

    void setPlaybackController(PlaybackController* playback);
    Q_INVOKABLE void setActive(bool active);
    Q_INVOKABLE void setWaveformTiming(const QString& trackId, double bpm,
                                       qint64 durationMs,
                                       const QVariantList& mixPeaks);
    Q_INVOKABLE void processPlaybackPosition(qint64 positionMs);
    void processSpectrum(const QVariantList& spectrum);

signals:
    void activeChanged();
    void featuresChanged();
    void beatReliableChanged();
    void derivedUpdateCountChanged();

private:
    void connectPlaybackSignals();
    void disconnectPlaybackSignals();
    void resetBeatPosition() noexcept;
    void resetTransientHistory() noexcept;
    static double adaptiveThreshold(const std::array<double, 24>& history,
                                    int sampleCount,
                                    double minimum) noexcept;
    static void appendTransientSample(std::array<double, 24>& history,
                                      int& sampleCount, int& writeIndex,
                                      double value) noexcept;
    void triggerImpact(double strength, bool notify = true);
    static double normalizedValue(const QVariant& value) noexcept;

    QPointer<PlaybackController> playback_;
    QMetaObject::Connection spectrumConnection_;
    QMetaObject::Connection positionConnection_;
    QMetaObject::Connection trackConnection_;
    bool active_ = false;
    QVariantList bands_{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    QVariantList previousSpectrum_;
    double energy_ = 0.0;
    double spectralFlux_ = 0.0;
    bool kickPulse_ = false;
    bool snarePulse_ = false;
    QString timingTrackId_;
    double bpm_ = 0.0;
    qint64 durationMs_ = 0;
    bool beatReliable_ = false;
    qint64 lastPositionMs_ = -1;
    qint64 lastImpactGroup_ = -1;
    std::array<double, 24> lowFluxHistory_{};
    std::array<double, 24> highFluxHistory_{};
    int transientSampleCount_ = 0;
    int transientWriteIndex_ = 0;
    QElapsedTimer fallbackDebounce_;
    quint64 impactRevision_ = 0;
    double impactStrength_ = 0.0;
    quint64 derivedUpdateCount_ = 0;
};
