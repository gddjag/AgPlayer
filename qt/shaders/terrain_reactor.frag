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
layout(location = 12) in float columnHeight;
layout(location = 13) in vec3 surfacePosition;
layout(location = 14) in float musicLight;
layout(location = 15) in vec4 material;
layout(location = 0) out vec4 fragColor;

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
    float edgeDistance = 0.5 - max(abs(surfacePosition.x), abs(surfacePosition.z));
    float edgeWidth = max(fwidth(edgeDistance), 0.010);
    float bevelWidth = mix(0.03 + material.y * 0.10,
                            0.04 + material.y * 0.16, jelly);
    float bevel = 1.0 - smoothstep(0.018, bevelWidth, edgeDistance);
    if (isTerrain > 0.5 && topSurface > 0.5) {
        vec2 edgeAxis = smoothstep(vec2(0.36), vec2(0.48), abs(surfacePosition.xz))
                      * sign(surfacePosition.xz);
        normal = normalize(vec3(edgeAxis.x * bevel * 0.55, 1.0,
                                 edgeAxis.y * bevel * 0.55));
    }
    vec3 keyDirection = normalize(vec3(-0.42, 0.78, 0.46));
    vec3 fillDirection = normalize(vec3(0.58, 0.26, -0.72));
    float keyDiffuse = max(dot(normal, keyDirection), 0.0);
    float fillDiffuse = max(dot(normal, fillDirection), 0.0);
    float sideFace = isTerrain * (1.0 - topSurface);
    float ambient = mix(0.12, 0.23, topSurface);
    float diffuse = keyDiffuse * 0.68 + fillDiffuse * 0.24;
    float heightOcclusion = isTerrain * clamp(columnHeight / 32.0, 0.0, 1.0);
    vec3 lit = color * (ambient + diffuse + 0.15);
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
    float fresnel = pow(1.0 - max(dot(normal, view), 0.0), 3.0);
    vec3 specularTint = mix(vec3(0.82, 0.90, 1.0),
                            vec3(1.0, 0.96, 0.91), topSurface);
    lit += specularTint * specular * (0.12 + topSurface * 0.32);
    lit += vec3(0.16, 0.34, 0.46) * fillDiffuse * sideFace * 0.14;
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
    finalColor += topGlow * topSurface * focus * (0.015 + rim * 0.12);
    finalColor += topGlow * jelly * rim * (0.045 + material.y * 0.12);
    // Height-selective crown light retains a focal highlight while leaving
    // side faces and the surrounding low tiles quiet.
    finalColor += topGlow * topSurface * clamp(columnHeight / 12.0, 0.0, 1.0)
                * (0.10 + musicLight * 0.65);
    finalColor += topGlow * streamSheen * (0.32 + topSurface * 0.46);
    finalColor = mix(finalColor, topGlow,
                     clamp(streamSheen * 0.28, 0.0, 0.52));
    finalColor *= 0.92 + focus * 0.18;
    // Darker steady material leaves headroom for the music pulse. Keep the
    // edge/specular definition; exposure does not alter column geometry.
    finalColor *= mix(1.0, 0.755 - jelly * 0.12 + musicLight * 1.15, isTerrain);
    finalColor *= mix(1.0, material.z, isTerrain);
    // A continuous shoulder protects crown color on hard beats. No bloom pass,
    // full-screen blur or extra render target is needed to expose the facets.
    finalColor /= vec3(1.0) + finalColor * 0.28;
    // Terrain already has a radial edge fade in the vertex shader. Applying
    // fog to alpha again made solid inner columns translucent and washed out
    // their lit crowns; atmospheric color attenuation is sufficient inside.
    float coverage = mix(clamp(0.18 + fog * 0.82, 0.0, 1.0), 1.0, isTerrain);
    fragColor = vec4(finalColor, opacity * coverage);
}
