#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

// Qt Quick ShaderEffect uniform contract. The source texture already contains
// horizontal threshold/blur output. terrainSource is the original premultiplied
// TerrainReactorItem texture and is used only to remove the bright core. The
// remaining perimeter uses alpha zero intentionally: Qt Quick's premultiplied
// source-over blend then becomes bounded additive RGB without covering facets.
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float glowIntensity;
    float threshold;
    vec2 texelStep;
} ubuf;

layout(binding = 1) uniform sampler2D source;
layout(binding = 2) uniform sampler2D terrainSource;

void main()
{
    vec4 bloom = texture(source, qt_TexCoord0) * 0.227027;
    bloom += texture(source, qt_TexCoord0 + ubuf.texelStep * 1.384615)
           * 0.316216;
    bloom += texture(source, qt_TexCoord0 - ubuf.texelStep * 1.384615)
           * 0.316216;
    bloom += texture(source, qt_TexCoord0 + ubuf.texelStep * 3.230769)
           * 0.070270;
    bloom += texture(source, qt_TexCoord0 - ubuf.texelStep * 3.230769)
           * 0.070270;

    vec4 terrain = texture(terrainSource, qt_TexCoord0);
    float terrainBrightness = max(terrain.r, max(terrain.g, terrain.b));
    float terrainCore = terrain.a
        * smoothstep(ubuf.threshold, min(1.0, ubuf.threshold + 0.28),
                     terrainBrightness);
    float halo = max(0.0, bloom.a - terrainCore);
    float strength = 0.08 * pow(clamp(ubuf.glowIntensity, 0.0, 2.0), 0.8);
    float additiveEnergy = min(0.025, halo * strength);
    vec3 tint = bloom.rgb / max(bloom.a, 0.0001);
    tint = clamp(tint / (vec3(1.0) + tint * 0.25), vec3(0.0), vec3(1.0));
    fragColor = vec4(tint * additiveEnergy * ubuf.qt_Opacity, 0.0);
}
