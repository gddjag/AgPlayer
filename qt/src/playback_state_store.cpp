#include "playback_state_store.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {
constexpr int StateVersion = 1;
}

PlaybackStateStore::PlaybackStateStore(QString filePath)
    : filePath_(std::move(filePath))
{
}

PlaybackStateStore::State PlaybackStateStore::load() const
{
    QFile file(filePath_);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("version")).toInt() != StateVersion) {
        return {};
    }

    State state;
    const QJsonArray queue = object.value(QStringLiteral("queueTrackIds")).toArray();
    state.queueTrackIds.reserve(queue.size());
    for (const QJsonValue& value : queue) {
        if (value.isString() && !value.toString().isEmpty()) {
            state.queueTrackIds.append(value.toString());
        }
    }
    state.currentTrackId = object.value(QStringLiteral("currentTrackId")).toString();
    state.positionMs = qMax<qint64>(
        0, object.value(QStringLiteral("positionMs")).toInteger());
    state.mode = object.value(QStringLiteral("mode")).toInt();
    state.cleanExit = object.value(QStringLiteral("cleanExit")).toBool(true);
    state.valid = true;
    return state;
}

bool PlaybackStateStore::save(const State& state) const
{
    // A paused/idle player publishes the same checkpoint every second.
    // Only cache successful commits; failed writes must remain retryable.
    if (lastSavedState_) {
        const auto& saved = *lastSavedState_;
        if (saved.positionMs == state.positionMs && saved.mode == state.mode
            && saved.cleanExit == state.cleanExit
            && saved.currentTrackId == state.currentTrackId
            && saved.queueTrackIds == state.queueTrackIds
            && QFile::exists(filePath_)) return true;
    }
    QJsonObject object{
        {QStringLiteral("version"), StateVersion},
        {QStringLiteral("queueTrackIds"), QJsonArray::fromStringList(state.queueTrackIds)},
        {QStringLiteral("currentTrackId"), state.currentTrackId},
        {QStringLiteral("positionMs"), state.positionMs},
        {QStringLiteral("mode"), state.mode},
        {QStringLiteral("cleanExit"), state.cleanExit},
    };
    const QByteArray data = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QSaveFile file(filePath_);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) return false;
    lastSavedState_ = state;
    return true;
}

bool PlaybackStateStore::markRunStarted() const
{
    State state = load();
    if (!state.valid) {
        state.valid = true;
    }
    state.cleanExit = false;
    return save(state);
}
