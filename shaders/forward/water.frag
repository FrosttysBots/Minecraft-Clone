#version 460 core
// Water fragment shader with enhanced underwater effects and seamless tiling
// Uses Simplex noise and FBM for natural-looking procedural water
// LOD system reduces quality for distant water to improve performance

layout(location = 0) in vec2 texCoord;
layout(location = 1) in vec2 texSlotBase;  // Base UV of texture slot for greedy meshing tiling
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;
layout(location = 4) in float aoFactor;
layout(location = 5) in float fogDepth;

layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D texAtlas;
uniform vec3 lightDir;
uniform vec3 lightColor;
uniform vec3 ambientColor;
uniform vec3 skyColor;
uniform vec3 cameraPos;
uniform float fogDensity;
uniform float isUnderwater;
uniform float time;
uniform vec4 waterTexBounds;
uniform float waterLodDistance;  // Distance threshold for LOD transitions

// Texture atlas constants for greedy meshing tiling
const float ATLAS_SIZE = 16.0;
const float SLOT_SIZE = 1.0 / ATLAS_SIZE;  // 0.0625

// ============================================================
// Volumetric Fog System (shared with main shader)
// ============================================================
const float FOG_HEIGHT_FALLOFF = 0.015;
const float FOG_BASE_HEIGHT = 64.0;
const float FOG_DENSITY_SCALE = 0.8;
const float FOG_INSCATTER_STRENGTH = 0.4;

float getFogDensityW(float y) {
    float heightAboveBase = max(y - FOG_BASE_HEIGHT, 0.0);
    float heightFactor = exp(-heightAboveBase * FOG_HEIGHT_FALLOFF);
    float belowBase = max(FOG_BASE_HEIGHT - y, 0.0);
    float valleyFactor = 1.0 + belowBase * 0.02;
    return heightFactor * valleyFactor;
}

// LOD-aware volumetric fog - fewer steps for distant water
vec2 computeVolumetricFogW(vec3 rayStart, vec3 rayEnd, vec3 sunDir, int fogSteps) {
    vec3 rayDir = rayEnd - rayStart;
    float rayLength = length(rayDir);
    if (rayLength < 0.001) return vec2(1.0, 0.0);
    rayDir /= rayLength;

    float stepSize = rayLength / float(fogSteps);
    float transmittance = 1.0;
    float inScatter = 0.0;

    float cosTheta = dot(rayDir, sunDir);
    float g = 0.7;
    float phase = (1.0 - g*g) / (4.0 * 3.14159 * pow(1.0 + g*g - 2.0*g*cosTheta, 1.5));

    for (int i = 0; i < fogSteps; i++) {
        float t = (float(i) + 0.5) * stepSize;
        vec3 samplePos = rayStart + rayDir * t;
        float density = getFogDensityW(samplePos.y) * fogDensity * FOG_DENSITY_SCALE;
        float extinction = exp(-density * stepSize * 2.0);
        float heightLight = clamp((samplePos.y - FOG_BASE_HEIGHT) / 40.0 + 0.5, 0.3, 1.0);
        float lightContrib = phase * heightLight;
        inScatter += transmittance * (1.0 - extinction) * lightContrib * FOG_INSCATTER_STRENGTH;
        transmittance *= extinction;
    }
    return vec2(transmittance, inScatter);
}

// ============================================================
// Simplex 2D Noise - by Ian McEwan, Stefan Gustavson
// https://gist.github.com/patriciogonzalezvivo/670c22f3966e662d2f83
// ============================================================
vec3 permute(vec3 x) { return mod(((x*34.0)+1.0)*x, 289.0); }

float snoise(vec2 v) {
    const vec4 C = vec4(0.211324865405187, 0.366025403784439,
                        -0.577350269189626, 0.024390243902439);
    vec2 i  = floor(v + dot(v, C.yy));
    vec2 x0 = v - i + dot(i, C.xx);
    vec2 i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec4 x12 = x0.xyxy + C.xxzz;
    x12.xy -= i1;
    i = mod(i, 289.0);
    vec3 p = permute(permute(i.y + vec3(0.0, i1.y, 1.0)) + i.x + vec3(0.0, i1.x, 1.0));
    vec3 m = max(0.5 - vec3(dot(x0,x0), dot(x12.xy,x12.xy), dot(x12.zw,x12.zw)), 0.0);
    m = m*m;
    m = m*m;
    vec3 x = 2.0 * fract(p * C.www) - 1.0;
    vec3 h = abs(x) - 0.5;
    vec3 ox = floor(x + 0.5);
    vec3 a0 = x - ox;
    m *= 1.79284291400159 - 0.85373472095314 * (a0*a0 + h*h);
    vec3 g;
    g.x = a0.x * x0.x + h.x * x0.y;
    g.yz = a0.yz * x12.xz + h.yz * x12.yw;
    return 130.0 * dot(m, g);
}

// ============================================================
// FBM (Fractal Brownian Motion) with domain rotation
// Based on techniques from Inigo Quilez: https://iquilezles.org/articles/fbm/
// LOD-aware: octaves parameter controls quality
// ============================================================
float fbm(vec2 p, float t, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    // Rotation matrix to prevent pattern alignment between octaves
    mat2 rot = mat2(0.80, 0.60, -0.60, 0.80);  // ~37 degree rotation

    for (int i = 0; i < octaves; i++) {
        float timeOffset = t * (0.3 + float(i) * 0.15) * (mod(float(i), 2.0) == 0.0 ? 1.0 : -0.7);
        vec2 animatedP = p * frequency + vec2(timeOffset * 0.5, timeOffset * 0.3);

        value += amplitude * snoise(animatedP);

        p = rot * p;
        frequency *= 2.03;
        amplitude *= 0.49;
    }

    return value;
}

// Secondary FBM with different parameters for variety
float fbm2(vec2 p, float t, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 0.7;

    mat2 rot = mat2(0.70, 0.71, -0.71, 0.70);

    for (int i = 0; i < octaves; i++) {
        float timeOffset = t * (0.2 + float(i) * 0.1) * (mod(float(i), 2.0) == 0.0 ? -1.0 : 0.8);
        vec2 animatedP = p * frequency + vec2(timeOffset * -0.3, timeOffset * 0.6);

        value += amplitude * snoise(animatedP);

        p = rot * p;
        frequency *= 1.97;
        amplitude *= 0.52;
    }

    return value;
}

void main() {
    // Sample position for noise (world space XZ)
    vec2 pos = fragPos.xz;

    // ============================================================
    // LOD calculation based on distance from camera
    // ============================================================
    float distToCamera = length(fragPos - cameraPos);
    float lodFactor = clamp(distToCamera / waterLodDistance, 0.0, 1.0);

    // LOD levels:
    // 0.0-0.3: Full quality (5 octaves, all detail)
    // 0.3-0.6: Medium quality (3 octaves, no fine detail)
    // 0.6-1.0: Low quality (2 octaves, simple waves only)
    int mainOctaves = lodFactor < 0.3 ? 5 : (lodFactor < 0.6 ? 3 : 2);
    int secondaryOctaves = lodFactor < 0.3 ? 4 : (lodFactor < 0.6 ? 2 : 1);
    bool doFineDetail = lodFactor < 0.3;
    bool doSparkle = lodFactor < 0.5;

    // ============================================================
    // Layer multiple FBM noise patterns for complex water surface
    // LOD reduces octaves and skips fine detail for distant water
    // ============================================================

    // Large slow-moving waves (main water motion)
    float largeWaves = fbm(pos * 0.08, time * 0.4, mainOctaves) * 0.6;

    // Medium waves moving in different direction
    float mediumWaves = fbm2(pos * 0.15, time * 0.6, secondaryOctaves) * 0.3;

    // Small detail ripples (only for close water)
    float smallRipples = doFineDetail ? fbm(pos * 0.4, time * 1.2, 3) * 0.15 : 0.0;

    // Very fine surface detail (only for close water)
    float fineDetail = doFineDetail ? snoise(pos * 1.5 + time * vec2(0.3, -0.2)) * 0.08 : 0.0;

    // Combine all wave layers
    float combinedWaves = largeWaves + mediumWaves + smallRipples + fineDetail;

    // Normalize to 0-1 range (noise returns roughly -1 to 1)
    float wavePattern = combinedWaves * 0.5 + 0.5;
    wavePattern = clamp(wavePattern, 0.0, 1.0);

    // ============================================================
    // Water coloring based on wave patterns
    // ============================================================
    vec3 waterDeep = vec3(0.05, 0.20, 0.45);      // Deep blue in troughs
    vec3 waterMid = vec3(0.12, 0.35, 0.60);       // Mid blue
    vec3 waterSurface = vec3(0.25, 0.50, 0.75);   // Lighter blue at peaks
    vec3 waterHighlight = vec3(0.45, 0.70, 0.90); // Highlights/foam hints

    // Multi-step color blending based on wave height
    vec3 waterColor;
    if (wavePattern < 0.4) {
        waterColor = mix(waterDeep, waterMid, wavePattern / 0.4);
    } else if (wavePattern < 0.7) {
        waterColor = mix(waterMid, waterSurface, (wavePattern - 0.4) / 0.3);
    } else {
        waterColor = mix(waterSurface, waterHighlight, (wavePattern - 0.7) / 0.3);
    }

    // Add subtle sparkle effect at wave peaks using high-frequency noise (LOD: skip for distant water)
    if (doSparkle) {
        float sparkleNoise = snoise(pos * 3.0 + time * vec2(1.5, -1.2));
        float sparkle = smoothstep(0.7, 0.95, wavePattern) * smoothstep(0.5, 0.9, sparkleNoise) * 0.3;
        waterColor += vec3(sparkle);
    }

    vec4 texColor = vec4(waterColor, 0.78);  // Semi-transparent

    // Lighting calculation
    vec3 norm = normalize(fragNormal);
    vec3 lightDirection = normalize(lightDir);

    // Ambient lighting (sky contribution)
    vec3 ambient = ambientColor * 0.6;

    // Diffuse lighting (sun/moon)
    float diff = max(dot(norm, lightDirection), 0.0);
    vec3 diffuse = diff * lightColor * 0.6;

    // Combine lighting with smooth AO
    vec3 lighting = (ambient + diffuse) * aoFactor;

    // Apply lighting to texture
    vec3 result = texColor.rgb * lighting;

    // Apply underwater effects (different fog system)
    if (isUnderwater > 0.5) {
        // Underwater uses simple dense fog
        float underwaterFogFactor = 1.0 - exp(-fogDensity * 16.0 * fogDepth * fogDepth);
        underwaterFogFactor = clamp(underwaterFogFactor, 0.0, 1.0);

        vec3 underwaterFogColor = vec3(0.05, 0.2, 0.35);
        result = mix(result, underwaterFogColor, underwaterFogFactor);

        // Strong blue-green color grading
        result = mix(result, result * vec3(0.4, 0.7, 0.9), 0.4);

        // Depth-based light absorption
        float depthDarkening = exp(-fogDepth * 0.02);
        result *= mix(vec3(0.3, 0.5, 0.7), vec3(1.0), depthDarkening);

        // Wavy light caustics on water surfaces
        float caustic1 = sin(fragPos.x * 3.0 + fragPos.z * 2.0 + time * 2.5) * 0.5 + 0.5;
        float caustic2 = sin(fragPos.x * 2.0 - fragPos.z * 3.0 + time * 1.8) * 0.5 + 0.5;
        float caustics = (caustic1 + caustic2) / 2.0;
        caustics = caustics * 0.2 + 0.9;
        result *= caustics;
    } else {
        // Volumetric fog for above water (LOD: fewer steps for distant water)
        int fogSteps = lodFactor < 0.3 ? 6 : (lodFactor < 0.6 ? 4 : 2);
        vec2 fogResult = computeVolumetricFogW(cameraPos, fragPos, lightDirection, fogSteps);
        float transmittance = fogResult.x;
        float inScatter = fogResult.y;

        // Fog color based on sun position and sky
        float sunUp = max(lightDirection.y, 0.0);
        vec3 fogScatterColor = mix(
            vec3(0.9, 0.85, 0.7),
            lightColor * 0.8,
            sunUp * 0.5
        );
        vec3 fogAmbientColor = mix(skyColor, fogScatterColor, 0.3);

        // Apply fog: attenuate object color and add in-scattered light
        result = result * transmittance + fogAmbientColor * (1.0 - transmittance) + fogScatterColor * inScatter;
    }

    FragColor = vec4(result, texColor.a);
}
