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
    vec4 colors[5];
    vec4 equalizerLow;
    vec4 equalizerHigh;
    vec4 styleParameters;
    vec4 styleDynamics;
    vec4 styleToggles;
    vec4 styleExtra;
} ubuf;

layout(location = 0) out vec3 color;
layout(location = 1) out float light;
layout(location = 2) out float fog;
layout(location = 3) out float opacity;
layout(location = 4) out float glow;

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
    vec4 bandsLow = ubuf.bandsLow * ubuf.equalizerLow;
    vec4 bandsHigh = ubuf.bandsHigh * ubuf.equalizerHigh;
    float amplitude = mix(0.25, 1.75, ubuf.styleParameters.x);
    float motion = mix(0.2, 1.8, ubuf.styleParameters.y);
    opacity = 1.0;

    if (type < 0.5) {
        float center = clamp(1.0 - distanceFromCore / 72.0, 0.0, 1.0);
        float core = pow(center, 1.42);
        float bass = bandsLow.x * core * 7.0
                   + bandsLow.y * center * (2.1 + randomValue * 2.9);
        float mids = bandsLow.z * randomValue * 2.2
                   + bandsLow.w * (0.7 + 0.3 * sin(position.x * 0.08 + t * 0.8 * motion)) * 2.0;
        float highSpike = bandsHigh.x
                        * (randomValue > 0.81 ? 3.4 : 0.35)
                        * center * mix(0.5, 1.8, ubuf.styleDynamics.y);
        float idle = 0.08 + 0.16 * sin(distanceFromCore * 0.07 - t * 0.4 + randomValue * 3.0);
        float rippleCount = max(1.0, ubuf.effects.w);
        float rippleSpacing = 96.0 / rippleCount;
        float rippleRadius = mod(t * 13.5, rippleSpacing);
        float ringDistance = abs(mod(distanceFromCore - rippleRadius
                                   + rippleSpacing * 0.5, rippleSpacing)
                               - rippleSpacing * 0.5);
        float ripple = ubuf.parameters.z * ubuf.styleToggles.x
                     * exp(-(ringDistance * ringDistance) / 25.0) * 4.1;
        idle *= ubuf.styleToggles.w;
        float height = clamp(idle + (bass + mids + highSpike + ripple) * amplitude,
                             0.035, 18.0);
        scale.y = height;
        position.y += height * 0.5;
    } else if (type < 1.5) {
        position.y += sin(t * 0.74 * motion + randomValue * 18.0) * 1.95
                    + bandsLow.x * 2.2;
        scale *= 1.0 + ubuf.parameters.z * 0.28;
    } else if (type < 2.5) {
        float cycle = 4.5 + randomValue * 2.0;
        float age = mod(t * motion + randomValue * cycle, cycle) / cycle;
        float fall = clamp(age / 0.72, 0.0, 1.0);
        float visibleFactor = 1.0 - step(0.72, age);
        position.y *= 1.0 - fall;
        position.x += fall * 6.0;
        scale.y *= (1.0 + ubuf.effects.y * 1.7) * visibleFactor;
        scale.xz *= visibleFactor;
        opacity = visibleFactor;
    } else if (type < 3.5) {
        float burst = ubuf.effects.x * (0.2 + randomValue);
        position.xz += vec2(cos(randomValue * 31.0), sin(randomValue * 31.0))
                     * mod(t * 3.0 + randomValue * 9.0, 8.0) * burst;
        position.y += abs(sin(t * 2.0 + randomValue * 12.0)) * 6.0 * burst;
    } else {
        float cycle = 4.5 + randomValue * 2.0;
        float age = mod(t * motion + randomValue * cycle, cycle) / cycle;
        float collision = step(0.72, age);
        float collisionProgress = clamp((age - 0.72) / 0.28, 0.0, 1.0);
        float angle = instanceData.w * 6.2831853;
        if (type < 4.5) {
            float delayedFall = clamp(age / 0.72 - instanceData.w * 0.12,
                                      0.0, 1.0);
            float visibleFactor = 1.0 - step(0.72, age);
            position.y *= 1.0 - delayedFall;
            position.x += delayedFall * 6.0;
            scale.y *= visibleFactor * (1.0 + instanceData.w * 2.2);
            scale.xz *= visibleFactor;
            opacity = visibleFactor * (1.0 - instanceData.w * 0.22);
        } else if (type < 5.5) {
            vec2 direction = vec2(cos(angle), sin(angle));
            position.x += 6.0;
            position.xz += direction * collisionProgress * 12.0;
            scale.x *= 1.0 + collisionProgress * 2.0;
            scale *= collision * sin(collisionProgress * 3.1415926);
            opacity = collision * (1.0 - collisionProgress);
        } else {
            vec2 direction = vec2(cos(angle), sin(angle));
            position.x += 6.0;
            position.xz += direction * collisionProgress * 8.0;
            position.y += sin(collisionProgress * 3.1415926) * 7.0;
            scale *= collision * (1.0 - collisionProgress);
            opacity = collision * (1.0 - collisionProgress);
        }
    }

    vec3 base = ubuf.colors[0].rgb;
    vec3 cool = ubuf.colors[1].rgb;
    vec3 warm = ubuf.colors[2].rgb;
    vec3 accent = ubuf.colors[3].rgb;
    vec3 peak = ubuf.colors[4].rgb;
    if (zone < 0.5) color = base;
    else if (zone < 1.5) color = cool;
    else if (zone < 2.5) color = warm;
    else if (zone < 3.5) color = accent;
    else color = peak;
    float layerCount = mix(2.0, 12.0, ubuf.styleDynamics.w);
    float layeredDistance = floor(clamp(distanceFromCore / 118.0, 0.0, 1.0)
                                * layerCount) / max(1.0, layerCount - 1.0);
    color = mix(color, peak, layeredDistance * 0.14);
    if (ubuf.styleDynamics.z > 1.5 || ubuf.styleExtra.x > 0.5) {
        color = 0.52 + 0.48 * cos(6.2831853
            * (vec3(0.0, 0.33, 0.67) + t * 0.035 + distanceFromCore * 0.004));
    } else if (ubuf.styleDynamics.z > 0.5) {
        color = mix(cool, accent, clamp(distanceFromCore / 110.0, 0.0, 1.0));
    }
    if (distanceFromCore < 13.0 && type < 0.5) {
        color = mix(color, peak, clamp(1.0 - distanceFromCore / 13.0, 0.0, 1.0));
    }
    if (type > 1.5) color = mix(color, vec3(1.0), 0.62);
    if (type > 4.5 && type < 5.5) color = vec3(1.0);

    vec3 worldPosition = position + vertexPosition * scale;
    gl_Position = ubuf.mvp * vec4(worldPosition, 1.0);
    light = 0.34 + 0.66 * max(dot(normalize(vertexNormal),
                                  normalize(vec3(-0.35, 0.82, 0.42))), 0.0);
    fog = clamp(1.0 - distanceFromCore / 118.0, 0.0, 1.0);
    glow = ubuf.styleParameters.z * (0.35 + max(max(color.r, color.g), color.b));
}
