#version 440

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
layout(location = 21) flat in vec2 referenceInstancePosition;
layout(binding = 1) uniform sampler2D shadowDepth;
layout(location = 0) out vec4 fragColor;

// Same std140 contract as terrain_reactor.vert / terrain_reactor_gpu_data.hpp.
// The existing binding is already available to both stages; no new upload.
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

vec3 srgbToLinear(vec3 value)
{
    value = clamp(value, vec3(0.0), vec3(1.0));
    return mix(value / 12.92, pow((value + 0.055) / 1.055, vec3(2.4)),
               greaterThan(value, vec3(0.04045)));
}

float externalVisibility()
{
    if (ubuf.shadowParameters.x < 0.5) return 1.0;
    vec3 normal = normalize(worldNormal);
    vec3 key = normalize(vec3(-0.55, 0.85, 0.45));
    // Small physical normal offset + slope bias avoid self-acne without
    // separating a neighbour's contact shadow from its base.
    vec4 projected = ubuf.lightMvp * vec4(worldPosition + normal * 0.035, 1.0);
    vec3 ndc = projected.xyz / projected.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (ubuf.shadowParameters.w > 0.5) uv.y = 1.0 - uv.y;
    float depth = ubuf.shadowParameters.z > 0.5 ? ndc.z : ndc.z * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))
        || depth <= 0.0 || depth >= 1.0) return 1.0;
    float bias = 0.00012 + (1.0 - max(dot(normal, key), 0.0)) * 0.0003;
    float visible = 0.0;
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) {
            vec2 offset = (vec2(x, y) - 0.5) * ubuf.shadowParameters.y;
            visible += step(depth - bias, texture(shadowDepth, uv + offset).r);
        }
    // Shared key visibility only: room fill and the inner emitter stay alive.
    // Global shell transparency attenuates the shadow continuously; only the
    // spatial rim fade participates in the caster cutoff in the depth pass.
    // Four PCF samples accumulate in [0,4]; normalize once to visibility [0,1].
    return mix(1.0, visible * 0.25, clamp(ubuf.sceneControls.x, 0.0, 1.0));
}


vec3 mediumTint(float height)
{
    if (material.w >= 0.0)
        return srgbToLinear(color);
    vec3 lower = mix(srgbToLinear(vec3(1.0, 0.65, 0.18)),
                     srgbToLinear(vec3(1.0, 0.20, 0.55)), smoothstep(0.0, 0.4, height));
    vec3 upper = mix(srgbToLinear(vec3(0.64, 0.32, 1.0)),
                     srgbToLinear(vec3(0.18, 0.94, 1.0)), smoothstep(0.7, 1.0, height));
    return mix(lower, upper, smoothstep(0.3, 0.65, height));
}

vec3 boundedSource(vec3 source, float budget)
{
    return source / max(1.0, max(source.r, max(source.g, source.b)) / budget);
}

void columnMedium(vec3 normal, vec3 view, float ior, out vec3 emittedLight, out vec3 risingLight)
{
    vec3 extent = max(columnExtent, vec3(0.001));
    // Analytic optical path inside the actual box. No ray marching, flow
    // texture, scene-color copy or per-column transparency sorting required.
    // This transmits the inner emitter, not geometry behind another column.
    vec3 ray = refract(-view, normal, 1.0 / ior);
    vec3 position = clamp(surfacePosition, vec3(-0.4999), vec3(0.4999)) * extent;
    vec3 exitPlane = mix(-extent * 0.5, extent * 0.5, greaterThanEqual(ray, vec3(0.0)));
    vec3 rayDistance = abs(exitPlane - position) / max(abs(ray), vec3(0.00001));
    float travel = min(rayDistance.x, min(rayDistance.y, rayDistance.z));
    travel = min(travel, length(extent));
    // A separate inset rectangular emitter, seen through a clear shell.
    // The ray's entry/exit changes with view, giving real interior parallax
    // without pretending to refract the scene behind the column.
    vec3 inverseRay = mix(vec3(-1.0), vec3(1.0), greaterThanEqual(ray, vec3(0.0)))
                    / max(abs(ray), vec3(0.00001));
    // A thin optical shell, not a percentage-height opaque lid. Tall columns
    // must keep their emitter close to the cap as their height changes.
    vec3 shellThickness = min(extent * 0.006, vec3(0.008));
    vec3 innerHalf = extent * 0.5 - shellThickness;
    vec3 firstHit = (-innerHalf - position) * inverseRay;
    vec3 lastHit = (innerHalf - position) * inverseRay;
    vec3 entry = min(firstHit, lastHit);
    vec3 leave = max(firstHit, lastHit);
    float innerStart = max(0.0, max(entry.x, max(entry.y, entry.z)));
    float innerEnd = min(travel, min(leave.x, min(leave.y, leave.z)));
    float innerLength = max(0.0, innerEnd - innerStart);
    float height = clamp((position.y + ray.y * (innerStart + innerLength * 0.5))
                         / extent.y + 0.5, 0.0, 1.0);
    vec3 tint = mediumTint(height);
    vec3 absorption = -log(max(tint, vec3(0.015))) * 0.04 + vec3(0.015);
    vec4 lowMidBands = clamp(ubuf.bandsLow * ubuf.equalizerLow, vec4(0.0), vec4(1.0));
    float lowMidEnergy = dot(lowMidBands, vec4(0.35, 0.30, 0.20, 0.15));
    vec4 upperBands = clamp(ubuf.bandsHigh * ubuf.equalizerHigh, vec4(0.0), vec4(1.0));
    float upperEnergy = dot(upperBands, vec4(0.38, 0.28, 0.20, 0.14));
    // No constant core lamp: sustained low/mid energy feeds the volume,
    // while the existing spatial beat and impact envelopes excite it further.
    float coreRadius = max(16.0, ubuf.styleAudio.z * 0.65);
    float coreField = exp(-dot(worldPosition.xz, worldPosition.xz)
                         / (coreRadius * coreRadius));
    float steadyField = 0.35 + 0.65 * coreField;
    // Elasticity drives a bounded inner-light response, not another height
    // scale. Read the original uniform; material.z carries display opacity.
    float elasticLight = step(0.5, material.x)
                       * clamp(ubuf.materialParameters.z, 0.0, 1.0)
                       * clamp(ubuf.audioEnvelope.z, 0.0, 1.0)
                       * coreField * clamp(ubuf.styleAudio.w, 0.0, 1.5);
    // Preserve headroom in sustained runtime passages, then let the actual
    // beat drive the inner core. Reference replay retains its captured light.
    bool runtimeTheme = ubuf.timbre.w >= 0.5;
    float pulse = runtimeTheme
        ? lowMidEnergy * 0.78 * steadyField * clamp(ubuf.styleAudio.w, 0.0, 1.5)
          + upperEnergy * 0.12 * clamp(ubuf.styleAudio.w, 0.0, 1.5)
          + clamp(musicLight, 0.0, 1.0) * 1.25
          + clamp(impactLight, 0.0, 1.0) * 1.00
        : lowMidEnergy * 1.05 * steadyField * clamp(ubuf.styleAudio.w, 0.0, 1.5)
          + upperEnergy * 0.18 * clamp(ubuf.styleAudio.w, 0.0, 1.5)
          + clamp(musicLight, 0.0, 1.0) * 0.85
          + clamp(impactLight, 0.0, 1.0) * 1.20;
    float sourcePower = clamp(ubuf.sceneLighting.x, 0.0, 2.0)
                      * (0.38 + clamp(glow, 0.0, 2.0) * 0.16) * pulse
                      * (runtimeTheme ? 1.18 : 1.4);
    // Emission belongs to the raised musical relief. Leave the flat apron
    // quiet so troughs and travelling wave crests retain visual separation.
    sourcePower *= mix(0.28, 1.0, smoothstep(0.12, 2.2, extent.y));
    // A distributed source fills the same medium up to its boundary.
    // A grazing ray missing the inset core must not paint an opaque black rim.
    // This remains audio-powered; it does not add a constant ambient lamp.
    // Diffuse light transport within the gel fills thin/grazing paths. Keep
    // optical-depth variation, but do not turn the perimeter into a dark cage.
    float crossSection = min(extent.x, extent.z);
    float opticalLength = max(crossSection * 0.32,
                              innerLength * 0.58 + crossSection * 0.42);
    // Near-surface scattering lets the flat cap receive the upper emitter's
    // light rather than the darker midpoint of a long downward viewing ray.
    float luminousHeight = mix(height, clamp(surfacePosition.y + 0.5, 0.0, 1.0), 0.75);
    // A smooth volume pulse rises from the foot after each detected bass beat.
    // It changes emitted light only: the shell and the ground never move.
    float riseAge = clamp(ubuf.audioEnvelope.w, 0.0, 1.0);
    float riseHeight = mix(0.16, 1.20, smoothstep(0.0, 0.80, riseAge));
    float riseProfile = exp(-pow((luminousHeight - riseHeight) / 0.26, 2.0));
    // Elasticity follows the traveling band rather than washing the whole face.
    float risingSource = (clamp(musicLight, 0.0, 1.0) + elasticLight) * riseProfile
                       * (1.0 - smoothstep(0.72, 1.0, riseAge))
                       * clamp(ubuf.sceneLighting.x, 0.0, 2.0) * 0.24;
    emittedLight = exp(-absorption * innerStart)
                 * ((vec3(1.0) - exp(-absorption * opticalLength)) / absorption)
                 * tint * sourcePower * (0.12 + 0.88 * luminousHeight * luminousHeight) * 2.6;
    risingLight = exp(-absorption * innerStart)
        * ((vec3(1.0)-exp(-absorption*opticalLength))/absorption)
        * tint * risingSource * 2.6;
}

vec3 receivedColumnLight(vec3 normal)
{
    if (ubuf.sceneLighting.x <= 0.0 || ubuf.sceneLighting.y <= 0.0)
        return vec3(0.0);
    // Neighbour spill is a wall-to-wall transport cue. Letting the same
    // representative sources illuminate upward-facing caps produced several
    // broad, projector-shaped pools across the terrain. Keep the reflection
    // on side/bevel faces so adjacent columns still share musical light, while
    // the cap remains driven by its own inner core and top glints.
    float receiverWall = 1.0 - smoothstep(0.20, 0.78, normal.y);
    // Four representative emitter groups share the existing spectrum/palette.
    // Real world-space distance and receiver normals light the neighbouring
    // terrain; this is intentionally bounded, shadowless group lighting, not
    // one point light or a shadow map for every individual column.
    const vec2 centers[4] = vec2[4](vec2(0.0), vec2(-24.0, 18.0),
                                    vec2(26.0, 12.0), vec2(4.0, -28.0));
    vec3 worldPosition = ubuf.cameraPosition.xyz - viewDirection;
    // Each representative source lights its local group, not the entire array.
    // Radius is a ground footprint; vertical separation only affects diffuse.
    float radiusControl = clamp(ubuf.sceneLighting.z, 0.0, 2.0);
    float radius = 4.0 + radiusControl * radiusControl * 8.0;
    vec3 received = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        float energy = clamp(ubuf.bandsLow[i] * ubuf.equalizerLow[i], 0.0, 1.0);
        vec3 lightPosition = vec3(centers[i].x, 14.0 + energy * 12.0, centers[i].y);
        vec3 delta = lightPosition - worldPosition;
        float distanceSquared = max(dot(delta, delta), 0.01);
        float attenuation = pow(clamp(1.0 - dot(delta.xz, delta.xz) / (radius * radius), 0.0, 1.0), 2.0);
        float diffuse = max(dot(normal, delta * inversesqrt(distanceSquared)), 0.0);
        vec3 tint = srgbToLinear(ubuf.colors[i + 1].rgb);
        // The representative emitters also go dark when their music stops.
        float power = energy * 0.80 + clamp(ubuf.audioEnvelope.z, 0.0, 1.0) * 0.45;
        received += tint * attenuation * diffuse * power;
    }
    return min(received * receiverWall * ubuf.sceneLighting.x
                    * ubuf.sceneLighting.y * 0.85,
               vec3(0.45));
}

vec3 terrainMaterial(vec3 normal, vec3 view)
{
    float jelly = step(0.5, material.x);
    float clarity = clamp(light, 0.0, 1.5);
    float ior = mix(1.46, 1.38, jelly);
    // MapShaderMaterial has an explicit body/ripple contract. Its low bands
    // deform the height field, but do not enter the native volumetric,
    // received-light, or thin-shell emission systems below.
    // A canonical palette selects the original material in the actual player
    // as well as in replay fixtures. Runtime flag 2 identifies a manual
    // material without a canonical palette; its lighting controls stay live.
    bool referenceMode = ubuf.timbre.w < 1.5 && ubuf.bodyColor.a > 0.5
        && (ubuf.rippleColor.a > 0.5 || ubuf.sceneControls.z <= 84.5);
    vec3 albedo = srgbToLinear(color);
    vec3 columnCenter = worldPosition - surfacePosition * columnExtent;
    if (referenceMode) {
        // Fixed Sonic Topography material contract. Except for the explicitly
        // authored localized-core material, keep this branch literal:
        // AgPlayer's optional gel lighting below must never perturb the
        // reference theme palette, cap edge, side gradient, ripple channels,
        // aerial perspective, or direct linear output.
        float relativeY = clamp(surfacePosition.y + 0.5, 0.0, 1.0);
        float distanceFromTop = 1.0 - relativeY;
        float normalizedElevation = clamp(reliefHeight / 8.0, 0.0, 1.0);
        float centerDistance = length(referenceInstancePosition);
        vec3 base1 = srgbToLinear(ubuf.colors[0].rgb);
        vec3 base2 = srgbToLinear(ubuf.bodyColor.rgb);
        vec3 coolCore = srgbToLinear(ubuf.colors[1].rgb);
        vec3 warmCore = srgbToLinear(ubuf.colors[2].rgb);
        vec3 coolEdge = srgbToLinear(ubuf.colors[3].rgb);
        vec3 warmEdge = srgbToLinear(ubuf.colors[4].rgb);
        float warmBlend = smoothstep(0.0, 1.0,
            clamp(ubuf.timbre.x, 0.0, 1.0) * 1.5 + 0.5 - centerDistance / 80.0);
        bool localizedCore = ubuf.bodyColor.a > 1.5;
        if (localizedCore) warmBlend = 0.0;
        vec3 zoneCore = mix(coolCore, warmCore, warmBlend);
        vec3 zoneEdge = mix(coolEdge, warmEdge, warmBlend);
        vec3 targetGlow = mix(zoneCore, zoneEdge, fract(columnRandom * 11.0));
        float distanceFade = 1.0 - smoothstep(40.0, 75.0, centerDistance);
        vec3 brightCool = mix(coolCore, vec3(1.0), 0.24);
        targetGlow = mix(targetGlow, brightCool,
                         clamp(ubuf.timbre.y, 0.0, 1.0) * 0.6);
        vec3 currentGlow = mix(base2, targetGlow, normalizedElevation)
                         * ubuf.styleParameters.z * distanceFade;
        currentGlow = mix(currentGlow, srgbToLinear(ubuf.rippleColor.rgb),
                          referenceRippleAnim.x);
        // The single-column study uses the production 112-unit stage. Its
        // cool base and coverage create a small warm delta at the white crest;
        // compensate only that widened runtime stage. The fixed 84-unit
        // reference replay keeps the literal vec3(1) source result.
        vec3 referenceWhite = mix(vec3(1.0), vec3(0.93, 1.0, 1.07),
                                  step(84.5, ubuf.sceneControls.z));
        currentGlow = mix(currentGlow, referenceWhite, referenceRippleAnim.y);
        vec3 referenceBody = mix(base1, base2, relativeY * distanceFade);
        vec3 result;
        bool isTop = normal.y > 0.5;
        if (isTop) {
            float topIntensity = smoothstep(0.0, 0.4, normalizedElevation);
            float twinkleDistance = smoothstep(60.0, 30.0, centerDistance);
            float twinkleMultiplier = mix(twinkleDistance, 1.0,
                smoothstep(0.01, 0.10, normalizedElevation));
            if (fract(columnRandom * 31.0) > 0.95 && normalizedElevation < 0.10)
                topIntensity += ubuf.bandsHigh.w * 2.0 * twinkleMultiplier;
            result = mix(base2, currentGlow, topIntensity);
            vec2 capUv = surfacePosition.xz + vec2(0.5);
            float edgeX = smoothstep(0.05, 0.01, capUv.x)
                        + smoothstep(0.95, 0.99, capUv.x);
            float edgeY = smoothstep(0.05, 0.01, capUv.y)
                        + smoothstep(0.95, 0.99, capUv.y);
            float edge = min(edgeX + edgeY, 1.0);
            result += currentGlow * edge * 0.8 * (topIntensity + 0.3);
            float flashChance = smoothstep(0.3, 1.0, ubuf.bandsHigh.y);
            if (fract(columnRandom * 53.0) > 0.98 - flashChance * 0.10) {
                float flashSync = sin(ubuf.parameters.w * 40.0
                                    + columnRandom * 100.0) * 0.5 + 0.5;
                result += mix(vec3(1.0), vec3(0.5, 1.0, 1.0), columnRandom)
                        * flashSync * ubuf.bandsHigh.y
                        * (1.0 + ubuf.timbre.z * 2.0) * twinkleMultiplier;
            }
            if (edge > 0.5
                && fract(columnRandom * 89.0 + ubuf.parameters.w * 2.0) > 0.98)
                result += vec3(1.0) * ubuf.bandsHigh.z * 3.0 * twinkleMultiplier;
        } else {
            // AudioEngine sharpness is a positive brightness delta * 10,
            // not a percentage. Preserve the source's sharper side-light
            // contraction above one instead of flattening strong attacks.
            float verticalFalloff = mix(1.0, 3.0, ubuf.timbre.z);
            float sideGlow = smoothstep(0.5 / verticalFalloff, 0.0,
                                        distanceFromTop) * normalizedElevation;
            if (normalizedElevation < 0.02) sideGlow = 0.0;
            result = mix(referenceBody, currentGlow, sideGlow * 1.5);
            float rimGlow = smoothstep(0.03, 0.0, distanceFromTop)
                          * normalizedElevation;
            result += currentGlow * rimGlow;
        }
        result += srgbToLinear(ubuf.rippleColor.rgb)
                * referenceRippleAnim.x * 0.6;
        result += referenceWhite * referenceRippleAnim.y * 1.2;
        if (localizedCore) {
            // Violet Heart: pink is an interior accent, not an audio-warmth
            // wash over the entire field. Use actual elevation (no new beat
            // clock); keep caps/ripples violet and let a small hot center warm
            // towards yellow only as the middle rises.
            float center = 1.0 - smoothstep(6.0, 22.0, centerDistance);
            float raised = smoothstep(0.02, 0.60, normalizedElevation);
            float hotCenter = (1.0 - smoothstep(0.0, 6.0, centerDistance))
                            * smoothstep(0.30, 0.80, normalizedElevation);
            vec3 innerTint = mix(warmCore, warmEdge, hotCenter * 0.85);
            float interior = isTop ? 0.0 : smoothstep(0.08, 0.22, relativeY)
                * (1.0 - smoothstep(0.68, 0.92, relativeY));
            result = mix(result, innerTint * ubuf.styleParameters.z,
                         center * raised * interior * 0.85);
        }
        vec3 atmosphere = mix(base1, base2, 0.4);
        result = mix(result, atmosphere,
                     smoothstep(30.0, 65.0, centerDistance) * 0.35);
        vec3 backdrop = ubuf.atmosphereColor.a > 0.5
                      ? srgbToLinear(ubuf.atmosphereColor.rgb) : vec3(0.0);
        float alphaBlend = smoothstep(55.0, 78.0, centerDistance);
        result = mix(result, backdrop, alphaBlend * 0.45);
        // On the wider production stage the newly exposed side coverage of a
        // white crest is still tinted by the cool base. Balance that coverage
        // without touching the fixed 84-unit reference replay or its oracle.
        float runtimeWhite = step(84.5, ubuf.sceneControls.z)
                           * referenceRippleAnim.y;
        result *= mix(vec3(1.0), vec3(0.70, 1.0, 1.0), runtimeWhite);
        return clamp(result * clamp(material.z, 0.0, 2.0),
                     vec3(0.0), vec3(1.0));
    }
    if (ubuf.bodyColor.a > 0.5) {
        // Theme roles are encoded only for transport; interpolate in linear
        // light. A missing production descriptor remains zero, not a fake band.
        // Reuse the vertex hash: fragment-position hashing causes face speckle.
        float warmWeight = smoothstep(0.0, 1.0,
            clamp(ubuf.timbre.x, 0.0, 1.0) * 1.5 + 0.5 - length(columnCenter.xz) / 80.0);
        vec3 coreTint = mix(srgbToLinear(ubuf.colors[1].rgb),
                            srgbToLinear(ubuf.colors[2].rgb), warmWeight);
        vec3 edgeTint = mix(srgbToLinear(ubuf.colors[3].rgb),
                            srgbToLinear(ubuf.colors[4].rgb), warmWeight);
        albedo = mix(coreTint, edgeTint, fract(columnRandom * 11.0));
        vec3 brightTint = mix(srgbToLinear(ubuf.colors[1].rgb), vec3(1.0), 0.24);
        albedo = mix(albedo, brightTint, clamp(ubuf.timbre.y, 0.0, 1.0) * 0.6);
        // The main vertex pass forwards the individual travelling-wave palette
        // before its legacy color stack. Apply it to the explicit theme's
        // linear material directly; shadow deformation has no color interface.
        albedo = mix(albedo, srgbToLinear(travelingWave.rgb),
                     clamp(travelingWave.a, 0.0, 0.96));
    }
    vec3 emission = vec3(0.0);
    vec3 risingLight = vec3(0.0);
    if (!referenceMode)
        columnMedium(normal, view, ior, emission, risingLight);
    // The terrain is a luminous solid, not a mirror of studio softboxes.
    // Fixed face-local gradients reveal its shape even between musical pulses.
    float surfaceHeight = clamp(surfacePosition.y + 0.5, 0.0, 1.0);
    float capFace = smoothstep(0.70, 0.98, normal.y);
    float borderDistance = 0.5 - max(abs(surfacePosition.x), abs(surfacePosition.z));
    // Softness controls the luminous edge rolloff, not an external reflection.
    float borderScale = 0.55 + clamp(material.y, 0.0, 1.0);
    float borderWidth = max(0.110 * borderScale, fwidth(borderDistance) * 1.5);
    float capBorder = capFace * (1.0 - smoothstep(0.010 * borderScale,
                                                borderWidth, borderDistance));
    if (ubuf.bodyColor.a > 0.5) {
        // Theme edge occupies a fixed face-local strip. Derivative-driven
        // widening can cover an entire distant cap and inflate its energy.
        // Material softness may widen that bounded strip, but never delegates
        // the width to screen-space derivatives.
        vec2 edgeDistance = vec2(0.5) - abs(surfacePosition.xz);
        float themeEdgeWidth = mix(0.035, 0.145,
                                   clamp(material.y, 0.0, 1.0));
        vec2 edgeGlow = vec2(1.0) - smoothstep(vec2(0.01),
                                               vec2(themeEdgeWidth), edgeDistance);
        capBorder = capFace * min(1.0, edgeGlow.x + edgeGlow.y);
    }
    float wallGradient = mix(0.055, 0.32, surfaceHeight * surfaceHeight);
    vec3 bodyTint = ubuf.bodyColor.a > 0.5 ? srgbToLinear(ubuf.bodyColor.rgb) : albedo;
    // A quiet cap preserves the theme's second base color. Added musical
    // elevation, not slab height or an external lamp, reveals its colored face.
    float raisedFraction = clamp(reliefHeight / 8.0, 0.0, 1.0);
    float faceActivation = smoothstep(0.0, 3.2, reliefHeight);
    float paletteReach = 1.0 - smoothstep(40.0, 75.0, length(columnCenter.xz));
    vec3 capGlow = mix(bodyTint, albedo, raisedFraction)
                 * ubuf.styleParameters.z * paletteReach;
    if (ubuf.bodyColor.a > 0.5 && ubuf.rippleColor.a > 0.5) {
        // Equivalent to the reference currentGlow override. Both channels are
        // independent, so normal/white overlap adds rather than cancels.
        capGlow = mix(capGlow, srgbToLinear(ubuf.rippleColor.rgb), referenceRippleAnim.x);
        capGlow = mix(capGlow, vec3(0.95, 1.0, 1.05), referenceRippleAnim.y * 0.5);
    }
    vec3 capLight = mix(bodyTint, capGlow, faceActivation);
    vec3 wallLight = bodyTint * wallGradient * (0.65 + clarity * 0.45)
                   * (0.25 + 0.75 * clamp(externalVisibility(), 0.0, 1.0));
    if (ubuf.bodyColor.a > 0.5) {
        // Theme bases describe an unlit luminous body, not a diffuse surface
        // under a studio key. Keep both colors and interpolate in linear space;
        // clarity and the shadow map must not turn this layer into a black shell.
        vec3 footColor = ubuf.sceneControls.z > 84.5
            ? bodyTint * 0.25
            : srgbToLinear(ubuf.colors[0].rgb);
        wallLight = mix(footColor, bodyTint, surfaceHeight * paletteReach);
        // Audio sharpness narrows the cap's vertical color reach. It is not
        // the user's material softness slider or an extra exposure multiplier.
        float verticalReach = 0.5 / mix(1.0, 3.0, clamp(ubuf.timbre.z, 0.0, 1.0));
        float upperGlow = raisedFraction < 0.02 ? 0.0
            : raisedFraction * (1.0 - smoothstep(0.0, verticalReach, 1.0 - surfaceHeight));
        wallLight = mix(wallLight, capGlow, upperGlow * 1.5);
        float shoulderRim = raisedFraction
            * (1.0 - smoothstep(0.0, 0.03, 1.0 - surfaceHeight));
        wallLight += capGlow * shoulderRim;
    }
    vec3 bodyLight = mix(wallLight, capLight, capFace);
    // Clarity separates faces and cap edges without introducing a black rim.
    // The control changes local contrast around the existing body colour; it
    // never supplies an independent studio light.
    float clarityAmount = clamp((clarity - 0.2) / 1.2, 0.0, 1.0);
    float faceDirection = max(dot(normal, normalize(vec3(-0.48, 0.74, 0.46))), 0.0);
    bodyLight *= mix(0.38, mix(0.58, 3.55, faceDirection), clarityAmount);
    // A faint self-luminous body keeps the floating platform readable in a
    // lightless environment. It follows the material colour and remains well
    // below the music-driven internal emitter.
    bodyLight += bodyTint * mix(0.10, 0.38, capFace)
               * (0.34 + clarityAmount * 0.66);
    bodyLight += capGlow * capBorder * clarityAmount * 2.00;
    float restingSurface = 1.0 - smoothstep(0.15, 1.6, reliefHeight);
    float runtimeRestLight = mix(0.20, 0.38, step(0.5, ubuf.timbre.w));
    bodyLight += bodyTint * restingSurface * capFace * runtimeRestLight;
    // This quiet rim is part of the static theme material.  The air response
    // below replaces its intensity delta only for a selected active cap.
    vec3 quietCapRim = capGlow * capBorder * (0.36 + faceActivation * 0.8);
    bodyLight += quietCapRim;
    if (!referenceMode)
        bodyLight += boundedSource(receivedColumnLight(normal) * mix(vec3(1.0), albedo, 0.25) * 3.4, 0.25);
    else {
        // Built-in reference themes still receive discrete beat/impact energy
        // from inside the terrain. The pulse is spatially bounded by the
        // vertex-provided music/impact fields and cannot light the environment.
        float internalEvent = clamp(musicLight * 0.32 + impactLight * 0.72,
                                    0.0, 1.0);
        bodyLight += boundedSource(albedo * internalEvent
                                   * mix(0.62, 1.0, capFace), 0.22);
    }

    vec4 highBands = clamp(ubuf.bandsHigh * ubuf.equalizerHigh, vec4(0.0), vec4(1.0));
    float cap = smoothstep(0.70, 0.98, normal.y);
    if (!referenceMode) {
        // A shallow audio-excited layer remains visible on the thinnest ground
        // cells, whose integrated volume tends to zero. The source follows the
        // existing beat/impact envelopes, never an autonomous animation clock.
        // Keep it height-selective and colored beneath the dielectric interface.
        float bodyHeight = clamp(surfacePosition.y + 0.5, 0.0, 1.0);
        float sourceHeight = 0.06 + 0.94 * bodyHeight * bodyHeight;
        vec4 lowMidBands = clamp(ubuf.bandsLow * ubuf.equalizerLow, vec4(0.0), vec4(1.0));
        float lowMidEnergy = dot(lowMidBands, vec4(0.35, 0.30, 0.20, 0.15));
        float centerRadius = max(16.0, ubuf.styleAudio.z * 0.38);
        float centerField = exp(-dot(worldPosition.xz, worldPosition.xz)
                               / (centerRadius * centerRadius));
        float steadySource = lowMidEnergy * centerField * 0.30
                           * clamp(ubuf.styleAudio.w, 0.0, 1.5);
        float highEnergy = dot(highBands, vec4(0.38, 0.28, 0.20, 0.14));
        float highSource = highEnergy * centerField * 0.18
                         * smoothstep(0.15, 3.0, columnExtent.y)
                         * clamp(ubuf.styleAudio.w, 0.0, 1.5);
        float eventSource = clamp(musicLight, 0.0, 1.0) * 0.80
                          + clamp(impactLight, 0.0, 1.0) * 1.20;
        vec3 thinSource = mediumTint(bodyHeight) * sourceHeight * (steadySource + highSource + eventSource)
                  * clamp(ubuf.sceneLighting.x, 0.0, 2.0)
                  * (1.0 - smoothstep(0.10, 1.20, columnExtent.y));
        // Upward light escape distinguishes the flat cap from the clear walls
        // without an opaque border or an added external lamp.
        emission *= 1.0 + cap * 0.30;
        emission = boundedSource(emission, 0.46) + boundedSource(risingLight, 0.18)
                 + boundedSource(thinSource, 0.08);
    }
    if (ubuf.bodyColor.a > 0.5) {
        // Theme top flashes deliberately use the original grid-position hash
        // (columnRandom), not streamSheen's legacy instance selection.  This
        // is a face-wide high-frequency response plus a distinct cap-edge
        // brilliance pass; no 24x24 local-particle field is layered on it.
        // MapScene uploads these as three independent descriptors: presence,
        // brilliance and air.  They must not inherit the legacy combined-high
        // activity used by the historical non-theme particle path.
        float referencePresence = highBands.y;
        float referenceAir = highBands.w;
        float referenceBrilliance = highBands.z;
        float radius = length(columnCenter.xz);
        float distanceTwinkle = 1.0 - smoothstep(30.0, 60.0, radius);
        float twinkleMultiplier = mix(distanceTwinkle, 1.0,
                                      smoothstep(0.01, 0.10, raisedFraction));
        if (cap > 0.0 && ubuf.styleExtra.z > 0.5) {
            float topIntensity = smoothstep(0.0, 0.4, raisedFraction);
            if (fract(columnRandom * 31.0) > 0.95 && raisedFraction < 0.10)
                topIntensity += referenceAir * 2.0 * twinkleMultiplier;
            // Air reaches a resting low column in the original material.  Do
            // not run this mix for non-air cells: a high band alone must not
            // recolor every quiet theme cap.
            if (fract(columnRandom * 31.0) > 0.95 && referenceAir > 0.0
                && raisedFraction < 0.10) {
                vec3 referenceTop = mix(bodyTint, capGlow, topIntensity);
                // Keep the existing quiet rim and received-light terms, then
                // replace only the reference cap and rim contributions.
                // Mixing the complete bodyLight here would erase both before
                // the rim delta below and effectively subtract the quiet rim
                // twice on a fully top-facing fragment.
                bodyLight += (referenceTop - capLight) * cap;
                float airRimIntensity = topIntensity * mix(
                    1.8, 1.0, smoothstep(0.20, 0.80, topIntensity));
                vec3 airCapRim = capGlow * capBorder * 0.8 * (airRimIntensity + 0.3);
                bodyLight += airCapRim - quietCapRim;
                // The complete selected cap receives a restrained Air lift;
                // the rim above remains the stronger visual cue.
                bodyLight += capGlow * referenceAir * cap * 0.025;
            }
            float flashChance = smoothstep(0.3, 1.0, referencePresence);
            if (fract(columnRandom * 53.0) > 0.98 - flashChance * 0.10) {
                float flashSync = sin(ubuf.parameters.w * 40.0 + columnRandom * 100.0)
                                * 0.5 + 0.5;
                vec3 flashTint = mix(vec3(1.0), vec3(0.5, 1.0, 1.0), columnRandom);
                bodyLight += flashTint * flashSync * referencePresence
                           * (1.0 + clamp(ubuf.timbre.z, 0.0, 1.0) * 2.0)
                           * twinkleMultiplier * cap;
            }
            if (capBorder > 0.5
                && fract(columnRandom * 89.0 + ubuf.parameters.w * 2.0) > 0.98)
                bodyLight += vec3(1.0) * referenceBrilliance * 3.0
                           * twinkleMultiplier * cap;
        }
    } else {
    float flash = clamp(streamSheen, 0.0, 4.0);
    flash *= smoothstep(0.015, 0.15, flash);
    float flashEnergy = dot(highBands, vec4(0.0, 0.42, 0.36, 0.22)) * 2.0
                      + clamp(musicLight, 0.0, 1.0) + clamp(impactLight, 0.0, 1.0);
    // Timing alone cannot illuminate a silent reactor; this local glint is
    // powered by the same inner-light control as the volume and thin shell.
    flash *= clamp(flashEnergy, 0.0, 1.0) * clamp(ubuf.sceneLighting.x, 0.0, 2.0);
    // Derivatives must be evaluated outside non-uniform cap/flash branches.
    vec2 surfaceFootprint = fwidth(surfacePosition.xz);
    // Walls and unselected columns cannot show cap glints. Avoid their
    // per-fragment hash/trigonometry work entirely.
    if (cap > 0.0 && flash > 0.0001) {
    // Microfacets cover the cap, not its rim. Stable local cells receive
    // different phases; never regenerate random noise each frame. Fade into
    // their average once subpixel to avoid distant shimmering / aliasing.
    vec2 facetUv = (surfacePosition.xz + vec2(0.5)) * 24.0;
    vec2 facetCell = floor(facetUv);
    float facetSeed = fract(sin(dot(facetCell, vec2(127.1, 311.7))) * 43758.5453);
    float facetPhase = ubuf.parameters.w * (2.4 + facetSeed * 2.0)
                     + facetSeed * 6.2831853;
    float facetPulse = pow(0.5 + 0.5 * sin(facetPhase), 10.0);
    float facetFootprint = max(surfaceFootprint.x, surfaceFootprint.y) * 24.0;
    float facetResolved = 1.0 - smoothstep(0.65, 1.5, facetFootprint);
    // A second, fixed 4x4 cluster scale preserves sparse glints when the fine
    // facets are subpixel. Never enlarge noise continuously with camera zoom:
    // cross-fade fixed scales and filter even the clusters at extreme distance.
    vec2 clusterUv = (surfacePosition.xz + vec2(0.5)) * 4.0;
    vec3 columnCenter = worldPosition - surfacePosition * columnExtent;
    float clusterSeed = fract(sin(dot(floor(clusterUv), vec2(39.73, 81.19))
                         + dot(columnCenter.xz, vec2(0.73, 1.31))) * 15731.743);
    float clusterPulse = pow(0.5 + 0.5 * sin(ubuf.parameters.w
                         * (2.4 + clusterSeed * 2.0) + clusterSeed * 6.2831853), 10.0);
    float clusterFootprint = max(surfaceFootprint.x, surfaceFootprint.y) * 4.0;
    float clusterResolved = 1.0 - smoothstep(0.8, 1.8, clusterFootprint);
    // One coherent flash lights the entire selected plane. Fine facets are
    // restrained highlights on that sheet, not isolated patches replacing it.
    float distantGrain = mix(0.12, clusterPulse * 1.4, clusterResolved);
    float grain = mix(distantGrain, facetPulse * 1.8, facetResolved);
    // A subdued colored sheet preserves the plane; silver points carry the
    // sparkle, instead of making the entire face a white light source.
    vec3 sheet = mix(albedo, srgbToLinear(vec3(0.85, 0.94, 1.0)), 0.25) * 0.65;
    vec3 silver = mix(albedo, srgbToLinear(vec3(0.94, 0.97, 1.0)), 0.90);
    emission += boundedSource((sheet + silver * grain) * cap * (flash / (1.0 + flash)), 0.22);
    }
    }
    // Distributed subsurface light remains visible at grazing angles; keep
    // reflection contrast without letting a dark environment blacken the rim.
    // Limit the source's chromatic radiance as a vector, not each channel:
    // simultaneous sources retain their hue instead of clipping into white.
    // This only bounds excited emission; it is not an output tone curve.
    vec3 radiance = bodyLight + emission;
    if (ubuf.bodyColor.a > 0.5 && ubuf.rippleColor.a > 0.5) {
        // Reference terminal ripple light, after material/top construction.
        radiance += srgbToLinear(ubuf.rippleColor.rgb) * referenceRippleAnim.x * 0.6
                  + vec3(0.95, 1.0, 1.05) * referenceRippleAnim.y * 1.2;
    }
    float atmosphereVisibility = pow(clamp(fog, 0.0, 1.0), 1.35);
    vec3 atmosphere = ubuf.atmosphereColor.a > 0.5 ? srgbToLinear(ubuf.atmosphereColor.rgb) : vec3(0.0);
    if (ubuf.bodyColor.a > 0.5) {
        float radius = length(columnCenter.xz);
        vec3 aerialTint = mix(srgbToLinear(ubuf.colors[0].rgb), bodyTint, 0.4);
        radiance = mix(radiance, aerialTint, smoothstep(30.0, 65.0, radius) * 0.35);
        // Match the spatial coverage ramp, independently of user opacity.
        // Otherwise lowering shell opacity would change its actual color.
        float halfExtent = max(1.0, ubuf.sceneControls.z);
        float backdropMix = smoothstep(halfExtent * (55.0 / 84.0),
                                      halfExtent * (78.0 / 84.0), radius) * 0.45;
        radiance = mix(radiance, atmosphere, backdropMix);
    } else {
        radiance = mix(atmosphere, radiance, atmosphereVisibility)
                 * (0.80 + clamp(focus, 0.0, 1.0) * 0.20);
    }
    // Keep headroom for beat-driven inner light and silver cap flashes.
    radiance *= clamp(material.z, 0.0, 2.0)
              * (ubuf.timbre.w >= 0.5 ? 0.88 : 1.0);
    // Bright themes need the same material contrast as dark themes. Their
    // pale base plus cap/self-light layers can otherwise clip an entire field
    // to white, erasing every individual column. Apply a luminance-selected
    // shoulder only to pale built-in materials; dark/night palettes remain
    // bit-for-bit on their existing path.
    float brightTheme = step(0.5, ubuf.bodyColor.a)
        * smoothstep(0.45, 0.75,
            dot(bodyTint, vec3(0.2126, 0.7152, 0.0722)));
    vec3 brightThemeShoulder = radiance / (vec3(1.0) + radiance * 0.72);
    radiance = mix(radiance, brightThemeShoulder, brightTheme);
    // Reference runtime probe: a .18 output writes byte46 to UNORM.
    // No additional shoulder or display encoding on the terrain path.
    return clamp(radiance, vec3(0.0), vec3(1.0));
}

void main()
{
    if ((objectKind > 1.5 && objectKind < 2.5) || objectKind > 6.5) {
        if (opacity < .012) discard;
        // MeshBasicMaterial: no scene lighting or tone-map shoulder. Three's
        // basic material performs display encoding before its fog chunk.
        vec3 linearColor = ubuf.meteorMaterialColor.a > .5
            ? ubuf.meteorMaterialColor.rgb
            : mix(srgbToLinear(ubuf.colors[2].rgb), vec3(1), .7);
        vec3 encoded = mix(linearColor*12.92,
            1.055*pow(max(linearColor,vec3(0)),vec3(1.0/2.4))-.055,
            step(vec3(.0031308),linearColor));
        float viewDepth = 1.0/max(gl_FragCoord.w,.00001);
        vec3 result = mix(encoded,srgbToLinear(ubuf.colors[0].rgb),smoothstep(30.0,95.0,viewDepth));
        fragColor = vec4(result,opacity);
        return;
    }
    if (objectKind > 0.5 && objectKind < 1.5) {
        float distance = length(columnExtent.xz), pulse = ubuf.floatingParameters.y;
        float height = clamp(pulse * 2.5, 0.0, 1.0);
        float warm = smoothstep(0.0,1.0,ubuf.timbre.x*1.5+.5-distance/80.0);
        if (ubuf.bodyColor.a > 1.5) warm = 0.0;
        vec3 base = srgbToLinear(ubuf.bodyColor.rgb), fogColor = srgbToLinear(ubuf.atmosphereColor.rgb);
        vec3 cool = srgbToLinear(ubuf.colors[1].rgb);
        vec3 core = mix(cool,srgbToLinear(ubuf.colors[2].rgb),warm);
        vec3 rim = mix(srgbToLinear(ubuf.colors[3].rgb),srgbToLinear(ubuf.colors[4].rgb),warm);
        vec3 tint = mix(core,rim,fract(columnRandom*11.0));
        tint = mix(tint,mix(cool,vec3(1),.24),ubuf.timbre.y*.6);
        vec3 emission = mix(base,tint,height)*ubuf.styleParameters.z*(1.0-smoothstep(40.0,75.0,distance));
        vec3 ripple = srgbToLinear(ubuf.rippleColor.rgb);
        emission = mix(mix(emission,ripple,pulse*.8),vec3(1),pulse*.3);
        float excitation = smoothstep(0.0,.4,height);
        vec3 localNormal=abs(normalize(cross(dFdx(surfacePosition),dFdy(surfacePosition))));
        vec2 uv = localNormal.y>.5 ? surfacePosition.xz+.5
                : (localNormal.x>.5 ? surfacePosition.zy+.5 : surfacePosition.xy+.5);
        vec2 edge = smoothstep(vec2(.05),vec2(.01),uv)+smoothstep(vec2(.95),vec2(.99),uv);
        float edgeMask=min(edge.x+edge.y,1.0);
        vec3 result=mix(base,emission,excitation)+emission*edgeMask*.8*(excitation+.3);
        float sparkleRange=mix(smoothstep(60.0,30.0,distance),1.0,smoothstep(.01,.1,height));
        float presence=ubuf.bandsHigh.y;
        if(fract(columnRandom*53.0)>.98-smoothstep(.3,1.0,presence)*.1)
            result+=mix(vec3(1),vec3(.5,1,1),columnRandom)*(.5+.5*sin(ubuf.parameters.w*40.0+columnRandom*100.0))*presence*(1.0+ubuf.timbre.z*2.0)*sparkleRange;
        if(edgeMask>.5&&fract(columnRandom*89.0+ubuf.parameters.w*2.0)>.98)
            result+=vec3(ubuf.bandsHigh.z*3.0*sparkleRange);
        result+=ripple*pulse*.48+vec3(pulse*.36);
        result=mix(result,mix(srgbToLinear(ubuf.colors[0].rgb),base,.4),smoothstep(30.0,65.0,distance)*.35);
        float alpha=1.0-smoothstep(55.0,78.0,distance);
        result=mix(result,fogColor,(1.0-alpha)*.45);
        fragColor=vec4(clamp(result,vec3(0),vec3(1)),alpha);
        return;
    }
    if (opacity < 0.012) {
        discard;
    }
    vec3 normal = normalize(worldNormal);
    vec3 view = normalize(viewDirection);
    if (objectKind > 2.5 && objectKind < 3.5) {
        // Distant stars are emissive, not little rocks shaded by the stage key.
        fragColor = vec4(mix(color, vec3(0.88, 0.94, 1.0), 0.25), opacity);
        return;
    }
    float isTerrain = 1.0 - step(0.5, objectKind);
    float columnHeight = columnExtent.y;
    // The ink preset also flashes the complete flat cap, not a narrow stripe.
    float crownSheen = streamSheen;
    float jelly = step(0.5, material.x) * (1.0 - step(1.5, material.x));
    if (material.x > 1.5 && isTerrain > 0.5) {
        // Ink is deposited on a light ground, not luminous dark-mode tiles
        // with inverted colors. Height/face light form the volume of the wash.
        vec3 paper = color;
        float facing = max(dot(normal, normalize(vec3(-0.42, 0.78, 0.46))), 0.0);
        float pigment = clamp((0.06 + columnHeight * 0.032)
                               * (0.25 + material.w * 1.25), 0.0, 0.93);
        pigment *= 0.66 + (1.0 - facing) * 0.34;
        float inkEdge = 1.0 - smoothstep(0.015, 0.07,
            0.5 - max(abs(surfacePosition.x), abs(surfacePosition.z)));
        pigment = clamp(pigment + inkEdge * topSurface * light * 0.24, 0.0, 0.96);
        float granulation = 0.96 + 0.04 * sin(surfacePosition.x * 31.0
                                               + surfacePosition.z * 23.0);
        vec3 ink = mix(vec3(0.055, 0.065, 0.068), vec3(0.12, 0.22, 0.21),
                       topSurface * 0.20);
        vec3 inkSurface = mix(paper, ink, pigment * granulation);
        inkSurface = material.z <= 1.0 ? inkSurface * material.z
                   : mix(inkSurface, paper, clamp(material.z - 1.0, 0.0, 1.0));
        fragColor = vec4(inkSurface,
                          opacity * pow(fog, 1.25 + material.y * 0.8));
        return;
    }
    if (isTerrain > 0.5) {
        // The mesh supplies actual bevel normals; do not paint a second
        // rounded normal field over its surface or treat the cap separately.
        fragColor = vec4(terrainMaterial(normal, view), opacity);
        return;
    }
    // Existing non-terrain media retain their established display-space look.
    // The PBR / linear-light conversion above is limited to the column material.
    float edgeDistance = 0.5 - max(abs(surfacePosition.x), abs(surfacePosition.z));
    float edgeWidth = max(fwidth(edgeDistance), 0.010);
    // A side's fixed coordinate is always +/-0.5: it cannot locate that
    // face's rim. Use its tangent for rim light and round only the shoulders.
    float sideTangent = abs(worldNormal.x) > 0.5
                      ? surfacePosition.z : surfacePosition.x;
    float sideEdgeDistance = 0.5 - abs(sideTangent);
    vec3 keyDirection = normalize(vec3(-0.42, 0.78, 0.46));
    vec3 fillDirection = normalize(vec3(0.58, 0.26, -0.72));
    float keyDiffuse = max(dot(normal, keyDirection), 0.0);
    float fillDiffuse = max(dot(normal, fillDirection), 0.0);
    float sideFace = isTerrain * (1.0 - topSurface);
    // A tinted solid absorbs more light towards its foot. Keep a colored
    // body, with a pale crown feeding only the upper wall, not a white tube.
    float bodyHeight = clamp(surfacePosition.y + 0.5, 0.0, 1.0);
    float bodyLens = 0.35 + 0.65 * sqrt(max(0.0, 1.0 - 4.0 * sideTangent * sideTangent));
    vec3 footTint = mix(color * color, color, 0.55);
    vec3 crownTint = mix(color, vec3(0.94, 0.98, 1.0), 0.08);
    vec3 bodyTint = mix(footTint, crownTint, bodyHeight * bodyHeight);
    vec3 albedo = mix(color, bodyTint, sideFace);
    float ambient = mix(0.12, 0.23, topSurface);
    float diffuse = keyDiffuse * 0.68 + fillDiffuse * 0.24;
    float heightOcclusion = isTerrain * clamp(columnHeight / 32.0, 0.0, 1.0);
    vec3 lit = albedo * (ambient + diffuse + 0.15);
    lit = max(vec3(0.0), (lit - vec3(0.06)) * (0.60 + light * 0.40) + vec3(0.06));
    lit *= 1.0 - sideFace * (0.18 + heightOcclusion * 0.08);
    lit *= mix(0.48, 1.0, smoothstep(-0.5, 0.36, surfacePosition.y));
    // Tall columns keep a restrained colored body instead of collapsing into
    // black silhouettes; the directional light still preserves face depth.
    lit += color * sideFace * heightOcclusion * 0.12;
    vec3 halfVector = normalize(keyDirection + view);
    float specularPower = mix(28.0, 42.0, topSurface);
    specularPower *= 1.45 - material.y;
    specularPower = mix(specularPower, mix(110.0, 16.0, material.y), jelly);
    float specular = pow(max(dot(normal, halfVector), 0.0), specularPower);
    // A broad restrained coat under the tighter key reflection gives the
    // rounded facets depth without another pass or a bloom surface.
    float coat = pow(max(dot(normal, halfVector), 0.0),
                     max(8.0, specularPower * 0.28));
    float fresnel = pow(1.0 - max(dot(normal, view), 0.0), 3.0);
    vec3 specularTint = mix(vec3(0.82, 0.90, 1.0),
                            vec3(1.0, 0.96, 0.91), topSurface);
    lit += specularTint * (specular * (0.12 + topSurface * 0.29)
                          + coat * isTerrain * 0.025);
    lit += vec3(0.16, 0.34, 0.46) * fillDiffuse * sideFace * 0.14;
    float backRim = pow(max(dot(normal, normalize(fillDirection + view)), 0.0), 36.0);
    lit += vec3(0.24, 0.48, 0.62) * backRim * sideFace * 0.10;
    lit += color * fresnel * (0.035 + sideFace * 0.06);
    // Colored body fill suggests a soft translucent solid without blending
    // thousands of transparent columns or allocating a refraction pass.
    lit += color * jelly * (0.06 + material.y * 0.20)
         * (0.35 + fresnel * 0.65) * (0.6 + topSurface * 0.4);
    lit = pow(max(lit, vec3(0.0)), vec3(0.98));
    float luminance = dot(lit, vec3(0.2126, 0.7152, 0.0722));
    lit = max(vec3(0.0), mix(vec3(luminance), lit, 1.02));
    lit = mix(vec3(luminance), lit, focus);
    lit *= 0.70 + focus * 0.30;
    vec3 deepSpace = vec3(0.006, 0.009, 0.017) + color * 0.012;
    float atmosphericVisibility = pow(fog, 0.72) * (0.78 + focus * 0.22);
    vec3 finalColor = mix(deepSpace, lit, atmosphericVisibility);
    float centralGlow = max(max(finalColor.r, finalColor.g), finalColor.b);
    finalColor += color * centralGlow * (0.028 + glow * 0.055);
    float luminousHaze = clamp(glow * 0.021 + centralGlow * 0.008, 0.0, 0.095);
    vec3 hazeTint = mix(color, vec3(1.0, 0.985, 0.975), 0.06);
    finalColor = mix(finalColor, hazeTint,
                     luminousHaze * focus * 0.38);
    finalColor += hazeTint * clamp(glow * 0.011, 0.0, 0.055) * focus;
    finalColor += hazeTint * impactLight * 1.65;
    vec3 topGlow = mix(color, vec3(0.88, 0.94, 1.0), 0.10);
    finalColor = mix(finalColor, topGlow,
                     topSurface * clamp(0.035 + glow * 0.016, 0.0, 0.12));
    // A narrow top rim makes each column readable without a full-screen blur.
    float rim = 1.0 - smoothstep(0.025, 0.025 + edgeWidth, edgeDistance);
    if (isTerrain > 0.5 && topSurface < 0.5)
        rim = 1.0 - smoothstep(0.025,
            0.025 + max(fwidth(sideEdgeDistance), 0.010), sideEdgeDistance);
    finalColor += topGlow * topSurface * focus * (0.015 + rim * 0.12);
    finalColor += topGlow * jelly * rim * (0.045 + material.y * 0.12);
    // Height-selective crown light retains a focal highlight while leaving
    // side faces and the surrounding low tiles quiet.
    finalColor += topGlow * topSurface * clamp(columnHeight / 12.0, 0.0, 1.0)
                 * (0.10 + pow(smoothstep(3.0, 10.0, columnHeight), 2.0) * 0.24
                          + musicLight * 0.24);
    finalColor += topGlow * crownSheen * topSurface * 0.78;
    finalColor = mix(finalColor, topGlow,
                     clamp(crownSheen * topSurface * 0.28, 0.0, 0.52));
    // Apply after the emissive layers so they cannot wash out the dark foot.
    // Directional shading remains visible on the opposing side faces.
    finalColor *= mix(1.0, 0.55 + bodyHeight * 0.45, sideFace);
    finalColor *= mix(1.0, 0.65 + bodyLens * 0.35, sideFace);
    finalColor *= 0.92 + focus * 0.18;
    // Darker steady material leaves headroom for the music pulse. Keep the
    // edge/specular definition; exposure does not alter column geometry.
    finalColor *= mix(1.0, 0.68 - jelly * 0.08 + musicLight * 0.34, isTerrain);
    finalColor *= mix(1.0, material.z, isTerrain);
    // Emissive layers must recede with the ground as well: adding crown and
    // ripple light after atmospheric shading used to reveal a solid outer disk.
    finalColor *= mix(1.0, 0.35 + fog * 0.65, isTerrain);
    // A continuous shoulder protects crown color on hard beats. No bloom pass,
    // full-screen blur or extra render target is needed to expose the facets.
    finalColor /= vec3(1.0) + finalColor * 0.28;
    // Terrain already has a radial edge fade in the vertex shader. Applying
    // fog to alpha again made solid inner columns translucent and washed out
    // their lit crowns; atmospheric color attenuation is sufficient inside.
    float coverage = mix(clamp(0.18 + fog * 0.82, 0.0, 1.0), 1.0, isTerrain);
    fragColor = vec4(finalColor, opacity * coverage);
}
