extern "C" {
#include <libavcodec/avcodec.h>
}

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

int main()
{
    return avcodec_version() > 0U && ma_version_string() != nullptr ? 0 : 1;
}
