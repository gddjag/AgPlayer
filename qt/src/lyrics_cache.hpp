#pragma once

#include "library_model.hpp"
#include "lyrics_line_model.hpp"

#include <optional>

class LyricsCache final {
public:
    struct Entry final {
        LyricsDocument document;
        QString source;
        bool instrumental = false;
    };

    explicit LyricsCache(QString cacheDirectory = {});

    void setCacheDirectory(QString cacheDirectory);
    [[nodiscard]] QString pathFor(const TrackRecord& track) const;
    [[nodiscard]] static QString keyFor(const TrackRecord& track);
    [[nodiscard]] std::optional<Entry> load(const TrackRecord& track) const;
    [[nodiscard]] bool save(const TrackRecord& track, const Entry& entry) const;

private:
    QString cacheDirectory_;
};
