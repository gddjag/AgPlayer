#include "bpm_analyzer.hpp"

#include "runtime_log.hpp"
#include "audio_editor/editor_playback_stream.hpp"
#include "../../core/src/bpm_analyzer.hpp"

#include <agplayer/c_api.h>

BpmAnalyzeResult analyze_bpm(const QString& filePath)
{
    BpmAnalyzeResult result;
    ag_bpm_result api_result{};
    const QByteArray path = filePath.toUtf8();
    const ag_result api_status = ag_bpm_analyze(path.constData(), &api_result);
    if (api_status != AG_OK) {
        result.status = api_status;
        RuntimeLog::log(api_status, QStringLiteral("BPM"),
            QStringLiteral("Failed to analyze BPM for %1").arg(filePath));
        return result;
    }
    result.bpm = api_result.bpm;
    result.confidence = api_result.confidence;
    return result;
}

BpmAnalyzeResult analyze_bpm(
    agplayer::editor::TimelineSnapshot snapshot,
    const std::atomic_bool* const cancelled)
{
    BpmAnalyzeResult result;
    std::string error;
    auto stream = agplayer::editor::EditorPlaybackStream::create(
        std::move(snapshot), {}, error);
    if (!stream) {
        result.status = AG_INVALID_ARGUMENT;
        result.error = QString::fromStdString(error);
        return result;
    }
    agplayer::BpmAnalyzeOutput output;
    result.status = agplayer::analyze_bpm(*stream, 90, cancelled, &output);
    if (result.status == AG_OK) {
        result.bpm = output.bpm;
        result.confidence = output.confidence;
    } else if (result.status != AG_CANCELLED) {
        result.error = QStringLiteral("Unable to analyze current editor timeline");
    }
    return result;
}
