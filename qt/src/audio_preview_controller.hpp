#pragma once

#include <agplayer/c_api.h>

#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

class AudioPreviewController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasSource READ hasSource NOTIFY sourceChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY stateChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY stateChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY sourceChanged)

public:
    explicit AudioPreviewController(
        ag_audio_backend backend = AG_AUDIO_BACKEND_DEFAULT,
        QObject* parent = nullptr);
    ~AudioPreviewController() override;

    bool hasSource() const noexcept;
    bool playing() const noexcept;
    qint64 positionMs() const noexcept;
    qint64 durationMs() const noexcept;
    double volume() const noexcept;
    QString sourcePath() const;

    Q_INVOKABLE void toggle(const QUrl& source);
    Q_INVOKABLE void play(const QUrl& source);
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(qint64 positionMs);
    void setVolume(double value);

signals:
    void sourceChanged();
    void stateChanged();
    void volumeChanged();
    void errorOccurred(const QString& message);

private:
    ag_player* player_ = nullptr;
    QTimer pollTimer_;
    QString sourcePath_;
    bool playing_ = false;
    qint64 positionMs_ = 0;
    qint64 durationMs_ = 0;
    double volume_ = 0.8;

    void pollSnapshot();
    void setError(const QString& message);
};
