#pragma once

#include <agplayer/c_api.h>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

#include <memory>

template <typename T>
class QFutureWatcher;
class QTemporaryDir;
class PlaybackController;
class AudioPreviewController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasSource READ hasSource NOTIFY sourceChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY stateChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY stateChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY sourceChanged)
    Q_PROPERTY(bool processing READ processing NOTIFY processingChanged)
    Q_PROPERTY(double speedRatio READ speedRatio NOTIFY dspParametersChanged)
    Q_PROPERTY(int pitchCents READ pitchCents NOTIFY dspParametersChanged)

public:
    explicit AudioPreviewController(
        ag_audio_backend backend = AG_AUDIO_BACKEND_DEFAULT,
        PlaybackController* mainPlayback = nullptr,
        QObject* parent = nullptr);
    ~AudioPreviewController() override;

    bool hasSource() const noexcept;
    bool playing() const noexcept;
    qint64 positionMs() const noexcept;
    qint64 durationMs() const noexcept;
    double volume() const noexcept;
    QString sourcePath() const;
    bool processing() const noexcept;
    double speedRatio() const noexcept;
    int pitchCents() const noexcept;

    Q_INVOKABLE void toggle(const QUrl& source);
    Q_INVOKABLE void play(const QUrl& source);
    Q_INVOKABLE void resume();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(qint64 positionMs);
    Q_INVOKABLE bool isCurrentSource(const QUrl& source) const;
    Q_INVOKABLE void setDspParameters(
        double speedRatio,
        int pitchCents,
        bool keepPitch,
        bool keepDuration,
        bool vocalProtection,
        bool smoothTransition);
    void setVolume(double value);

signals:
    void sourceChanged();
    void stateChanged();
    void volumeChanged();
    void processingChanged();
    void dspParametersChanged();
    void errorOccurred(const QString& message);

private:
    friend class AudioPreviewControllerTest;

    ag_player* player_ = nullptr;
    ag_audio_backend backend_ = AG_AUDIO_BACKEND_DEFAULT;
    PlaybackController* mainPlayback_ = nullptr;
    QTimer pollTimer_;
    QString sourcePath_;
    QString playbackPath_;
    bool playing_ = false;
    qint64 positionMs_ = 0;
    qint64 durationMs_ = 0;
    double volume_ = 0.8;
    bool processing_ = false;
    double speedRatio_ = 1.0;
    int pitchCents_ = 0;
    bool keepPitch_ = true;
    bool keepDuration_ = true;
    bool vocalProtection_ = false;
    bool smoothTransition_ = false;
    int dspRevision_ = 0;
    bool dspDirty_ = false;
    QPointer<QFutureWatcher<int>> dspWatcher_;
    ag_cancel_token* dspCancelToken_ = nullptr;
    QTemporaryDir* previewTempDir_ = nullptr;

    void stopPlaybackAndClear();
    void clearSourceState();
    void pollSnapshot();
    void setError(const QString& message);
    bool loadPlaybackPath(const QString& logicalPath,
                          const QString& playbackPath,
                          double resumeFraction,
                          bool startPlaying);
    bool hasNeutralDspParameters() const noexcept;
    void scheduleDspPreview();
};
