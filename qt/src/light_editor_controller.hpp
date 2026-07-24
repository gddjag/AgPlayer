#pragma once

#include <agplayer/c_api.h>

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include <QMutex>
#include <QPointer>

#include <atomic>
#include <vector>

template <typename T>
class QFutureWatcher;

// LightEditor: QML singleton for basic audio editing (trim, fade, gain).
// Wraps ag_multitrack_edit. When a file is loaded, duration is read via
// ag_metadata_open so the QML trim sliders can be bounded correctly.
class LightEditor final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString inputFileName READ inputFileName NOTIFY inputFileChanged)
    Q_PROPERTY(bool hasInput READ hasInput NOTIFY inputFileChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY inputFileChanged)
    Q_PROPERTY(QString inputFormat READ inputFormat NOTIFY inputFileChanged)
    Q_PROPERTY(int inputSampleRate READ inputSampleRate NOTIFY inputFileChanged)
    Q_PROPERTY(int inputChannels READ inputChannels NOTIFY inputFileChanged)
    Q_PROPERTY(QVariantList waveformPeaks READ waveformPeaks NOTIFY waveformPeaksChanged)

    // Multi-track model
    Q_PROPERTY(int trackCount READ trackCount CONSTANT)
    Q_PROPERTY(int selectedTrack READ selectedTrack WRITE setSelectedTrack NOTIFY selectedTrackChanged)
    Q_PROPERTY(QVariantList trackNames READ trackNames NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList trackHasFiles READ trackHasFiles NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList trackPeaks READ trackPeaks NOTIFY tracksChanged)

public:
    explicit LightEditor(QObject* parent = nullptr);
    ~LightEditor() override;

    double progress() const noexcept;
    bool busy() const noexcept;
    QString inputFileName() const noexcept;
    bool hasInput() const noexcept;
    qint64 durationMs() const noexcept;
    QString inputFormat() const noexcept;
    int inputSampleRate() const noexcept;
    int inputChannels() const noexcept;
    QVariantList waveformPeaks() const noexcept;

    int trackCount() const noexcept;
    int selectedTrack() const noexcept;
    void setSelectedTrack(int value);
    QVariantList trackNames() const noexcept;
    QVariantList trackHasFiles() const noexcept;
    QVariantList trackPeaks() const noexcept;

    Q_INVOKABLE void loadFile(const QUrl& url);
    Q_INVOKABLE void loadFileToTrack(int trackIndex, const QUrl& url);
    Q_INVOKABLE void start(qint64 trimStartMs, qint64 trimEndMs,
                           int fadeInMs, int fadeOutMs, double gain,
                           const QString& outputDir,
                           const QString& outputFormat = QString(),
                           int outputSampleRate = 0,
                           int outputChannels = 0);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void clearTrack(int trackIndex);

signals:
    void progressChanged();
    void busyChanged();
    void inputFileChanged();
    void waveformPeaksChanged();
    void lightEditCompleted(const QString& outputPath);
    void errorOccurred(const QString& message);
    void selectedTrackChanged();
    void tracksChanged();

private:
    struct Track {
        QString path;
        QString name;
        QString format;
        qint64 durationMs = 0;
        int sampleRate = 0;
        int channels = 0;
        QVariantList peaks;
    };

    std::vector<Track> tracks_;
    int selectedTrack_ = 0;
    static constexpr int kTrackCount = 4;

    std::atomic<bool> busy_{false};
    std::atomic<double> progress_{0.0};
    std::atomic<ag_cancel_token*> token_{nullptr};
    QMutex tokenMutex_;
    QPointer<QFutureWatcher<int>> watcher_;

    const Track& currentTrack() const;
    Track& currentTrack();
    bool isValidTrackIndex(int index) const noexcept;
    void loadPathIntoTrack(const QString& path, int trackIndex);

    void setBusy(bool value);
    void setProgress(double value);
    QString computeOutputPath(const QString& firstInputPath,
                              const QString& outputDir,
                              const QString& outputFormat,
                              int loadedCount) const;
};
