#include <agplayer/c_api.h>

#include "core_context.hpp"
#include "decoder.hpp"

#include <cstring>
#include <new>

struct ag_player {
    agplayer::CoreContext context;
};

struct ag_metadata {
    agplayer::MediaMetadata value;
};

ag_result ag_player_create(ag_player** out_player)
{
    if (out_player == nullptr) {
        return AG_INVALID_ARGUMENT;
    }

    *out_player = new (std::nothrow) ag_player{};
    return *out_player == nullptr ? AG_INTERNAL_ERROR : AG_OK;
}

void ag_player_destroy(ag_player* player)
{
    delete player;
}

ag_result ag_player_last_error(const ag_player* player,
                               char* buffer,
                               const size_t capacity,
                               size_t* required)
{
    if (player == nullptr || required == nullptr) {
        return AG_INVALID_ARGUMENT;
    }

    const std::string& message = player->context.last_error();
    *required = message.size() + 1U;

    if (capacity == 0U) {
        return AG_OK;
    }
    if (buffer == nullptr || capacity < *required) {
        return AG_INVALID_ARGUMENT;
    }

    std::memcpy(buffer, message.c_str(), *required);
    return AG_OK;
}

ag_result ag_metadata_open(const char* utf8_path, ag_metadata** out_metadata)
{
    if (out_metadata == nullptr) {
        return AG_INVALID_ARGUMENT;
    }

    *out_metadata = nullptr;
    if (utf8_path == nullptr || utf8_path[0] == '\0') {
        return AG_INVALID_ARGUMENT;
    }

    try {
        agplayer::Decoder decoder;
        const ag_result result = decoder.open(utf8_path);
        if (result != AG_OK) {
            return result;
        }
        *out_metadata = new (std::nothrow) ag_metadata{decoder.metadata()};
        return *out_metadata == nullptr ? AG_INTERNAL_ERROR : AG_OK;
    } catch (...) {
        return AG_INTERNAL_ERROR;
    }
}

void ag_metadata_destroy(ag_metadata* metadata)
{
    delete metadata;
}

const char* ag_metadata_title(const ag_metadata* metadata)
{
    return metadata == nullptr ? "" : metadata->value.title.c_str();
}

const char* ag_metadata_artist(const ag_metadata* metadata)
{
    return metadata == nullptr ? "" : metadata->value.artist.c_str();
}

const char* ag_metadata_album(const ag_metadata* metadata)
{
    return metadata == nullptr ? "" : metadata->value.album.c_str();
}

const char* ag_metadata_format(const ag_metadata* metadata)
{
    return metadata == nullptr ? "" : metadata->value.format.c_str();
}

int ag_metadata_sample_rate(const ag_metadata* metadata)
{
    return metadata == nullptr ? 0 : metadata->value.sample_rate;
}

int ag_metadata_channels(const ag_metadata* metadata)
{
    return metadata == nullptr ? 0 : metadata->value.channels;
}

int ag_metadata_bits_per_sample(const ag_metadata* metadata)
{
    return metadata == nullptr ? 0 : metadata->value.bits_per_sample;
}

long long ag_metadata_bit_rate(const ag_metadata* metadata)
{
    return metadata == nullptr ? 0 : metadata->value.bit_rate;
}

long long ag_metadata_duration_ms(const ag_metadata* metadata)
{
    return metadata == nullptr ? 0 : metadata->value.duration_ms;
}

const unsigned char* ag_metadata_cover(const ag_metadata* metadata,
                                       size_t* size,
                                       const char** mime_type)
{
    if (size != nullptr) {
        *size = metadata == nullptr ? 0U : metadata->value.cover.size();
    }
    if (mime_type != nullptr) {
        *mime_type = metadata == nullptr
                         ? ""
                         : metadata->value.cover_mime_type.c_str();
    }
    return metadata == nullptr || metadata->value.cover.empty()
               ? nullptr
               : metadata->value.cover.data();
}
