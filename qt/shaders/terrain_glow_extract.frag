#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

// Qt Quick ShaderEffect uniform contract. Keep qt_Matrix and qt_Opacity first;
// QML appends threshold and texelStep by matching these member names.
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float threshold;
    vec2 texelStep;
} ubuf;

layout(binding = 1) uniform sampler2D source;

vec4 highlightedSample(vec2 uv)
{
    vec4 sampleColor = texture(source, uv);
    float brightness = max(sampleColor.r,
                           max(sampleColor.g, sampleColor.b));
    float gate = smoothstep(ubuf.threshold,
                            min(1.0, ubuf.threshold + 0.28), brightness);
    float coverage = sampleColor.a * gate;
    vec3 straightColor = sampleColor.rgb / max(sampleColor.a, 0.0001);
    return vec4(straightColor * coverage, coverage);
}

void main()
{
    vec4 bloom = highlightedSample(qt_TexCoord0) * 0.227027;
    bloom += highlightedSample(qt_TexCoord0 + ubuf.texelStep * 1.384615)
           * 0.316216;
    bloom += highlightedSample(qt_TexCoord0 - ubuf.texelStep * 1.384615)
           * 0.316216;
    bloom += highlightedSample(qt_TexCoord0 + ubuf.texelStep * 3.230769)
           * 0.070270;
    bloom += highlightedSample(qt_TexCoord0 - ubuf.texelStep * 3.230769)
           * 0.070270;
    fragColor = bloom * ubuf.qt_Opacity;
}
