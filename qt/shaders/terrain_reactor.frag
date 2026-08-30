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
    vec3 lit = color * mix(0.78, 1.0, light);
    lit = pow(max(lit, vec3(0.0)), vec3(0.88));
    float luminance = dot(lit, vec3(0.2126, 0.7152, 0.0722));
    lit = max(vec3(0.0), mix(vec3(luminance), lit, 1.18));
    lit = mix(vec3(luminance), lit, focus);
    lit *= 0.62 + focus * 0.38;
    vec3 deepSpace = vec3(0.006, 0.009, 0.017) + color * 0.012;
    float atmosphericVisibility = pow(fog, 0.94) * (0.58 + focus * 0.42);
    vec3 finalColor = mix(deepSpace, lit, atmosphericVisibility);
    float centralGlow = max(max(finalColor.r, finalColor.g), finalColor.b);
    finalColor += color * centralGlow * (0.18 + glow * 0.48);
    float luminousHaze = clamp(glow * 0.21 + centralGlow * 0.09, 0.0, 0.74);
    vec3 hazeTint = mix(color, vec3(1.0, 0.985, 0.975), 0.32);
    finalColor = mix(finalColor, hazeTint,
                     luminousHaze * focus * 0.82);
    fragColor = vec4(finalColor,
                     opacity * clamp(0.18 + fog * 0.82, 0.0, 1.0));
}
