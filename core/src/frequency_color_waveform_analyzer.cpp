#include "frequency_color_waveform_analyzer.hpp"

#include "decoder.hpp"
#include "waveform_analyzer_filters.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace agplayer {
namespace {

constexpr float kMidGain = 0.8912509381337456F;
constexpr float kHighGain = 1.4125375446227544F;
constexpr float kSilenceGate = 0.000251188643150958F;

bool is_cancelled(const std::atomic_bool* const cancelled) noexcept
{
    return cancelled != nullptr
           && cancelled->load(std::memory_order_relaxed);
}

void clear_output(FrequencyColorWaveformData& output) noexcept
{
    output = {};
}

float envelope(const detail::BucketStats& stats) noexcept
{
    if (stats.count == 0U || !std::isfinite(stats.peak)
        || !std::isfinite(stats.sum_squares)) {
        return 0.0F;
    }
    const double rms = std::sqrt(
        stats.sum_squares / static_cast<double>(stats.count));
    const double value = 0.7 * static_cast<double>(stats.peak) + 0.3 * rms;
    return std::isfinite(value) ? static_cast<float>(value) : 0.0F;
}

void smooth_occupied(std::vector<float>& values,
                     const std::vector<std::uint8_t>& occupied,
                     const std::size_t window)
{
    if (window <= 1U || values.empty()) return;
    const std::vector<float> input = values;
    const std::size_t radius = window / 2U;
    for (std::size_t index = 0U; index < values.size(); ++index) {
        if (occupied[index] == 0U) {
            values[index] = 0.0F;
            continue;
        }
        const std::size_t begin = index > radius ? index - radius : 0U;
        const std::size_t end = std::min(values.size(), index + radius + 1U);
        double sum = 0.0;
        for (std::size_t neighbor = begin; neighbor < end; ++neighbor) {
            sum += input[neighbor];
        }
        values[index] = static_cast<float>(
            sum / static_cast<double>(end - begin));
    }
}

float percentile_reference(const std::vector<float>& mix,
                           const std::vector<float>& low,
                           const std::vector<float>& mid,
                           const std::vector<float>& high,
                           const std::vector<std::uint8_t>& occupied)
{
    std::vector<float> maxima;
    maxima.reserve(mix.size());
    for (std::size_t index = 0U; index < mix.size(); ++index) {
        if (occupied[index] == 0U) continue;
        const float value = std::max({mix[index], low[index], mid[index],
                                      high[index]});
        if (std::isfinite(value) && value > 0.0F) maxima.push_back(value);
    }
    if (maxima.empty()) return 0.0F;
    const std::size_t rank = std::min(
        maxima.size() - 1U,
        static_cast<std::size_t>(std::ceil(0.995 * maxima.size())) - 1U);
    std::nth_element(maxima.begin(), maxima.begin() + rank, maxima.end());
    return maxima[rank];
}

void display_map(std::vector<float>& values,
                 const std::vector<std::uint8_t>& occupied,
                 const float reference) noexcept
{
    for (std::size_t index = 0U; index < values.size(); ++index) {
        float& value = values[index];
        if (occupied[index] == 0U || !std::isfinite(value)
            || value < kSilenceGate || reference <= 0.0F) {
            value = 0.0F;
            continue;
        }
        const float normalized = std::clamp(value / reference, 0.0F, 1.0F);
        value = std::min(std::pow(normalized, 0.62F), 0.98F);
    }
}

} // namespace

namespace detail {

FrequencyColorAccumulator::FrequencyColorAccumulator(
    const std::uint64_t timeline_frames,
    const std::size_t point_count)
    : timeline_frames_(timeline_frames),
      point_count_(point_count),
      mix_(point_count),
      low_(point_count),
      mid_(point_count),
      high_(point_count),
      occupied_(point_count, 0U),
      failed_(timeline_frames == 0U || point_count == 0U)
{
}

ag_result FrequencyColorAccumulator::beginBlock(
    const std::int64_t timestamp_frame,
    const std::size_t frame_count) noexcept
{
    if (failed_ || finished_ || timestamp_frame < 0) {
        failed_ = true;
        return AG_DECODE_ERROR;
    }
    const auto begin = static_cast<std::uint64_t>(timestamp_frame);
    if (begin < next_allowed_frame_
        || frame_count > std::numeric_limits<std::uint64_t>::max() - begin) {
        failed_ = true;
        return AG_DECODE_ERROR;
    }
    active_begin_ = begin;
    active_end_ = begin + static_cast<std::uint64_t>(frame_count);
    next_allowed_frame_ = active_end_;
    return AG_OK;
}

ag_result FrequencyColorAccumulator::addFrame(
    const std::uint64_t absolute_frame,
    const FrequencyFrameValues& values) noexcept
{
    if (failed_ || finished_ || absolute_frame < active_begin_
        || absolute_frame >= active_end_
        || !std::isfinite(values.mix) || !std::isfinite(values.low)
        || !std::isfinite(values.mid) || !std::isfinite(values.high)) {
        failed_ = true;
        return AG_DECODE_ERROR;
    }
    const std::uint64_t bucket_wide = std::min<std::uint64_t>(
        static_cast<std::uint64_t>(point_count_ - 1U),
        absolute_frame * static_cast<std::uint64_t>(point_count_)
            / timeline_frames_);
    const std::size_t bucket = static_cast<std::size_t>(bucket_wide);
    const auto add = [bucket](std::vector<BucketStats>& target,
                              const float sample) noexcept {
        BucketStats& stats = target[bucket];
        stats.peak = std::max(stats.peak, std::abs(sample));
        stats.sum_squares += static_cast<double>(sample) * sample;
        ++stats.count;
    };
    add(mix_, values.mix);
    add(low_, values.low);
    add(mid_, values.mid);
    add(high_, values.high);
    occupied_[bucket] = 1U;
    return AG_OK;
}

ag_result FrequencyColorAccumulator::addBlock(
    const std::int64_t timestamp_frame,
    const std::vector<FrequencyFrameValues>& frames) noexcept
{
    const ag_result begin_result = beginBlock(timestamp_frame, frames.size());
    if (begin_result != AG_OK) return begin_result;
    const std::uint64_t begin = static_cast<std::uint64_t>(timestamp_frame);
    for (std::size_t index = 0U; index < frames.size(); ++index) {
        const ag_result add_result = addFrame(begin + index, frames[index]);
        if (add_result != AG_OK) return add_result;
    }
    return AG_OK;
}

ag_result FrequencyColorAccumulator::finish(
    FrequencyColorWaveformData& output) noexcept
{
    output.mix.clear();
    output.low.clear();
    output.mid.clear();
    output.high.clear();
    if (failed_ || finished_) return AG_DECODE_ERROR;
    finished_ = true;

    output.mix.resize(point_count_);
    output.low.resize(point_count_);
    output.mid.resize(point_count_);
    output.high.resize(point_count_);
    for (std::size_t index = 0U; index < point_count_; ++index) {
        output.mix[index] = envelope(mix_[index]);
        output.low[index] = envelope(low_[index]);
        output.mid[index] = envelope(mid_[index]) * kMidGain;
        output.high[index] = envelope(high_[index]) * kHighGain;
    }
    smooth_occupied(output.low, occupied_, 7U);
    smooth_occupied(output.mid, occupied_, 5U);
    smooth_occupied(output.high, occupied_, 3U);
    const float reference = percentile_reference(
        output.mix, output.low, output.mid, output.high, occupied_);
    display_map(output.mix, occupied_, reference);
    display_map(output.low, occupied_, reference);
    display_map(output.mid, occupied_, reference);
    display_map(output.high, occupied_, reference);
    return AG_OK;
}

std::size_t FrequencyColorAccumulator::memoryBytes() const noexcept
{
    return (mix_.capacity() + low_.capacity() + mid_.capacity()
            + high_.capacity()) * sizeof(BucketStats)
           + occupied_.capacity() * sizeof(std::uint8_t);
}

} // namespace detail

ag_result FrequencyColorWaveformAnalyzer::analyze(
    const std::string& utf8_path,
    const std::size_t point_count,
    const std::atomic_bool* const cancelled,
    const ag_progress_callback progress,
    void* const user_data,
    FrequencyColorWaveformData& output,
    FrequencyColorAnalysisDiagnostics* const diagnostics) noexcept
{
    clear_output(output);
    if (diagnostics != nullptr) *diagnostics = {};
    if (utf8_path.empty() || point_count == 0U) return AG_INVALID_ARGUMENT;
    if (is_cancelled(cancelled)) return AG_CANCELLED;

    try {
        if (progress != nullptr) progress(0.0F, user_data);
        if (is_cancelled(cancelled)) return AG_CANCELLED;

        Decoder decoder;
        DecoderOpenOptions options;
        options.downmix = DecoderDownmix::AnalysisMono;
        if (diagnostics != nullptr) diagnostics->decoder_open_count = 1U;
        const ag_result open_result = decoder.open(utf8_path, options);
        if (open_result != AG_OK) return open_result;

        const DecodedAudioFormat format = decoder.output_format();
        if (!format.has_timeline || format.timeline_frames == 0U) {
            return AG_UNSUPPORTED_FORMAT;
        }
        if (format.channels != 1 || format.sample_rate <= 0) {
            return AG_UNSUPPORTED_FORMAT;
        }

        detail::FrequencyBandSplitter splitter(
            static_cast<float>(format.sample_rate));
        detail::FrequencyColorAccumulator accumulator(
            format.timeline_frames, point_count);
        if (diagnostics != nullptr) {
            diagnostics->accumulator_bytes = accumulator.memoryBytes();
        }

        std::uint64_t decoded_frames = 0U;
        bool timeline_started = false;
        std::uint64_t expected_timeline_frame = 0U;
        const std::uint64_t timestamp_rounding_tolerance =
            std::max<std::uint64_t>(1U,
                static_cast<std::uint64_t>(format.sample_rate) / 1'000U);
        DecodedAudioBlock block;
        do {
            if (is_cancelled(cancelled)) return AG_CANCELLED;
            const ag_result read_result = decoder.read(block);
            if (read_result != AG_OK) return read_result;
            if (block.samples.size() != block.frames) return AG_DECODE_ERROR;
            if (block.frames > 0U) {
                std::size_t leading_preroll = 0U;
                if (block.timestamp_frame < 0) {
                    if (timeline_started) return AG_DECODE_ERROR;
                    const std::uint64_t magnitude = block.timestamp_frame
                        == std::numeric_limits<std::int64_t>::min()
                        ? std::uint64_t{1U} << 63U
                        : static_cast<std::uint64_t>(-block.timestamp_frame);
                    leading_preroll = static_cast<std::size_t>(
                        std::min<std::uint64_t>(magnitude, block.frames));
                }
                const std::size_t timeline_frame_count =
                    block.frames - leading_preroll;
                std::int64_t adjusted_begin = block.timestamp_frame
                    + static_cast<std::int64_t>(leading_preroll);
                if (timeline_frame_count > 0U) {
                    if (timeline_started) {
                        const std::uint64_t candidate =
                            static_cast<std::uint64_t>(adjusted_begin);
                        const std::uint64_t difference = candidate
                            > expected_timeline_frame
                            ? candidate - expected_timeline_frame
                            : expected_timeline_frame - candidate;
                        if (difference <= timestamp_rounding_tolerance) {
                            adjusted_begin = static_cast<std::int64_t>(
                                expected_timeline_frame);
                        }
                    }
                    const ag_result begin_result = accumulator.beginBlock(
                        adjusted_begin, timeline_frame_count);
                    if (begin_result != AG_OK) return begin_result;
                    timeline_started = true;
                    expected_timeline_frame = static_cast<std::uint64_t>(
                        adjusted_begin) + timeline_frame_count;
                }
                for (std::size_t index = 0U; index < block.frames; ++index) {
                    const float sample = block.samples[index];
                    if (!std::isfinite(sample)) return AG_DECODE_ERROR;
                    const detail::FrequencyBandValues bands =
                        splitter.process(sample);
                    if (index < leading_preroll) continue;
                    const ag_result add_result = accumulator.addFrame(
                        static_cast<std::uint64_t>(adjusted_begin)
                            + index - leading_preroll,
                        {sample, bands.low, bands.mid, bands.high});
                    if (add_result != AG_OK) return add_result;
                }
                if (block.frames
                    > std::numeric_limits<std::uint64_t>::max()
                          - decoded_frames) {
                    return AG_INTERNAL_ERROR;
                }
                decoded_frames += static_cast<std::uint64_t>(block.frames);
                if (diagnostics != nullptr) ++diagnostics->decoded_block_count;
                if (progress != nullptr) {
                    const std::uint64_t block_end = block.timestamp_frame >= 0
                        ? static_cast<std::uint64_t>(block.timestamp_frame)
                              + static_cast<std::uint64_t>(block.frames)
                        : timeline_frame_count > 0U
                        ? static_cast<std::uint64_t>(timeline_frame_count)
                        : 0U;
                    const long double ratio = std::min<long double>(
                        static_cast<long double>(block_end)
                            / static_cast<long double>(format.timeline_frames),
                        1.0L);
                    progress(static_cast<float>(ratio * 0.99L), user_data);
                }
                if (is_cancelled(cancelled)) return AG_CANCELLED;
            }
        } while (!block.end_of_stream);

        if (decoded_frames == 0U) return AG_DECODE_ERROR;
        const ag_result finish_result = accumulator.finish(output);
        if (finish_result != AG_OK) {
            clear_output(output);
            return finish_result;
        }
        output.timeline_frames = format.timeline_frames;
        output.decoded_frames = decoded_frames;
        output.sample_rate = static_cast<std::uint32_t>(format.sample_rate);
        output.duration_ms = static_cast<std::uint64_t>(std::llround(
            static_cast<long double>(format.timeline_frames) * 1'000.0L
            / static_cast<long double>(format.sample_rate)));
        if (progress != nullptr) progress(1.0F, user_data);
        if (is_cancelled(cancelled)) {
            clear_output(output);
            return AG_CANCELLED;
        }
        return AG_OK;
    } catch (...) {
        clear_output(output);
        return AG_INTERNAL_ERROR;
    }
}

} // namespace agplayer
