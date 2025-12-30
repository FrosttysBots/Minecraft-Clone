#version 460 core
layout(location = 0) in vec2 texCoord;
layout(location = 1) in vec2 texSlotBase;  // Base UV of texture slot for greedy meshing tiling
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;
layout(location = 4) in float aoFactor;
layout(location = 5) in float lightLevel;
layout(location = 6) in float fogDepth;
layout(location = 7) in vec2 screenPos;
layout(location = 8) in vec4 fragPosLightSpace;
flat in uint texSlotIndex;  // For biome tinting check
in vec2 biomeCoord;         // Biome colormap UV (temperature, humidity)

layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D texAtlas;
layout(binding = 1) uniform sampler2D shadowMap;
layout(binding = 2) uniform sampler3D lightmapTexture;  // 3D lightmap for smooth lighting
layout(binding = 3) uniform sampler2D normalMap;  // Normal map atlas
layout(binding = 4) uniform sampler2D grassColormap;  // Biome colormap for grass/foliage tinting

uniform bool useNormalMap;  // Enable normal mapping

// Tintable texture slot indices (must match BiomeColormap.h TintableSlots)
const uint TINT_GRASS_TOP = 2u;   // grass_top
const uint TINT_GRASS_SIDE = 3u;  // grass_side (partial tint)
const uint TINT_LEAVES = 8u;      // leaves_oak
const uint TINT_WATER = 11u;      // water_still

// Texture atlas constants for greedy meshing tiling
const float ATLAS_SIZE = 16.0;
const float SLOT_SIZE = 1.0 / ATLAS_SIZE;  // 0.0625

uniform vec3 chunkOffset;  // World position of chunk origin (for lightmap sampling)
uniform vec3 lightDir;
uniform vec3 lightColor;
uniform vec3 ambientColor;
uniform vec3 skyColor;
uniform vec3 cameraPos;
uniform float fogDensity;
uniform float isUnderwater;
uniform float time;
uniform float shadowStrength;
uniform float renderDistanceBlocks;  // For LOD-hiding fog
uniform float brightness;  // Overall brightness multiplier

// ============================================================
// Shadow Mapping with PCF (Percentage Closer Filtering)
// ============================================================
float calculateShadow(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    // Perform perspective divide
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;

    // Transform to [0,1] range
    projCoords = projCoords * 0.5 + 0.5;

    // Check if outside shadow map
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z > 1.0) {
        return 0.0;  // Not in shadow
    }

    // Get current fragment depth
    float currentDepth = projCoords.z;

    // Calculate bias based on surface angle to light
    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.001);

    // PCF - sample surrounding texels for soft shadows
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);

    for (int x = -2; x <= 2; x++) {
        for (int y = -2; y <= 2; y++) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 25.0;  // 5x5 kernel

    // Fade shadow at distance
    float distFade = smoothstep(100.0, 200.0, length(fragPos - cameraPos));
    shadow *= (1.0 - distFade);

    return shadow * shadowStrength;
}

// ============================================================
// Volumetric Fog System
// Height-based density with light scattering
// ============================================================

// Fog parameters
const float FOG_HEIGHT_FALLOFF = 0.015;   // How quickly fog thins with height
const float FOG_BASE_HEIGHT = 64.0;       // Sea level - fog is densest here
const float FOG_DENSITY_SCALE = 0.8;      // Overall fog intensity
const float FOG_INSCATTER_STRENGTH = 0.4; // Light scattering intensity

// Calculate fog density at a given height
float getFogDensity(float y) {
    // Exponential falloff above base height
    float heightAboveBase = max(y - FOG_BASE_HEIGHT, 0.0);
    float heightFactor = exp(-heightAboveBase * FOG_HEIGHT_FALLOFF);

    // Slightly denser below base height (valleys/water)
    float belowBase = max(FOG_BASE_HEIGHT - y, 0.0);
    float valleyFactor = 1.0 + belowBase * 0.02;

    return heightFactor * valleyFactor;
}

// Analytical integration of exponential height fog along a ray
// Based on: https://iquilezles.org/articles/fog/
// Enhanced with LOD-hiding fog that intensifies at render distance edge
vec2 computeVolumetricFog(vec3 rayStart, vec3 rayEnd, vec3 sunDir) {
    vec3 rayDir = rayEnd - rayStart;
    float rayLength = length(rayDir);

    if (rayLength < 0.001) return vec2(1.0, 0.0);

    rayDir /= rayLength;

    // Sample fog along the ray (simplified integration)
    const int FOG_STEPS = 8;
    float stepSize = rayLength / float(FOG_STEPS);

    float transmittance = 1.0;
    float inScatter = 0.0;

    // Henyey-Greenstein phase function approximation for forward scattering
    float cosTheta = dot(rayDir, sunDir);
    float g = 0.7; // Forward scattering bias
    float phase = (1.0 - g*g) / (4.0 * 3.14159 * pow(1.0 + g*g - 2.0*g*cosTheta, 1.5));

    for (int i = 0; i < FOG_STEPS; i++) {
        float t = (float(i) + 0.5) * stepSize;
        vec3 samplePos = rayStart + rayDir * t;

        // Get fog density at this height
        float density = getFogDensity(samplePos.y) * fogDensity * FOG_DENSITY_SCALE;

        // Beer's law extinction
        float extinction = exp(-density * stepSize * 2.0);

        // Light contribution at this point (simplified - assumes light reaches all points)
        // In reality would need shadow marching, but too expensive
        float heightLight = clamp((samplePos.y - FOG_BASE_HEIGHT) / 40.0 + 0.5, 0.3, 1.0);
        float lightContrib = phase * heightLight;

        // Accumulate in-scattering (light scattered toward camera)
        inScatter += transmittance * (1.0 - extinction) * lightContrib * FOG_INSCATTER_STRENGTH;

        // Update transmittance
        transmittance *= extinction;
    }

    // LOD-hiding fog: extra fog at 70-100% of render distance
    // This hides the LOD transition zone - Distant Horizons style
    float lodStartDist = renderDistanceBlocks * 0.7;
    float lodEndDist = renderDistanceBlocks;
    float lodFogFactor = smoothstep(lodStartDist, lodEndDist, rayLength);
    // Reduce transmittance by up to 40% at the far edge
    transmittance *= (1.0 - lodFogFactor * 0.4);

    return vec2(transmittance, inScatter);
}

// ============================================================
// Biome Tinting System
// Apply temperature/humidity based color to grass/leaves
// ============================================================
vec3 applyBiomeTint(vec3 baseColor, uint slot, vec2 biomeUV) {
    vec3 tintColor = texture(grassColormap, biomeUV).rgb;

    // Grass top and leaves: full tint
    if (slot == TINT_GRASS_TOP || slot == TINT_LEAVES) {
        return baseColor * tintColor * 1.4;
    }
    // Grass side: partial tint for natural look
    if (slot == TINT_GRASS_SIDE) {
        return mix(baseColor, baseColor * tintColor * 1.4, 0.6);
    }
    // Water: subtle blue tint based on biome (optional)
    if (slot == TINT_WATER) {
        return baseColor * mix(vec3(1.0), tintColor * 1.2, 0.3);
    }
    // Non-tintable blocks: return unchanged
    return baseColor;
}

// Check if texture coordinates indicate an emissive block
// Texture atlas is 16x16, each slot is 1/16 = 0.0625
// Glowstone = slot 22 (row 1, col 6), Lava = slot 23 (row 1, col 7)
float getEmission(vec2 uv) {
    float slotSize = 1.0 / 16.0;
    int col = int(uv.x / slotSize);
    int row = int(uv.y / slotSize);
    int slot = row * 16 + col;

    if (slot == 22) return 1.0;  // Glowstone
    if (slot == 23) return 0.95; // Lava
    return 0.0;
}

void main() {
    // Sample texture with greedy meshing tiling support
    // fract(texCoord) tiles within each block, then offset to correct atlas slot
    vec2 tiledUV = texSlotBase + fract(texCoord) * SLOT_SIZE;
    vec4 texColor = texture(texAtlas, tiledUV);

    // Discard very transparent pixels (for glass, leaves)
    if (texColor.a < 0.1) discard;

    // Apply biome-based tinting to grass, leaves, and water
    texColor.rgb = applyBiomeTint(texColor.rgb, texSlotIndex, biomeCoord);

    // Check for emissive blocks (use tiledUV to identify block type)
    float emission = getEmission(tiledUV);

    // Sample 3D lightmap for smooth block lighting (from emissive blocks like torches/glowstone)
    // Note: This is BLOCK LIGHT only, not skylight. Skylight comes from sun/ambient calculations.
    // Calculate local position within chunk (0-16 for X/Z, 0-256 for Y)
    vec3 localPos = fragPos - chunkOffset;
    // Convert to normalized texture coordinates (0-1 range)
    // Add 0.5 to sample at block center, divide by chunk dimensions
    vec3 lightmapCoord = vec3(
        (localPos.x + 0.5) / 16.0,   // X: 0-16 -> 0-1
        (localPos.z + 0.5) / 16.0,   // Z maps to texture Y (height in 3D tex)
        (localPos.y + 0.5) / 256.0   // Y maps to texture Z (depth in 3D tex)
    );
    // Clamp to valid range to avoid edge artifacts
    lightmapCoord = clamp(lightmapCoord, 0.001, 0.999);
    // Sample lightmap (GL_LINEAR filtering provides smooth interpolation)
    float sampledLight = texture(lightmapTexture, lightmapCoord).r;
    // Use the higher of sampled lightmap or vertex light to ensure we don't darken areas
    // that should be lit (vertex light is passed through as fallback)
    float finalBlockLight = max(sampledLight, lightLevel);

    // Normal mapping with TBN matrix for voxel faces
    vec3 norm = normalize(fragNormal);

    if (useNormalMap) {
        // Sample normal map at the same UV as albedo
        vec3 normalSample = texture(normalMap, tiledUV).rgb;
        normalSample = normalSample * 2.0 - 1.0;  // Convert from [0,1] to [-1,1]

        // Compute TBN matrix for axis-aligned voxel faces
        // For voxel faces, we can derive tangent/bitangent from the face normal
        vec3 tangent, bitangent;
        vec3 absNormal = abs(fragNormal);

        if (absNormal.y > 0.9) {
            // Top/bottom face (Y-aligned): tangent = X, bitangent = Z
            tangent = vec3(1.0, 0.0, 0.0);
            bitangent = vec3(0.0, 0.0, fragNormal.y > 0.0 ? 1.0 : -1.0);
        } else if (absNormal.x > 0.9) {
            // Left/right face (X-aligned): tangent = Z, bitangent = Y
            tangent = vec3(0.0, 0.0, fragNormal.x > 0.0 ? -1.0 : 1.0);
            bitangent = vec3(0.0, 1.0, 0.0);
        } else {
            // Front/back face (Z-aligned): tangent = X, bitangent = Y
            tangent = vec3(fragNormal.z > 0.0 ? 1.0 : -1.0, 0.0, 0.0);
            bitangent = vec3(0.0, 1.0, 0.0);
        }

        mat3 TBN = mat3(tangent, bitangent, norm);
        norm = normalize(TBN * normalSample);
    }

    vec3 lightDirection = normalize(lightDir);

    // Calculate shadow
    float shadow = calculateShadow(fragPosLightSpace, norm, lightDirection);

    // Ambient lighting (sky contribution) - not affected by shadow
    vec3 ambient = ambientColor * 0.6;

    // Diffuse lighting (sun/moon) - affected by shadow
    float diff = max(dot(norm, lightDirection), 0.0);
    vec3 diffuse = diff * lightColor * 0.6 * (1.0 - shadow);

    // Point light contribution from emissive blocks (glowstone, lava)
    // Use combined lightmap + vertex light for smooth lighting across greedy-meshed quads
    vec3 pointLight = finalBlockLight * vec3(1.0, 0.85, 0.6) * 1.2;

    // Combine lighting with smooth AO
    // Point lights are added on top (not multiplied by AO for better effect near sources)
    vec3 lighting = (ambient + diffuse) * aoFactor + pointLight;

    // Apply lighting to texture (emissive blocks ignore lighting and shadows)
    vec3 result;
    if (emission > 0.0) {
        // Emissive blocks glow with their natural color, plus a brightness boost
        vec3 glowColor = texColor.rgb * (1.5 + emission * 0.5);
        // Add slight pulsing effect for lava
        if (emission < 1.0) {
            float pulse = sin(time * 2.0) * 0.1 + 1.0;
            glowColor *= pulse;
        }
        result = glowColor;
    } else {
        result = texColor.rgb * lighting;
    }

    // Apply underwater effects (different fog system)
    if (isUnderwater > 0.5) {
        // Underwater uses simple dense fog
        float underwaterFogFactor = 1.0 - exp(-fogDensity * 16.0 * fogDepth * fogDepth);
        underwaterFogFactor = clamp(underwaterFogFactor, 0.0, 1.0);

        vec3 underwaterFogColor = vec3(0.05, 0.2, 0.35);
        result = mix(result, underwaterFogColor, underwaterFogFactor);

        // Strong blue-green color grading
        result = mix(result, result * vec3(0.4, 0.7, 0.9), 0.4);

        // Depth-based light absorption (deeper = darker and more blue)
        float depthDarkening = exp(-fogDepth * 0.02);
        result *= mix(vec3(0.3, 0.5, 0.7), vec3(1.0), depthDarkening);

        // Vignette effect (darker edges like diving mask)
        float vignette = 1.0 - length(screenPos) * 0.5;
        vignette = clamp(vignette, 0.0, 1.0);
        vignette = smoothstep(0.0, 1.0, vignette);
        result *= mix(0.4, 1.0, vignette);

        // Wavy light caustics effect
        float caustic1 = sin(fragPos.x * 3.0 + fragPos.z * 2.0 + time * 2.5) * 0.5 + 0.5;
        float caustic2 = sin(fragPos.x * 2.0 - fragPos.z * 3.0 + time * 1.8) * 0.5 + 0.5;
        float caustic3 = sin((fragPos.x + fragPos.z) * 4.0 + time * 3.2) * 0.5 + 0.5;
        float caustics = (caustic1 + caustic2 + caustic3) / 3.0;
        caustics = caustics * 0.25 + 0.85;
        result *= caustics;

        // Subtle color shimmer
        float shimmer = sin(fragPos.x * 5.0 + fragPos.y * 3.0 + time * 4.0) * 0.02;
        result.b += shimmer;
        result.g += shimmer * 0.5;
    } else {
        // Volumetric fog for above water
        vec2 fogResult = computeVolumetricFog(cameraPos, fragPos, lightDirection);
        float transmittance = fogResult.x;
        float inScatter = fogResult.y;

        // Emissive blocks resist fog - they pierce through it
        if (emission > 0.0) {
            transmittance = mix(transmittance, 1.0, emission * 0.7);
            inScatter *= (1.0 - emission * 0.5);
        }

        // Fog color based on sun position and sky
        float sunUp = max(lightDirection.y, 0.0);
        vec3 fogScatterColor = mix(
            vec3(0.9, 0.85, 0.7),   // Warm scattered light
            lightColor * 0.8,       // Sun color contribution
            sunUp * 0.5
        );

        // Blend fog color with sky color for ambient fog
        vec3 fogAmbientColor = mix(skyColor, fogScatterColor, 0.3);

        // Apply fog: attenuate object color and add in-scattered light
        result = result * transmittance + fogAmbientColor * (1.0 - transmittance) + fogScatterColor * inScatter;
    }

    // Apply brightness adjustment
    result *= brightness;

    FragColor = vec4(result, texColor.a);
}
