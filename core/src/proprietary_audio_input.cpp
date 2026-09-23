#include "proprietary_audio_input.hpp"
#include "decoder.hpp"
#include "kgm_cipher_tables.hpp"
#include "qmc_key_codec.hpp"

extern "C" {
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavutil/aes.h>
#include <libavutil/avutil.h>
#include <libavutil/base64.h>
#include <libavutil/mem.h>
}

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

// QMC static substitution table adapted from the MIT-licensed Unlock Music
// implementation (Copyright (c) 2019-2023 MengYX). See LICENSES/unlock-music.txt.
namespace agplayer {
namespace {
constexpr std::array<std::uint8_t, 256> kQmcBox{{
    0x77, 0x48, 0x32, 0x73, 0xDE, 0xF2, 0xC0, 0xC8, 0x95, 0xEC, 0x30, 0xB2, 0x51, 0xC3, 0xE1, 0xA0,
    0x9E, 0xE6, 0x9D, 0xCF, 0xFA, 0x7F, 0x14, 0xD1, 0xCE, 0xB8, 0xDC, 0xC3, 0x4A, 0x67, 0x93, 0xD6,
    0x28, 0xC2, 0x91, 0x70, 0xCA, 0x8D, 0xA2, 0xA4, 0xF0, 0x08, 0x61, 0x90, 0x7E, 0x6F, 0xA2, 0xE0,
    0xEB, 0xAE, 0x3E, 0xB6, 0x67, 0xC7, 0x92, 0xF4, 0x91, 0xB5, 0xF6, 0x6C, 0x5E, 0x84, 0x40, 0xF7,
    0xF3, 0x1B, 0x02, 0x7F, 0xD5, 0xAB, 0x41, 0x89, 0x28, 0xF4, 0x25, 0xCC, 0x52, 0x11, 0xAD, 0x43,
    0x68, 0xA6, 0x41, 0x8B, 0x84, 0xB5, 0xFF, 0x2C, 0x92, 0x4A, 0x26, 0xD8, 0x47, 0x6A, 0x7C, 0x95,
    0x61, 0xCC, 0xE6, 0xCB, 0xBB, 0x3F, 0x47, 0x58, 0x89, 0x75, 0xC3, 0x75, 0xA1, 0xD9, 0xAF, 0xCC,
    0x08, 0x73, 0x17, 0xDC, 0xAA, 0x9A, 0xA2, 0x16, 0x41, 0xD8, 0xA2, 0x06, 0xC6, 0x8B, 0xFC, 0x66,
    0x34, 0x9F, 0xCF, 0x18, 0x23, 0xA0, 0x0A, 0x74, 0xE7, 0x2B, 0x27, 0x70, 0x92, 0xE9, 0xAF, 0x37,
    0xE6, 0x8C, 0xA7, 0xBC, 0x62, 0x65, 0x9C, 0xC2, 0x08, 0xC9, 0x88, 0xB3, 0xF3, 0x43, 0xAC, 0x74,
    0x2C, 0x0F, 0xD4, 0xAF, 0xA1, 0xC3, 0x01, 0x64, 0x95, 0x4E, 0x48, 0x9F, 0xF4, 0x35, 0x78, 0x95,
    0x7A, 0x39, 0xD6, 0x6A, 0xA0, 0x6D, 0x40, 0xE8, 0x4F, 0xA8, 0xEF, 0x11, 0x1D, 0xF3, 0x1B, 0x3F,
    0x3F, 0x07, 0xDD, 0x6F, 0x5B, 0x19, 0x30, 0x19, 0xFB, 0xEF, 0x0E, 0x37, 0xF0, 0x0E, 0xCD, 0x16,
    0x49, 0xFE, 0x53, 0x47, 0x13, 0x1A, 0xBD, 0xA4, 0xF1, 0x40, 0x19, 0x60, 0x0E, 0xED, 0x68, 0x09,
    0x06, 0x5F, 0x4D, 0xCF, 0x3D, 0x1A, 0xFE, 0x20, 0x77, 0xE4, 0xD9, 0xDA, 0xF9, 0xA4, 0x2B, 0x76,
    0x1C, 0x71, 0xDB, 0x00, 0xBC, 0xFD, 0x0C, 0x6C, 0xA5, 0x47, 0xF7, 0xF6, 0x00, 0x79, 0x4A, 0x11,
}};
constexpr std::string_view kKuwoKey = "MoOtOiTvINGwd2E6n0E1i7L5t2IoOoNk";

std::uint32_t little_u32(const std::uint8_t* data) noexcept
{
    return static_cast<std::uint32_t>(data[0])
        | (static_cast<std::uint32_t>(data[1]) << 8)
        | (static_cast<std::uint32_t>(data[2]) << 16)
        | (static_cast<std::uint32_t>(data[3]) << 24);
}

std::uint32_t big_u32(const std::uint8_t* data) noexcept
{
    return (static_cast<std::uint32_t>(data[0]) << 24)
        | (static_cast<std::uint32_t>(data[1]) << 16)
        | (static_cast<std::uint32_t>(data[2]) << 8)
        | data[3];
}

std::uint64_t little_u64(const std::uint8_t* data) noexcept
{
    std::uint64_t value = 0;
    for (int index = 7; index >= 0; --index) value = (value << 8) | data[index];
    return value;
}

std::string extension_of(const std::string& path)
{
    const std::size_t separator = path.find_last_of("/\\");
    const std::size_t start = separator == std::string::npos ? 0 : separator + 1;
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || dot < start || dot + 1 >= path.size()) {
        return {};
    }
    std::string extension = path.substr(dot + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension;
}

std::string parse_json_string(const std::string_view json, std::size_t quote)
{
    if (quote >= json.size() || json[quote] != '"') return {};
    std::string result;
    for (std::size_t i = quote + 1; i < json.size(); ++i) {
        if (json[i] == '"') return result;
        if (json[i] == '\\' && ++i < json.size()) {
            switch (json[i]) {
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'u': {
                if (i + 4 >= json.size()) return {};
                unsigned codepoint = 0;
                for (int digit = 0; digit < 4; ++digit) {
                    const char hex = json[++i];
                    codepoint <<= 4;
                    if (hex >= '0' && hex <= '9') codepoint |= hex - '0';
                    else if (hex >= 'a' && hex <= 'f') codepoint |= hex - 'a' + 10;
                    else if (hex >= 'A' && hex <= 'F') codepoint |= hex - 'A' + 10;
                    else return {};
                }
                if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                    if (i + 6 >= json.size() || json[i + 1] != '\\'
                        || json[i + 2] != 'u') return {};
                    i += 2;
                    unsigned low = 0;
                    for (int digit = 0; digit < 4; ++digit) {
                        const char hex = json[++i];
                        low <<= 4;
                        if (hex >= '0' && hex <= '9') low |= hex - '0';
                        else if (hex >= 'a' && hex <= 'f') low |= hex - 'a' + 10;
                        else if (hex >= 'A' && hex <= 'F') low |= hex - 'A' + 10;
                        else return {};
                    }
                    if (low < 0xdc00 || low > 0xdfff) return {};
                    codepoint = 0x10000 + ((codepoint - 0xd800) << 10)
                        + (low - 0xdc00);
                } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                    return {};
                }
                if (codepoint < 0x80) result.push_back(static_cast<char>(codepoint));
                else if (codepoint < 0x800) {
                    result.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
                    result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
                } else if (codepoint < 0x10000) {
                    result.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
                    result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
                    result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
                } else {
                    result.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
                    result.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
                    const char continuation = static_cast<char>(
                        0x80 | ((codepoint >> 6) & 0x3f));
                    result.push_back(continuation);
                    result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
                }
                break;
            }
            default: return {};
            }
        } else {
            result.push_back(json[i]);
        }
    }
    return {};
}

std::size_t json_string_end(const std::string_view json, const std::size_t quote)
{
    if (quote >= json.size() || json[quote] != '"') return std::string_view::npos;
    for (std::size_t i = quote + 1; i < json.size(); ++i) {
        if (json[i] == '\\') ++i;
        else if (json[i] == '"') return i + 1;
    }
    return std::string_view::npos;
}

std::size_t json_skip_space(const std::string_view json, std::size_t offset)
{
    while (offset < json.size()
           && std::isspace(static_cast<unsigned char>(json[offset]))) ++offset;
    return offset;
}

std::size_t json_value_end(const std::string_view json, const std::size_t value)
{
    if (value >= json.size()) return std::string_view::npos;
    if (json[value] == '"') return json_string_end(json, value);
    if (json[value] == '[' || json[value] == '{') {
        int depth = 0;
        for (std::size_t i = value; i < json.size(); ++i) {
            if (json[i] == '"') {
                i = json_string_end(json, i);
                if (i == std::string_view::npos) return i;
                --i;
            } else if (json[i] == '[' || json[i] == '{') {
                ++depth;
            } else if (json[i] == ']' || json[i] == '}') {
                if (--depth == 0) return i + 1;
            }
        }
        return std::string_view::npos;
    }
    const std::size_t end = json.find_first_of(",} \t\r\n", value);
    return end == std::string_view::npos ? json.size() : end;
}

std::size_t json_field_position(const std::string_view json,
                                const std::string_view name)
{
    constexpr std::string_view prefix = "music:";
    const std::size_t start = json.substr(0, prefix.size()) == prefix
        ? prefix.size() : 0;
    std::size_t key = json_skip_space(json, start);
    if (key >= json.size() || json[key++] != '{') return std::string_view::npos;
    for (;;) {
        key = json_skip_space(json, key);
        if (key >= json.size() || json[key] == '}') return std::string_view::npos;
        const std::size_t key_end = json_string_end(json, key);
        if (key_end == std::string_view::npos) return key_end;
        std::size_t value = json_skip_space(json, key_end);
        if (value >= json.size() || json[value++] != ':') return std::string_view::npos;
        value = json_skip_space(json, value);
        if (parse_json_string(json, key) == name) return value;
        key = json_value_end(json, value);
        if (key == std::string_view::npos) return key;
        key = json_skip_space(json, key);
        if (key >= json.size() || json[key] != ',') return std::string_view::npos;
        ++key;
    }
}

std::string json_field(const std::string_view json, const std::string_view name)
{
    std::size_t value = json_field_position(json, name);
    if (value == std::string_view::npos) return {};
    if (name != "artist") return parse_json_string(json, value);
    if (value >= json.size() || json[value++] != '[') return {};
    std::string artists;
    for (;;) {
        value = json_skip_space(json, value);
        if (value >= json.size() || json[value] == ']') return artists;
        if (json[value] != '[') return {};
        std::size_t first = json_skip_space(json, value + 1);
        if (first >= json.size() || json[first] != '"') return {};
        const std::string artist = parse_json_string(json, first);
        if (!artist.empty()) {
            if (!artists.empty()) artists += " / ";
            artists += artist;
        }
        value = json_value_end(json, value);
        if (value == std::string_view::npos) return {};
        value = json_skip_space(json, value);
        if (value < json.size() && json[value] == ']') return artists;
        if (value >= json.size() || json[value] != ',') return {};
        ++value;
    }
}

std::string decrypt_ncm_json(std::vector<std::uint8_t> blob)
{
    for (auto& byte : blob) byte ^= 0x63;
    constexpr std::string_view prefix = "163 key(Don't modify):";
    if (blob.size() <= prefix.size()
        || !std::equal(prefix.begin(), prefix.end(), blob.begin())) return {};
    const std::string encoded(reinterpret_cast<const char*>(blob.data()
        + prefix.size()), blob.size() - prefix.size());
    std::vector<std::uint8_t> decoded(encoded.size());
    const int count = av_base64_decode(decoded.data(), encoded.c_str(),
                                       static_cast<int>(decoded.size()));
    if (count <= 0 || count % 16 != 0) return {};
    decoded.resize(static_cast<std::size_t>(count));
    constexpr std::array<std::uint8_t, 16> meta_key{
        '#','1','4','l','j','k','_','!','\\',']','&','0','U','<','\'','('};
    AVAES* aes = av_aes_alloc();
    if (aes == nullptr) return {};
    const int result = av_aes_init(aes, meta_key.data(), 128, 1);
    if (result >= 0) av_aes_crypt(aes, decoded.data(), decoded.data(),
                                  count / 16, nullptr, 1);
    av_free(aes);
    if (result < 0) return {};
    const std::uint8_t pad = decoded.back();
    if (pad == 0 || pad > 16 || pad > decoded.size()
        || !std::all_of(decoded.end() - pad, decoded.end(),
                        [pad](std::uint8_t value) { return value == pad; })) {
        return {};
    }
    return {decoded.begin(), decoded.end() - pad};
}
} // namespace

bool ProprietaryAudioInput::recognizes_path(const std::string& path)
{
    const std::string extension = extension_of(path);
    return extension == "ncm" || extension == "kwm"
        || extension == "kgm" || extension == "kgma" || extension == "vpr"
        || extension == "qmc0" || extension == "qmc2"
        || extension == "qmc3" || extension == "qmc4"
        || extension == "qmc6" || extension == "qmc8"
        || extension == "qmcflac" || extension == "qmcogg"
        || extension == "mflac" || extension == "mflac0"
        || extension == "mgg" || extension == "mgg0"
        || extension == "mgg1" || extension == "mggl"
        || extension == "tkm" || extension == "bkcmp3"
        || extension == "bkcm4a" || extension == "bkcflac"
        || extension == "bkcwav" || extension == "bkcape"
        || extension == "bkcogg" || extension == "bkcwma";
}

std::unique_ptr<ProprietaryAudioInput> ProprietaryAudioInput::open(
    const std::string& path, std::string& error)
{
    error.clear();
    if (!recognizes_path(path)) return nullptr;
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    if (!file) {
        error = "Cannot open proprietary audio file";
        return nullptr;
    }
    auto input = std::unique_ptr<ProprietaryAudioInput>(
        new ProprietaryAudioInput(std::move(file)));
    if (!input->initialize(extension_of(path), error)) return nullptr;
    return input;
}

bool ProprietaryAudioInput::initialize(const std::string& extension,
                                       std::string& error)
{
    file_.seekg(0, std::ios::end);
    const std::streamoff size = file_.tellg();
    if (size < 0) {
        error = "Cannot determine proprietary audio size";
        return false;
    }
    if (extension == "ncm") {
        if (size < 44) {
            error = "Invalid NCM header";
            return false;
        }
        std::array<std::uint8_t, 14> header{};
        file_.seekg(0);
        file_.read(reinterpret_cast<char*>(header.data()), header.size());
        if (std::string_view(reinterpret_cast<char*>(header.data()), 8)
                != "CTENFDAM") {
            error = "Unsupported NCM variant";
            return false;
        }
        const std::uint32_t key_size = little_u32(header.data() + 10);
        if (key_size == 0 || key_size > 1U << 20
            || static_cast<std::uint64_t>(key_size) + 30 > static_cast<std::uint64_t>(size)
            || key_size % 16 != 0) {
            error = "Invalid NCM key length";
            return false;
        }
        std::vector<std::uint8_t> key_blob(key_size);
        file_.read(reinterpret_cast<char*>(key_blob.data()), key_blob.size());
        for (auto& value : key_blob) value ^= 0x64;
        constexpr std::array<std::uint8_t, 16> core_key{
            'h','z','H','R','A','m','s','o','5','k','I','n','b','a','x','W'};
        AVAES* aes = av_aes_alloc();
        if (aes == nullptr) {
            error = "NCM key allocation failed";
            return false;
        }
        const int aes_result = av_aes_init(aes, core_key.data(), 128, 1);
        if (aes_result >= 0) {
            av_aes_crypt(aes, key_blob.data(), key_blob.data(),
                         static_cast<int>(key_blob.size() / 16), nullptr, 1);
        }
        av_free(aes);
        const std::uint8_t pad = key_blob.back();
        if (aes_result < 0 || pad == 0 || pad > 16
            || key_blob.size() < static_cast<std::size_t>(pad) + 18
            || !std::all_of(key_blob.end() - pad, key_blob.end(),
                            [pad](std::uint8_t value) { return value == pad; })) {
            error = "Invalid NCM embedded key";
            return false;
        }
        const std::size_t key_end = key_blob.size() - pad;
        const std::size_t stream_key_size = key_end - 17;
        for (std::size_t i = 0; i < ncm_box_.size(); ++i) {
            ncm_box_[i] = static_cast<std::uint8_t>(i);
        }
        std::uint8_t j = 0;
        for (std::size_t i = 0; i < ncm_box_.size(); ++i) {
            j = static_cast<std::uint8_t>(
                j + ncm_box_[i] + key_blob[17 + i % stream_key_size]);
            std::swap(ncm_box_[i], ncm_box_[j]);
        }
        std::array<std::uint8_t, 4> length{};
        file_.read(reinterpret_cast<char*>(length.data()), length.size());
        const std::uint32_t metadata_size = little_u32(length.data());
        const std::uint64_t image_header = 14ULL + key_size + 4ULL
            + metadata_size + 5ULL;
        if (metadata_size > 64U * 1024U * 1024U
            || image_header + 8 > static_cast<std::uint64_t>(size)) {
            error = "Invalid NCM metadata length";
            return false;
        }
        if (metadata_size > 0 && metadata_size <= 4U * 1024U * 1024U) {
            std::vector<std::uint8_t> blob(metadata_size);
            file_.read(reinterpret_cast<char*>(blob.data()), blob.size());
            const std::string json = decrypt_ncm_json(std::move(blob));
            container_title_ = json_field(json, "musicName");
            container_artist_ = json_field(json, "artist");
            container_album_ = json_field(json, "album");
        }
        file_.seekg(static_cast<std::streamoff>(image_header));
        std::array<std::uint8_t, 8> image_lengths{};
        file_.read(reinterpret_cast<char*>(image_lengths.data()),
                   image_lengths.size());
        const std::uint32_t image_space = little_u32(image_lengths.data());
        const std::uint32_t image_size = little_u32(image_lengths.data() + 4);
        payload_offset_ = image_header + 8ULL + image_space;
        if (image_size > image_space
            || payload_offset_ >= static_cast<std::uint64_t>(size)) {
            error = "Invalid NCM audio offset";
            return false;
        }
        if (image_size > 0 && image_size <= 32U * 1024U * 1024U) {
            container_cover_.resize(image_size);
            file_.read(reinterpret_cast<char*>(container_cover_.data()),
                       container_cover_.size());
            if (container_cover_.size() >= 8
                && std::equal(container_cover_.begin(),
                              container_cover_.begin() + 8,
                              std::array<std::uint8_t, 8>{
                                  0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a}.begin())) {
                container_cover_mime_ = "image/png";
                if (container_cover_.size() >= 24) {
                    container_cover_width_ = static_cast<int>(
                        big_u32(container_cover_.data() + 16));
                    container_cover_height_ = static_cast<int>(
                        big_u32(container_cover_.data() + 20));
                }
            } else if (container_cover_.size() >= 2
                       && container_cover_[0] == 0xff
                       && container_cover_[1] == 0xd8) {
                container_cover_mime_ = "image/jpeg";
                for (std::size_t marker = 2;
                     marker + 9 < container_cover_.size();) {
                    if (container_cover_[marker] != 0xff) break;
                    const std::uint8_t type = container_cover_[marker + 1];
                    if (type == 0xd9 || type == 0xda) break;
                    if (type == 0x01 || (type >= 0xd0 && type <= 0xd7)) {
                        marker += 2;
                        continue;
                    }
                    const std::size_t segment =
                        (static_cast<std::size_t>(container_cover_[marker + 2]) << 8)
                        | container_cover_[marker + 3];
                    if (segment < 2 || marker + 2 + segment
                        > container_cover_.size()) break;
                    if ((type >= 0xc0 && type <= 0xc3)
                        || (type >= 0xc5 && type <= 0xc7)
                        || (type >= 0xc9 && type <= 0xcb)
                        || (type >= 0xcd && type <= 0xcf)) {
                        container_cover_height_ =
                            (container_cover_[marker + 5] << 8)
                            | container_cover_[marker + 6];
                        container_cover_width_ =
                            (container_cover_[marker + 7] << 8)
                            | container_cover_[marker + 8];
                        break;
                    }
                    marker += segment + 2;
                }
            } else {
                container_cover_.clear();
            }
            if (container_cover_width_ <= 0 || container_cover_height_ <= 0
                || container_cover_width_ > 32768
                || container_cover_height_ > 32768) {
                container_cover_.clear();
                container_cover_mime_.clear();
            }
        }
        cipher_ = Cipher::Ncm;
        payload_size_ = static_cast<std::uint64_t>(size) - payload_offset_;
    } else if (extension == "kgm" || extension == "kgma"
               || extension == "vpr") {
        if (size <= 0x400) {
            error = "Invalid KGM header";
            return false;
        }
        std::array<std::uint8_t, 44> header{};
        file_.seekg(0);
        file_.read(reinterpret_cast<char*>(header.data()), header.size());
        const bool vpr = extension == "vpr";
        const auto& magic = vpr ? kVprHeader : kKgmHeader;
        if (!std::equal(magic.begin(), magic.end(), header.begin())) {
            error = "Unsupported KGM/VPR variant";
            return false;
        }
        payload_offset_ = little_u32(header.data() + 0x10);
        const std::uint32_t version = little_u32(header.data() + 0x14);
        if (version == 5) {
            error = "KGM v5 requires external authorization data";
            return false;
        }
        if (payload_offset_ < 0x400
            || payload_offset_ >= static_cast<std::uint64_t>(size)) {
            error = "Invalid KGM audio offset";
            return false;
        }
        std::copy_n(header.data() + 0x1c, 16, kgm_key_.begin());
        cipher_ = vpr ? Cipher::Vpr : Cipher::Kgm;
        payload_size_ = static_cast<std::uint64_t>(size) - payload_offset_;
    } else if (extension == "kwm") {
        if (size < 32) {
            error = "Invalid KWM header";
            return false;
        }
        std::array<std::uint8_t, 32> header{};
        file_.seekg(0);
        file_.read(reinterpret_cast<char*>(header.data()), header.size());
        const std::string_view magic(reinterpret_cast<const char*>(header.data()), 16);
        if (magic != "yeelion-kuwo-tme" && magic != std::string_view(
                "yeelion-kuwo\0\0\0\0", 16)) {
            if (header[0] == 0xff && (header[1] & 0xf6U) == 0xf0U) {
                cipher_ = Cipher::Passthrough;
                payload_size_ = static_cast<std::uint64_t>(size);
                file_.clear();
                file_.seekg(0);
                return true;
            }
            error = "Unsupported KWM variant";
            return false;
        }
        if (size <= 0x400) {
            error = "Invalid KWM audio length";
            return false;
        }
        const std::string key = std::to_string(little_u64(header.data() + 0x18));
        for (std::size_t i = 0; i < kwm_mask_.size(); ++i) {
            kwm_mask_[i] = static_cast<std::uint8_t>(
                kKuwoKey[i] ^ key[i % key.size()]);
        }
        cipher_ = Cipher::Kwm;
        payload_offset_ = 0x400;
        payload_size_ = static_cast<std::uint64_t>(size) - payload_offset_;
    } else {
        if (size <= 4) {
            error = "Invalid QMC file";
            return false;
        }
        std::array<std::uint8_t, 4> trailer{};
        file_.seekg(size - static_cast<std::streamoff>(trailer.size()));
        file_.read(reinterpret_cast<char*>(trailer.data()), trailer.size());
        if (trailer == std::array<std::uint8_t, 4>{'S', 'T', 'a', 'g'}) {
            error = "QMC file has no embedded key";
            return false;
        }
        const bool qtag = trailer == std::array<std::uint8_t, 4>{'Q', 'T', 'a', 'g'};
        std::uint32_t key_size = 0;
        std::uint64_t trailer_size = 4;
        if (qtag) {
            std::array<std::uint8_t, 4> length{};
            file_.seekg(size - 8);
            file_.read(reinterpret_cast<char*>(length.data()), length.size());
            key_size = big_u32(length.data());
            trailer_size = 8;
        } else {
            key_size = little_u32(trailer.data());
        }
        if (qtag || key_size < 0x400) {
            if (key_size == 0 || key_size > 4096
                || static_cast<std::uint64_t>(key_size) + trailer_size
                    >= static_cast<std::uint64_t>(size)) {
                error = "Invalid QMC embedded key length";
                return false;
            }
            payload_size_ = static_cast<std::uint64_t>(size)
                - key_size - trailer_size;
            std::string encoded(key_size, '\0');
            file_.seekg(static_cast<std::streamoff>(payload_size_));
            file_.read(encoded.data(), encoded.size());
            const std::size_t terminator = encoded.find(qtag ? ',' : '\0');
            if (terminator != std::string::npos) encoded.resize(terminator);
            if (!decode_qmc_embedded_key(encoded, qmc_key_)) {
                error = "Cannot decode QMC embedded key";
                return false;
            }
            if (qmc_key_.size() <= 300) {
                cipher_ = Cipher::QmcMap;
            } else {
                cipher_ = Cipher::QmcRc4;
                const std::size_t count = qmc_key_.size();
                std::vector<std::uint8_t> state(count);
                for (std::size_t i = 0; i < count; ++i) {
                    state[i] = static_cast<std::uint8_t>(i);
                }
                std::size_t j = 0;
                for (std::size_t i = 0; i < count; ++i) {
                    j = (j + state[i] + qmc_key_[i]) % count;
                    std::swap(state[i], state[j]);
                }
                for (const std::uint8_t value : qmc_key_) {
                    if (value == 0) continue;
                    const std::uint32_t next = qmc_hash_ * value;
                    if (next == 0 || next <= qmc_hash_) break;
                    qmc_hash_ = next;
                }
                qmc_keystream_.resize(5120 + count);
                std::size_t i = 0;
                j = 0;
                for (auto& value : qmc_keystream_) {
                    i = (i + 1) % count;
                    j = (j + state[i]) % count;
                    std::swap(state[i], state[j]);
                    value = state[(state[i] + state[j]) % count];
                }
            }
        } else {
            cipher_ = Cipher::QmcStatic;
            payload_size_ = static_cast<std::uint64_t>(size);
        }
    }
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(payload_offset_));
    return true;
}

int ProprietaryAudioInput::read(std::uint8_t* buffer, const int capacity) noexcept
{
    if (buffer == nullptr || capacity <= 0) return AVERROR(EINVAL);
    if (position_ >= payload_size_) return AVERROR_EOF;
    const auto count = static_cast<std::streamsize>(std::min<std::uint64_t>(
        static_cast<std::uint64_t>(capacity), payload_size_ - position_));
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(payload_offset_ + position_));
    file_.read(reinterpret_cast<char*>(buffer), count);
    const auto actual = file_.gcount();
    if (actual <= 0) return AVERROR(EIO);
    for (std::streamsize index = 0; index < actual; ++index) {
        const std::uint64_t offset = position_ + static_cast<std::uint64_t>(index);
        if (cipher_ == Cipher::Passthrough) {
            continue;
        } else if (cipher_ == Cipher::Kwm) {
            buffer[index] ^= kwm_mask_[offset % kwm_mask_.size()];
        } else if (cipher_ == Cipher::Ncm) {
            const std::uint8_t i = static_cast<std::uint8_t>(offset + 1);
            const std::uint8_t si = ncm_box_[i];
            buffer[index] ^= ncm_box_[static_cast<std::uint8_t>(si + ncm_box_[
                static_cast<std::uint8_t>(i + si)])];
        } else if (cipher_ == Cipher::Kgm || cipher_ == Cipher::Vpr) {
            std::uint8_t mixed = buffer[index] ^ kgm_key_[offset % 17];
            mixed ^= static_cast<std::uint8_t>((mixed & 0x0fU) << 4);
            std::uint64_t block = offset >> 4;
            std::uint8_t modifier = 0;
            while (block >= 0x11) {
                modifier ^= kKgmTable1[block % kKgmTable1.size()];
                block >>= 4;
                modifier ^= kKgmTable2[block % kKgmTable2.size()];
                block >>= 4;
            }
            std::uint8_t mask = kKgmPredefinedMask[offset % kKgmPredefinedMask.size()]
                ^ modifier;
            mask ^= static_cast<std::uint8_t>((mask & 0x0fU) << 4);
            buffer[index] = mixed ^ mask;
            if (cipher_ == Cipher::Vpr) {
                buffer[index] ^= kVprMaskDiff[offset % kVprMaskDiff.size()];
            }
        } else if (cipher_ == Cipher::QmcMap) {
            const std::uint64_t adjusted = offset > 0x7fff ? offset % 0x7fff : offset;
            const std::size_t key_index = static_cast<std::size_t>(
                (adjusted * adjusted + 71214) % qmc_key_.size());
            const unsigned rotation = static_cast<unsigned>((key_index + 4) % 8);
            const std::uint8_t value = qmc_key_[key_index];
            buffer[index] ^= static_cast<std::uint8_t>(
                (value << rotation) | (value >> rotation));
        } else if (cipher_ == Cipher::QmcRc4) {
            if (offset < 128) {
                buffer[index] ^= qmc_key_[qmc_segment_key(offset)];
            } else {
                const std::uint64_t segment = offset / 5120;
                const std::uint64_t skip = qmc_segment_key(segment);
                buffer[index] ^= qmc_keystream_[skip + offset % 5120];
            }
        } else {
            const std::uint64_t adjusted = offset > 0x7fff ? offset % 0x7fff : offset;
            buffer[index] ^= kQmcBox[(adjusted * adjusted + 27) & 0xff];
        }
    }
    position_ += static_cast<std::uint64_t>(actual);
    return static_cast<int>(actual);
}

std::uint64_t ProprietaryAudioInput::qmc_segment_key(
    const std::uint64_t id) const noexcept
{
    const std::uint64_t count = qmc_key_.size();
    const std::uint8_t seed = qmc_key_[id % count];
    if (seed == 0) return 0;
    const double index = static_cast<double>(qmc_hash_)
        / (static_cast<double>(id + 1) * seed) * 100.0;
    return static_cast<std::uint64_t>(index) % count;
}

std::int64_t ProprietaryAudioInput::seek(const std::int64_t offset,
                                         const int whence) noexcept
{
    const int origin = whence & ~AVSEEK_FORCE;
    if (origin == AVSEEK_SIZE) return static_cast<std::int64_t>(payload_size_);
    std::int64_t base = 0;
    if (origin == SEEK_CUR) base = static_cast<std::int64_t>(position_);
    else if (origin == SEEK_END) base = static_cast<std::int64_t>(payload_size_);
    else if (origin != SEEK_SET) return AVERROR(EINVAL);
    if ((offset > 0 && base > std::numeric_limits<std::int64_t>::max() - offset)
        || (offset < 0 && offset < -base)) return AVERROR(EINVAL);
    const std::int64_t target = base + offset;
    if (target < 0 || static_cast<std::uint64_t>(target) > payload_size_) {
        return AVERROR(EINVAL);
    }
    position_ = static_cast<std::uint64_t>(target);
    return target;
}

int ProprietaryAudioInput::read_callback(void* opaque, std::uint8_t* buffer,
                                          const int capacity) noexcept
{
    return static_cast<ProprietaryAudioInput*>(opaque)->read(buffer, capacity);
}

std::int64_t ProprietaryAudioInput::seek_callback(void* opaque,
                                                   const std::int64_t offset,
                                                   const int whence) noexcept
{
    return static_cast<ProprietaryAudioInput*>(opaque)->seek(offset, whence);
}

void ProprietaryAudioInput::apply_container_metadata(
    MediaMetadata& metadata) const
{
    if (!container_title_.empty()) metadata.title = container_title_;
    if (!container_artist_.empty()) metadata.artist = container_artist_;
    if (!container_album_.empty()) metadata.album = container_album_;
    if (!container_cover_.empty()) {
        metadata.cover = container_cover_;
        metadata.cover_mime_type = container_cover_mime_;
    }
}

void ProprietaryAudioInput::apply_format_tags(AVFormatContext* format) const
{
    if (format == nullptr) return;
    if (!container_title_.empty()) {
        av_dict_set(&format->metadata, "title", container_title_.c_str(), 0);
    }
    if (!container_artist_.empty()) {
        av_dict_set(&format->metadata, "artist", container_artist_.c_str(), 0);
    }
    if (!container_album_.empty()) {
        av_dict_set(&format->metadata, "album", container_album_.c_str(), 0);
    }
    if (!container_cover_.empty()) {
        bool has_cover = false;
        for (unsigned index = 0; index < format->nb_streams; ++index) {
            has_cover |= (format->streams[index]->disposition
                          & AV_DISPOSITION_ATTACHED_PIC) != 0;
        }
        if (!has_cover) {
            AVStream* stream = avformat_new_stream(format, nullptr);
            if (stream == nullptr) return;
            stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
            stream->codecpar->codec_id = container_cover_mime_ == "image/png"
                ? AV_CODEC_ID_PNG : AV_CODEC_ID_MJPEG;
            stream->codecpar->width = container_cover_width_;
            stream->codecpar->height = container_cover_height_;
            stream->disposition |= AV_DISPOSITION_ATTACHED_PIC;
            if (av_new_packet(&stream->attached_pic,
                              static_cast<int>(container_cover_.size())) < 0) return;
            std::copy(container_cover_.begin(), container_cover_.end(),
                      stream->attached_pic.data);
            stream->attached_pic.stream_index = stream->index;
            stream->attached_pic.flags |= AV_PKT_FLAG_KEY;
        }
    }
}

} // namespace agplayer
