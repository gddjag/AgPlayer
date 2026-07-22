#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ag_player ag_player;
typedef struct ag_metadata ag_metadata;

typedef enum ag_result {
    AG_OK = 0,
    AG_INVALID_ARGUMENT = 1,
    AG_IO_ERROR = 2,
    AG_UNSUPPORTED_FORMAT = 3,
    AG_DECODE_ERROR = 4,
    AG_DEVICE_ERROR = 5,
    AG_CANCELLED = 6,
    AG_INTERNAL_ERROR = 7
} ag_result;

ag_result ag_player_create(ag_player** out_player);
void ag_player_destroy(ag_player* player);
ag_result ag_player_last_error(const ag_player* player,
                               char* buffer,
                               size_t capacity,
                               size_t* required);

ag_result ag_metadata_open(const char* utf8_path, ag_metadata** out_metadata);
void ag_metadata_destroy(ag_metadata* metadata);
const char* ag_metadata_title(const ag_metadata* metadata);
const char* ag_metadata_artist(const ag_metadata* metadata);
const char* ag_metadata_album(const ag_metadata* metadata);
const char* ag_metadata_format(const ag_metadata* metadata);
int ag_metadata_sample_rate(const ag_metadata* metadata);
int ag_metadata_channels(const ag_metadata* metadata);
int ag_metadata_bits_per_sample(const ag_metadata* metadata);
long long ag_metadata_bit_rate(const ag_metadata* metadata);
long long ag_metadata_duration_ms(const ag_metadata* metadata);
const unsigned char* ag_metadata_cover(const ag_metadata* metadata,
                                       size_t* size,
                                       const char** mime_type);

#ifdef __cplusplus
}
#endif
