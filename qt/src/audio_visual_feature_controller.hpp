#pragma once

#include <QObject>
#include <QMetaObject>
#include <QPointer>
#include <QVariantList>

class PlaybackController;

class AudioVisualFeatureController final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QVariantList bands READ bands NOTIFY featuresChanged)
    Q_PROPERTY(double energy READ energy NOTIFY featuresChanged)
    Q_PROPERTY(double spectralFlux READ spectralFlux NOTIFY featuresChanged)
    Q_PROPERTY(bool kickPulse READ kickPulse NOTIFY featuresChanged)
    Q_PROPERTY(bool snarePulse READ snarePulse NOTIFY featuresChanged)
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
    quint64 derivedUpdateCount() const noexcept;

    void setPlaybackController(PlaybackController* playback);
    Q_INVOKABLE void setActive(bool active);
    void processSpectrum(const QVariantList& spectrum);

signals:
    void activeChanged();
    void featuresChanged();
    void derivedUpdateCountChanged();

private:
    void connectSpectrum();
    void disconnectSpectrum();
    static double normalizedValue(const QVariant& value) noexcept;

    QPointer<PlaybackController> playback_;
    QMetaObject::Connection spectrumConnection_;
    bool active_ = false;
    QVariantList bands_{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    QVariantList previousSpectrum_;
    double energy_ = 0.0;
    double spectralFlux_ = 0.0;
    bool kickPulse_ = false;
    bool snarePulse_ = false;
    quint64 derivedUpdateCount_ = 0;
};
