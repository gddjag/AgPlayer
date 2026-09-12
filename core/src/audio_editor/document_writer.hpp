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
};

struct WriteResult final {
    WriteError error{WriteError::None};
    std::string message;
    SampleFrame frames{};

    [[nodiscard]] bool ok() const noexcept { return error == WriteError::None; }
};

class DocumentWriter final {
public:
    [[nodiscard]] WriteResult write(
        const WriteRequest& request,
        const std::atomic_bool* cancelled = nullptr,
        std::function<void(float)> progress = {}) const;
};

} // namespace agplayer::editor
