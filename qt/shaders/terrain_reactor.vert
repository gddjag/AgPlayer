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
    vec4 waveSources[8];
    vec4 audioEnvelope;
    vec4 cameraPosition;
    vec4 materialParameters;
    vec4 sceneControls;
    vec4 waveParameters;
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
layout(location = 9) out vec3 worldNormal;
layout(location = 10) out vec3 viewDirection;
layout(location = 11) flat out float objectKind;
layout(location = 12) out float columnHeight;
layout(location = 13) out vec3 surfacePosition;
layout(location = 14) out float musicLight;
layout(location = 15) out vec4 material;

void main()
{
    float type = instanceData.x;
    float zone = instanceData.y;
    float randomValue = instanceData.z;
    float t = ubuf.parameters.w;
    material = ubuf.materialParameters;
    vec3 position = instancePosition;
    vec3 scale = instanceScale;
    float distanceFromCore = length(position.xz);
    vec4 bandsLow = ubuf.bandsLow * ubuf.equalizerLow;
    vec4 bandsHigh = ubuf.bandsHigh * ubuf.equalizerHigh;
    float amplitude = mix(0.10, 4.30, ubuf.styleParameters.x);
    float motion = mix(0.2, 1.8, ubuf.styleParameters.y);
    float responseRadius = max(28.0, ubuf.styleAudio.z);
    float impactStrength = ubuf.impact.x;
    float impactAge = ubuf.impact.y;
    float fastBass = ubuf.audioEnvelope.x;
    float slowBass = ubuf.audioEnvelope.y;
    float beatPulse = clamp(ubuf.audioEnvelope.z * 1.8
                            + max(0.0, fastBass - slowBass) * 2.0
                              * ubuf.stylePresentation.x,
                            0.0, 1.0);
    float impactWave = 0.0;
    float coreGlow = 0.0;
    float steadyCoreGlow = 0.0;
    float rippleWave = 0.0;
    float terrainSpike = 0.0;
    float towerGlow = 0.0;
    opacity = 1.0;
    impactLight = 0.0;
    topSurface = 0.0;
    streamSheen = 0.0;
    musicLight = 0.0;

    if (type < 0.5) {
        float center = clamp(1.0 - distanceFromCore / responseRadius, 0.0, 1.0);
        float core = pow(center, 1.18);
        // Existing short beat/onset envelope drives illumination separately
        // from sustained band energy. No oscillator masquerades as a beat.
        musicLight = clamp(beatPulse * (0.45 + core * 0.55)
                            * ubuf.styleAudio.w, 0.0, 1.0);
        float terrainField = 1.0 - smoothstep(responseRadius * 0.45,
                                              responseRadius * 1.15,
                                              distanceFromCore);
        float flowTime = t * motion;
        float bassField = 0.78 + 0.22
            * sin(position.x * 0.038 - position.z * 0.029 + flowTime * 0.20);
        float ridgeA = 0.5 + 0.5
            * sin(position.z * 0.052 + position.x * 0.027 + flowTime * 0.28);
        float ridgeB = 0.5 + 0.5
            * cos(position.x * 0.041 - position.z * 0.036 - flowTime * 0.22);
        float wideRidge = ridgeA * 0.56 + ridgeB * 0.44;
        float bass = bandsLow.x * (1.25 + core * 2.15)
                   + bandsLow.y * (1.10 + bassField * 1.45) * center
                   + fastBass * core * 1.55 + slowBass * terrainField * 0.78;
        // The bass bed and regional mids form connected relief. Bounded local
        // variation keeps its columns legible without isolated skyscrapers.
        bass *= 1.90;
        float ridgeCoordinateA = position.x * 0.052
                               + position.z * 0.024;
        float ridgeCoordinateB = position.z * 0.061
                               - position.x * 0.019;
        float ridgeMaskA = pow(0.5 + 0.5 * sin(ridgeCoordinateA
                                             + flowTime * 0.24), 2.4);
        float ridgeMaskB = pow(0.5 + 0.5 * cos(ridgeCoordinateB
                                             - flowTime * 0.19), 2.7);
        float midRegionalField = clamp(0.36 + (1.0 - center) * 0.38
                                     + ridgeMaskA * 0.45
                                     + ridgeMaskB * 0.12, 0.0, 1.35);
        float mids = (bandsLow.z * (0.58 + wideRidge * 2.30)
                   + bandsLow.w * (0.62 + (1.0 - wideRidge) * 2.05))
                   * midRegionalField;
        mids *= 2.40;
        float midSpire = (bandsLow.z * pow(ridgeMaskA, 5.0)
                        + bandsLow.w * pow(ridgeMaskB, 5.0))
                       * center * 3.8;
        float detailA = 0.5 + 0.5
            * sin(position.x * 0.18 + position.z * 0.11);
        float detailB = 0.5 + 0.5
            * cos(position.z * 0.16 - position.x * 0.09);
        float coherentDetail = detailA * 0.58 + detailB * 0.42;
        // Continuous neighbouring relief avoids isolated black skyscrapers.
        float towerCluster = detailA * 0.56 + detailB * 0.44;
        float towerGate = 0.18 + towerCluster * 0.46 + randomValue * 0.36;
        float lowMidTowerEnergy = clamp(bandsLow.x * 0.34
                                      + bandsLow.y * 0.28
                                      + bandsLow.z * 0.22
                                      + bandsLow.w * 0.16, 0.0, 1.0);
        float frequencyTowers = lowMidTowerEnergy * pow(towerGate, 1.35)
                              * (0.35 + center * 0.65)
                              * (1.2 + ridgeMaskA * 3.8);
        towerGlow = lowMidTowerEnergy * towerGate * 0.30;
        float highEnergy = clamp(bandsHigh.x * 0.38
                               + bandsHigh.y * 0.28
                               + bandsHigh.z * 0.20
                               + bandsHigh.w * 0.14, 0.0, 1.0);
        float peakControl = mix(0.42, 1.0,
                                clamp(ubuf.styleDynamics.y, 0.0, 1.0));
        float localizedHigh = 0.25 + 0.75 * pow(coherentDetail, 3.0);
        float highDetail = highEnergy * (0.65 + coherentDetail * 4.2)
                         * center * peakControl * localizedHigh;
        terrainSpike = clamp(highDetail * 0.70, 0.0, 1.0);
        float idlePhase = sin(position.x * 0.032 + position.z * 0.041) * 0.72;
        float reliefA = 0.5 + 0.5 * sin(position.x * 0.055
            + position.z * 0.032 + flowTime * 0.18);
        float reliefB = 0.5 + 0.5 * cos(position.z * 0.070
            - position.x * 0.018 - flowTime * 0.14);
        float baseRelief = (0.24 + 0.48
            * (reliefA * 0.55 + reliefB * 0.45)) * terrainField
            + core * 1.08;
        float idle = baseRelief + 0.06 + 0.10
            * sin(distanceFromCore * 0.067 - flowTime * 0.36 + idlePhase);
        float cellModulation = 0.86 + coherentDetail * 0.14;
        float waveField = 0.0;
        float waveEnergy = 0.65 + fastBass * 0.35;
        int waveCount = int(clamp(ubuf.effects.w, 0.0, 8.0));
        if (ubuf.styleToggles.x > 0.5 && waveCount > 0) {
            for (int waveIndex = 0; waveIndex < 8; ++waveIndex) {
                if (waveIndex >= waveCount) break;
                vec4 source = ubuf.waveSources[waveIndex];
                if (source.w <= 0.001) continue;
                float age = source.z;
                float waveRadius = age * 31.0;
                float sourceDistance = length(position.xz - source.xy);
                float ridgeDistance = sourceDistance - waveRadius;
                float waveWidth = max(0.2, ubuf.waveParameters.y);
                float waveLife = 2.8 / max(0.2, ubuf.waveParameters.z);
                float ridge = exp(-(ridgeDistance * ridgeDistance) / (9.0 * waveWidth * waveWidth));
                float tail = exp(-max(0.0, waveRadius - sourceDistance) / (14.0 * waveWidth))
                           * step(sourceDistance, waveRadius);
                waveField += (ridge + tail * 0.14)
                           * max(0.0, 1.0 - age / waveLife) * source.w;
            }
        }
        float ripple = waveField * waveEnergy * ubuf.styleToggles.x
                     * 13.0 * cellModulation * ubuf.waveParameters.x;
        rippleWave = ripple;
        float travelingRadius = impactAge * responseRadius * 0.92;
        float impactWidth = max(0.2, ubuf.waveParameters.y);
        float firstRing = exp(-pow((distanceFromCore - travelingRadius) / impactWidth, 2.0) / 12.0);
        float secondRing = exp(-pow((distanceFromCore - max(0.0, travelingRadius - 9.0)) / impactWidth, 2.0) / 18.0);
        float thirdRing = exp(-pow((distanceFromCore - max(0.0, travelingRadius - 18.0)) / impactWidth, 2.0) / 25.0);
        impactWave = impactStrength * ubuf.styleToggles.x
                   * (firstRing + secondRing * 0.72 + thirdRing * 0.48)
                   * (4.0 + ubuf.stylePresentation.x * 5.5) * ubuf.waveParameters.x;
        float domeRadius = max(12.0, responseRadius * 0.36);
        float dome = exp(-(distanceFromCore * distanceFromCore)
                       / (domeRadius * domeRadius));
        coreGlow = impactStrength * dome * ubuf.styleAudio.w;
        float impactDomeRadius = max(24.0, responseRadius * 0.82);
        float impactDome = exp(-(distanceFromCore * distanceFromCore)
                             / (impactDomeRadius * impactDomeRadius));
        impactLight = clamp(impactStrength * impactDome
                            * ubuf.styleAudio.w * 0.36
                            + impactWave * 0.055, 0.0, 1.0);
        steadyCoreGlow = pow(center, 1.88)
                       * (0.012 + ubuf.parameters.x * 0.035
                          + slowBass * 0.045 + beatPulse * 0.12)
                       * ubuf.styleAudio.w;
        float coreLift = (slowBass * 0.15 + beatPulse * 1.25) * dome * 4.25;
        float centerShoulders = ubuf.parameters.x * ubuf.styleAudio.w
                              * terrainField
                              * (0.035 + core * 0.14 + wideRidge * 0.26);
        // Connected ridge shelves and quiet valleys make side faces visible.
        // Per-cell noise is restricted to small detail, not the main height.
        float ridgeUnion = max(ridgeMaskA, ridgeMaskB * 0.78);
        // Separated shelves and troughs expose side faces instead of joining
        // every frequency region into one smooth dome.
        float localContour = 0.10 + pow(ridgeUnion, 3.2) * 1.15;
        float swellPhase = distanceFromCore * 0.18 - flowTime * 1.05;
        float swells = pow(0.5 + 0.5 * sin(swellPhase), 2.0)
                     * (bandsLow.x + bandsLow.y) * terrainField * 2.3;
        float printRelief = (0.25 + 0.75 * smoothstep(0.3, 0.8, randomValue))
                          * (bandsLow.z + bandsLow.w) * center * 1.8;
        frequencyTowers *= 0.58 + coherentDetail * 0.42;
        idle *= ubuf.styleToggles.w;
        float rawHeight = max(0.0,
            idle + (((bass + mids) * localContour + highDetail)
                    * terrainField + ripple + swells + printRelief) * amplitude
            + midSpire * amplitude
            + frequencyTowers * amplitude
            + coreLift * amplitude + centerShoulders
            + impactWave + coreGlow * 1.55);
        float softCap = mix(42.0, 48.0, step(0.001, impactStrength));
        float height = max(0.035,
            softCap * (1.0 - exp(-rawHeight / softCap)));
        scale.y = height;
        // The layout already leaves a small physical gutter. Keep only a
        // hairline here so the centered tile grid cannot read as black seams.
        scale.xz *= 0.98;
        if (material.x > 0.5 && material.x < 1.5) {
            // Bounded beat-driven squash/rebound: no independent simulation,
            // audio buffer, or free-running wobble during silence.
            float rebound = sin(ubuf.audioEnvelope.w * 12.56637)
                          * beatPulse * material.z;
            scale.y *= 1.0 + rebound * 0.18;
            scale.xz *= 1.0 - rebound * 0.035;
        }
        position.y += scale.y * 0.5;
        float stageHalfExtent = max(1.0, ubuf.sceneControls.z);
        float stageDistance = max(abs(instancePosition.x),
                                  abs(instancePosition.z));
        // A maximum response radius can outgrow the stage. Do not feed
        // reversed edges to smoothstep: that is undefined and hid the wider
        // field instead of letting it cover the expanded ground.
        float outerField = 0.0;
        if (responseRadius * 1.15 < stageHalfExtent) {
            float outerFieldStart = max(responseRadius * 1.15,
                                        stageHalfExtent * 0.72);
            outerField = smoothstep(outerFieldStart, stageHalfExtent,
                                    stageDistance);
        }
        float coherentNoise = 0.5 + 0.5 * sin(instancePosition.x * 0.11
                                             + instancePosition.z * 0.075);
        float cellNoise = clamp(coherentNoise * 0.68
                                + randomValue * 0.32, 0.0, 1.0);
        float sparseCell = smoothstep(0.54 + outerField * 0.20,
                                      0.92, cellNoise);
        opacity *= mix(1.0, 0.18 + sparseCell * 0.58, outerField);
        opacity *= 1.0 - smoothstep(stageHalfExtent * 0.96,
                                    stageHalfExtent, stageDistance);
        opacity *= ubuf.sceneControls.x;
        topSurface = smoothstep(0.72, 0.98, vertexNormal.y);
        float flowPhase = fract(t * (0.12 + highEnergy * 0.22)
                                + position.x * 0.018 + position.z * 0.011
                                + coherentDetail * 0.16);
        float flowingBand = exp(-pow((flowPhase - 0.20) / 0.075, 2.0));
        float sparkle = pow(0.5 + 0.5 * sin(t * (4.2 + highEnergy * 9.0)
                                           + randomValue * 47.0), 18.0);
        float presence = clamp(bandsHigh.y * 0.42 + bandsHigh.z * 0.36
                               + bandsHigh.w * 0.22, 0.0, 1.0);
        // Keep the high-band stream cue on selected top facets. A non-zero
        // floor made the cue brighten nearly every visible tile at once.
        float sheenMask = smoothstep(0.48, 0.94, randomValue);
        streamSheen = ubuf.styleExtra.z * topSurface
                    * (0.075 + presence * 0.72 + beatPulse * 0.22)
                    * (flowingBand * 1.18 + sparkle * 1.36)
                    * sheenMask * (0.25 + ubuf.styleParameters.z * 1.9);
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
        float starDepth = clamp((length(instancePosition) - 240.0) / 320.0, 0.0, 1.0);
        float starPulse = 0.5 + 0.5
            * sin(t * (0.7 + randomValue * 1.8) + randomValue * 38.0);
        float burst = ubuf.effects.x * ubuf.styleExtra.y;
        vec2 outward = normalize(position.xz + vec2(0.001));
        position.xz += outward * mod(t * 1.4 + randomValue * 11.0, 10.0)
                     * burst * 0.42;
        position.y += sin(t * 0.34 + randomValue * 17.0) * (0.35 + burst);
        position.xz += vec2(sin(t * 0.09 + randomValue * 31.0),
                            cos(t * 0.07 + randomValue * 29.0))
                     * (1.0 - starDepth) * 0.45;
        scale *= 0.92 + starPulse * 0.20 + burst * 0.12;
        // Keep distant stars resolvable at normal window sizes. The floor is
        // angular (camera distance), so zoom does not turn them into blocks.
        float starDistance = length(ubuf.cameraPosition.xyz - position);
        float angularFloor = 0.00085 * mix(1.65, 0.85, starDepth);
        scale = max(scale, vec3(starDistance * angularFloor));
        opacity = 0.42 + starPulse * 0.30 + burst * 0.08;
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
            scale.y *= visibleFactor * (1.0 - localValue * 0.42);
            scale.xz *= visibleFactor;
            opacity = visibleFactor * (1.0 - localValue * 0.62);
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
            * sin(position.x * 0.044 + position.z * 0.029 + t * 0.025);
        float radialField = 0.5 + 0.5
            * sin(distanceFromCore * 0.115 - t * 0.055
                  + position.x * 0.010);
        float accentField = 0.5 + 0.5
            * cos(position.x * 0.019 - position.z * 0.031 - t * 0.018);
        float colorBand = warmField * 0.72 + radialField * 0.28;
        // Preserve cool and warm islands instead of mixing their complements
        // into a large muddy middle band.
        color = mix(cool, warm, smoothstep(0.42, 0.68, colorBand));
        color = mix(color, accent,
                    smoothstep(0.66, 0.92, accentField) * 0.70);
        float edge = smoothstep(0.34, 0.94,
                                clamp(distanceFromCore / 118.0, 0.0, 1.0));
        color = mix(color, base, edge * 0.52);
        float corePresence = pow(clamp(1.0 - distanceFromCore / 84.0,
                                       0.0, 1.0), 0.58);
        color *= mix(0.10, 0.90, corePresence);
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
        // Sweep through the preset's palette rather than replacing every
        // preset with the same absolute rainbow.
        float sweep = 0.5 + 0.5 * sin(t * 0.22 + distanceFromCore * 0.025);
        color = mix(mix(cool, warm, smoothstep(0.0, 0.55, sweep)),
                    accent, smoothstep(0.55, 1.0, sweep));
    } else if (ubuf.styleDynamics.z > 0.5) {
        color = mix(color, mix(cool, accent,
                    clamp(distanceFromCore / 110.0, 0.0, 1.0)), 0.25);
    }
    if (distanceFromCore < 24.0 && type < 0.5) {
        color = mix(color, peak,
                    clamp(0.08 * (1.0 - distanceFromCore / 24.0), 0.0, 0.08));
    }
    if (type < 0.5 && rippleWave > 0.001) {
        vec3 ringTint = mix(cool, warm,
                            0.5 + 0.5 * sin(t * 0.9 + distanceFromCore * 0.16));
        ringTint = mix(ringTint, accent, 0.24);
        color = mix(color, ringTint, clamp(rippleWave * 0.78, 0.0, 0.96));
    }
    if (type < 0.5 && steadyCoreGlow > 0.001) {
        color = mix(color, peak, clamp(steadyCoreGlow * 0.10, 0.0, 0.16));
        color = mix(color, peak, clamp(terrainSpike * 0.08, 0.0, 0.12));
    }
    if (type < 0.5 && impactStrength > 0.001) {
        color = mix(color, accent, clamp(impactWave * 0.14, 0.0, 0.72));
        color = mix(color, peak, clamp(coreGlow * 0.42, 0.0, 0.58));
    }
    if (type > 2.5 && type < 3.5) {
        color = mix(mix(cool, peak, randomValue), vec3(1.0), 0.68);
    } else if (type > 1.5) {
        color = mix(color, vec3(1.0), 0.62);
    }
    if (type > 4.5 && type < 5.5) color = mix(cool, accent, randomValue);

    vec3 localVertex = vertexPosition * scale;
    vec3 normal = vertexNormal;
    if (type > 0.5 && type < 1.5) {
        // Slow rigid-body rotation of the existing cube and its normals.
        vec3 axis = normalize(vec3(0.45, 1.0, 0.25 + randomValue));
        float angle = t * motion * 0.30 + randomValue * 6.2831853;
        float c = cos(angle);
        float s = sin(angle);
        localVertex = localVertex * c + cross(axis, localVertex) * s
                    + axis * dot(axis, localVertex) * (1.0 - c);
        normal = normal * c + cross(axis, normal) * s
               + axis * dot(axis, normal) * (1.0 - c);
    }
    if ((type > 1.5 && type < 2.5) || (type > 3.5 && type < 4.5)) {
        // Orient both the mesh and normal along its actual trajectory; a
        // sheared vertical bar gave the old streak an unrelated direction.
        float primary = 1.0 - step(0.5, floor(instanceData.w + 0.001));
        vec3 direction = normalize(vec3(-instancePosition.x * 0.86 * primary
                                         + 6.0 * (1.0 - primary),
                                        -max(1.0, instancePosition.y),
                                        -instancePosition.z * 0.86 * primary));
        vec3 across = normalize(cross(direction, vec3(0.0, 0.0, 1.0)));
        vec3 third = normalize(cross(across, direction));
        mat3 flightFrame = mat3(across, direction, third);
        localVertex = flightFrame * localVertex;
        normal = flightFrame * normal;
    }
    vec3 worldPosition = position + localVertex;
    gl_Position = ubuf.mvp * vec4(worldPosition, 1.0);
    worldNormal = normalize(normal);
    viewDirection = normalize(ubuf.cameraPosition.xyz - worldPosition);
    objectKind = type;
    columnHeight = scale.y;
    surfacePosition = vertexPosition;
    light = ubuf.stylePresentation.z; // face/edge clarity, not exposure
    fog = 1.0 - smoothstep(42.0, 100.0, distanceFromCore) * 0.88;
    float focusBand = exp(-pow(distanceFromCore - responseRadius * 0.34, 2.0)
                        / max(80.0, responseRadius * responseRadius * 0.18));
    focus = mix(1.0, 0.82 + focusBand * 0.18,
                clamp(ubuf.stylePresentation.y / 1.5, 0.0, 1.0));
    glow = ubuf.styleParameters.z * (0.35 + max(max(color.r, color.g), color.b))
         + steadyCoreGlow * 0.38 + towerGlow * 0.42
         + terrainSpike * 0.22 + coreGlow * 2.6
         + rippleWave * 0.30 + impactWave * 0.24;
    if (type > 2.5 && type < 3.5) {
        color = mix(color, peak, 0.36);
        glow = 0.08 + ubuf.effects.x * 0.16;
        focus = 1.0;
        fog = 0.92;
    }
    if (material.x > 1.5 && type < 0.5) {
        float baseLuminance = dot(base, vec3(0.2126, 0.7152, 0.0722));
        color = baseLuminance < 0.5 ? vec3(0.95) + base * 0.03 : base;
    }
    // Elasticity is consumed in the vertex stage only. Reuse that varying
    // component for exposure instead of increasing the interpolator budget.
    material.z = ubuf.sceneControls.y;
    // Add depth behind the focal center without attenuating its highlights
    // again: the radial fog above already controls the scene's exposure.
    fog *= exp(-max(0.0, length(ubuf.cameraPosition.xyz - worldPosition)
                       - length(ubuf.cameraPosition.xyz))
               * (0.0012 + ubuf.cameraPosition.w * 0.0025));
}
