#include <agplayer/c_api.h>

#include <stddef.h>
#include <stdint.h>

typedef char ag_video_frame_size_is_uint32[
    sizeof(((ag_video_frame*)0)->struct_size) == sizeof(uint32_t) ? 1 : -1];
typedef char ag_video_frame_is_complete[
    offsetof(ag_video_frame, data) > 0 ? 1 : -1];

int main(void)
{
    ag_video_decoder* decoder = NULL;
    ag_video_frame frame = {0};
    frame.struct_size = (uint32_t)sizeof(frame);
    return decoder == NULL && frame.data == NULL ? 0 : 1;
}
