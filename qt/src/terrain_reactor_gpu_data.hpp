#pragma once

#include <QtGlobal>
#include <array>

namespace agplayer::terrain::gpu {

constexpr int cubeVertexCount = 24;
constexpr int cubeIndexCount = 36;

struct Vertex {
    float position[3];
    float normal[3];
};

struct GpuInstance {
    float position[3];
    float scale[3];
    float data[4];
};

struct alignas(16) UniformBlock {
    float mvp[16]{};
    float bandsLow[4]{};
    float bandsHigh[4]{};
    float parameters[4]{}; // energy, flux, ripple, time
    float effects[4]{}; // particles, meteors, punch, fog
    float colors[5][4]{}; // base, cool, warm, accent, peak
    float equalizerLow[4]{};
    float equalizerHigh[4]{};
    float styleParameters[4]{}; // amplitude, motion, glow, cinema
    float styleDynamics[4]{}; // rotate, peak, color mode, gradient
    float styleToggles[4]{}; // ripples, cubes, meteors, breathing
    float styleExtra[4]{}; // theme cycle, burst, stream highlight, meteor flight age
    float styleAudio[4]{}; // compression, response, range, center highlight
    float stylePresentation[4]{}; // rhythm, depth, clarity, rotation speed
    float impact[4]{}; // core pulse strength/age, selected group + 1 (0=none), sensitivity
    float waveSources[8][4]{}; // stage x/z, age + 32 * palette index (0..3), strength
    float audioEnvelope[4]{}; // fast bass, slow bass, beat strength, beat age
    float cameraPosition[4]{}; // world-space eye position
    float materialParameters[4]{}; // mode, softness, elasticity, ink density
    float sceneControls[4]{}; // column opacity, terrain exposure, reserved
    float waveParameters[4]{}; // strength, width, lifetime factor, reserved
    float sceneLighting[4]{}; // inner light, light spill, light radius, reserved
    float lightMvp[16]{};
    float shadowParameters[4]{}; // enabled, texel size, depth zero-to-one, texture Y flip
};

static_assert(alignof(UniformBlock) == 16);
static_assert(sizeof(UniformBlock) % 16 == 0);

constexpr std::array<Vertex, cubeVertexCount> cubeVertices{{
    {{-0.5F, -0.5F,  0.5F}, { 0.0F,  0.0F,  1.0F}},
    {{ 0.5F, -0.5F,  0.5F}, { 0.0F,  0.0F,  1.0F}},
    {{ 0.5F,  0.5F,  0.5F}, { 0.0F,  0.0F,  1.0F}},
    {{-0.5F,  0.5F,  0.5F}, { 0.0F,  0.0F,  1.0F}},
    {{ 0.5F, -0.5F, -0.5F}, { 0.0F,  0.0F, -1.0F}},
    {{-0.5F, -0.5F, -0.5F}, { 0.0F,  0.0F, -1.0F}},
    {{-0.5F,  0.5F, -0.5F}, { 0.0F,  0.0F, -1.0F}},
    {{ 0.5F,  0.5F, -0.5F}, { 0.0F,  0.0F, -1.0F}},
    {{-0.5F, -0.5F, -0.5F}, {-1.0F,  0.0F,  0.0F}},
    {{-0.5F, -0.5F,  0.5F}, {-1.0F,  0.0F,  0.0F}},
    {{-0.5F,  0.5F,  0.5F}, {-1.0F,  0.0F,  0.0F}},
    {{-0.5F,  0.5F, -0.5F}, {-1.0F,  0.0F,  0.0F}},
    {{ 0.5F, -0.5F,  0.5F}, { 1.0F,  0.0F,  0.0F}},
    {{ 0.5F, -0.5F, -0.5F}, { 1.0F,  0.0F,  0.0F}},
    {{ 0.5F,  0.5F, -0.5F}, { 1.0F,  0.0F,  0.0F}},
    {{ 0.5F,  0.5F,  0.5F}, { 1.0F,  0.0F,  0.0F}},
    {{-0.5F,  0.5F,  0.5F}, { 0.0F,  1.0F,  0.0F}},
    {{ 0.5F,  0.5F,  0.5F}, { 0.0F,  1.0F,  0.0F}},
    {{ 0.5F,  0.5F, -0.5F}, { 0.0F,  1.0F,  0.0F}},
    {{-0.5F,  0.5F, -0.5F}, { 0.0F,  1.0F,  0.0F}},
    {{-0.5F, -0.5F, -0.5F}, { 0.0F, -1.0F,  0.0F}},
    {{ 0.5F, -0.5F, -0.5F}, { 0.0F, -1.0F,  0.0F}},
    {{ 0.5F, -0.5F,  0.5F}, { 0.0F, -1.0F,  0.0F}},
    {{-0.5F, -0.5F,  0.5F}, { 0.0F, -1.0F,  0.0F}},
}};

constexpr std::array<quint16, cubeIndexCount> cubeIndices{{
     0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,
     8,  9, 10,  8, 10, 11, 12, 13, 14, 12, 14, 15,
    16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
}};

} // namespace agplayer::terrain::gpu
