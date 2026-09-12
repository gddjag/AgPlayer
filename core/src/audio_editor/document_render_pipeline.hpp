#pragma once

#include "document_writer.hpp"
#include "time_pitch_session.hpp"

namespace agplayer::editor {

class DocumentRenderPipeline final {
public:
    [[nodiscard]] WriteResult write(
        const WriteRequest& request,
        const TimePitchSession& time_pitch,
        const std::atomic_bool* cancelled = nullptr,
        std::function<void(float)> progress = {}) const;
};

} // namespace agplayer::editor
