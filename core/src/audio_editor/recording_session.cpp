#include "recording_session.hpp"

#include "../pcm_ring_buffer.hpp"

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace agplayer::editor {
namespace {

std::string device_token(const ma_device_id& id)
{
#ifdef _WIN32
    const auto* bytes = reinterpret_cast<const unsigned char*>(id.wasapi);
    std::size_t byte_count = 0;
    while (byte_count + sizeof(wchar_t) <= sizeof(id.wasapi)
           && id.wasapi[byte_count / sizeof(wchar_t)] != 0) {
        byte_count += sizeof(wchar_t);
    }
#else
    const auto* bytes = reinterpret_cast<const unsigned char*>(&id);
    const std::size_t byte_count = sizeof(id);
#endif
    static constexpr char hex[] = "0123456789abcdef";
    std::string result = "capture:";
    result.reserve(result.size() + byte_count * 2);
    for (std::size_t index = 0; index < byte_count; ++index) {
        result.push_back(hex[bytes[index] >> 4U]);
        result.push_back(hex[bytes[index] & 0x0FU]);
    }
    return result;
}

void write_u16(std::ostream& stream, const std::uint16_t value)
{
    const std::array<char, 2> bytes{
        static_cast<char>(value & 0xFFU),
        static_cast<char>((value >> 8U) & 0xFFU)};
    stream.write(bytes.data(), bytes.size());
}

void write_u32(std::ostream& stream, const std::uint32_t value)
{
    const std::array<char, 4> bytes{
        static_cast<char>(value & 0xFFU),
        static_cast<char>((value >> 8U) & 0xFFU),
        static_cast<char>((value >> 16U) & 0xFFU),
        static_cast<char>((value >> 24U) & 0xFFU)};
    stream.write(bytes.data(), bytes.size());
}

bool atomic_commit(const std::filesystem::path& staged,
                   const std::filesystem::path& target)
{
#ifdef _WIN32
    if (std::filesystem::exists(target)) {
        return ReplaceFileW(target.wstring().c_str(), staged.wstring().c_str(),
                            nullptr, REPLACEFILE_WRITE_THROUGH,
                            nullptr, nullptr) != 0;
    }
    return MoveFileExW(staged.wstring().c_str(), target.wstring().c_str(),
                       MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code error;
    std::filesystem::rename(staged, target, error);
    return !error;
#endif
}

bool read_journal(const std::filesystem::path& journal,
                  std::filesystem::path& staged,
                  std::filesystem::path& target)
{
    std::ifstream stream(journal);
    std::string staged_text;
    std::string target_text;
    std::string sample_rate;
    std::string channels;
    if (!std::getline(stream, staged_text)
        || !std::getline(stream, target_text)
        || !std::getline(stream, sample_rate)
        || !std::getline(stream, channels)
        || staged_text.empty() || target_text.empty()) {
        return false;
    }
    try {
        const auto rate = std::stoul(sample_rate);
        const auto channel_count = std::stoul(channels);
        if (rate < 8'000 || rate > 192'000
            || channel_count == 0 || channel_count > 2) {
            return false;
        }
    } catch (...) {
        return false;
    }
    staged = std::filesystem::u8path(staged_text);
    target = std::filesystem::u8path(target_text);
    const auto journal_directory = std::filesystem::absolute(
        journal.parent_path()).lexically_normal();
    const auto staged_directory = std::filesystem::absolute(
        staged.parent_path()).lexically_normal();
    const auto target_directory = std::filesystem::absolute(
        target.parent_path()).lexically_normal();
#ifdef _WIN32
    return _wcsicmp(staged_directory.c_str(), journal_directory.c_str()) == 0
        && _wcsicmp(target_directory.c_str(), journal_directory.c_str()) == 0;
#else
    return staged_directory == journal_directory
        && target_directory == journal_directory;
#endif
}

} // namespace

class RecordingSession::Impl final {
public:
    void clear_last_error()
    {
        const std::lock_guard<std::mutex> lock(last_error_mutex);
        last_error.clear();
    }

    void set_last_error(std::string message)
    {
        const std::lock_guard<std::mutex> lock(last_error_mutex);
        last_error = std::move(message);
    }

    [[nodiscard]] std::string last_error_copy() const
    {
        const std::lock_guard<std::mutex> lock(last_error_mutex);
        return last_error;
    }

    ~Impl()
    {
        if (state.load() != RecordingState::Idle) {
            (void)stop();
        }
    }

    bool start_common(const RecordingConfig& value)
    {
        clear_last_error();
        if (state.load() != RecordingState::Idle) {
            set_last_error("recording session is already active");
            return false;
        }
        if (value.output_path.empty()) {
            set_last_error("recording output path is empty");
            return false;
        }
        if (value.sample_rate < 8'000 || value.sample_rate > 192'000
            || value.channels == 0 || value.channels > 2) {
            set_last_error("unsupported recording format");
            return false;
        }
        config = value;
        std::error_code error;
        std::filesystem::create_directories(config.output_path.parent_path(), error);
        if (error) {
            set_last_error("cannot create recording output directory");
            return false;
        }
        (void)RecordingSession::recoverIncomplete(
            config.output_path.parent_path());
        staged_path = config.output_path;
        staged_path += ".agplayer-recording.tmp";
        journal_path = config.output_path;
        journal_path += ".agplayer-recording.journal";
        stream.open(staged_path, std::ios::binary | std::ios::trunc);
        if (!stream) {
            set_last_error("cannot create recording staging file");
            return false;
        }
        write_header(0);
        {
            std::ofstream journal(journal_path, std::ios::trunc);
            journal << staged_path.u8string() << '\n'
                    << config.output_path.u8string() << '\n'
                    << config.sample_rate << '\n' << config.channels << '\n';
        }
        try {
            ring = std::make_unique<agplayer::PcmRingBuffer>(
                config.sample_rate * 2U, config.channels);
        } catch (...) {
            set_last_error("cannot allocate recording buffer");
            cleanup_files();
            return false;
        }
        peak_value.store(0.0F);
        peak_write_index.store(0, std::memory_order_release);
        for (auto& peak_sample : recent_peaks) peak_sample.store(0.0F);
        captured.store(0);
        written.store(0);
        dropped.store(0);
        writer_exit.store(false);
        state.store(RecordingState::Recording);
        writer = std::thread([this] { writer_loop(); });
        return true;
    }

    bool start_device(const RecordingConfig& value)
    {
        if (!start_common(value)) return false;
        ma_backend backend = ma_backend_wasapi;
        if (ma_context_init(&backend, 1, nullptr, &context) != MA_SUCCESS) {
            set_last_error("WASAPI context initialization failed");
            abort_start();
            return false;
        }
        context_ready = true;
        ma_device_id chosen{};
        const ma_device_id* chosen_ptr = nullptr;
        if (!value.device_id.empty()) {
            ma_device_info* capture = nullptr;
            ma_uint32 capture_count = 0;
            if (ma_context_get_devices(&context, nullptr, nullptr,
                                       &capture, &capture_count) != MA_SUCCESS) {
                set_last_error("WASAPI capture device enumeration failed");
                abort_start();
                return false;
            }
            for (ma_uint32 index = 0; index < capture_count; ++index) {
                if (device_token(capture[index].id) == value.device_id) {
                    chosen = capture[index].id;
                    chosen_ptr = &chosen;
                    break;
                }
            }
            if (chosen_ptr == nullptr) {
                set_last_error("WASAPI capture device was not found");
                abort_start();
                return false;
            }
        }
        ma_device_config device_config = ma_device_config_init(
            value.monitor ? ma_device_type_duplex : ma_device_type_capture);
        device_config.capture.pDeviceID = chosen_ptr;
        device_config.capture.format = ma_format_f32;
        device_config.capture.channels = value.channels;
        device_config.capture.shareMode = ma_share_mode_shared;
        if (value.monitor) {
            device_config.playback.format = ma_format_f32;
            device_config.playback.channels = value.channels;
            device_config.playback.shareMode = ma_share_mode_shared;
        }
        device_config.sampleRate = value.sample_rate;
        device_config.dataCallback = data_callback;
        device_config.notificationCallback = notification_callback;
        device_config.pUserData = this;
        if (ma_device_init(&context, &device_config, &device) != MA_SUCCESS) {
            set_last_error("WASAPI capture initialization failed; check microphone permissions and format");
            abort_start();
            return false;
        }
        device_ready = true;
        if (ma_device_start(&device) != MA_SUCCESS) {
            set_last_error("WASAPI capture start failed; check microphone access");
            abort_start();
            return false;
        }
        device_started = true;
        return true;
    }

    std::size_t push(const float* input, const std::size_t frames) noexcept
    {
        if (input == nullptr || frames == 0
            || state.load(std::memory_order_acquire) != RecordingState::Recording
            || ring == nullptr) {
            return 0;
        }
        float local_peak = 0.0F;
        const std::size_t samples = frames * config.channels;
        for (std::size_t index = 0; index < samples; ++index) {
            local_peak = (std::max)(local_peak, std::abs(input[index]));
        }
        peak_value.store((std::max)(peak_value.load(std::memory_order_relaxed),
                                    local_peak), std::memory_order_release);
        const std::uint64_t peak_index = peak_write_index.fetch_add(
            1, std::memory_order_acq_rel);
        recent_peaks[peak_index % recent_peaks.size()].store(
            local_peak, std::memory_order_release);
        const std::size_t accepted = ring->write(input, frames);
        captured.fetch_add(static_cast<SampleFrame>(accepted));
        dropped.fetch_add(frames - accepted);
        return accepted;
    }

    RecordingResult stop()
    {
        RecordingState current = state.load();
        if (current == RecordingState::Idle) {
            return {false, "recording is not active", {}, 0, 0.0F};
        }
        state.store(RecordingState::Finalizing);
        if (device_started) ma_device_stop(&device);
        device_started = false;
        if (device_ready) ma_device_uninit(&device);
        device_ready = false;
        if (context_ready) ma_context_uninit(&context);
        context_ready = false;
        writer_exit.store(true, std::memory_order_release);
        if (writer.joinable()) writer.join();
        const SampleFrame frame_count = written.load();
        bool success = stream.good()
            && frame_count <= static_cast<SampleFrame>(
                std::numeric_limits<std::uint32_t>::max()
                / (config.channels * 3U));
        if (success) {
            stream.seekp(0);
            write_header(static_cast<std::uint32_t>(
                frame_count * config.channels * 3U));
            stream.flush();
            success = stream.good();
        }
        stream.close();
        if (success) success = atomic_commit(staged_path, config.output_path);
        std::error_code ignored;
        if (success) std::filesystem::remove(journal_path, ignored);
        else std::filesystem::remove(staged_path, ignored);
        const RecordingResult result{
            success, success ? std::string{} : "recording finalization failed",
            success ? config.output_path : std::filesystem::path{},
            frame_count, peak_value.load()};
        ring.reset();
        state.store(success ? RecordingState::Idle : RecordingState::Error);
        if (!success) state.store(RecordingState::Idle);
        return result;
    }

    bool cancel()
    {
        if (state.load() == RecordingState::Idle) return false;
        state.store(RecordingState::Finalizing);
        if (device_started) ma_device_stop(&device);
        device_started = false;
        if (device_ready) ma_device_uninit(&device);
        device_ready = false;
        if (context_ready) ma_context_uninit(&context);
        context_ready = false;
        writer_exit.store(true, std::memory_order_release);
        if (writer.joinable()) writer.join();
        cleanup_files();
        ring.reset();
        state.store(RecordingState::Idle);
        return true;
    }

    static void data_callback(ma_device* device_ptr, void* output,
                              const void* input, ma_uint32 frames)
    {
        auto* self = static_cast<Impl*>(device_ptr->pUserData);
        if (self == nullptr || input == nullptr) return;
        if (output != nullptr) {
            std::memcpy(output, input,
                static_cast<std::size_t>(frames) * self->config.channels
                * sizeof(float));
        }
        (void)self->push(static_cast<const float*>(input), frames);
    }

    static void notification_callback(const ma_device_notification* notice)
    {
        if (notice == nullptr || notice->pDevice == nullptr) return;
        auto* self = static_cast<Impl*>(notice->pDevice->pUserData);
        if (self != nullptr && notice->type == ma_device_notification_type_rerouted) {
            self->rerouted.store(true);
        }
    }

    void writer_loop()
    {
        std::vector<float> buffer(2'048U * config.channels);
        while (!writer_exit.load(std::memory_order_acquire)
               || (ring != nullptr && ring->available_frames() > 0)) {
            const std::size_t frames = ring == nullptr ? 0
                : ring->read(buffer.data(), 2'048U);
            if (frames == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            std::array<char, 3> bytes{};
            for (std::size_t index = 0; index < frames * config.channels; ++index) {
                const float sample = std::clamp(buffer[index], -1.0F, 1.0F);
                const auto value = static_cast<std::int32_t>(std::lround(
                    sample * 8'388'607.0F));
                bytes[0] = static_cast<char>(value & 0xFF);
                bytes[1] = static_cast<char>((value >> 8) & 0xFF);
                bytes[2] = static_cast<char>((value >> 16) & 0xFF);
                stream.write(bytes.data(), bytes.size());
            }
            written.fetch_add(static_cast<SampleFrame>(frames));
        }
    }

    void write_header(const std::uint32_t data_bytes)
    {
        stream.write("RIFF", 4);
        write_u32(stream, 36U + data_bytes);
        stream.write("WAVEfmt ", 8);
        write_u32(stream, 16U);
        write_u16(stream, 1U);
        write_u16(stream, static_cast<std::uint16_t>(config.channels));
        write_u32(stream, config.sample_rate);
        write_u32(stream, config.sample_rate * config.channels * 3U);
        write_u16(stream, static_cast<std::uint16_t>(config.channels * 3U));
        write_u16(stream, 24U);
        stream.write("data", 4);
        write_u32(stream, data_bytes);
    }

    void cleanup_files()
    {
        if (stream.is_open()) stream.close();
        std::error_code ignored;
        std::filesystem::remove(staged_path, ignored);
        std::filesystem::remove(journal_path, ignored);
    }

    void abort_start()
    {
        if (device_started) ma_device_stop(&device);
        device_started = false;
        if (device_ready) ma_device_uninit(&device);
        device_ready = false;
        if (context_ready) ma_context_uninit(&context);
        context_ready = false;
        state.store(RecordingState::Finalizing);
        writer_exit.store(true, std::memory_order_release);
        if (writer.joinable()) writer.join();
        ring.reset();
        cleanup_files();
        state.store(RecordingState::Idle);
    }

    RecordingConfig config;
    std::filesystem::path staged_path;
    std::filesystem::path journal_path;
    std::ofstream stream;
    std::unique_ptr<agplayer::PcmRingBuffer> ring;
    std::thread writer;
    std::atomic<RecordingState> state{RecordingState::Idle};
    std::atomic<bool> writer_exit{false};
    std::atomic<float> peak_value{0.0F};
    std::atomic<SampleFrame> captured{0};
    std::atomic<SampleFrame> written{0};
    std::atomic<std::uint64_t> dropped{0};
    std::atomic<bool> rerouted{false};
    std::array<std::atomic<float>, 2'048> recent_peaks{};
    std::atomic<std::uint64_t> peak_write_index{0};
    ma_context context{};
    ma_device device{};
    bool context_ready{};
    bool device_ready{};
    bool device_started{};
    mutable std::mutex last_error_mutex;
    std::string last_error;
};

RecordingSession::RecordingSession() : impl_(std::make_unique<Impl>()) {}
RecordingSession::~RecordingSession() = default;

std::vector<RecordingDevice> RecordingSession::inputDevices()
{
    ma_context context{};
    ma_backend backend = ma_backend_wasapi;
    if (ma_context_init(&backend, 1, nullptr, &context) != MA_SUCCESS) return {};
    ma_device_info* capture = nullptr;
    ma_uint32 count = 0;
    std::vector<RecordingDevice> result;
    if (ma_context_get_devices(&context, nullptr, nullptr, &capture, &count)
        == MA_SUCCESS) {
        result.reserve(count);
        for (ma_uint32 index = 0; index < count; ++index) {
            if (capture[index].name[0] != '\0') {
                result.push_back({device_token(capture[index].id),
                                  capture[index].name,
                                  capture[index].isDefault != 0});
            }
        }
    }
    ma_context_uninit(&context);
    std::stable_sort(result.begin(), result.end(),
        [](const RecordingDevice& left, const RecordingDevice& right) {
            return left.is_default && !right.is_default;
        });
    return result;
}

std::vector<RecordingResult> RecordingSession::recoverIncomplete(
    const std::filesystem::path& directory)
{
    std::vector<RecordingResult> results;
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error) || error) {
        return results;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error) error.clear();
        const auto path = entry.path();
        const std::string filename = path.filename().u8string();
        static constexpr char journal_suffix[] = ".agplayer-recording.journal";
        if (!entry.is_regular_file(error)
            || filename.size() < sizeof(journal_suffix) - 1U
            || filename.compare(filename.size() - (sizeof(journal_suffix) - 1U),
                                sizeof(journal_suffix) - 1U,
                                journal_suffix) != 0) {
            error.clear();
            continue;
        }
        std::filesystem::path staged;
        std::filesystem::path target;
        if (!read_journal(path, staged, target)
            || !std::filesystem::is_regular_file(staged, error)
            || std::filesystem::file_size(staged, error) <= 44U) {
            results.push_back({false, "invalid recording journal", {}, 0, 0.0F});
            error.clear();
            continue;
        }
        if (std::filesystem::exists(target, error)) {
            const auto directory_path = target.parent_path();
            const auto stem = target.stem().u8string();
            const auto extension = target.extension().u8string();
            std::uint32_t index = 1;
            do {
                target = directory_path / std::filesystem::u8path(
                    stem + "-recovered-" + std::to_string(index++) + extension);
            } while (std::filesystem::exists(target, error));
        }
        const bool success = atomic_commit(staged, target);
        if (success) std::filesystem::remove(path, error);
        results.push_back({success,
            success ? std::string{} : "recording recovery failed",
            success ? target : std::filesystem::path{}, 0, 0.0F});
    }
    return results;
}

bool RecordingSession::start(const RecordingConfig& config)
{
    return impl_->start_device(config);
}

bool RecordingSession::startManual(const RecordingConfig& config)
{
    return impl_->start_common(config);
}

bool RecordingSession::pause() noexcept
{
    RecordingState expected = RecordingState::Recording;
    return impl_->state.compare_exchange_strong(expected, RecordingState::Paused);
}

bool RecordingSession::resume() noexcept
{
    RecordingState expected = RecordingState::Paused;
    return impl_->state.compare_exchange_strong(expected, RecordingState::Recording);
}

RecordingResult RecordingSession::stop() { return impl_->stop(); }

bool RecordingSession::cancel() { return impl_->cancel(); }

std::size_t RecordingSession::pushCapturedFrames(
    const float* interleaved, const std::size_t frames) noexcept
{
    return impl_->push(interleaved, frames);
}

RecordingState RecordingSession::state() const noexcept { return impl_->state.load(); }
float RecordingSession::peak() const noexcept { return impl_->peak_value.load(); }
SampleFrame RecordingSession::framesCaptured() const noexcept { return impl_->captured.load(); }
std::uint64_t RecordingSession::droppedFrames() const noexcept { return impl_->dropped.load(); }

std::vector<float> RecordingSession::recentPeaks(const std::size_t maximum) const
{
    if (maximum == 0) return {};
    const std::uint64_t end = impl_->peak_write_index.load(
        std::memory_order_acquire);
    const std::size_t count = (std::min)({maximum, impl_->recent_peaks.size(),
        static_cast<std::size_t>((std::min<std::uint64_t>)(
            end, impl_->recent_peaks.size()))});
    std::vector<float> result;
    result.reserve(count);
    for (std::size_t offset = count; offset > 0; --offset) {
        result.push_back(impl_->recent_peaks[
            (end - offset) % impl_->recent_peaks.size()].load(
                std::memory_order_acquire));
    }
    return result;
}

std::string RecordingSession::lastError() const
{
    return impl_->last_error_copy();
}

} // namespace agplayer::editor
