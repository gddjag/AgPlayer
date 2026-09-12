#version 440
layout(std140, binding = 0) uniform LyricsUniforms {
    mat4 mvp;
    vec4 params;
    vec4 geometry;
} ubuf;
layout(binding = 1) uniform sampler2D oldLyrics;
layout(binding = 2) uniform sampler2D newLyrics;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;
vec4 readLine(sampler2D source, vec2 p) {
    return p.y < 0.0 || p.y > 1.0 ? vec4(0) : texture(source, p);
}
void main() {
    float blend = smoothstep(0.0, 1.0, ubuf.params.x);
    vec4 oldColor = readLine(oldLyrics, uv + vec2(0, blend * 0.22));
    vec4 newColor = readLine(newLyrics, uv - vec2(0, (1.0 - blend) * 0.22));
    fragColor = mix(oldColor, newColor, blend) * ubuf.params.y;
    if (fragColor.a < 0.002) discard;
}
