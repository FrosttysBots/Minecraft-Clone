#version 460 core
// Deferred Water Fragment Shader
// Features: DUDV distortion, SSR reflections, caustics, Fresnel

layout(location = 0) out vec4 FragColor;

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec4 clipSpacePos;
layout(location = 4) in vec4 clipSpacePosOld;
layout(location = 5) in float waterDepth;

// Scene textures
layout(binding = 0) uniform sampler2D sceneColor;      // Rendered scene for refraction
layout(binding = 1) uniform sampler2D sceneDepth;      // Depth buffer for depth-based effects
layout(binding = 2) uniform sampler2D gNormal;         // Normal buffer for SSR
layout(binding = 3) uniform sampler2D dudvMap;         // DUDV distortion map (procedural if not available)

uniform mat4 view;
uniform mat4 projection;
uniform mat4 invViewProj;
uniform vec3 cameraPos;
uniform vec3 lightDir;
uniform vec3 lightColor;
uniform vec3 skyColor;
uniform float time;
uniform float nearPlane;
uniform float farPlane;
uniform bool useDudvTexture;  // Whether to use texture or procedural DUDV

// Water parameters
const vec3 WATER_COLOR_SHALLOW = vec3(0.1, 0.4, 0.6);
const vec3 WATER_COLOR_DEEP = vec3(0.02, 0.1, 0.2);
const float WATER_CLARITY = 4.0;  // How far you can see through water
const float DISTORTION_STRENGTH = 0.02;
const float SSR_MAX_DISTANCE = 50.0;
const int SSR_STEPS = 32;
const float SSR_THICKNESS = 0.5;

// ============================================================
// Noise functions for procedural effects
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
// DUDV Distortion - creates wavy refraction
// ============================================================
vec2 getDUDV(vec2 worldXZ) {
    if (useDudvTexture) {
        // Use texture-based DUDV
        vec2 uv1 = worldXZ * 0.1 + time * vec2(0.02, 0.01);
        vec2 uv2 = worldXZ * 0.1 - time * vec2(0.01, 0.02);
        vec2 dudv1 = texture(dudvMap, uv1).rg * 2.0 - 1.0;
        vec2 dudv2 = texture(dudvMap, uv2).rg * 2.0 - 1.0;
        return (dudv1 + dudv2) * 0.5;
    } else {
        // Procedural DUDV using noise
        float n1 = snoise(worldXZ * 0.15 + time * 0.1);
        float n2 = snoise(worldXZ * 0.15 + vec2(5.2, 1.3) + time * 0.08);
        float n3 = snoise(worldXZ * 0.3 + time * 0.15);
        float n4 = snoise(worldXZ * 0.3 + vec2(9.7, 3.2) + time * 0.12);

        return vec2(
            (n1 + n3 * 0.5) * 0.5,
            (n2 + n4 * 0.5) * 0.5
        );
    }
}

// ============================================================
// Calculate water surface normal from noise
// ============================================================
vec3 getWaterNormal(vec2 worldXZ) {
    float eps = 0.5;

    // Sample heights at nearby points
    float h0 = snoise(worldXZ * 0.2 + time * 0.1);
    float h1 = snoise((worldXZ + vec2(eps, 0)) * 0.2 + time * 0.1);
    float h2 = snoise((worldXZ + vec2(0, eps)) * 0.2 + time * 0.1);

    // Add detail layer
    h0 += snoise(worldXZ * 0.5 + time * 0.2) * 0.3;
    h1 += snoise((worldXZ + vec2(eps, 0)) * 0.5 + time * 0.2) * 0.3;
    h2 += snoise((worldXZ + vec2(0, eps)) * 0.5 + time * 0.2) * 0.3;

    // Compute normal from height differences
    vec3 tangent = normalize(vec3(eps, (h1 - h0) * 0.5, 0));
    vec3 bitangent = normalize(vec3(0, (h2 - h0) * 0.5, eps));

    return normalize(cross(bitangent, tangent));
}

// ============================================================
// Fresnel effect - more reflection at grazing angles
// ============================================================
float fresnel(vec3 viewDir, vec3 normal, float R0) {
    float cosTheta = max(dot(viewDir, normal), 0.0);
    return R0 + (1.0 - R0) * pow(1.0 - cosTheta, 5.0);
}

// ============================================================
// Screen-Space Reflections (SSR)
// ============================================================
vec3 SSR(vec3 worldPos, vec3 reflectDir, vec2 screenUV) {
    // Transform to view space
    vec4 viewPos = view * vec4(worldPos, 1.0);
    vec3 viewReflect = normalize((view * vec4(reflectDir, 0.0)).xyz);

    // Ray march in screen space
    vec3 rayPos = viewPos.xyz;
    float stepSize = SSR_MAX_DISTANCE / float(SSR_STEPS);

    for (int i = 0; i < SSR_STEPS; i++) {
        rayPos += viewReflect * stepSize;

        // Project to screen space
        vec4 projPos = projection * vec4(rayPos, 1.0);
        vec2 sampleUV = (projPos.xy / projPos.w) * 0.5 + 0.5;

        // Check bounds
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 ||
            sampleUV.y < 0.0 || sampleUV.y > 1.0) {
            break;
        }

        // Sample depth at this position
        float sampledDepth = texture(sceneDepth, sampleUV).r;

        // Convert to linear depth
        float linearSampledDepth = (2.0 * nearPlane * farPlane) /
            (farPlane + nearPlane - (sampledDepth * 2.0 - 1.0) * (farPlane - nearPlane));
        float rayDepth = -rayPos.z;

        // Check for intersection
        float depthDiff = rayDepth - linearSampledDepth;
        if (depthDiff > 0.0 && depthDiff < SSR_THICKNESS) {
            // Found intersection - sample scene color
            vec3 hitColor = texture(sceneColor, sampleUV).rgb;

            // Fade based on distance and edge
            float edgeFade = 1.0 - smoothstep(0.0, 0.1, abs(sampleUV.x - 0.5) * 2.0);
            edgeFade *= 1.0 - smoothstep(0.0, 0.1, abs(sampleUV.y - 0.5) * 2.0);
            float distFade = 1.0 - float(i) / float(SSR_STEPS);

            return hitColor * edgeFade * distFade;
        }
    }

    // No hit - return sky reflection
    return skyColor * 0.8;
}

// ============================================================
// Caustics - light patterns on underwater surfaces
// ============================================================
float caustics(vec2 worldXZ, float depth) {
    // Multi-layer caustic pattern
    vec2 uv1 = worldXZ * 0.1 + time * vec2(0.03, 0.02);
    vec2 uv2 = worldXZ * 0.1 - time * vec2(0.02, 0.03);
    vec2 uv3 = worldXZ * 0.15 + time * vec2(-0.01, 0.025);

    float c1 = snoise(uv1 * 5.0);
    float c2 = snoise(uv2 * 5.0);
    float c3 = snoise(uv3 * 7.0);

    // Combine for cellular-looking caustics
    float caustic = (c1 + c2 + c3) / 3.0;
    caustic = smoothstep(-0.2, 0.8, caustic);

    // Depth falloff
    float depthFade = exp(-depth * 0.1);

    return caustic * depthFade * 0.5 + 0.5;
}

// ============================================================
// Main
// ============================================================
void main() {
    // Screen-space coordinates
    vec2 ndc = clipSpacePos.xy / clipSpacePos.w;
    vec2 screenUV = ndc * 0.5 + 0.5;

    // View direction
    vec3 viewDir = normalize(cameraPos - fragPos);

    // Get water surface normal (perturbed for waves)
    vec3 waterNormal = getWaterNormal(fragPos.xz);

    // Blend with geometry normal
    waterNormal = normalize(mix(fragNormal, waterNormal, 0.7));

    // ============================================================
    // DUDV Distortion for refraction
    // ============================================================
    vec2 dudv = getDUDV(fragPos.xz);
    vec2 distortedUV = screenUV + dudv * DISTORTION_STRENGTH;

    // Clamp to avoid sampling outside screen
    distortedUV = clamp(distortedUV, 0.001, 0.999);

    // Sample scene depth for water depth calculation
    float sceneDepthVal = texture(sceneDepth, distortedUV).r;

    // Calculate water depth (distance to underwater surface)
    vec2 ndcScene = distortedUV * 2.0 - 1.0;
    vec4 clipPosScene = vec4(ndcScene, sceneDepthVal * 2.0 - 1.0, 1.0);
    vec4 worldPosScene = invViewProj * clipPosScene;
    vec3 underwaterPos = worldPosScene.xyz / worldPosScene.w;

    float depthToBottom = length(underwaterPos - fragPos);

    // ============================================================
    // Refraction (looking through water)
    // ============================================================
    vec3 refractedColor = texture(sceneColor, distortedUV).rgb;

    // Apply caustics to refracted light
    float causticPattern = caustics(underwaterPos.xz, depthToBottom);
    vec3 sunDir = normalize(lightDir);
    float sunContrib = max(dot(sunDir, vec3(0, 1, 0)), 0.0);
    refractedColor *= 1.0 + (causticPattern - 0.5) * sunContrib * 0.6;

    // Water color absorption based on depth
    float depthFactor = 1.0 - exp(-depthToBottom / WATER_CLARITY);
    vec3 waterTint = mix(WATER_COLOR_SHALLOW, WATER_COLOR_DEEP, depthFactor);
    refractedColor = mix(refractedColor, waterTint, depthFactor * 0.7);

    // ============================================================
    // Reflection (SSR + sky fallback)
    // ============================================================
    vec3 reflectDir = reflect(-viewDir, waterNormal);
    vec3 reflectedColor = SSR(fragPos, reflectDir, screenUV);

    // Specular highlight
    vec3 halfVec = normalize(viewDir + sunDir);
    float spec = pow(max(dot(waterNormal, halfVec), 0.0), 256.0);
    vec3 specular = lightColor * spec * 0.8;

    // ============================================================
    // Fresnel blending
    // ============================================================
    float fresnelFactor = fresnel(viewDir, waterNormal, 0.02);

    // Blend refraction and reflection
    vec3 waterColor = mix(refractedColor, reflectedColor, fresnelFactor);
    waterColor += specular;

    // ============================================================
    // Fog/distance fade
    // ============================================================
    float distanceToCamera = length(fragPos - cameraPos);
    float fogFactor = 1.0 - exp(-distanceToCamera * 0.005);
    waterColor = mix(waterColor, skyColor, fogFactor * 0.3);

    // ============================================================
    // Edge foam (where water meets terrain)
    // ============================================================
    float edgeFoam = smoothstep(0.0, 1.0, depthToBottom);
    edgeFoam = 1.0 - edgeFoam;
    float foamNoise = snoise(fragPos.xz * 2.0 + time * 0.5) * 0.5 + 0.5;
    edgeFoam *= foamNoise;
    waterColor = mix(waterColor, vec3(0.9, 0.95, 1.0), edgeFoam * 0.5);

    // Alpha based on depth (shallow water more transparent)
    float alpha = mix(0.6, 0.9, depthFactor);
    alpha = max(alpha, edgeFoam * 0.3 + 0.5);

    FragColor = vec4(waterColor, alpha);
}
