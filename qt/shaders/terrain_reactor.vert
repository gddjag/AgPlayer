#version 440

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;
layout(location = 2) in vec3 instancePosition;
layout(location = 3) in vec3 instanceScale;
layout(location = 4) in vec4 instanceData;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec4 bandsLow;
    vec4 bandsHigh;
    vec4 parameters;
    vec4 effects;
} ubuf;

layout(location = 0) out vec3 color;
layout(location = 1) out float light;
layout(location = 2) out float fog;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main()
{
    float type = instanceData.x;
    float zone = instanceData.y;
    float randomValue = instanceData.z;
    float t = ubuf.parameters.w;
    vec3 position = instancePosition;
    vec3 scale = instanceScale;
    float distanceFromCore = length(position.xz);

    if (type < 0.5) {
        float center = clamp(1.0 - distanceFromCore / 72.0, 0.0, 1.0);
        float core = pow(center, 1.42);
        float bass = ubuf.bandsLow.x * core * 7.0
                   + ubuf.bandsLow.y * center * (2.1 + randomValue * 2.9);
        float mids = ubuf.bandsLow.z * randomValue * 2.2
                   + ubuf.bandsLow.w * (0.7 + 0.3 * sin(position.x * 0.08 + t * 0.8)) * 2.0;
        float highSpike = ubuf.bandsHigh.x
                        * (randomValue > 0.81 ? 3.4 : 0.35) * center;
        float idle = 0.08 + 0.16 * sin(distanceFromCore * 0.07 - t * 0.4 + randomValue * 3.0);
        float rippleCount = max(1.0, ubuf.effects.w);
        float rippleSpacing = 96.0 / rippleCount;
        float rippleRadius = mod(t * 13.5, rippleSpacing);
        float ringDistance = abs(mod(distanceFromCore - rippleRadius
                                   + rippleSpacing * 0.5, rippleSpacing)
                               - rippleSpacing * 0.5);
        float ripple = ubuf.parameters.z * exp(-(ringDistance * ringDistance) / 25.0) * 4.1;
        float height = clamp(idle + bass + mids + highSpike + ripple, 0.035, 18.0);
        scale.y = height;
        position.y += height * 0.5;
    } else if (type < 1.5) {
        position.y += sin(t * 0.74 + randomValue * 18.0) * 1.95
                    + ubuf.bandsLow.x * 2.2;
        scale *= 1.0 + ubuf.parameters.z * 0.28;
    } else if (type < 2.5) {
        float fall = mod(t * (5.0 + ubuf.effects.y * 18.0) + randomValue * 31.0, 46.0);
        position.y -= fall;
        position.x += fall * 0.13;
        scale.y *= 1.0 + ubuf.effects.y * 1.7;
    } else {
        float burst = ubuf.effects.x * (0.2 + randomValue);
        position.xz += vec2(cos(randomValue * 31.0), sin(randomValue * 31.0))
                     * mod(t * 3.0 + randomValue * 9.0, 8.0) * burst;
        position.y += abs(sin(t * 2.0 + randomValue * 12.0)) * 6.0 * burst;
    }

    vec3 cool = vec3(0.17, 0.66, 1.0);
    vec3 warm = vec3(1.0, 0.26, 0.35);
    vec3 accent = vec3(0.96, 0.24, 0.66);
    vec3 peak = vec3(0.82, 0.98, 1.0);
    if (zone < 0.5) color = vec3(0.015, 0.025, 0.055);
    else if (zone < 1.5) color = cool;
    else if (zone < 2.5) color = warm;
    else if (zone < 3.5) color = accent;
    else color = peak;
    if (distanceFromCore < 13.0 && type < 0.5) {
        color = mix(color, peak, clamp(1.0 - distanceFromCore / 13.0, 0.0, 1.0));
    }
    if (type > 1.5) color = mix(color, vec3(1.0), 0.62);

    vec3 worldPosition = position + vertexPosition * scale;
    gl_Position = ubuf.mvp * vec4(worldPosition, 1.0);
    light = 0.34 + 0.66 * max(dot(normalize(vertexNormal),
                                  normalize(vec3(-0.35, 0.82, 0.42))), 0.0);
    fog = clamp(1.0 - distanceFromCore / 118.0, 0.0, 1.0);
}
