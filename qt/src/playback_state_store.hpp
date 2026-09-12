#pragma once

#include <QString>
#include <QStringList>
#include <optional>

class PlaybackStateStore final {
public:
    struct State {
        QStringList queueTrackIds;
        QString currentTrackId;
        qint64 positionMs = 0;
        int mode = 0;
        bool cleanExit = true;
        bool valid = false;
    };

    explicit PlaybackStateStore(QString filePath);

    State load() const;
    bool save(const State& state) const;
    bool markRunStarted() const;

private:
    QString filePath_;
    mutable std::optional<State> lastSavedState_;
};
