// Keep assert() active in Release: these tests intentionally perform calls
// with side effects inside assertions.
#undef NDEBUG

#include <agplayer/c_api.h>

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

namespace {

ag_video_frame empty_frame()
{
    ag_video_frame frame{};
    frame.struct_size = sizeof(frame);
    return frame;
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

void test_rotation(const char* rotated_video_path)
{
    ag_video_decoder* decoder = nullptr;
    assert(ag_video_decoder_create(&decoder) == AG_OK);
    assert(ag_video_decoder_open(decoder, rotated_video_path) == AG_OK);
    ag_video_frame frame = empty_frame();
    assert(ag_video_decoder_read(decoder, &frame) == AG_OK);
    assert(frame.width == 320);
    assert(frame.height == 180);
    assert(frame.sar_num == 1);
    assert(frame.sar_den == 1);
    assert(frame.rotation_degrees == 90);
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

void test_cross_thread_cancellation_and_reset(const char* video_path)
{
    ag_video_decoder* decoder = nullptr;
    assert(ag_video_decoder_create(&decoder) == AG_OK);

    // Cancellation is safe from another thread and is observed by the next
    // potentially blocking operation.
    std::thread cancel_before_open([decoder] { ag_video_decoder_cancel(decoder); });
    cancel_before_open.join();
    assert(ag_video_decoder_open(decoder, video_path) == AG_CANCELLED);

    ag_video_decoder_close(decoder); // resets cancellation for reuse
    assert(ag_video_decoder_open(decoder, video_path) == AG_OK);
    std::thread cancel_before_read([decoder] { ag_video_decoder_cancel(decoder); });
    cancel_before_read.join();
    ag_video_frame frame = empty_frame();
    assert(ag_video_decoder_read(decoder, &frame) == AG_CANCELLED);

    ag_video_decoder_close(decoder);
    assert(ag_video_decoder_open(decoder, video_path) == AG_OK);
    std::thread cancel_before_seek([decoder] { ag_video_decoder_cancel(decoder); });
    cancel_before_seek.join();
    assert(ag_video_decoder_seek(decoder, 500) == AG_CANCELLED);

    ag_video_decoder_close(decoder);
    assert(ag_video_decoder_open(decoder, video_path) == AG_OK);
    frame = empty_frame();
    assert(ag_video_decoder_read(decoder, &frame) == AG_OK);
    ag_video_decoder_destroy(decoder);
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
    assert(argc == 5);
    const std::filesystem::path video_with_audio = argv[1];
    test_null_and_state_boundaries(argv[1]);
    test_open_read_and_eof(argv[1]);
    test_open_read_and_eof(argv[2]);
    test_seek(argv[2]);
    test_rotation(argv[4]);
    test_unsupported_and_corrupt_inputs(video_with_audio, argv[3]);
    test_cross_thread_cancellation_and_reset(argv[2]);
    test_one_hundred_complete_lifecycles(argv[2]);
    return 0;
}
