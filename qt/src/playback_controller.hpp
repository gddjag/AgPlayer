#pragma once

#include <QObject>
#include <QMetaObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>

struct ag_player;
class LibraryModel;

class PlaybackController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY positionMsChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY durationMsChanged)
    Q_PROPERTY(float volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY mutedChanged)
    Q_PROPERTY(Mode mode READ mode NOTIFY modeChanged)
    Q_PROPERTY(qint64 trackIndex READ trackIndex NOTIFY trackIndexChanged)
    Q_PROPERTY(qint64 trackCount READ trackCount NOTIFY trackCountChanged)
    Q_PROPERTY(QString currentTrackId READ currentTrackId NOTIFY currentTrackIdChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    enum State { Stopped, Loading, Playing, Paused, Error };
    Q_ENUM(State)

    enum Mode { Sequential, RepeatOne, Shuffle };
    Q_ENUM(Mode)

    explicit PlaybackController(ag_player* player = nullptr,
                                LibraryModel* library = nullptr,
                                QObject* parent = nullptr);

    State state() const noexcept;
    qint64 positionMs() const noexcept;
    qint64 durationMs() const noexcept;
    float volume() const noexcept;
    bool muted() const noexcept;
    Mode mode() const noexcept;
    qint64 trackIndex() const noexcept;
    qint64 trackCount() const noexcept;
    QString currentTrackId() const;
    QString errorMessage() const;

    void setLibraryModel(LibraryModel* library);

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlayback();
    Q_INVOKABLE void seek(qint64 positionMs);
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void setVolume(float volume);
    Q_INVOKABLE void toggleMuted();
    Q_INVOKABLE void cycleMode();
    Q_INVOKABLE void playRow(int row);
    Q_INVOKABLE void toggleFavorite();
    Q_INVOKABLE void toggleFavorite(int row);

signals:
    void stateChanged();
    void positionMsChanged();
    void durationMsChanged();
    void volumeChanged();
    void mutedChanged();
    void modeChanged();
    void trackIndexChanged();
    void trackCountChanged();
    void currentTrackIdChanged();
    void errorMessageChanged();

private:
    void pollSnapshot();
    void setErrorMessage(QString message);
    void runCommand(int result);

    ag_player* player_ = nullptr;
    QPointer<LibraryModel> library_;
    QMetaObject::Connection playRequestedConnection_;
    QTimer pollTimer_;
    State state_ = Stopped;
    qint64 positionMs_ = 0;
    qint64 durationMs_ = 0;
    float volume_ = 1.0F;
    bool muted_ = false;
    Mode mode_ = Sequential;
    qint64 trackIndex_ = -1;
    qint64 trackCount_ = 0;
    QString currentTrackId_;
    QString errorMessage_;
    QStringList queueTrackIds_;
};
