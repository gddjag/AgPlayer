#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <atomic>

// PitchShifter: QML singleton for pitch shifting audio files.
// Supports pitch shift in cents (-1200..1200), keep-tempo mode, tempo ratio,
// and export to file. Processing runs in background via QtConcurrent.
class PitchShifter final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString inputFileName READ inputFileName NOTIFY inputFileChanged)
    Q_PROPERTY(bool hasInput READ hasInput NOTIFY inputFileChanged)

public:
    explicit PitchShifter(QObject* parent = nullptr);

    double progress() const noexcept;
    bool busy() const noexcept;
    QString inputFileName() const noexcept;
    bool hasInput() const noexcept;

    Q_INVOKABLE void loadFile(const QUrl& url);
    Q_INVOKABLE void start(int pitchCents, bool keepTempo, double tempoRatio,
                           const QString& outputDir);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();

signals:
    void progressChanged();
    void busyChanged();
    void inputFileChanged();
    void pitchShiftCompleted(const QString& outputPath);
    void errorOccurred(const QString& message);

private:
    QString inputPath_;
    QString inputFileName_;
    std::atomic<bool> cancelFlag_{false};
    std::atomic<double> progress_{0.0};
    std::atomic<bool> busy_{false};

    void setBusy(bool value);
    void setProgress(double value);
    QString computeOutputPath(const QString& inputPath,
                              const QString& outputDir) const;
};
