#version 440

layout(location = 0) in float localX;
layout(location = 1) in float vertexAlphaRole;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    vec4 baseColor;
    vec4 alphas;
    vec4 progress;
} ubuf;

void main()
{
    float feather = max(ubuf.progress.y, 0.001);
    float played = 1.0 - smoothstep(ubuf.progress.x - feather,
                                    ubuf.progress.x + feather,
                                    localX);
    float gain = mix(ubuf.progress.z, 1.0, played);
    float luminance = dot(ubuf.baseColor.rgb,
                          vec3(0.2126, 0.7152, 0.0722));
    vec3 unplayedRgb = mix(ubuf.baseColor.rgb, vec3(luminance),
                           ubuf.progress.w);
    vec3 outputRgb = mix(unplayedRgb, ubuf.baseColor.rgb, played);
    float roleAlpha = mix(ubuf.alphas.x, ubuf.alphas.y,
                          clamp(vertexAlphaRole, 0.0, 1.0));
    float alpha = roleAlpha * ubuf.alphas.z * gain * ubuf.alphas.w;
    fragColor = vec4(outputRgb * alpha, alpha);
}
