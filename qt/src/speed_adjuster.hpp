#pragma once

#include <agplayer/c_api.h>

#include <QObject>
#include <QString>
#include <QUrl>

#include <atomic>

// SpeedAdjuster: QML singleton for changing audio playback speed while
// preserving pitch. Internally delegates to ag_pitch_shift with pitch_cents=0
// and keep_tempo=true, converting speed_ratio to tempo_ratio=1/speed_ratio.
//   speed_ratio > 1.0: faster playback (shorter duration)
//   speed_ratio < 1.0: slower playback (longer duration)
class SpeedAdjuster final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString inputFileName READ inputFileName NOTIFY inputFileChanged)
    Q_PROPERTY(bool hasInput READ hasInput NOTIFY inputFileChanged)

public:
    explicit SpeedAdjuster(QObject* parent = nullptr);
    ~SpeedAdjuster() override;

    double progress() const noexcept;
    bool busy() const noexcept;
    QString inputFileName() const noexcept;
    bool hasInput() const noexcept;

    Q_INVOKABLE void loadFile(const QUrl& url);
    Q_INVOKABLE void start(double speedRatio, const QString& outputDir);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();

signals:
    void progressChanged();
    void busyChanged();
    void inputFileChanged();
    void speedAdjustCompleted(const QString& outputPath);
    void errorOccurred(const QString& message);

private:
    QString inputPath_;
    QString inputFileName_;
    std::atomic<bool> busy_{false};
    std::atomic<double> progress_{0.0};

    // Cancel token is created in start() (main thread) and used by the
    // background task. cancel() flips the flag on the token pointer.
    std::atomic<ag_cancel_token*> token_{nullptr};

    void setBusy(bool value);
    void setProgress(double value);
    QString computeOutputPath(const QString& inputPath,
                              const QString& outputDir) const;
};
