#include "timeline_mixer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace agplayer::editor {

bool TimelineMixer::prepare(TimelineSnapshot& snapshot, std::string& error)
{
    if (snapshot.events.empty() || snapshot.totalFrames <= 0
        || snapshot.events.size() > kMaxTimelineEvents) {
        error = "invalid render request";
        return false;
    }
    const auto& first = snapshot.events.front();
    if (!first.source) {
        error = "invalid or unsupported timeline event";
        return false;
    }
    // An absent project rate marks a legacy aggregate snapshot. Preserve its
    // source channel layout; all new project snapshots carry explicit format.
    if (!snapshot.sampleRate) {
        snapshot.sampleRate = first.source->sample_rate;
        snapshot.channels = first.source->channels;
    }
    if (!snapshot.sampleRate || snapshot.sampleRate > 768'000
        || !snapshot.channels || snapshot.channels > 8
        || !std::isfinite(snapshot.legacyMasterGain)
        || snapshot.legacyMasterGain < 0) {
        error = "document has no valid audio format";
        return false;
    }
    for (const auto& track : snapshot.tracks) {
        if (!std::isfinite(track.gain) || track.gain < 0 || track.gain > 2) {
            error = "invalid track gain";
            return false;
        }
    }
    std::size_t points = 0;
    for (auto& event : snapshot.events) {
        event.timelineSampleRate = snapshot.sampleRate;
        points += event.envelope.size();
        if (!isValid(event) || !event.source->sample_rate
            || event.source->sample_rate > 768'000
            || !event.source->channels || event.source->channels > 64
            || audibleFrames(event) <= 0
            || event.timelineStart > snapshot.totalFrames
            || audibleFrames(event) > snapshot.totalFrames - event.timelineStart
            || points > kMaxTimelineEnvelopePoints) {
            error = "invalid or unsupported timeline event";
            return false;
        }
    }
    std::stable_sort(snapshot.events.begin(), snapshot.events.end(),
        [](const AudioEvent& a, const AudioEvent& b) { return a.timelineStart < b.timelineStart; });
    std::array<SampleFrame, kTrackCount> previousEnds{};
    for (const auto& event : snapshot.events) {
        auto& previousEnd = previousEnds[static_cast<std::size_t>(event.trackIndex)];
        if (event.timelineStart < previousEnd) {
            error = "events overlap on the same track";
            return false;
        }
        previousEnd = event.timelineStart + audibleFrames(event);
    }
    error.clear();
    return true;
}

TimelineMixer::TimelineMixer(TimelineSnapshot snapshot, const std::atomic_bool* cancelled,
                             agplayer::TimePitchEngineFactory engineFactory)
    : snapshot_(std::move(snapshot)), cancelled_(cancelled), engine_factory_(engineFactory)
{
    const bool anySolo = std::any_of(snapshot_.tracks.begin(), snapshot_.tracks.end(),
        [](const TrackState& track) { return track.solo; });
    for (std::size_t i = 0; i < gains_.size(); ++i) {
        const auto& track = snapshot_.tracks[i];
        gains_[i] = track.muted || (anySolo && !track.solo) ? 0.0F : track.gain;
    }
    for (std::size_t i = 0; i < snapshot_.events.size(); ++i) {
        readers_[static_cast<std::size_t>(snapshot_.events[i].trackIndex)].events.push_back(i);
    }
}

bool TimelineMixer::cancelled() const noexcept
{ return cancelled_ && cancelled_->load(std::memory_order_relaxed); }

void TimelineMixer::setTrackGains(const std::array<float, kTrackCount>& gains)
{
    std::lock_guard<std::mutex> guard(gain_mutex_);
    gains_ = gains;
}

void TimelineMixer::seek(const SampleFrame frame)
{
    cursor_ = std::clamp<SampleFrame>(frame, 0, snapshot_.totalFrames);
    for (auto& reader : readers_) {
        reader.decoder.reset();
        reader.processor.reset();
        reader.block.frames = 0;
        reader.offset = 0;
        reader.flushed = false;
        reader.index = static_cast<std::size_t>(std::lower_bound(
            reader.events.begin(), reader.events.end(), cursor_,
            [this](std::size_t index, SampleFrame value) {
                const auto& event = snapshot_.events[index];
                return event.timelineStart + audibleFrames(event) <= value;
            }) - reader.events.begin());
    }
}

bool TimelineMixer::open(TrackReader& reader, const AudioEvent& event,
                          const SampleFrame localOffset)
{
    if (reader.decoder) return true;
    auto decoder = std::make_unique<agplayer::Decoder>();
    agplayer::DecoderOpenOptions options;
    options.output_sample_rate = static_cast<int>(snapshot_.sampleRate);
    options.output_channels = static_cast<int>(snapshot_.channels);
    options.interrupt_context = this;
    options.interrupt_callback = [](void* context) noexcept {
        return static_cast<TimelineMixer*>(context)->cancelled();
    };
    if (decoder->open(event.source->path.u8string(), options) != AG_OK) return false;
    // Decoder::seekFrame is in its OUTPUT rate, not the source rate. Mapping
    // absolute source boundaries also keeps split clips on the same PCM grid.
    const SampleFrame sourceStartInProject = sourceToProjectFrames(
        event.sourceStart, event.source->sample_rate, snapshot_.sampleRate);
    const SampleFrame scaledOffset = static_cast<SampleFrame>(std::llround(
        static_cast<long double>(localOffset) * event.speedRatio));
    if (scaledOffset > std::numeric_limits<SampleFrame>::max() - sourceStartInProject
        || decoder->seekFrame(sourceStartInProject + scaledOffset) != AG_OK) return false;
    const SampleFrame sourceEndInProject = sourceToProjectFrames(
        event.sourceEnd, event.source->sample_rate, snapshot_.sampleRate);
    reader.source_frames_left = std::max<SampleFrame>(0,
        sourceEndInProject - sourceStartInProject - scaledOffset);
    if (event.speedRatio != 1.0 || event.pitchSemitone != 0) {
        auto processor = engine_factory_ ? engine_factory_() : nullptr;
        if (!processor || !processor->configure(static_cast<int>(snapshot_.sampleRate),
                static_cast<int>(snapshot_.channels))
            || !processor->setTempoRatio(event.speedRatio)
            || !processor->setPitchCents(event.pitchSemitone * 100)
            || !processor->setFormantPreservation(false)) return false;
        reader.processor = std::move(processor);
    }
    reader.block.frames = 0;
    reader.offset = 0;
    reader.flushed = false;
    reader.decoder = std::move(decoder);
    return true;
}

ag_result TimelineMixer::nextProcessedBlock(TrackReader& reader)
{
    const auto channels = static_cast<std::size_t>(snapshot_.channels);
    reader.processed.resize(kBlockFrames * channels);
    reader.block.frames = 0;
    for (;;) {
        if (cancelled()) return AG_CANCELLED;
        const auto count = reader.processor->receive(reader.processed.data(), kBlockFrames);
        if (reader.processor->failed()) return AG_INTERNAL_ERROR;
        if (count) {
            reader.block.samples.assign(reader.processed.begin(),
                reader.processed.begin() + static_cast<std::ptrdiff_t>(count * channels));
            reader.block.frames = count;
            reader.offset = 0;
            return AG_OK;
        }
        if (reader.flushed) return AG_OK;
        if (reader.source_frames_left <= 0) {
            reader.processor->flush();
            if (reader.processor->failed()) return AG_INTERNAL_ERROR;
            reader.flushed = true;
            continue;
        }
        agplayer::DecodedAudioBlock decoded;
        if (reader.decoder->read(decoded) != AG_OK) return AG_DECODE_ERROR;
        if (decoded.frames == 0) {
            if (decoded.end_of_stream) return AG_DECODE_ERROR;
            continue;
        }
        const auto supplied = static_cast<std::size_t>(std::min<SampleFrame>(
            reader.source_frames_left, static_cast<SampleFrame>(decoded.frames)));
        reader.processor->put(decoded.samples.data(), supplied);
        if (reader.processor->failed()) return AG_INTERNAL_ERROR;
        reader.source_frames_left -= static_cast<SampleFrame>(supplied);
    }
}

ag_result TimelineMixer::mix(TrackReader& reader, std::vector<float>& samples,
                              const SampleFrame blockEnd)
{
    const auto channels = static_cast<std::size_t>(snapshot_.channels);
    while (reader.index < reader.events.size()) {
        if (cancelled()) return AG_CANCELLED;
        const auto& event = snapshot_.events[reader.events[reader.index]];
        const float trackGain = block_gains_[static_cast<std::size_t>(event.trackIndex)];
        const SampleFrame eventEnd = event.timelineStart + audibleFrames(event);
        if (eventEnd <= cursor_) {
            ++reader.index;
            reader.decoder.reset();
            reader.processor.reset();
            continue;
        }
        if (event.timelineStart >= blockEnd) break;
        SampleFrame position = std::max(cursor_, event.timelineStart);
        const SampleFrame end = std::min(blockEnd, eventEnd);
        if (!event.mute && trackGain != 0
            && snapshot_.legacyMasterGain != 0) {
            if (!reader.decoder && !open(reader, event, position - event.timelineStart)) {
                return cancelled() ? AG_CANCELLED : AG_DECODE_ERROR;
            }
            while (position < end) {
                if (cancelled()) return AG_CANCELLED;
                if (reader.offset >= reader.block.frames) {
                    const auto status = reader.processor
                        ? nextProcessedBlock(reader) : reader.decoder->read(reader.block);
                    if (status != AG_OK) {
                        return cancelled() ? AG_CANCELLED : status;
                    }
                    reader.offset = 0;
                    if (!reader.block.frames) {
                        if (reader.processor && reader.flushed) break;
                        if (reader.block.end_of_stream) return AG_DECODE_ERROR;
                        continue;
                    }
                }
                const auto count = static_cast<std::size_t>(std::min<SampleFrame>(
                    end - position, static_cast<SampleFrame>(reader.block.frames - reader.offset)));
                for (std::size_t frame = 0; frame < count; ++frame) {
                    const auto local = position - event.timelineStart + static_cast<SampleFrame>(frame);
                    const float gain = eventAmplitudeGainAt(event, local)
                        * trackGain * snapshot_.legacyMasterGain;
                    const auto outputFrame = static_cast<std::size_t>(position - cursor_) + frame;
                    for (std::size_t channel = 0; channel < channels; ++channel) {
                        const float source = reader.block.samples[(reader.offset + frame) * channels + channel];
                        // Malformed source PCM must not poison the session DSP.
                        if (std::isfinite(source)) samples[outputFrame * channels + channel] += source * gain;
                    }
                }
                position += static_cast<SampleFrame>(count);
                reader.offset += count;
            }
        }
        // A muted reader must reopen at the current timeline position when
        // enabled again, rather than resume stale PCM from before the mute.
        if (event.mute || trackGain == 0 || snapshot_.legacyMasterGain == 0) {
            reader.decoder.reset();
            reader.processor.reset();
        }
        if (eventEnd > blockEnd) break;
        ++reader.index;
        reader.decoder.reset();
        reader.processor.reset();
    }
    return AG_OK;
}

ag_result TimelineMixer::read(std::vector<float>& samples, const SampleFrame endFrame)
{
    if (cancelled()) return AG_CANCELLED;
    { std::lock_guard<std::mutex> guard(gain_mutex_); block_gains_ = gains_; }
    const SampleFrame remaining = std::max<SampleFrame>(0,
        std::min(endFrame, snapshot_.totalFrames) - cursor_);
    const auto count = static_cast<std::size_t>(std::min<SampleFrame>(remaining, kBlockFrames));
    samples.assign(count * snapshot_.channels, 0.0F);
    const SampleFrame blockEnd = cursor_ + static_cast<SampleFrame>(count);
    for (auto& reader : readers_) {
        const auto status = mix(reader, samples, blockEnd);
        if (status != AG_OK) return status;
    }
    cursor_ = blockEnd;
    return AG_OK;
}

void finalizeTimelineSamples(std::vector<float>& samples) noexcept
{
    for (float& sample : samples) sample = std::isfinite(sample)
        ? std::clamp(sample, -1.0F, 1.0F) : 0.0F;
}

} // namespace agplayer::editor
