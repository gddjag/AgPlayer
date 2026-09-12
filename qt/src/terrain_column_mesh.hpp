#pragma once

#include "terrain_reactor_gpu_data.hpp"
#include <algorithm>

namespace agplayer::terrain::gpu {

inline constexpr int columnVertexCount = cubeVertexCount;
inline constexpr int columnIndexCount = cubeIndexCount;
inline constexpr int columnTriangleCount = cubeIndexCount / 3;
inline constexpr const auto& columnVertices = cubeVertices;
inline constexpr const auto& columnIndices = cubeIndices;

// Ordinary unit box coordinates: six straight faces with a full flat cap.
// The shared terrain/shadow vertex shader uses the same componentwise scale;
// audio changes height, never rounds the shoulders or expands the cross-section.
inline std::array<float, 3> decodeColumnPosition(const Vertex& vertex,
    std::array<float, 3> extent) noexcept
{
    std::array<float, 3> position{};
    for (size_t axis = 0; axis < 3; ++axis)
        position[axis] = vertex.position[axis] * std::max(0.001F, extent[axis]);
    return position;
}

} // namespace agplayer::terrain::gpu
