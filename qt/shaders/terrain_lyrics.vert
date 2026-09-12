#version 440
layout(std140, binding = 0) uniform LyricsUniforms {
    mat4 mvp;
    vec4 params;
    vec4 geometry;
} ubuf;
layout(location = 0) out vec2 uv;
void main() {
    const vec2 corners[6] = vec2[6](vec2(0,0),vec2(1,0),vec2(0,1),vec2(0,1),vec2(1,0),vec2(1,1));
    int segment = gl_VertexIndex / 6;
    vec2 corner = corners[gl_VertexIndex % 6];
    uv = vec2((float(segment) + corner.x) / 64.0, corner.y);
    float angle = (uv.x - 0.5) * 1.16;
    // Original 52-unit cylinder, width factor for 27 x 120px, world scale 5.
    float radius = 280.05;
    vec3 local = vec3(sin(angle) * radius, (0.5 - uv.y) * 72.0,
                      -cos(angle) * radius) * ubuf.geometry.x;
    float rotation = -0.78539816339 - ubuf.params.z;
    float orbit = atan(-122.72792206, 97.27207794) + ubuf.params.z;
    float distance = length(vec2(97.27207794, -122.72792206)) * ubuf.geometry.y;
    vec3 world = vec3(cos(orbit)*distance, 24.0 + ubuf.params.w, sin(orbit)*distance)
        + vec3(cos(rotation)*local.x + sin(rotation)*local.z, local.y,
               -sin(rotation)*local.x + cos(rotation)*local.z);
    gl_Position = ubuf.mvp * vec4(world, 1);
}
