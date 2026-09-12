#include <agplayer/c_api.h>

#include <stddef.h>
#include <stdint.h>

typedef char ag_video_frame_size_is_uint32[
    sizeof(((ag_video_frame*)0)->struct_size) == sizeof(uint32_t) ? 1 : -1];
typedef char ag_video_frame_is_complete[
    offsetof(ag_video_frame, data) > 0 ? 1 : -1];
typedef char ag_video_media_info_size_is_uint32[
    sizeof(((ag_video_media_info*)0)->struct_size) == sizeof(uint32_t) ? 1 : -1];
typedef char ag_video_media_info_is_complete[
    offsetof(ag_video_media_info, has_audio) > 0 ? 1 : -1];

int main(void)
{
    ag_video_decoder* decoder = NULL;
    ag_video_frame frame = {0};
    ag_video_media_info media_info = {0};
    frame.struct_size = (uint32_t)sizeof(frame);
    media_info.struct_size = (uint32_t)sizeof(media_info);
    return decoder == NULL && frame.data == NULL
            && media_info.valid == 0 && media_info.has_video == 0
        ? 0 : 1;
}
