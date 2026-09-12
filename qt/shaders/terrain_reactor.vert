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
    vec4 waveSources[10];
    vec4 audioEnvelope;
    vec4 cameraPosition;
    vec4 materialParameters;
    vec4 sceneControls;
    vec4 waveParameters;
    vec4 sceneLighting;
    mat4 lightMvp;
    vec4 shadowParameters;
    vec4 bodyColor;
    vec4 atmosphereColor;
    vec4 timbre;
    vec4 rippleColor;
    vec4 meteorTrajectory;
    vec4 floatingParameters;
    vec4 meteorMaterialColor;
} ubuf;

#ifdef TERRAIN_SHADOW_PASS
// Keep the shared deformation below, but give D3D a matching one-value
// VS/PS signature instead of linking a sparse subset of the material outputs.
layout(location = 0) out float opacity;
vec3 color;
float light, fog, glow, focus, impactLight, topSurface, streamSheen;
vec3 worldNormal, viewDirection;
float objectKind;
vec3 columnExtent, surfacePosition;
float musicLight;
vec4 material;
vec3 worldPosition;
float reliefHeight;
float columnRandom;
#else
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
layout(location = 12) out vec3 columnExtent;
layout(location = 13) out vec3 surfacePosition;
layout(location = 14) out float musicLight;
layout(location = 15) out vec4 material;
layout(location = 16) out vec3 worldPosition;
layout(location = 17) flat out float reliefHeight;
layout(location = 18) flat out float columnRandom;
// Main-pass only: explicit theme material cannot recover a travelling-wave
// palette from the final legacy vertex color without also inheriting its other
// color effects.  Keep the shadow signature unchanged.
layout(location = 19) flat out vec4 travelingWave;
// Reference ripple normal/white channels; main pass only, constant across a
// column. Shadow receives the same height deformation but no color interface.
layout(location = 20) flat out vec2 referenceRippleAnim;
layout(location = 21) flat out vec2 referenceInstancePosition;
#endif

// 2D simplex kernel adapted directly from Ashima Arts' MIT-licensed
// webgl-noise, commit705a24f80b8e59a905ac5a31de2ea7aa6dce16f0.
// Copyright (C)2011 Ashima Arts. See THIRD-PARTY-NOTICES.md.
vec3 noiseWrap(vec3 v) { return v - floor(v * (1.0 / 289.0)) * 289.0; }
vec2 noiseWrap(vec2 v) { return v - floor(v * (1.0 / 289.0)) * 289.0; }
vec3 noisePermutation(vec3 v) { return noiseWrap((v * 34.0 + 1.0) * v); }
float terrainNoise(vec2 point)
{
    const vec4 c = vec4(0.211324865405187, 0.366025403784439,
                       -0.577350269189626, 0.024390243902439);
    vec2 cell = floor(point + dot(point, c.yy));
    vec2 a = point - cell + dot(cell, c.xx);
    vec2 corner = a.x > a.y ? vec2(1, 0) : vec2(0, 1);
    vec4 b = a.xyxy + c.xxzz;
    b.xy -= corner;
    cell = noiseWrap(cell);
    vec3 permutation = noisePermutation(noisePermutation(cell.y + vec3(0, corner.y, 1))
                                     + cell.x + vec3(0, corner.x, 1));
    vec3 weight = max(0.5 - vec3(dot(a,a), dot(b.xy,b.xy), dot(b.zw,b.zw)), 0.0);
    weight *= weight;
    weight *= weight;
    vec3 gradientX = 2.0 * fract(permutation * c.w) - 1.0;
    vec3 gradientY = abs(gradientX) - 0.5;
    gradientX -= floor(gradientX + 0.5);
    weight *= 1.79284291400159 - 0.85373472095314
            * (gradientX * gradientX + gradientY * gradientY);
    return 130.0 * dot(weight, vec3(gradientX.x * a.x + gradientY.x * a.y,
        gradientX.y * b.x + gradientY.y * b.y, gradientX.z * b.z + gradientY.z * b.w));
}

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
    float flightAge = ubuf.styleExtra.w;
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
    vec3 travelingWaveTint = vec3(0.0);
    float travelingWaveWeight = 0.0;
    float terrainSpike = 0.0;
    float towerGlow = 0.0;
    opacity = 1.0;
    impactLight = 0.0;
    topSurface = 0.0;
    streamSheen = 0.0;
    musicLight = 0.0;
    columnRandom = 0.0;
#ifndef TERRAIN_SHADOW_PASS
    travelingWave = vec4(0.0);
    referenceRippleAnim = vec2(0.0);
    referenceInstancePosition = instancePosition.xz;
#endif

    if (type < 0.5) {
        float center = clamp(1.0 - distanceFromCore / responseRadius, 0.0, 1.0);
        float core = pow(center, 1.18);
        // Existing short beat/onset envelope drives illumination separately
        // from sustained band energy. No oscillator masquerades as a beat.
        musicLight = clamp(beatPulse * (0.18 + pow(core, 1.15) * 0.82)
                            * ubuf.styleAudio.w * 0.70, 0.0, 1.0);
        // The field is assembled from independent frequency regions, not a
        // concentric terrace multiplier. Time is an explicit renderer input;
        // only column height changes, never the instance's base anchor.
        float reliefDisk = 1.0 - smoothstep(30.0, 60.0, distanceFromCore);
        float smoothness = clamp(ubuf.waveParameters.w, 0.0, 1.0);
        float density = clamp(ubuf.sceneLighting.w, 0.0, 1.0);
        // The canonical stage uses the original geometry in the player too.
        bool referenceGeometry = ubuf.timbre.w < 1.5 && ubuf.sceneControls.z <= 84.5;
        vec2 p = position.xz;
        float terrainRandom = fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453123);
        columnRandom = terrainRandom;
        // Preserve the reference's moving frequency fields while suppressing
        // unexcited relief in runtime silence below.
        float idleClock = t;
        float broadNoise = terrainNoise(p * 0.05
                                      + vec2(idleClock * 0.1, idleClock * 0.05));
        float diagonal = sin(dot(p, vec2(0.15, 0.1)) - idleClock * 0.6);
        float idle = mix(0.5 + broadNoise * 0.5, 0.5 + diagonal * 0.5,
                         0.2 + smoothness * 0.5) * 0.8 * reliefDisk
                     * ubuf.styleToggles.w;
        float subRegion = 1.0 - smoothstep(0.0, 25.0, distanceFromCore);
        // The original has a subtle moving ocean even without audio. Only
        // custom materials retain the older energy-dependent idle suppression.
        bool runtimeTheme = ubuf.timbre.w >= 0.5;
        if (!referenceGeometry && runtimeTheme) {
            float audibleIdle = smoothstep(0.006, 0.075, ubuf.parameters.x);
            idle *= audibleIdle;
        }
        float fieldClock = t;
        float bassOffset = terrainNoise(p * 0.1 - vec2(0.0, fieldClock * 0.2));
        float bassRegion = 1.0 - smoothstep(5.0, 35.0, distanceFromCore + bassOffset * 5.0);
        float lowMidShape = 0.5 + 0.5 * terrainNoise(p * 0.05 + vec2(fieldClock * 0.1, 0.0));
        float midShape = max(0.0, sin(dot(p, vec2(0.2))
                               + terrainNoise(p * 0.1) * 2.0 - fieldClock * 2.0));
        float highMidShape = fract(terrainRandom * 13.3) > 0.8
            ? smoothstep(10.0, 45.0, distanceFromCore) * fract(terrainRandom * 7.7) : 0.0;
        vec4 regionWeights = vec4(subRegion * 5.0,
            bassRegion * smoothstep(0.0, 1.0, terrainRandom + density * 0.5) * 4.0,
            lowMidShape * 2.5, midShape * 3.0);
        float bandRelief = dot(bandsLow, regionWeights)
            + bandsHigh.x * highMidShape * 2.5;
        if (!referenceGeometry) {
            float highTexture = smoothstep(0.48, 0.96,
                                           fract(terrainRandom * 17.31));
            bandRelief += dot(bandsHigh, vec4(0.38, 0.28, 0.20, 0.14))
                        * (0.35 + highTexture * 1.15) * reliefDisk;
        }
        if (terrainRandom > 0.99) bandRelief += ubuf.parameters.x * 5.0;
        // Gate before amplitude, so low-level energy cannot raise the apron.
        // The fixed 168-unit reference stage stores amplitude on a half-scale
        // adapter: 50% -> .5 here -> original uAmplitude 1.0. Standalone
        // native material fixtures retain the stronger UI response curve.
        float rawAmplitude = max(0.0, ubuf.styleParameters.x);
        float amplitudeControl = referenceGeometry
            ? rawAmplitude * 2.0
            : rawAmplitude * (0.35 + rawAmplitude * 3.65);
        bandRelief = max(0.0, bandRelief * reliefDisk - 0.2)
                   * amplitudeControl;
        float highEnergy = clamp(dot(bandsHigh, vec4(0.38, 0.28, 0.20, 0.14)), 0.0, 1.0);
        float coherentDetail = lowMidShape;
        terrainSpike = clamp(bandsHigh.x * highMidShape, 0.0, 1.0);
        towerGlow = clamp(bandRelief * 0.04, 0.0, 0.3);
        float cellModulation = 0.86 + coherentDetail * 0.14;
        float waveField = 0.0;
        float referenceRippleElevation = 0.0;
        vec2 referenceRipple = vec2(0.0);
        bool referenceRippleMode = ubuf.rippleColor.a > 0.5;
        float waveEnergy = 0.65 + fastBass * 0.35;
        int waveCount = int(clamp(ubuf.effects.w, 0.0, 8.0));
        if (referenceRippleMode && ubuf.styleToggles.x > 0.5) {
            // Reference contract: all ten slots hold center, elapsed age and
            // signed strength. Normal and white rings use separate physical
            // speed/width/fade/elevation constants and never touch the native
            // 42-unit overlap limiter or palette packing.
            for (int waveIndex = 0; waveIndex < 10; ++waveIndex) {
                vec4 source = ubuf.waveSources[waveIndex];
                bool white = source.w < 0.0;
                float strengthControl = max(0.0, ubuf.waveParameters.x);
                float strength = abs(source.w) * strengthControl;
                if (strength == 0.0) continue;
                float speed = white ? 20.0 : 15.0;
                // 100% is the original shader's neutral 1.0 value.
                float widthControl = max(0.2, ubuf.waveParameters.y);
                float decayControl = max(0.2, ubuf.waveParameters.z);
                float width = (white ? 1.0 : 3.0) * widthControl;
                float fadeDistance = (white ? 8.0 : 15.0) / decayControl;
                float elevationScale = white ? 1.0 : 4.0;
                float radius = max(0.0, source.z) * speed;
                float distanceToRing = length(position.xz - source.xy) - radius;
                float ring = exp(-(distanceToRing * distanceToRing) / width);
                float pulse = ring * exp(-radius / fadeDistance) * strength;
                referenceRippleElevation += pulse * elevationScale;
                if (white) referenceRipple.y += pulse;
                else referenceRipple.x += pulse;
            }
        } else if (ubuf.styleToggles.x > 0.5 && waveCount > 0) {
            for (int waveIndex = 0; waveIndex < 8; ++waveIndex) {
                if (waveIndex >= waveCount) break;
                vec4 source = ubuf.waveSources[waveIndex];
                if (source.w <= 0.001) continue;
                float age = mod(source.z, 32.0);
                float waveRadius = age * 31.0;
                float sourceDistance = length(position.xz - source.xy);
                float ridgeDistance = sourceDistance - waveRadius;
                float waveWidth = max(0.2, ubuf.waveParameters.y);
                float waveLife = 3.6 / max(0.2, ubuf.waveParameters.z);
                // A distinct leading edge followed by a softer trailing ridge.
                // Keep at least one cell of front width to avoid a broken ring.
                float decaySpread = mix(4.60, 0.48,
                    smoothstep(0.2, 2.0, ubuf.waveParameters.z));
                float ridgeWidth = ridgeDistance > 0.0
                    ? max(1.2, 3.4 * waveWidth * decaySpread)
                    : 6.0 * waveWidth * decaySpread;
                float ridge = exp(-(ridgeDistance * ridgeDistance) / (ridgeWidth * ridgeWidth));
                float tail = exp(-max(0.0, waveRadius - sourceDistance)
                                 / (22.0 * waveWidth * decaySpread))
                           * step(sourceDistance, waveRadius);
                float widthEnergy = mix(0.58, 1.30,
                    smoothstep(0.2, 2.0, waveWidth));
                float decayEnvelope = exp(-age * max(0.2, ubuf.waveParameters.z) * 3.0);
                float decayEnergy = mix(1.85, 0.70,
                    smoothstep(0.2, 2.0, ubuf.waveParameters.z));
                float weight = (ridge + tail * 0.18)
                             * max(0.0, 1.0 - age / waveLife)
                             * decayEnvelope * decayEnergy * widthEnergy * source.w;
                waveField += weight;
                // The pool advances once per emitted wave: each event retains
                // its own palette anchor, rather than changing hue mid-flight.
                int tintIndex = 1 + int(floor(source.z / 32.0));
                travelingWaveTint += ubuf.colors[tintIndex].rgb * weight;
                travelingWaveWeight += weight;
            }
        }
        float ripple = waveField * waveEnergy * ubuf.styleToggles.x
                     * 13.0 * cellModulation * ubuf.waveParameters.x;
        rippleWave = ripple;
        // All large rings now share the beat-driven, 3--6 second wave gate.
        // An eighth-beat impact still lights the core and releases meteors,
        // but must not add three unthrottled rings over the travelling wave.
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
        // Keep the user control visible without letting the native center lamp
        // overpower the reference material's elevation-driven glow.
        float centerLampScale = runtimeTheme ? 0.25 : 1.0;
        steadyCoreGlow = pow(center, 1.88)
                       * (0.012 + ubuf.parameters.x * 0.035
                          + slowBass * 0.045 + beatPulse * 0.12)
                       * ubuf.styleAudio.w * centerLampScale;
        // No extra whole-field gain, shoulders or soft-cap compression.
        // Emitted ripples remain separate from the finite musical relief disk.
        // Keep the legacy42-unit safety budget on native wave overlap only;
        // it must not compress the reference frequency field or its rest slab.
        float nativeWaveLift = referenceRippleMode ? 0.0
            : 42.0 * (1.0 - exp(-max(0.0, ripple * amplitude * 0.55) / 42.0));
        float height = max(0.0, instanceScale.y)
            + max(0.0, idle + bandRelief + nativeWaveLift
                      + referenceRippleElevation);
        scale.y = height;
        // Instance width is physical geometry, not layout spacing.
        float readabilityScale = referenceGeometry ? 1.0 : 1.28;
        scale.xz *= clamp(ubuf.sceneControls.w, 0.5, 2.0) * readabilityScale;
#ifndef TERRAIN_SHADOW_PASS
        referenceRippleAnim = clamp(referenceRipple, vec2(0.0), vec2(1.0));
#endif
        // Jelly is an optical material, not an extra whole-column beat scale.
        position.y += scale.y * 0.5;
        float stageHalfExtent = max(1.0, ubuf.sceneControls.z);
        float stageDistance = distanceFromCore;
        float edgeCoverage = 1.0 - smoothstep(stageHalfExtent * (55.0 / 84.0),
                                              stageHalfExtent * (78.0 / 84.0), stageDistance);
        opacity *= referenceGeometry
            ? max(0.0, edgeCoverage)
            : sqrt(sqrt(max(0.0, edgeCoverage)));
#ifndef TERRAIN_SHADOW_PASS
        opacity *= ubuf.sceneControls.x;
#endif
        topSurface = smoothstep(0.72, 0.98, vertexNormal.y);
        float flowPhase = fract(t * (0.12 + highEnergy * 0.22)
                                + position.x * 0.018 + position.z * 0.011
                                + coherentDetail * 0.16);
        float flowingBand = exp(-pow((flowPhase - 0.20) / 0.075, 2.0));
        float sparkle = pow(0.5 + 0.5 * sin(t * (4.2 + highEnergy * 9.0)
                                           + randomValue * 47.0), 18.0);
        float presence = clamp(bandsHigh.y * 0.42 + bandsHigh.z * 0.36
                               + bandsHigh.w * 0.22, 0.0, 1.0);
        // Selected columns catch glints with different timing and strength.
        // The fragment shader confines the flash to the top plane, with
        // local silver grains; this is not a constant whole-scene light.
        float sheenMask = mix(0.60, 1.0, randomValue);
        streamSheen = ubuf.styleExtra.z
                    * (0.075 + presence * 0.72 + beatPulse * 0.22)
                    * (flowingBand * 1.18 + sparkle * 1.36)
                    * sheenMask * (0.25 + ubuf.styleParameters.z * 1.9);
        // Stable sparse quarter of columns; no frame-to-frame reshuffle.
        streamSheen *= 1.0 - step(0.25, randomValue);
    } else if (type < 1.5) {
        float index = instanceData.w;
        float rotationRate = 0.18 + mod(index * 7.0, 10.0) * 0.035;
        position.y += sin(t * (0.55 + rotationRate) + index * 0.73) * 0.45
                    + ubuf.floatingParameters.x * ubuf.floatingParameters.w * 1.4;
        scale *= ubuf.floatingParameters.z;
        opacity = 1.0;
    } else if (type < 2.5) {
        float group = floor(instanceData.w + 0.001);
        float duration = ubuf.meteorTrajectory.z / max(0.001, ubuf.meteorTrajectory.w * 60.0);
        float visibleFactor = (1.0 - step(0.5, abs(group + 1.0 - ubuf.impact.z)))
            * (1.0 - step(duration, flightAge));
        position.xz = ubuf.meteorTrajectory.xy;
        position.y = max(0.0, ubuf.meteorTrajectory.z - ubuf.meteorTrajectory.w * 60.0 * flightAge);
        scale = vec3(0.4, 1.2, 0.4) * 1.5 * visibleFactor;
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
    } else if (type > 6.5) {
        // Reference particle pool supplies already-integrated world positions.
        opacity = instanceData.z;
    } else {
        // Retired legacy tail/ring/burst kinds are never emitted.
        opacity = 0.0;
        scale = vec3(0.0);
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
        // A readable focal hierarchy inside each preset's own palette:
        // warmer central relief, cooler middle distance, quiet outer apron.
        float focalColor = exp(-distanceFromCore * distanceFromCore / 780.0);
        color = mix(color, mix(warm, accent, 0.22), focalColor * 0.78);
        float colorRadius = min(ubuf.sceneControls.z, responseRadius * 1.4);
        float edge = smoothstep(0.34, 0.94,
                                clamp(distanceFromCore / colorRadius, 0.0, 1.0));
        color = mix(color, base, edge * 0.52);
        float corePresence = pow(clamp(1.0 - distanceFromCore / colorRadius,
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
    color = mix(color, peak, layeredDistance * 0.035);
    if (ubuf.styleDynamics.z > 1.5 || ubuf.styleExtra.x > 0.5) {
        // Sweep through the preset's palette rather than replacing every
        // preset with the same absolute rainbow.
        float sweep = 0.5 + 0.5 * sin(t * 0.22 + distanceFromCore * 0.025);
        vec3 cyclingColor = mix(mix(cool, warm, smoothstep(0.0, 0.55, sweep)),
                               accent, smoothstep(0.55, 1.0, sweep));
        color = mix(color, cyclingColor, 0.45);
    } else if (ubuf.styleDynamics.z > 0.5) {
        color = mix(color, mix(cool, accent,
                    clamp(distanceFromCore / 110.0, 0.0, 1.0)), 0.25);
    }
    if (distanceFromCore < 24.0 && type < 0.5) {
        color = mix(color, peak,
                    clamp(0.025 * ubuf.styleAudio.w
                          * (1.0 - distanceFromCore / 24.0), 0.0, 0.025));
    }
    if (type < 0.5 && rippleWave > 0.001) {
        vec3 ringTint = travelingWaveTint / max(0.001, travelingWaveWeight);
        color = mix(color, ringTint, clamp(rippleWave * 0.78, 0.0, 0.96));
#ifndef TERRAIN_SHADOW_PASS
        travelingWave = vec4(ringTint, clamp(rippleWave * 0.78, 0.0, 0.96));
#endif
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
    if (type > 0.5 && type < 1.5) {
        // Stable per-crystal variety inside the active preset, not one
        // washed-out peak color shared by every floating instance.
        color = mix(cool, warm, smoothstep(0.0, 0.55, randomValue));
        color = mix(color, accent, smoothstep(0.55, 0.90, randomValue));
        color = mix(color, peak, smoothstep(0.90, 1.0, randomValue) * 0.15);
    }

    if (type < 0.5) scale = max(scale, vec3(0.001));
    vec3 localVertex = vertexPosition * scale;
    vec3 normal = vertexNormal;
    if (type > 0.5 && type < 1.5) {
        float index=instanceData.w, rate=0.18+mod(index*7.0,10.0)*0.035;
        vec3 angles=vec3(t*rate+index*.73,t*rate*.7+index*.73,t*rate*.45);
        vec3 c=cos(angles),s=sin(angles);
        mat3 rx=mat3(1,0,0,0,c.x,s.x,0,-s.x,c.x);
        mat3 ry=mat3(c.y,0,-s.y,0,1,0,s.y,0,c.y);
        mat3 rz=mat3(c.z,s.z,0,-s.z,c.z,0,0,0,1);
        mat3 rotation=rx*ry*rz;
        localVertex=rotation*localVertex;normal=rotation*normal;
    }
    worldPosition = position + localVertex;
    if (ubuf.timbre.w > 0.5 && ubuf.rippleColor.w > 0.5) {
        float angle = ubuf.stylePresentation.w;
        mat3 platter = mat3(cos(angle),0,-sin(angle),0,1,0,sin(angle),0,cos(angle));
        worldPosition = platter * worldPosition;
        normal = platter * normal;
    }
    gl_Position = ubuf.mvp * vec4(worldPosition, 1.0);
    worldNormal = normalize(normal);
    // Interpolate the actual eye vector, not six normalized vertex rays.
    // The fragment material converts it through the nonuniform column scale.
    viewDirection = ubuf.cameraPosition.xyz - worldPosition;
    objectKind = type;
    columnExtent = scale;
    if (type > 0.5 && type < 1.5) {
        columnExtent = instancePosition;
        columnRandom = fract(sin(dot(instancePosition.xz, vec2(12.9898,78.233))) * 43758.5453123);
    }
    // Material activation follows added relief, not the physical rest slab.
    reliefHeight = type < 0.5 ? max(0.0, scale.y - instanceScale.y) : 0.0;
    surfacePosition = type < 0.5 ? localVertex / scale : vertexPosition;
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
    // Fragment elasticity reads its original uniform directly. Reuse this
    // varying for exposure instead of increasing the interpolator budget.
    material.z = ubuf.sceneControls.y;
    // Ink density is unused by non-ink material. Reuse its varying lane as a
    // signed rainbow-emission flag instead of adding an interpolator/uniform.
    if (type < 0.5 && material.x < 1.5 && ubuf.styleDynamics.z > 2.5)
        material.w = -1.0;
    // Add depth behind the focal center without attenuating its highlights
    // again: the radial fog above already controls the scene's exposure.
    fog *= exp(-max(0.0, length(ubuf.cameraPosition.xyz - worldPosition)
                       - length(ubuf.cameraPosition.xyz))
               * (0.0012 + ubuf.cameraPosition.w * 0.0025));
}
