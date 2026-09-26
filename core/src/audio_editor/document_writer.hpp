#pragma once

#include "audio_document.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace agplayer::editor {

enum class WriteError {
    None,
    InvalidRequest,
    UnsupportedEncoder,
    Cancelled,
    RenderFailed,
    EncodeFailed,
    MetadataFailed,
    VerificationFailed,
    CommitFailed
};

enum class OutputCommitMode {
    CreateNoReplace,
    Overwrite
};

struct WriteRequest final {
    TimelineSnapshot snapshot;
    std::filesystem::path output_path;
    std::string codec_name;
    std::filesystem::path metadata_source_path;
    long long bit_rate{};
    int sample_rate{};
    int channels{};
    bool keep_metadata{};
    bool variable_bit_rate{};
    int quality{75};
    std::optional<Selection> range;
    OutputCommitMode commit_mode{OutputCommitMode::CreateNoReplace};
};

struct WriteResult final {
    WriteError error{WriteError::None};
    std::string message;
    SampleFrame frames{};

    [[nodiscard]] bool ok() const noexcept { return error == WriteError::None; }
};

// Check the original request before any stage replaces its source snapshot.
[[nodiscard]] bool outputOverwritesSource(const WriteRequest& request);

class DocumentWriter final {
public:
    [[nodiscard]] WriteResult write(
        const WriteRequest& request,
        const std::atomic_bool* cancelled = nullptr,
        std::function<void(float)> progress = {}) const;
};

} // namespace agplayer::editor
