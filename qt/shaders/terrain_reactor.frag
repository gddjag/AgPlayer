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
    vec4 waveSources[8];
    vec4 audioEnvelope;
    vec4 cameraPosition;
    vec4 materialParameters;
    vec4 sceneControls;
    vec4 waveParameters;
    vec4 sceneLighting;
    mat4 lightMvp;
    vec4 shadowParameters;
} ubuf;

// Independently written from the published GGX / Smith / Schlick equations:
// https://google.github.io/filament/main/filament.html (material and IBL chapters).
// No engine shader source or third-party runtime is included.
const float PI = 3.14159265359;

vec3 srgbToLinear(vec3 value)
{
    value = clamp(value, vec3(0.0), vec3(1.0));
    return mix(value / 12.92, pow((value + 0.055) / 1.055, vec3(2.4)),
               greaterThan(value, vec3(0.04045)));
}

vec3 linearToSrgb(vec3 value)
{
    value = max(value, vec3(0.0));
    return mix(value * 12.92, 1.055 * pow(value, vec3(1.0 / 2.4)) - 0.055,
               greaterThan(value, vec3(0.0031308)));
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
    return mix(1.0, visible * 0.25, clamp(ubuf.sceneControls.x, 0.0, 1.0));
}

vec3 fresnelSchlick(float cosine, vec3 f0)
{
    float edge = 1.0 - clamp(cosine, 0.0, 1.0);
    float edge2 = edge * edge;
    return f0 + (vec3(1.0) - f0) * edge2 * edge2 * edge;
}

vec3 dielectricBrdf(vec3 normal, vec3 view, vec3 incoming,
                    vec3 albedo, float roughness, vec3 f0)
{
    float noL = max(dot(normal, incoming), 0.0);
    float noV = max(dot(normal, view), 0.001);
    vec3 halfVector = view + incoming;
    halfVector *= inversesqrt(max(dot(halfVector, halfVector), 0.000001));
    float noH = max(dot(normal, halfVector), 0.0);
    float voH = max(dot(view, halfVector), 0.0);
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denominator = noH * noH * (alpha2 - 1.0) + 1.0;
    float distribution = alpha2 / max(PI * denominator * denominator, 0.000000001);
    // Height-correlated Smith visibility already contains 1/(4 NoL NoV).
    float smithV = noL * sqrt(noV * noV * (1.0 - alpha2) + alpha2);
    float smithL = noV * sqrt(noL * noL * (1.0 - alpha2) + alpha2);
    float visibility = 0.5 / max(smithV + smithL, 0.000001);
    vec3 fresnel = fresnelSchlick(voH, f0);
    // A small scattering layer below the dielectric shell, not painted walls.
    vec3 diffuse = (vec3(1.0) - fresnel) * albedo * (0.18 / PI);
    return (diffuse + fresnel * distribution * visibility) * noL;
}

float studioBox(vec3 direction, vec3 axis, vec2 halfSize, float roughness)
{
    vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), axis));
    vec3 up = cross(axis, right);
    float facing = dot(direction, axis);
    vec2 coordinate = vec2(dot(direction, right), dot(direction, up));
    float blur = 0.018 + roughness * roughness * 0.65;
    vec2 expanded = halfSize + vec2(blur * 0.65);
    vec2 mask = vec2(1.0) - smoothstep(expanded - vec2(blur),
                                       expanded + vec2(blur), abs(coordinate));
    float normalization = (halfSize.x * halfSize.y) / (expanded.x * expanded.y);
    return mask.x * mask.y * smoothstep(0.0, 0.25, facing) * normalization;
}

vec3 studioEnvironment(vec3 direction, float roughness)
{
    // Faint residual environment: broadening approximates a prefiltered probe.
    // Their reflection follows the actual geometry normal and camera ray.
    // This is an environment approximation, not a reflection of neighbouring columns.
    float key = studioBox(direction, normalize(vec3(-0.55, 0.12, 0.83)),
                          vec2(0.085, 0.30), roughness);
    float fill = studioBox(direction, normalize(vec3(0.78, 0.12, 0.61)),
                           vec2(0.075, 0.34), roughness);
    float ceiling = studioBox(direction, normalize(vec3(-0.12, 0.96, -0.25)),
                              vec2(0.30, 0.16), roughness);
    vec3 room = mix(srgbToLinear(vec3(0.13, 0.16, 0.20)),
                    srgbToLinear(vec3(0.29, 0.33, 0.39)),
                    smoothstep(-0.6, 0.7, direction.y));
    // Only enough external light to read a quiet shell, not a lit studio.
    // Audio-powered emission below is the scene's primary light source.
    return (room + srgbToLinear(vec3(1.0, 0.96, 0.90)) * key * 3.5
                + srgbToLinear(vec3(0.77, 0.87, 1.0)) * fill * 2.4
                + srgbToLinear(vec3(0.91, 0.97, 1.0)) * ceiling * 1.8) * 0.06;
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

void columnMedium(vec3 normal, vec3 view, float ior, float roughness,
                  out vec3 transmittedLight, out vec3 emittedLight)
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
    vec3 absorption = -log(max(tint, vec3(0.015))) * 0.06 + vec3(0.015);
    vec3 transmission = exp(-absorption * travel);
    vec4 lowMidBands = clamp(ubuf.bandsLow * ubuf.equalizerLow, vec4(0.0), vec4(1.0));
    float lowMidEnergy = dot(lowMidBands, vec4(0.35, 0.30, 0.20, 0.15));
    vec4 upperBands = clamp(ubuf.bandsHigh * ubuf.equalizerHigh, vec4(0.0), vec4(1.0));
    float upperEnergy = dot(upperBands, vec4(0.38, 0.28, 0.20, 0.14));
    // No constant core lamp: sustained low/mid energy feeds the volume,
    // while the existing spatial beat and impact envelopes excite it further.
    float coreRadius = max(16.0, ubuf.styleAudio.z * 0.65);
    float steadyField = 0.35 + 0.65 * exp(-dot(worldPosition.xz, worldPosition.xz)
                                         / (coreRadius * coreRadius));
    float pulse = lowMidEnergy * 1.05 * steadyField * clamp(ubuf.styleAudio.w, 0.0, 1.5)
                + upperEnergy * 0.18 * clamp(ubuf.styleAudio.w, 0.0, 1.5)
                + clamp(musicLight, 0.0, 1.0) * 0.85
                + clamp(impactLight, 0.0, 1.0) * 1.20;
    float sourcePower = clamp(ubuf.sceneLighting.x, 0.0, 2.0)
                      * (0.38 + clamp(glow, 0.0, 2.0) * 0.16) * pulse * 1.4;
    // Emission belongs to the raised musical relief. Leave the flat apron
    // quiet so troughs and travelling wave crests retain visual separation.
    sourcePower *= mix(0.28, 1.0, smoothstep(0.12, 2.2, extent.y));
    // A distributed source fills the same medium up to its boundary.
    // A grazing ray missing the inset core must not paint an opaque black rim.
    // This remains audio-powered; it does not add a constant ambient lamp.
    // Diffuse light transport within the gel fills thin/grazing paths. Keep
    // optical-depth variation, but do not turn the perimeter into a dark cage.
    float crossSection = min(extent.x, extent.z);
    float opticalLength = max(crossSection * 0.80,
                              innerLength * 0.35 + crossSection * 0.65);
    // Near-surface scattering lets the flat cap receive the upper emitter's
    // light rather than the darker midpoint of a long downward viewing ray.
    float luminousHeight = mix(height, clamp(surfacePosition.y + 0.5, 0.0, 1.0), 0.75);
    // A smooth volume pulse rises from the foot after each detected bass beat.
    // It changes emitted light only: the shell and the ground never move.
    float riseAge = clamp(ubuf.audioEnvelope.w, 0.0, 1.0);
    float riseHeight = mix(0.16, 1.20, smoothstep(0.0, 0.80, riseAge));
    float riseProfile = exp(-pow((luminousHeight - riseHeight) / 0.26, 2.0));
    float risingSource = clamp(musicLight, 0.0, 1.0) * riseProfile
                       * (1.0 - smoothstep(0.72, 1.0, riseAge))
                       * clamp(ubuf.sceneLighting.x, 0.0, 2.0) * 0.24;
    emittedLight = exp(-absorption * innerStart)
                 * ((vec3(1.0) - exp(-absorption * opticalLength)) / absorption)
                 * tint * (sourcePower * (0.12 + 0.88 * luminousHeight * luminousHeight)
                           + risingSource) * 2.3;
    // The distant environment is a dim background behind the emitting core,
    // not another white studio panel painted across the entire front face.
    transmittedLight = transmission * studioEnvironment(-view, roughness * 0.7) * 0.08;
}

vec3 receivedColumnLight(vec3 normal)
{
    if (ubuf.sceneLighting.x <= 0.0 || ubuf.sceneLighting.y <= 0.0)
        return vec3(0.0);
    // Four representative emitter groups share the existing spectrum/palette.
    // Real world-space distance and receiver normals light the neighbouring
    // terrain; this is intentionally bounded, shadowless group lighting, not
    // one point light or a shadow map for every individual column.
    const vec2 centers[4] = vec2[4](vec2(0.0), vec2(-24.0, 18.0),
                                    vec2(26.0, 12.0), vec2(4.0, -28.0));
    vec3 worldPosition = ubuf.cameraPosition.xyz - viewDirection;
    float radius = 12.0 + ubuf.sceneLighting.z * 20.0;
    vec3 received = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        float energy = clamp(ubuf.bandsLow[i] * ubuf.equalizerLow[i], 0.0, 1.0);
        vec3 lightPosition = vec3(centers[i].x, 14.0 + energy * 12.0, centers[i].y);
        vec3 delta = lightPosition - worldPosition;
        float distanceSquared = max(dot(delta, delta), 0.01);
        float attenuation = pow(clamp(1.0 - distanceSquared / (radius * radius), 0.0, 1.0), 2.0);
        float diffuse = max(dot(normal, delta * inversesqrt(distanceSquared)), 0.0);
        vec3 tint = srgbToLinear(ubuf.colors[i + 1].rgb);
        // The representative emitters also go dark when their music stops.
        float power = energy * 0.80 + clamp(ubuf.audioEnvelope.z, 0.0, 1.0) * 0.45;
        received += tint * attenuation * diffuse * power;
    }
    return min(received * ubuf.sceneLighting.x * ubuf.sceneLighting.y * 0.85,
                vec3(0.45));
}

vec3 terrainMaterial(vec3 normal, vec3 view)
{
    float softness = clamp(material.y, 0.0, 1.0);
    float jelly = step(0.5, material.x);
    float clarity = clamp(light, 0.0, 1.5);
    float roughness = clamp(mix(0.10, 0.38, softness) + jelly * 0.035
                             - clarity * 0.035, 0.08, 0.60);
    float ior = mix(1.46, 1.38, jelly);
    float f0Value = (ior - 1.0) / (ior + 1.0);
    vec3 f0 = vec3(f0Value * f0Value);
    vec3 albedo = srgbToLinear(color);
    vec3 keyDirection = normalize(vec3(-0.55, 0.85, 0.45));
    vec3 fillDirection = normalize(vec3(0.70, 0.30, -0.64));
    float visibility = clamp(externalVisibility(), 0.0, 1.0);
    vec3 direct = dielectricBrdf(normal, view, keyDirection, albedo, roughness, f0)
                * srgbToLinear(vec3(1.0, 0.96, 0.90)) * 0.10 * visibility;
    direct += dielectricBrdf(normal, view, fillDirection, albedo, roughness, f0)
             * srgbToLinear(vec3(0.78, 0.88, 1.0)) * 0.025;
    vec3 reflection = studioEnvironment(reflect(-view, normal), roughness);
    vec3 fresnel = fresnelSchlick(max(dot(normal, view), 0.0), f0);
    vec3 transmitted, emission;
    columnMedium(normal, view, ior, roughness, transmitted, emission);
    // One dielectric interface divides reflected and transmitted energy.
    // Shadowed surfaces retain room fill but cannot shadow their own emission.
    vec3 externalLight = direct + (reflection * fresnel
                  + (transmitted * 0.82 + albedo * 0.006)
                    * (vec3(1.0) - fresnel)) * (0.38 + visibility * 0.62);
    // Clear gel transmits neighbouring colored light; multiplying it by a
    // strongly colored opaque albedo a second time erased spill/radius changes.
    externalLight += receivedColumnLight(normal)
                   * mix(vec3(1.0), albedo, 0.25) * 2.0;
    externalLight *= 0.65 + clarity * 0.45;

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
    vec4 highBands = clamp(ubuf.bandsHigh * ubuf.equalizerHigh, vec4(0.0), vec4(1.0));
    float highEnergy = dot(highBands, vec4(0.38, 0.28, 0.20, 0.14));
    // Actual raised relief carries the local high-frequency field. Excite
    // only raised bodies, weakly, without lighting the flat outer apron or
    // adding another animated pattern to the wall or the cap glint.
    float highSource = highEnergy * centerField * 0.18
                     * smoothstep(0.15, 3.0, columnExtent.y)
                     * clamp(ubuf.styleAudio.w, 0.0, 1.5);
    float eventSource = clamp(musicLight, 0.0, 1.0) * 0.80
                      + clamp(impactLight, 0.0, 1.0) * 1.20;
    emission += mediumTint(bodyHeight) * sourceHeight * (steadySource + highSource + eventSource)
              * clamp(ubuf.sceneLighting.x, 0.0, 2.0)
              * (1.0 - smoothstep(0.10, 1.20, columnExtent.y));
    float cap = smoothstep(0.70, 0.98, normal.y);
    // Upward light escape distinguishes the flat cap from the clear walls
    // without an opaque border or an added external lamp.
    emission *= 1.0 + cap * 0.30;
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
    emission += (sheet + silver * grain) * cap * (flash / (1.0 + flash));
    }
    // Distributed subsurface light remains visible at grazing angles; keep
    // reflection contrast without letting a dark environment blacken the rim.
    vec3 radiance = externalLight + emission * (vec3(1.0) - min(fresnel, vec3(0.10)));
    radiance *= pow(clamp(fog, 0.0, 1.0), 1.35) * (0.80 + clamp(focus, 0.0, 1.0) * 0.20);
    radiance *= clamp(material.z, 0.0, 2.0);
    // Hue-preserving bounded shoulder, followed by exactly one display
    // encoding for QQuickRhiItem's UNORM RGBA8 / Qt Quick SDR composition.
    float peak = max(radiance.r, max(radiance.g, radiance.b));
    return linearToSrgb(max(radiance, vec3(0.0)) / (1.0 + max(peak, 0.0)));
}

void main()
{
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
                         + musicLight * 0.65);
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
    finalColor *= mix(1.0, 0.615 - jelly * 0.10 + musicLight * 1.15, isTerrain);
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
