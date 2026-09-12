#pragma once

#include "../../../core/src/audio_editor/audio_document.hpp"

#include <QString>
#include <QtGlobal>

#include <atomic>
#include <memory>
#include <vector>

namespace agplayer::editor {

struct ProjectExportSettings final {
    QString codecName;
    int sampleRate{};
    int bitDepth{24};
    int channels{};
    qint64 bitRate{320'000};
    bool keepMetadata{true};
    bool variableBitRate{true};
    int quality{80};
    QString outputDirectory;
};

struct ProjectEditorSettings final {
    double originalBpm{};
    double targetBpm{};
    double speedPercent{100.0};
    bool keepPitch{true};
    bool formantPreservation{};
    int pitchCents{};
    bool trackMuted{};
    bool trackSolo{};
    double trackGainDb{};
};

[[nodiscard]] bool isValidProjectExportSettings(
    const ProjectExportSettings& settings) noexcept;

struct ProjectSourceRecord final {
    quint64 sourceId{};
    std::shared_ptr<const AudioSource> source;
    qint64 fileSize{-1};
    qint64 lastModifiedUtcMs{-1};
};

enum class ProjectSourceIssueKind { Missing, IdentityMismatch, Unavailable };

struct ProjectSourceIssue final {
    ProjectSourceIssueKind kind{ProjectSourceIssueKind::Missing};
    quint64 sourceId{};
    QString path;
    QString message;
};

struct ProjectSaveRequest final {
    const AudioDocument* document{};
    SampleFrame playheadFrame{};
    SampleFrame visibleStartFrame{};
    SampleFrame visibleEndFrame{};
    ProjectExportSettings exportSettings;
    ProjectEditorSettings editorSettings;
    const std::vector<ProjectSourceRecord>* sourceRecords{};
};

struct ProjectSaveResult final {
    bool success{};
    QString message;
    std::vector<ProjectSourceRecord> sources{};
    [[nodiscard]] bool ok() const noexcept { return success; }
};

struct ProjectLoadResult final {
    std::unique_ptr<AudioDocument> document;
    std::vector<ProjectSourceRecord> sources;
    std::vector<ProjectSourceIssue> issues;
    SampleFrame playheadFrame{};
    SampleFrame visibleStartFrame{};
    SampleFrame visibleEndFrame{};
    ProjectExportSettings exportSettings;
    ProjectEditorSettings editorSettings;
    QString message;
    [[nodiscard]] bool ok() const noexcept { return document != nullptr; }
};

struct ProjectRelinkResult final {
    bool success{};
    QString message;
    [[nodiscard]] bool ok() const noexcept { return success; }
};

class ProjectDocument final {
public:
    [[nodiscard]] static constexpr int schemaVersion() noexcept { return 2; }
    [[nodiscard]] static ProjectSaveResult save(
        const QString& path, const ProjectSaveRequest& request);
    [[nodiscard]] static ProjectLoadResult load(
        const QString& path, const std::atomic_bool* cancelled = nullptr);
    [[nodiscard]] static ProjectRelinkResult relink(
        AudioDocument& document, std::vector<ProjectSourceRecord>& sources,
        quint64 sourceId, const QString& replacementPath,
        const std::atomic_bool* cancelled = nullptr);
};

} // namespace agplayer::editor
