#pragma once

#include <agplayer/c_api.h>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include <atomic>

template <typename T>
class QFutureWatcher;

// SpeedAdjuster: QML singleton for changing audio playback speed while
// preserving pitch. Internally delegates to ag_pitch_shift with pitch_cents=0
// and keep_tempo=true, converting speed_ratio to tempo_ratio=1/speed_ratio.
//   speed_ratio > 1.0: faster playback (shorter duration)
//   speed_ratio < 1.0: slower playback (longer duration)
//
// BPM mode exposes a detected BPM, target BPM and derived speed percentage;
// the legacy speed-ratio mode is kept for compatibility.
class SpeedAdjuster final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString inputFileName READ inputFileName NOTIFY inputFileChanged)
    Q_PROPERTY(QUrl inputUrl READ inputUrl NOTIFY inputFileChanged)
    Q_PROPERTY(bool hasInput READ hasInput NOTIFY inputFileChanged)
    Q_PROPERTY(int inputDurationMs READ inputDurationMs NOTIFY inputFileChanged)
    Q_PROPERTY(QVariantList waveformPeaks READ waveformPeaks NOTIFY waveformPeaksChanged)

    Q_PROPERTY(double detectedBpm READ detectedBpm NOTIFY bpmChanged)
    Q_PROPERTY(double bpmConfidence READ bpmConfidence NOTIFY bpmChanged)
    Q_PROPERTY(double targetBpm READ targetBpm WRITE setTargetBpm NOTIFY targetBpmChanged)
    Q_PROPERTY(double speedPercentage READ speedPercentage NOTIFY targetBpmChanged)
    Q_PROPERTY(bool keepPitch READ keepPitch WRITE setKeepPitch NOTIFY keepPitchChanged)
    Q_PROPERTY(bool beatAlign READ beatAlign WRITE setBeatAlign NOTIFY beatAlignChanged)
    Q_PROPERTY(QVariantList markers READ markers NOTIFY markersChanged)

public:
    explicit SpeedAdjuster(QObject* parent = nullptr);
    ~SpeedAdjuster() override;

    double progress() const noexcept;
    bool busy() const noexcept;
    QString inputFileName() const noexcept;
    QUrl inputUrl() const;
    bool hasInput() const noexcept;
    int inputDurationMs() const noexcept;
    QVariantList waveformPeaks() const noexcept;

    double detectedBpm() const noexcept;
    double bpmConfidence() const noexcept;
    double targetBpm() const noexcept;
    void setTargetBpm(double value);
    double speedPercentage() const noexcept;
    bool keepPitch() const noexcept;
    void setKeepPitch(bool value);
    bool beatAlign() const noexcept;
    void setBeatAlign(bool value);
    QVariantList markers() const noexcept;

    Q_INVOKABLE void loadFile(const QUrl& url);
    Q_INVOKABLE void start(double speedRatio, const QString& outputDir);
    Q_INVOKABLE void startBpmAdjust(double targetBpm,
                                    bool keepPitch,
                                    const QString& outputFormat,
                                    const QString& outputDir);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();

    Q_INVOKABLE void analyzeBpm();
    Q_INVOKABLE void halfBeat();
    Q_INVOKABLE void doubleBeat();
    Q_INVOKABLE void addMarker(double timeMs, const QString& label);
    Q_INVOKABLE void removeMarker(int index);
    Q_INVOKABLE void clearMarkers();

signals:
    void progressChanged();
    void busyChanged();
    void inputFileChanged();
    void waveformPeaksChanged();
    void bpmChanged();
    void targetBpmChanged();
    void keepPitchChanged();
    void beatAlignChanged();
    void markersChanged();
    void speedAdjustCompleted(const QString& outputPath);
    void warningOccurred(const QString& message);
    void errorOccurred(const QString& message);

private:
    QString inputPath_;
    QString inputFileName_;
    int inputDurationMs_ = 0;
    QVariantList waveformPeaks_;
    std::atomic<bool> busy_{false};
    std::atomic<double> progress_{0.0};

    double detectedBpm_ = 0.0;
    double bpmConfidence_ = 0.0;
    double targetBpm_ = 120.0;
    bool keepPitch_ = true;
    bool beatAlign_ = false;
    QVariantList markers_;

    // Cancel token is created in start() (main thread) and used by the
    // background task. cancel() flips the flag on the token pointer.
    std::atomic<ag_cancel_token*> token_{nullptr};
    QPointer<QFutureWatcher<int>> watcher_;

    void setBusy(bool value);
    void setProgress(double value);
    void refreshWaveform();
    QString computeOutputPath(const QString& inputPath,
                              const QString& outputFormat,
                              const QString& outputDir,
                              const QString& suffix) const;
    double targetBpmToSpeedRatio(double targetBpm) const noexcept;
};
