// Keep assert() active in Release: these tests intentionally perform calls
// with side effects inside assertions.
#undef NDEBUG

#include <agplayer/c_api.h>
#include "video_decoder.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using agplayer::VideoDecoderTestPoint;

class ControlledHook final {
public:
    explicit ControlledHook(const VideoDecoderTestPoint target,
                            const bool block = false,
                            const int forced_eagain_count = 0)
        : target_(target), block_(block), forced_eagain_count_(
              forced_eagain_count)
    {
        agplayer::set_video_decoder_test_hook(&invoke, this);
    }

    ~ControlledHook()
    {
        release();
        agplayer::set_video_decoder_test_hook(nullptr, nullptr);
    }

    ControlledHook(const ControlledHook&) = delete;
    ControlledHook& operator=(const ControlledHook&) = delete;

    void wait_until_entered()
    {
        std::unique_lock lock(mutex_);
        assert(condition_.wait_for(lock, std::chrono::seconds(5),
                                   [this] { return entered_; }));
    }

    void release() noexcept
    {
        try {
            std::lock_guard lock(mutex_);
            released_ = true;
            condition_.notify_all();
        } catch (...) {
        }
    }

private:
    static bool invoke(const VideoDecoderTestPoint point,
                       void* const opaque) noexcept
    {
        return static_cast<ControlledHook*>(opaque)->on_hook(point);
    }

    bool on_hook(const VideoDecoderTestPoint point) noexcept
    {
        if (point != target_) {
            return false;
        }
        try {
            std::unique_lock lock(mutex_);
            entered_ = true;
            condition_.notify_all();
            if (block_) {
                condition_.wait(lock, [this] { return released_; });
            }
            if (forced_eagain_count_ > 0) {
                --forced_eagain_count_;
                return true;
            }
        } catch (...) {
        }
        return false;
    }

    VideoDecoderTestPoint target_;
    bool block_ = false;
    int forced_eagain_count_ = 0;
    std::mutex mutex_;
    std::condition_variable condition_;
    bool entered_ = false;
    bool released_ = false;
};

ag_video_frame empty_frame()
{
    ag_video_frame frame{};
    frame.struct_size = sizeof(frame);
    return frame;
}

ag_video_media_info empty_media_info()
{
    ag_video_media_info info{};
    info.struct_size = sizeof(info);
    return info;
}

std::vector<unsigned char> copy_pixels(const ag_video_frame& frame)
{
    assert(frame.data != nullptr);
    assert(frame.data_size > 0U);
    return {frame.data, frame.data + frame.data_size};
}

void test_null_and_state_boundaries(const char* video_path)
{
    assert(ag_video_decoder_create(nullptr) == AG_INVALID_ARGUMENT);
    ag_video_decoder_cancel(nullptr);
    ag_video_decoder_close(nullptr);
    ag_video_decoder_destroy(nullptr);

    ag_video_decoder* decoder = reinterpret_cast<ag_video_decoder*>(
        static_cast<std::uintptr_t>(1U));
    assert(ag_video_decoder_create(&decoder) == AG_OK);
    assert(decoder != nullptr);

    assert(ag_video_decoder_open(nullptr, video_path) == AG_INVALID_ARGUMENT);
    assert(ag_video_decoder_open(decoder, nullptr) == AG_INVALID_ARGUMENT);
    assert(ag_video_decoder_open(decoder, "") == AG_INVALID_ARGUMENT);
    ag_video_media_info info = empty_media_info();
    assert(ag_video_decoder_open_with_media_info(nullptr, video_path, &info)
           == AG_INVALID_ARGUMENT);
    assert(ag_video_decoder_open_with_media_info(decoder, nullptr, &info)
           == AG_INVALID_ARGUMENT);
    assert(ag_video_decoder_open_with_media_info(decoder, video_path, nullptr)
           == AG_INVALID_ARGUMENT);
    info.struct_size = sizeof(info) - 1U;
    assert(ag_video_decoder_open_with_media_info(decoder, video_path, &info)
           == AG_INVALID_ARGUMENT);
    assert(ag_video_decoder_read(nullptr, nullptr) == AG_INVALID_ARGUMENT);
    assert(ag_video_decoder_read(decoder, nullptr) == AG_INVALID_ARGUMENT);

    ag_video_frame frame = empty_frame();
    assert(ag_video_decoder_read(decoder, &frame) == AG_INVALID_ARGUMENT);
    assert(ag_video_decoder_seek(nullptr, 0) == AG_INVALID_ARGUMENT);
    assert(ag_video_decoder_seek(decoder, 0) == AG_INVALID_ARGUMENT);
    assert(ag_video_decoder_seek(decoder, -1) == AG_INVALID_ARGUMENT);

    assert(ag_video_decoder_open(decoder, video_path) == AG_OK);
    frame = empty_frame();
    frame.struct_size = 0;
    assert(ag_video_decoder_read(decoder, &frame) == AG_INVALID_ARGUMENT);
    ag_video_decoder_close(decoder);
    ag_video_decoder_close(decoder);
    ag_video_decoder_destroy(decoder);
}

void test_media_info_classification_and_abi(
    const char* video_with_audio_path, const char* audio_only_path,
    const char* attached_picture_path)
{
    struct FutureMediaInfo {
        ag_video_media_info info{};
        std::uint64_t future_field = 0x1AF01AF01AF01AF0ULL;
    } future;
    future.info.struct_size = sizeof(future);

    ag_video_decoder* decoder = nullptr;
    assert(ag_video_decoder_create(&decoder) == AG_OK);
    assert(ag_video_decoder_open_with_media_info(
               decoder, video_with_audio_path, &future.info)
           == AG_OK);
    assert(future.info.struct_size == sizeof(future));
    assert(future.info.valid == 1);
    assert(future.info.has_audio == 1);
    assert(future.info.has_video == 1);
    assert(future.future_field == 0x1AF01AF01AF01AF0ULL);
    ag_video_decoder_close(decoder);

    ag_video_media_info info = empty_media_info();
    assert(ag_video_decoder_open_with_media_info(decoder, audio_only_path,
                                                 &info)
           == AG_UNSUPPORTED_FORMAT);
    assert(info.valid == 1);
    assert(info.has_audio == 1);
    assert(info.has_video == 0);

    info = empty_media_info();
    assert(ag_video_decoder_open_with_media_info(decoder,
                                                 attached_picture_path, &info)
           == AG_UNSUPPORTED_FORMAT);
    assert(info.valid == 1);
    assert(info.has_audio == 1);
    assert(info.has_video == 0);

    // The pre-existing entry point keeps its decoder-ready return contract.
    assert(ag_video_decoder_open(decoder, video_with_audio_path) == AG_OK);
    ag_video_frame frame = empty_frame();
    assert(ag_video_decoder_read(decoder, &frame) == AG_OK);
    assert(frame.end_of_stream == 0);
    ag_video_decoder_destroy(decoder);
}

void test_media_info_survives_unsupported_codec(const char* video_path)
{
    ag_video_decoder* decoder = nullptr;
    assert(ag_video_decoder_create(&decoder) == AG_OK);
    ag_video_media_info info = empty_media_info();
    {
        ControlledHook hook(VideoDecoderTestPoint::codec_open_entered,
                            false, 1);
        assert(ag_video_decoder_open_with_media_info(decoder, video_path,
                                                     &info)
               == AG_UNSUPPORTED_FORMAT);
    }
    assert(info.valid == 1);
    assert(info.has_audio == 1);
    assert(info.has_video == 1);

    // A failed codec open leaves the existing handle reusable.
    assert(ag_video_decoder_open(decoder, video_path) == AG_OK);
    ag_video_decoder_destroy(decoder);
}

void test_open_read_and_eof(const char* video_path)
{
    ag_video_decoder* decoder = nullptr;
    assert(ag_video_decoder_create(&decoder) == AG_OK);
    assert(ag_video_decoder_open(decoder, video_path) == AG_OK);

    ag_video_frame first = empty_frame();
    assert(ag_video_decoder_read(decoder, &first) == AG_OK);
    assert(first.end_of_stream == 0);
    assert(first.width == 320);
    assert(first.height == 180);
    assert(first.stride == 320 * 4);
    assert(first.data_size == 320U * 180U * 4U);
    assert(first.pixel_format == AG_VIDEO_PIXEL_FORMAT_BGRA8);
    assert(first.pts_ms == 0);
    assert(first.sar_num == 1);
    assert(first.sar_den == 1);
    assert(first.rotation_degrees == 0);
    const std::vector<unsigned char> first_pixels = copy_pixels(first);

    ag_video_frame second = empty_frame();
    assert(ag_video_decoder_read(decoder, &second) == AG_OK);
    assert(second.end_of_stream == 0);
    assert(second.pts_ms > first.pts_ms);
    const std::vector<unsigned char> second_pixels = copy_pixels(second);
    assert(first_pixels != second_pixels);

    int decoded_frames = 2;
    for (;;) {
        ag_video_frame frame = empty_frame();
        assert(ag_video_decoder_read(decoder, &frame) == AG_OK);
        if (frame.end_of_stream != 0) {
            assert(frame.data == nullptr);
            assert(frame.data_size == 0U);
            break;
        }
        ++decoded_frames;
    }
    assert(decoded_frames == 60);

    ag_video_frame repeated_eof = empty_frame();
    assert(ag_video_decoder_read(decoder, &repeated_eof) == AG_OK);
    assert(repeated_eof.end_of_stream == 1);
    assert(repeated_eof.data == nullptr);
    ag_video_decoder_destroy(decoder);
}

void test_future_sized_frame_and_open_on_open(const char* video_path)
{
    struct FutureFrame {
        ag_video_frame frame{};
        std::uint64_t future_field = 0xC0DEC0DEC0DEC0DEULL;
    } output;
    output.frame.struct_size = sizeof(output);

    ag_video_decoder* decoder = nullptr;
    assert(ag_video_decoder_create(&decoder) == AG_OK);
    assert(ag_video_decoder_open(decoder, video_path) == AG_OK);
    assert(ag_video_decoder_read(decoder, &output.frame) == AG_OK);
    assert(output.frame.struct_size == sizeof(output));
    assert(output.future_field == 0xC0DEC0DEC0DEC0DEULL);
    const std::int64_t first_pts = output.frame.pts_ms;

    assert(ag_video_decoder_open(decoder, video_path) == AG_INVALID_ARGUMENT);
    output.frame.struct_size = sizeof(output);
    assert(ag_video_decoder_read(decoder, &output.frame) == AG_OK);
    assert(output.frame.pts_ms > first_pts);
    assert(output.future_field == 0xC0DEC0DEC0DEC0DEULL);
    ag_video_decoder_destroy(decoder);
}

void test_seek(const char* video_path)
{
    ag_video_decoder* decoder = nullptr;
    assert(ag_video_decoder_create(&decoder) == AG_OK);
    assert(ag_video_decoder_open(decoder, video_path) == AG_OK);
    assert(ag_video_decoder_seek(decoder, 1'000) == AG_OK);

    ag_video_frame frame = empty_frame();
    assert(ag_video_decoder_read(decoder, &frame) == AG_OK);
    assert(frame.end_of_stream == 0);
    assert(frame.pts_ms >= 900);
    assert(frame.pts_ms <= 1'100);

    assert(ag_video_decoder_seek(decoder, 0) == AG_OK);
    frame = empty_frame();
    assert(ag_video_decoder_read(decoder, &frame) == AG_OK);
    assert(frame.pts_ms == 0);
    ag_video_decoder_destroy(decoder);
}

void assert_geometry(const char* video_path, const int expected_rotation,
                     const int expected_sar_num, const int expected_sar_den,
                     const int expected_width = 320,
                     const int expected_height = 180)
{
    ag_video_decoder* decoder = nullptr;
    assert(ag_video_decoder_create(&decoder) == AG_OK);
    assert(ag_video_decoder_open(decoder, video_path) == AG_OK);
    ag_video_frame frame = empty_frame();
    assert(ag_video_decoder_read(decoder, &frame) == AG_OK);
    assert(frame.width == expected_width);
    assert(frame.height == expected_height);
    assert(frame.sar_num == expected_sar_num);
    assert(frame.sar_den == expected_sar_den);
    assert(frame.rotation_degrees == expected_rotation);
    ag_video_decoder_destroy(decoder);
}

void test_best_real_stream_after_attached_selection(const char* video_path)
{
    // Matroska exposes the two real video tracks as 0/1 and the attachment as
    // stream 2. Force the upstream best-stream result to that attachment.
    agplayer::set_video_decoder_forced_best_stream(2);
    ag_video_decoder* decoder = nullptr;
    assert(ag_video_decoder_create(&decoder) == AG_OK);
    const ag_result opened = ag_video_decoder_open(decoder, video_path);
    agplayer::set_video_decoder_forced_best_stream(-1);
    assert(opened == AG_OK);
    ag_video_frame frame = empty_frame();
    assert(ag_video_decoder_read(decoder, &frame) == AG_OK);
    assert(frame.width == 320);
    assert(frame.height == 180);
    ag_video_decoder_destroy(decoder);
}

void test_unsupported_and_corrupt_inputs(
    const std::filesystem::path& video_path,
    const char* attached_picture_path)
{
    ag_video_decoder* decoder = nullptr;
    assert(ag_video_decoder_create(&decoder) == AG_OK);
    assert(ag_video_decoder_open(decoder, attached_picture_path)
           == AG_UNSUPPORTED_FORMAT);

    const std::filesystem::path missing =
        video_path.parent_path() / "missing-video-file.avi";
    std::filesystem::remove(missing);
    assert(ag_video_decoder_open(decoder, missing.string().c_str())
           == AG_IO_ERROR);

    const std::filesystem::path corrupt =
        video_path.parent_path() / "corrupt-video-file.avi";
    {
        std::ofstream stream(corrupt, std::ios::binary | std::ios::trunc);
        stream << "not a video";
    }
    assert(ag_video_decoder_open(decoder, corrupt.string().c_str())
           == AG_UNSUPPORTED_FORMAT);
    std::filesystem::remove(corrupt);

    // A failed open must leave the handle reusable.
    assert(ag_video_decoder_open(decoder, video_path.string().c_str()) == AG_OK);
    ag_video_decoder_destroy(decoder);
}

void assert_reopen_after_cancel(ag_video_decoder* decoder, const char* video_path)
{
    ag_video_decoder_close(decoder);
    assert(ag_video_decoder_open(decoder, video_path) == AG_OK);
    ag_video_frame frame = empty_frame();
    assert(ag_video_decoder_read(decoder, &frame) == AG_OK);
    assert(frame.end_of_stream == 0);
}

void test_overlapping_cancellation_and_reset(const char* video_path)
{
    ag_video_decoder* decoder = nullptr;
    assert(ag_video_decoder_create(&decoder) == AG_OK);

    {
        ControlledHook hook(VideoDecoderTestPoint::open_entered, true);
        std::atomic<ag_result> result{AG_INTERNAL_ERROR};
        ag_video_media_info info = empty_media_info();
        std::thread operation([&] {
            result.store(ag_video_decoder_open_with_media_info(
                decoder, video_path, &info));
        });
        hook.wait_until_entered();
        std::thread cancellation([decoder] { ag_video_decoder_cancel(decoder); });
        cancellation.join();
        hook.release();
        operation.join();
        assert(result.load() == AG_CANCELLED);
        assert(info.valid == 0);
        assert(info.has_audio == 0);
        assert(info.has_video == 0);
    }
    assert_reopen_after_cancel(decoder, video_path);

    for (const VideoDecoderTestPoint point : {
             VideoDecoderTestPoint::frame_received,
             VideoDecoderTestPoint::frame_converted}) {
        ControlledHook hook(point, true);
        ag_video_frame frame = empty_frame();
        frame.data = reinterpret_cast<const unsigned char*>(
            static_cast<std::uintptr_t>(1));
        frame.data_size = 42;
        std::atomic<ag_result> result{AG_INTERNAL_ERROR};
        std::thread operation([&] {
            result.store(ag_video_decoder_read(decoder, &frame));
        });
        hook.wait_until_entered();
        std::thread cancellation([decoder] { ag_video_decoder_cancel(decoder); });
        cancellation.join();
        hook.release();
        operation.join();
        assert(result.load() == AG_CANCELLED);
        assert(frame.data == nullptr);
        assert(frame.data_size == 0U);
        assert_reopen_after_cancel(decoder, video_path);
    }

    {
        ControlledHook hook(VideoDecoderTestPoint::seek_entered, true);
        std::atomic<ag_result> result{AG_INTERNAL_ERROR};
        std::thread operation([&] {
            result.store(ag_video_decoder_seek(decoder, 500));
        });
        hook.wait_until_entered();
        std::thread cancellation([decoder] { ag_video_decoder_cancel(decoder); });
        cancellation.join();
        hook.release();
        operation.join();
        assert(result.load() == AG_CANCELLED);
    }
    assert_reopen_after_cancel(decoder, video_path);
    ag_video_decoder_destroy(decoder);
}

void test_packet_and_drain_eagain_are_retried(const char* video_path)
{
    for (const VideoDecoderTestPoint point : {
             VideoDecoderTestPoint::send_packet,
             VideoDecoderTestPoint::send_drain}) {
        ag_video_decoder* decoder = nullptr;
        assert(ag_video_decoder_create(&decoder) == AG_OK);
        assert(ag_video_decoder_open(decoder, video_path) == AG_OK);
        ControlledHook hook(point, false, 1);
        int frames = 0;
        for (;;) {
            ag_video_frame frame = empty_frame();
            assert(ag_video_decoder_read(decoder, &frame) == AG_OK);
            if (frame.end_of_stream != 0) {
                break;
            }
            if (frames == 0) {
                assert(frame.pts_ms == 0);
            }
            ++frames;
        }
        assert(frames == 60);
        ag_video_decoder_destroy(decoder);
    }
}

void test_destroy_waits_for_in_flight_read(const char* video_path)
{
    ag_video_decoder* decoder = nullptr;
    assert(ag_video_decoder_create(&decoder) == AG_OK);
    assert(ag_video_decoder_open(decoder, video_path) == AG_OK);
    ControlledHook hook(VideoDecoderTestPoint::frame_received, true);
    ag_video_frame frame = empty_frame();
    std::atomic<ag_result> read_result{AG_INTERNAL_ERROR};
    std::atomic_bool destroy_finished{false};
    std::thread reader([&] {
        read_result.store(ag_video_decoder_read(decoder, &frame));
    });
    hook.wait_until_entered();
    std::thread destroyer([&] {
        ag_video_decoder_destroy(decoder);
        destroy_finished.store(true);
    });

    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(5);
    while (!agplayer::video_decoder_is_shutting_down_for_test(decoder)
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    assert(agplayer::video_decoder_is_shutting_down_for_test(decoder));
    assert(ag_video_decoder_seek(decoder, 0) == AG_CANCELLED);
    assert(!destroy_finished.load());
    hook.release();
    reader.join();
    destroyer.join();
    assert(read_result.load() == AG_CANCELLED);
    assert(destroy_finished.load());
}

void test_one_hundred_complete_lifecycles(const char* video_path)
{
    for (int iteration = 0; iteration < 100; ++iteration) {
        ag_video_decoder* decoder = nullptr;
        assert(ag_video_decoder_create(&decoder) == AG_OK);
        assert(ag_video_decoder_open(decoder, video_path) == AG_OK);
        ag_video_frame frame = empty_frame();
        assert(ag_video_decoder_read(decoder, &frame) == AG_OK);
        assert(frame.end_of_stream == 0);
        ag_video_decoder_close(decoder);
        ag_video_decoder_destroy(decoder);
    }
}

} // namespace

int main(const int argc, char** argv)
{
    assert(argc == 10);
    const std::filesystem::path video_with_audio = argv[1];
    test_null_and_state_boundaries(argv[1]);
    test_media_info_classification_and_abi(argv[1], argv[9], argv[3]);
    test_media_info_survives_unsupported_codec(argv[1]);
    test_open_read_and_eof(argv[1]);
    test_open_read_and_eof(argv[2]);
    test_future_sized_frame_and_open_on_open(argv[2]);
    test_seek(argv[2]);
    assert_geometry(argv[4], 90, 1, 1);
    assert_geometry(argv[5], 180, 1, 1);
    assert_geometry(argv[6], 270, 1, 1);
    assert_geometry(argv[7], 0, 4, 3);
    test_best_real_stream_after_attached_selection(argv[8]);
    test_unsupported_and_corrupt_inputs(video_with_audio, argv[3]);
    test_overlapping_cancellation_and_reset(argv[2]);
    test_packet_and_drain_eagain_are_retried(argv[2]);
    test_one_hundred_complete_lifecycles(argv[2]);
    test_destroy_waits_for_in_flight_read(argv[2]);
    return 0;
}
