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
    vec4 styleAudio;
    vec4 stylePresentation;
    vec4 impact;
} ubuf;

layout(location = 0) out vec3 color;
layout(location = 1) out float light;
layout(location = 2) out float fog;
layout(location = 3) out float opacity;
layout(location = 4) out float glow;
layout(location = 5) out float focus;
layout(location = 6) out float impactLight;
layout(location = 7) out float topSurface;
layout(location = 8) out float streamSheen;

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
    float amplitude = mix(0.20, 1.45, ubuf.styleParameters.x);
    float motion = mix(0.2, 1.8, ubuf.styleParameters.y);
    float responseRadius = max(28.0, ubuf.styleAudio.z);
    float impactStrength = ubuf.impact.x;
    float impactAge = ubuf.impact.y;
    float impactWave = 0.0;
    float coreGlow = 0.0;
    float steadyCoreGlow = 0.0;
    float rippleWave = 0.0;
    float terrainSpike = 0.0;
    opacity = 1.0;
    impactLight = 0.0;
    topSurface = 0.0;
    streamSheen = 0.0;

    if (type < 0.5) {
        float center = clamp(1.0 - distanceFromCore / responseRadius, 0.0, 1.0);
        float core = pow(center, 1.18);
        float terrainField = smoothstep(responseRadius * 1.15,
                                        responseRadius * 0.45,
                                        distanceFromCore);
        float bassField = 0.78 + 0.22
            * sin(position.x * 0.038 - position.z * 0.029 + t * 0.20);
        float ridgeA = 0.5 + 0.5
            * sin(position.z * 0.052 + position.x * 0.027 + t * 0.28);
        float ridgeB = 0.5 + 0.5
            * cos(position.x * 0.041 - position.z * 0.036 - t * 0.22);
        float wideRidge = ridgeA * 0.56 + ridgeB * 0.44;
        float bass = bandsLow.x * (1.65 + core * 2.75)
                   + bandsLow.y * (1.35 + bassField * 1.70) * center;
        float mids = bandsLow.z * (0.75 + wideRidge * 1.95)
                   + bandsLow.w * (0.80 + (1.0 - wideRidge) * 1.65);
        float detailA = 0.5 + 0.5
            * sin(position.x * 0.18 + position.z * 0.11);
        float detailB = 0.5 + 0.5
            * cos(position.z * 0.16 - position.x * 0.09);
        float coherentDetail = detailA * 0.58 + detailB * 0.42;
        float highEnergy = clamp(bandsHigh.x * 0.38
                               + bandsHigh.y * 0.28
                               + bandsHigh.z * 0.20
                               + bandsHigh.w * 0.14, 0.0, 1.0);
        float peakControl = mix(0.42, 1.0,
                                clamp(ubuf.styleDynamics.y, 0.0, 1.0));
        float highDetail = highEnergy * (0.20 + coherentDetail * 1.45)
                         * center * peakControl;
        terrainSpike = clamp(highDetail / 1.65, 0.0, 1.0);
        float idlePhase = sin(position.x * 0.032 + position.z * 0.041) * 0.72;
        float reliefA = 0.5 + 0.5 * sin(position.x * 0.055
            + position.z * 0.032 + t * 0.18);
        float reliefB = 0.5 + 0.5 * cos(position.z * 0.070
            - position.x * 0.018 - t * 0.14);
        float baseRelief = (0.24 + 0.48
            * (reliefA * 0.55 + reliefB * 0.45)) * terrainField
            + core * 1.08;
        float idle = baseRelief + 0.06 + 0.10
            * sin(distanceFromCore * 0.067 - t * 0.36 + idlePhase);
        float rippleCount = max(1.0, ubuf.effects.w);
        float rippleSpacing = 96.0 / rippleCount;
        float rippleRadius = mod(t * 13.5, rippleSpacing);
        float ringDistance = abs(mod(distanceFromCore - rippleRadius
                                   + rippleSpacing * 0.5, rippleSpacing)
                               - rippleSpacing * 0.5);
        float cellModulation = 0.55 + randomValue * 0.45;
        float ripple = ubuf.parameters.z * ubuf.styleToggles.x
                     * exp(-(ringDistance * ringDistance) / 30.25) * 3.35
                     * cellModulation;
        float ringPhase = 0.5 + 0.5 * cos(distanceFromCore * 0.29
                                       - t * 1.15 * motion);
        float structuralRing = pow(ringPhase, 6.0)
                             * (0.20 + ubuf.parameters.x * 0.95)
                             * 0.88 * ubuf.styleToggles.x * terrainField
                             * cellModulation;
        ripple += structuralRing;
        rippleWave = ripple;
        float travelingRadius = impactAge * responseRadius * 0.92;
        float firstRing = exp(-pow(distanceFromCore - travelingRadius, 2.0) / 12.0);
        float secondRing = exp(-pow(distanceFromCore - max(0.0, travelingRadius - 9.0), 2.0) / 18.0);
        float thirdRing = exp(-pow(distanceFromCore - max(0.0, travelingRadius - 18.0), 2.0) / 25.0);
        impactWave = impactStrength * (firstRing + secondRing * 0.72 + thirdRing * 0.48)
                   * (4.0 + ubuf.stylePresentation.x * 5.5);
        float domeRadius = max(12.0, responseRadius * 0.36);
        float dome = exp(-(distanceFromCore * distanceFromCore)
                       / (domeRadius * domeRadius));
        coreGlow = impactStrength * dome * ubuf.styleAudio.w;
        float impactDomeRadius = max(24.0, responseRadius * 0.82);
        float impactDome = exp(-(distanceFromCore * distanceFromCore)
                             / (impactDomeRadius * impactDomeRadius));
        impactLight = clamp(impactStrength * impactDome
                            * (0.62 + ubuf.styleAudio.w * 0.38)
                            + impactWave * 0.12, 0.0, 1.0);
        steadyCoreGlow = pow(center, 2.05)
                       * (0.16 + ubuf.parameters.x * 0.92
                          + bandsLow.x * 0.72)
                       * ubuf.styleAudio.w;
        float coreLift = ubuf.parameters.x * ubuf.styleAudio.w
                       * (0.35 + bandsLow.x * 0.65) * dome * 10.8;
        float centerShoulders = ubuf.parameters.x * ubuf.styleAudio.w
                              * terrainField
                              * (0.42 + core * 1.55 + wideRidge * 0.38);
        idle *= ubuf.styleToggles.w;
        float rawHeight = max(0.0,
            idle + ((bass + mids + highDetail) * terrainField + ripple) * amplitude
            + coreLift + centerShoulders
            + impactWave + coreGlow * 7.2);
        float softCap = mix(32.0, 36.0, step(0.001, impactStrength));
        float height = max(0.035,
            softCap * (1.0 - exp(-rawHeight / softCap)));
        scale.y = height;
        position.y += height * 0.5;
        float outerField = smoothstep(responseRadius * 0.84,
                                      84.0, distanceFromCore);
        float coherentNoise = 0.5 + 0.5 * sin(instancePosition.x * 0.11
                                             + instancePosition.z * 0.075);
        float cellNoise = clamp(coherentNoise * 0.68
                                + randomValue * 0.32, 0.0, 1.0);
        float sparseCell = smoothstep(0.54 + outerField * 0.20,
                                      0.92, cellNoise);
        opacity *= mix(1.0, 0.18 + sparseCell * 0.58, outerField);
        opacity *= 1.0 - smoothstep(79.5, 84.0, distanceFromCore);
        topSurface = smoothstep(0.72, 0.98, vertexNormal.y);
        float flowPhase = fract(t * (0.12 + highEnergy * 0.22)
                                + randomValue * 0.83
                                + distanceFromCore * 0.010);
        float flowingBand = exp(-pow((flowPhase - 0.20) / 0.075, 2.0));
        float sparkle = pow(0.5 + 0.5 * sin(t * (3.4 + highEnergy * 8.0)
                                           + randomValue * 47.0), 18.0);
        streamSheen = ubuf.styleExtra.z * topSurface
                    * (0.02 + highEnergy * 0.48)
                    * (flowingBand * 0.86 + sparkle * 1.02);
    } else if (type < 1.5) {
        position.y += sin(t * 0.74 * motion + randomValue * 18.0) * 1.95
                    + bandsLow.x * 2.2;
        scale *= 1.0 + ubuf.parameters.z * 0.28;
    } else if (type < 2.5) {
        float group = floor(instanceData.w + 0.001);
        float cycle = 4.5 + randomValue * 2.0;
        float age = mod(t * motion + randomValue * cycle, cycle) / cycle;
        if (group < 0.5 && ubuf.impact.z > 0.5) {
            age = clamp(impactAge / 0.78, 0.0, 1.0);
        }
        float fall = clamp(age / 0.72, 0.0, 1.0);
        float visibleFactor = group < 0.5
            ? 1.0 - step(0.72, age)
            : 1.0 - step(0.18, age);
        position.y *= 1.0 - fall;
        position.xz = mix(position.xz, vec2(0.0), fall * (group < 0.5 ? 0.86 : 0.0));
        position.x += fall * 6.0 * step(0.5, group);
        scale.y *= (1.0 + ubuf.effects.y * 1.7 + impactStrength * 3.0)
                 * visibleFactor;
        scale.xz *= visibleFactor;
        opacity = visibleFactor;
    } else if (type < 3.5) {
        float starPulse = 0.5 + 0.5
            * sin(t * (0.7 + randomValue * 1.8) + randomValue * 38.0);
        float burst = ubuf.effects.x * ubuf.styleExtra.y;
        vec2 outward = normalize(position.xz + vec2(0.001));
        position.xz += outward * mod(t * 1.4 + randomValue * 11.0, 10.0)
                     * burst * 0.42;
        position.y += sin(t * 0.34 + randomValue * 17.0) * (0.35 + burst);
        scale *= 0.62 + starPulse * 0.78 + burst * 0.35;
        opacity = 0.24 + starPulse * 0.58 + burst * 0.18;
    } else {
        float group = floor(instanceData.w + 0.001);
        float localValue = fract(instanceData.w);
        float cycle = 4.5 + randomValue * 2.0;
        float age = mod(t * motion + randomValue * cycle, cycle) / cycle;
        if (group < 0.5 && ubuf.impact.z > 0.5) {
            age = clamp(impactAge / 0.78, 0.0, 1.0);
        }
        float collision = step(0.72, age);
        float collisionProgress = clamp((age - 0.72) / 0.28, 0.0, 1.0);
        float angle = localValue * 6.2831853;
        if (type < 4.5) {
            float delayedFall = clamp(age / 0.72 - localValue * 0.12,
                                      0.0, 1.0);
            float visibleFactor = group < 0.5
                ? 1.0 - step(0.72, age)
                : 1.0 - step(0.18, age);
            position.y *= 1.0 - delayedFall;
            position.xz = mix(position.xz, vec2(0.0), delayedFall
                            * (group < 0.5 ? 0.86 : 0.0));
            position.x += delayedFall * 6.0 * step(0.5, group);
            scale.y *= visibleFactor * (1.0 + localValue * 2.2);
            scale.xz *= visibleFactor;
            opacity = visibleFactor * (1.0 - localValue * 0.22);
        } else if (type < 5.5) {
            vec2 direction = vec2(cos(angle), sin(angle));
            position.xz = group < 0.5 ? vec2(0.0) : position.xz + vec2(6.0, 0.0);
            position.xz += direction * collisionProgress * 12.0;
            scale.x *= 1.0 + collisionProgress * 2.0;
            scale *= collision * sin(collisionProgress * 3.1415926);
            opacity = collision * (1.0 - collisionProgress);
        } else {
            vec2 direction = vec2(cos(angle), sin(angle));
            position.xz = group < 0.5 ? vec2(0.0) : position.xz + vec2(6.0, 0.0);
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
    if (type < 0.5) {
        float warmField = 0.5 + 0.5
            * sin(position.x * 0.026 + position.z * 0.018 + t * 0.025);
        float radialField = 0.5 + 0.5
            * sin(distanceFromCore * 0.115 - t * 0.055
                  + position.x * 0.010);
        float accentField = 0.5 + 0.5
            * cos(position.x * 0.019 - position.z * 0.031 - t * 0.018);
        float colorBand = warmField * 0.35 + radialField * 0.65;
        color = mix(cool, warm, smoothstep(0.14, 0.86, colorBand));
        color = mix(color, accent,
                    smoothstep(0.68, 0.98, accentField) * 0.68);
        float edge = smoothstep(0.34, 0.94,
                                clamp(distanceFromCore / 118.0, 0.0, 1.0));
        color = mix(color, base, edge * 0.52);
        float corePresence = pow(clamp(1.0 - distanceFromCore / 84.0,
                                       0.0, 1.0), 0.58);
        color *= mix(0.15, 1.04, corePresence);
    } else if (type < 1.5) color = mix(warm, peak, 0.76);
    else if (zone < 0.5) color = base;
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
    if (distanceFromCore < 24.0 && type < 0.5) {
        color = mix(color, peak,
                    clamp(0.24 * (1.0 - distanceFromCore / 24.0), 0.0, 0.24));
    }
    if (type < 0.5 && rippleWave > 0.001) {
        vec3 ringTint = mix(cool, warm,
                            0.5 + 0.5 * sin(t * 0.9 + distanceFromCore * 0.16));
        ringTint = mix(ringTint, accent, 0.24);
        color = mix(color, ringTint, clamp(rippleWave * 0.78, 0.0, 0.96));
    }
    if (type < 0.5 && steadyCoreGlow > 0.001) {
        color = mix(color, peak, clamp(steadyCoreGlow * 0.14, 0.0, 0.28));
        color = mix(color, vec3(1.0, 0.985, 0.965),
                    clamp(terrainSpike * 0.12, 0.0, 0.16));
    }
    if (type < 0.5 && impactStrength > 0.001) {
        color = mix(color, accent, clamp(impactWave * 0.14, 0.0, 0.72));
        color = mix(color, peak, clamp(coreGlow * 0.88, 0.0, 0.92));
    }
    if (type > 1.5) color = mix(color, vec3(1.0), 0.62);
    if (type > 4.5 && type < 5.5) color = vec3(1.0);

    vec3 worldPosition = position + vertexPosition * scale;
    gl_Position = ubuf.mvp * vec4(worldPosition, 1.0);
    light = 0.46 + 0.54 * max(dot(normalize(vertexNormal),
                                  normalize(vec3(-0.35, 0.82, 0.42))), 0.0);
    light *= type < 0.5 ? mix(0.78, 1.08, topSurface) : 1.0;
    light *= clamp(0.66 + ubuf.stylePresentation.z * 0.34, 0.72, 1.18);
    fog = clamp(1.0 - distanceFromCore / 132.0, 0.0, 1.0);
    float focusBand = exp(-pow(distanceFromCore - responseRadius * 0.34, 2.0)
                        / max(80.0, responseRadius * responseRadius * 0.18));
    focus = mix(1.0, 0.62 + focusBand * 0.38,
                clamp(ubuf.stylePresentation.y / 1.5, 0.0, 1.0));
    glow = ubuf.styleParameters.z * (0.35 + max(max(color.r, color.g), color.b))
         + steadyCoreGlow * 1.25 + terrainSpike * 0.65 + coreGlow * 7.2
         + rippleWave * 0.30 + impactWave * 0.24;
    if (type > 2.5 && type < 3.5) {
        color = mix(color, peak, 0.36);
        glow = 0.08 + ubuf.effects.x * 0.16;
        focus = 1.0;
    }
}
