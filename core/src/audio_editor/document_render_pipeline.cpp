#include "document_render_pipeline.hpp"

#include "../decoder.hpp"

#include <chrono>
#include <cmath>
#include <system_error>

namespace agplayer::editor {
namespace {
bool needs_time_pitch(const TimePitchSession& session) noexcept
{
    return std::abs(session.speedPercent() - 100.0) > 0.001
        || session.pitchCents() != 0 || session.formantPreservation();
}

std::filesystem::path intermediate_path_for(const std::filesystem::path& output)
{
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return output.parent_path() / std::filesystem::u8path(
        output.filename().u8string() + ".agplayer-time-pitch-"
        + std::to_string(ticks) + ".wav");
}

WriteResult verify_decodable(const WriteRequest& request, WriteResult result)
{
    if (!result.ok()) return result;
    agplayer::Decoder decoder;
    agplayer::DecodedAudioBlock block;
    if (decoder.open(request.output_path.u8string()) != AG_OK
        || decoder.read(block) != AG_OK || block.frames == 0) {
        return {WriteError::VerificationFailed,
                "final export is not decodable", result.frames};
    }
    return result;
}
} // namespace

WriteResult DocumentRenderPipeline::write(
    const WriteRequest& request, const TimePitchSession& time_pitch,
    const std::atomic_bool* cancelled, std::function<void(float)> progress) const
{
    if (outputOverwritesSource(request)) {
        return {WriteError::InvalidRequest, "output would overwrite a source file", 0};
    }
    if (!needs_time_pitch(time_pitch)) {
        return verify_decodable(request,
            DocumentWriter{}.write(request, cancelled, progress));
    }
    const auto intermediate = intermediate_path_for(request.output_path);
    const auto processed = time_pitch.process(request.snapshot, intermediate,
        request.range, cancelled,
        progress ? [progress](float value) { progress(value * 0.70F); }
                 : std::function<void(float)>{});
    if (!processed.success) {
        return {cancelled && cancelled->load(std::memory_order_acquire)
                    ? WriteError::Cancelled : WriteError::RenderFailed,
                processed.message, 0};
    }
    WriteRequest processedRequest = request;
    AudioEvent renderedEvent{1, std::make_shared<const AudioSource>(processed.source),
        0, processed.source.total_frames, 0};
    renderedEvent.timelineSampleRate = processed.source.sample_rate;
    // Preserve the rendered project format, including legacy mono projects.
    // Track/event automation is already baked into this single final stream.
    processedRequest.snapshot = TimelineSnapshot{{renderedEvent},
        processed.source.total_frames, request.snapshot.revision};
    processedRequest.snapshot.sampleRate = processed.source.sample_rate;
    processedRequest.snapshot.channels = processed.source.channels;
    processedRequest.range.reset();
    const auto result = DocumentWriter{}.write(processedRequest, cancelled,
        progress ? [progress](float value) { progress(0.70F + value * 0.30F); }
                 : std::function<void(float)>{});
    std::error_code ignored;
    std::filesystem::remove(intermediate, ignored);
    return verify_decodable(request, result);
}

} // namespace agplayer::editor
