#version 440

layout(location = 0) in vec3 color;
layout(location = 1) in float light;
layout(location = 2) in float fog;
layout(location = 3) in float opacity;
layout(location = 4) in float glow;
layout(location = 5) in float focus;
layout(location = 0) out vec4 fragColor;

void main()
{
    vec3 lit = color * light;
    float luminance = dot(lit, vec3(0.2126, 0.7152, 0.0722));
    lit = mix(vec3(luminance), lit, focus);
    lit *= 0.62 + focus * 0.38;
    vec3 deepSpace = vec3(0.006, 0.009, 0.017) + color * 0.012;
    float atmosphericVisibility = pow(fog, 1.35) * (0.48 + focus * 0.52);
    vec3 finalColor = mix(deepSpace, lit, atmosphericVisibility);
    float centralGlow = max(max(finalColor.r, finalColor.g), finalColor.b);
    finalColor += color * centralGlow * (0.18 + glow * 0.56);
    float luminousHaze = clamp(glow * 0.22 + centralGlow * 0.10, 0.0, 0.82);
    finalColor = mix(finalColor, vec3(1.0, 0.985, 0.975),
                     luminousHaze * focus);
    fragColor = vec4(finalColor,
                     opacity * clamp(0.18 + fog * 0.82, 0.0, 1.0));
}
