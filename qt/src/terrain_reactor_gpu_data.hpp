#pragma once

#include <QtGlobal>
#include <array>
#include <cstddef>

namespace agplayer::terrain::gpu {

constexpr quint32 maximumInstances = 224U * 224U + 120U
    + 28U * (1U + 3U + 16U + 12U) + 1600U;

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
    // Native mode uses the first eight entries with age + 32 * palette index.
    // Reference-ripple mode consumes all ten as x/z, age seconds, signed strength.
    float waveSources[10][4]{};
    float audioEnvelope[4]{}; // fast bass, slow bass, beat strength, beat age
    float cameraPosition[4]{}; // world-space eye position
    float materialParameters[4]{}; // mode, softness, elasticity, ink density
    float sceneControls[4]{}; // column opacity, exposure, stage half extent, column size
    float waveParameters[4]{}; // strength, width, decay speed, terrain smoothness descriptor
    float sceneLighting[4]{}; // inner light, light spill, light radius, terrain density descriptor
    float lightMvp[16]{};
    float shadowParameters[4]{}; // enabled, texel size, depth zero-to-one, texture Y flip
    // Display-encoded colors, like colors above. A=0 selects the palette
    // fallback; A=1 supplies an explicit material/atmosphere color.
    // Body A=2 additionally confines warm colors to the raised central interior.
    float bodyColor[4]{};
    float atmosphereColor[4]{};
    float timbre[4]{}; // warmth, brightness, sharpness, reserved; normalized descriptors
    // Display-encoded reference ripple tint. A > .5 selects reference ripple
    // semantics; A == 0 preserves the native travelling-wave interpretation.
    float rippleColor[4]{};
    float meteorTrajectory[4]{}; // x,z,start height,fall speed; appended internal ABI
    float floatingParameters[4]{}; // pulse,sizeMix,scale,intensity
    float meteorMaterialColor[4]{}; // working-linear RGB; A selects live shared material
};

static_assert(alignof(UniformBlock) == 16);
static_assert(sizeof(UniformBlock) % 16 == 0);
static_assert(sizeof(UniformBlock) == 800, "Terrain UBO layout must match GLSL including shared meteor material");
static_assert(offsetof(UniformBlock, meteorMaterialColor) == 784, "Append material without shifting existing uniforms");
static_assert(offsetof(UniformBlock, meteorTrajectory) == 752, "Append trajectory without shifting existing uniforms");
static_assert(offsetof(UniformBlock, floatingParameters) == 768, "Append floating state without shifting existing uniforms");

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
