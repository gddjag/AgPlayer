#include "qmc_key_codec.hpp"

extern "C" {
#include <libavutil/base64.h>
}

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

// Compatible with the MIT-licensed Unlock Music QMC key implementation
// (Copyright (c) 2019-2023 MengYX). See LICENSES/unlock-music.txt.
namespace agplayer {
namespace {
constexpr std::uint32_t kTeaDelta = 0x9e3779b9U;
constexpr std::string_view kV2Prefix = "QQMusic EncV2,Key:";
constexpr std::array<std::uint8_t, 16> kV2Key1{
    0x33,0x38,0x36,0x5a,0x4a,0x59,0x21,0x40,
    0x23,0x2a,0x24,0x25,0x5e,0x26,0x29,0x28};
constexpr std::array<std::uint8_t, 16> kV2Key2{
    0x2a,0x2a,0x23,0x21,0x28,0x23,0x24,0x25,
    0x26,0x5e,0x61,0x31,0x63,0x5a,0x2c,0x54};

std::vector<std::uint8_t> base64_decode(const std::string_view text)
{
    if (text.empty() || text.size() > 8192) return {};
    std::vector<std::uint8_t> output(text.size());
    const int count = av_base64_decode(output.data(),
                                       std::string(text).c_str(),
                                       static_cast<int>(output.size()));
    if (count < 0) return {};
    output.resize(static_cast<std::size_t>(count));
    return output;
}

std::uint32_t be32(const std::uint8_t* source) noexcept
{
    return (static_cast<std::uint32_t>(source[0]) << 24)
        | (static_cast<std::uint32_t>(source[1]) << 16)
        | (static_cast<std::uint32_t>(source[2]) << 8)
        | source[3];
}

void put_be32(std::uint8_t* destination, const std::uint32_t value) noexcept
{
    destination[0] = static_cast<std::uint8_t>(value >> 24);
    destination[1] = static_cast<std::uint8_t>(value >> 16);
    destination[2] = static_cast<std::uint8_t>(value >> 8);
    destination[3] = static_cast<std::uint8_t>(value);
}

void decrypt_block(const std::uint8_t* input, std::uint8_t* output,
                   const std::array<std::uint8_t, 16>& key) noexcept
{
    const std::array<std::uint32_t, 4> words{
        be32(key.data()), be32(key.data() + 4),
        be32(key.data() + 8), be32(key.data() + 12)};
    std::uint32_t hi = be32(input);
    std::uint32_t lo = be32(input + 4);
    std::uint32_t sum = kTeaDelta * 16U;
    for (int round = 0; round < 16; ++round) {
        lo -= ((hi << 4) + words[2]) ^ (sum + hi)
            ^ ((hi >> 5) + words[3]);
        hi -= ((lo << 4) + words[0]) ^ (sum + lo)
            ^ ((lo >> 5) + words[1]);
        sum -= kTeaDelta;
    }
    put_be32(output, hi);
    put_be32(output + 4, lo);
}

bool decrypt_tea(const std::vector<std::uint8_t>& input,
                 const std::array<std::uint8_t, 16>& key,
                 std::vector<std::uint8_t>& output)
{
    if (input.size() < 16 || input.size() % 8 != 0) return false;
    std::vector<std::uint8_t> plain(input.size());
    std::array<std::uint8_t, 8> previous_cipher{};
    std::array<std::uint8_t, 8> previous_intermediate{};
    for (std::size_t offset = 0; offset < input.size(); offset += 8) {
        std::array<std::uint8_t, 8> mixed{};
        for (std::size_t i = 0; i < 8; ++i) {
            mixed[i] = input[offset + i] ^ previous_intermediate[i];
        }
        std::array<std::uint8_t, 8> intermediate{};
        decrypt_block(mixed.data(), intermediate.data(), key);
        for (std::size_t i = 0; i < 8; ++i) {
            plain[offset + i] = intermediate[i] ^ previous_cipher[i];
            previous_cipher[i] = input[offset + i];
        }
        previous_intermediate = intermediate;
    }
    const std::size_t start = 1 + (plain[0] & 7U) + 2;
    const std::size_t end = plain.size() - 7;
    if (start > end || !std::all_of(plain.begin() + end, plain.end(),
                                  [](std::uint8_t byte) { return byte == 0; })) {
        return false;
    }
    output.assign(plain.begin() + start, plain.begin() + end);
    return true;
}
} // namespace

bool decode_qmc_embedded_key(const std::string_view encoded,
                             std::vector<std::uint8_t>& key)
{
    key.clear();
    std::vector<std::uint8_t> raw = base64_decode(encoded);
    if (raw.size() < 16) return false;
    if (raw.size() >= kV2Prefix.size()
        && std::equal(kV2Prefix.begin(), kV2Prefix.end(), raw.begin())) {
        std::vector<std::uint8_t> layer(raw.begin() + kV2Prefix.size(), raw.end());
        std::vector<std::uint8_t> decoded;
        if (!decrypt_tea(layer, kV2Key1, decoded)
            || !decrypt_tea(decoded, kV2Key2, layer)) return false;
        const auto nul = std::find(layer.begin(), layer.end(), 0);
        raw = base64_decode(std::string_view(
            reinterpret_cast<const char*>(layer.data()),
            static_cast<std::size_t>(nul - layer.begin())));
        if (raw.size() < 16) return false;
    }
    std::array<std::uint8_t, 16> tea_key{};
    for (std::size_t i = 0; i < 8; ++i) {
        const float angle = 106.0f + static_cast<float>(i) * 0.1f;
        const float value = std::fabs(std::tan(angle)) * 100.0f;
        tea_key[2 * i] = static_cast<std::uint8_t>(
            static_cast<unsigned int>(value) & 0xffU);
        tea_key[2 * i + 1] = raw[i];
    }
    std::vector<std::uint8_t> encrypted(raw.begin() + 8, raw.end());
    std::vector<std::uint8_t> decoded;
    if (!decrypt_tea(encrypted, tea_key, decoded)) return false;
    key.assign(raw.begin(), raw.begin() + 8);
    key.insert(key.end(), decoded.begin(), decoded.end());
    return !key.empty();
}

} // namespace agplayer
