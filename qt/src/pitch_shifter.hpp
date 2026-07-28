#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include <atomic>

// PitchShifter: QML singleton for pitch shifting audio files.
// Supports pitch shift in cents (-1200..1200), semitone/cents editing,
// keep-tempo mode, tempo ratio, output format/sample-rate selection,
// vocal protection / smooth-transition toggles, and export to file.
// Processing runs in background via QtConcurrent.
class PitchShifter final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString inputFileName READ inputFileName NOTIFY inputFileChanged)
    Q_PROPERTY(QUrl inputUrl READ inputUrl NOTIFY inputFileChanged)
    Q_PROPERTY(bool hasInput READ hasInput NOTIFY inputFileChanged)
    Q_PROPERTY(QString inputFormat READ inputFormat NOTIFY inputFileChanged)
    Q_PROPERTY(int inputSampleRate READ inputSampleRate NOTIFY inputFileChanged)
    Q_PROPERTY(int inputDurationMs READ inputDurationMs NOTIFY inputFileChanged)
    Q_PROPERTY(QVariantList waveformPeaks READ waveformPeaks NOTIFY waveformPeaksChanged)

public:
    explicit PitchShifter(QObject* parent = nullptr);

    double progress() const noexcept;
    bool busy() const noexcept;
    QString inputFileName() const noexcept;
    QUrl inputUrl() const;
    bool hasInput() const noexcept;
    QString inputFormat() const noexcept;
    int inputSampleRate() const noexcept;
    int inputDurationMs() const noexcept;
    QVariantList waveformPeaks() const noexcept;

    Q_INVOKABLE void loadFile(const QUrl& url);
    Q_INVOKABLE void start(int pitchCents,
                           bool keepTempo,
                           double tempoRatio,
                           const QString& outputFormat,
                           int outputSampleRate,
                           bool vocalProtection,
                           bool smoothTransition,
                           const QString& outputDir);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();

signals:
    void progressChanged();
    void busyChanged();
    void inputFileChanged();
    void waveformPeaksChanged();
    void pitchShiftCompleted(const QString& outputPath);
    void pitchShiftCompletedWithWarnings(const QString& outputPath,
                                         const QString& warning);
    void errorOccurred(const QString& message);

private:
    QString inputPath_;
    QString inputFileName_;
    QString inputFormat_;
    int inputSampleRate_ = 0;
    int inputDurationMs_ = 0;
    QVariantList waveformPeaks_;
    std::atomic<bool> cancelFlag_{false};
    std::atomic<double> progress_{0.0};
    std::atomic<bool> busy_{false};

    void setBusy(bool value);
    void setProgress(double value);
    QString computeOutputPath(const QString& inputPath,
                              const QString& outputFormat,
                              const QString& outputDir) const;
};
