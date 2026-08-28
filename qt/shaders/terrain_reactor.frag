#version 440

layout(location = 0) in vec3 color;
layout(location = 1) in float light;
layout(location = 2) in float fog;
layout(location = 3) in float opacity;
layout(location = 4) in float glow;
layout(location = 0) out vec4 fragColor;

void main()
{
    vec3 lit = color * light;
    vec3 deepSpace = vec3(0.0, 0.002, 0.008);
    vec3 finalColor = mix(deepSpace, lit, fog);
    float centralGlow = max(max(finalColor.r, finalColor.g), finalColor.b);
    finalColor += color * centralGlow * (0.18 + glow * 0.42);
    fragColor = vec4(finalColor,
                     opacity * clamp(0.18 + fog * 0.82, 0.0, 1.0));
}
