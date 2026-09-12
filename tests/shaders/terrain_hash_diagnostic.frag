#version 440

// Test-only replay diagnostic. It deliberately does not reproduce material
// appearance: flat values isolate per-column hash transport from geometry.
//
// Keep the main-pass v2f signature intact. On D3D11 a sparse consumer showed
// only a coverage-composited random value for opaque pixels even though the
// standalone QSB reflection reported TEXCOORD18. The sentinel branch keeps
// every production semantic live without changing finite replay output.
layout(location = 0) in vec3 color;
layout(location = 1) in float light;
layout(location = 2) in float fog;
layout(location = 3) in float opacity;
layout(location = 4) in float glow;
layout(location = 5) in float focus;
layout(location = 6) in float impactLight;
layout(location = 7) in float topSurface;
layout(location = 8) in float streamSheen;
layout(location = 9) in vec3 worldNormal;
layout(location = 10) in vec3 viewDirection;
layout(location = 11) flat in float objectKind;
layout(location = 12) in vec3 columnExtent;
layout(location = 13) in vec3 surfacePosition;
layout(location = 14) in float musicLight;
layout(location = 15) in vec4 material;
layout(location = 16) in vec3 worldPosition;
layout(location = 17) flat in float reliefHeight;
layout(location = 18) flat in float columnRandom;
layout(location = 19) flat in vec4 travelingWave;
layout(location = 20) flat in vec2 referenceRippleAnim;
layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vec4(clamp(columnRandom, 0.0, 1.0),
                     clamp(reliefHeight / 16.0, 0.0, 1.0),
                     0.0,
                     clamp(opacity, 0.0, 1.0));
    float sentinel = dot(color, vec3(1.0)) + light + fog + opacity + glow + focus + impactLight
        + topSurface + streamSheen + dot(worldNormal, vec3(1.0)) + dot(viewDirection, vec3(1.0))
        + objectKind + dot(columnExtent, vec3(1.0)) + dot(surfacePosition, vec3(1.0)) + musicLight
        + dot(material, vec4(1.0)) + dot(worldPosition, vec3(1.0)) + dot(travelingWave, vec4(1.0))
        + dot(referenceRippleAnim, vec2(1.0));
    if (sentinel < -1.0e20) fragColor = vec4(1.0, 0.0, 1.0, 1.0);
}
