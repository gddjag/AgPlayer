#pragma once

#include <agplayer/c_api.h>

#include <QObject>
#include <QString>
#include <QUrl>

#include <atomic>

// LightEditor: QML singleton for basic audio editing (trim, fade, gain).
// Wraps ag_light_edit. When a file is loaded, duration is read via
// ag_metadata_open so the QML trim sliders can be bounded correctly.
class LightEditor final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString inputFileName READ inputFileName NOTIFY inputFileChanged)
    Q_PROPERTY(bool hasInput READ hasInput NOTIFY inputFileChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY inputFileChanged)

public:
    explicit LightEditor(QObject* parent = nullptr);
    ~LightEditor() override;

    double progress() const noexcept;
    bool busy() const noexcept;
    QString inputFileName() const noexcept;
    bool hasInput() const noexcept;
    qint64 durationMs() const noexcept;

    Q_INVOKABLE void loadFile(const QUrl& url);
    Q_INVOKABLE void start(qint64 trimStartMs, qint64 trimEndMs,
                           int fadeInMs, int fadeOutMs, double gain,
                           const QString& outputDir);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();

signals:
    void progressChanged();
    void busyChanged();
    void inputFileChanged();
    void lightEditCompleted(const QString& outputPath);
    void errorOccurred(const QString& message);

private:
    QString inputPath_;
    QString inputFileName_;
    qint64 durationMs_ = 0;
    std::atomic<bool> busy_{false};
    std::atomic<double> progress_{0.0};
    std::atomic<ag_cancel_token*> token_{nullptr};

    void setBusy(bool value);
    void setProgress(double value);
    QString computeOutputPath(const QString& inputPath,
                              const QString& outputDir) const;
};
