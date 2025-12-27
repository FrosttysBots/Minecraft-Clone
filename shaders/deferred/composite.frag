#version 460 core
layout(location = 0) out vec4 FragColor;

layout(location = 0) in vec2 TexCoords;

// G-Buffer textures
layout(binding = 0) uniform sampler2D gPosition;
layout(binding = 1) uniform sampler2D gNormal;
layout(binding = 2) uniform sampler2D gAlbedo;
layout(binding = 3) uniform sampler2D gDepth;

// SSAO
layout(binding = 4) uniform sampler2D ssaoTexture;
uniform bool enableSSAO;

// Cascade shadow maps
layout(binding = 5) uniform sampler2DArrayShadow cascadeShadowMaps;
uniform mat4 cascadeMatrices[3];
uniform float cascadeSplits[3];
uniform float shadowStrength;

// Lighting uniforms
uniform vec3 lightDir;
uniform vec3 lightColor;
uniform vec3 ambientColor;
uniform vec3 skyColor;
uniform vec3 cameraPos;
uniform float time;

// Fog parameters
uniform float fogDensity;
uniform float isUnderwater;
uniform mat4 invViewProj;
uniform float renderDistanceBlocks;  // Render distance in blocks (chunks * 16)

const float FOG_HEIGHT_FALLOFF = 0.015;
const float FOG_BASE_HEIGHT = 64.0;

uniform int debugMode;  // 0=normal, 1=albedo, 2=normals, 3=position, 4=depth

float getFogDensity(float y) {
    float heightAboveBase = max(y - FOG_BASE_HEIGHT, 0.0);
    float heightFactor = exp(-heightAboveBase * FOG_HEIGHT_FALLOFF);
    float belowBase = max(FOG_BASE_HEIGHT - y, 0.0);
    float valleyFactor = 1.0 + belowBase * 0.02;
    return heightFactor * valleyFactor;
}

float calculateCascadeShadow(vec3 fragPos, vec3 normal, float viewDepth) {
    // Early out if shadows disabled
    if (shadowStrength < 0.01) return 0.0;

    // Select cascade based on view depth
    int cascade = 2;
    if (viewDepth < cascadeSplits[0]) cascade = 0;
    else if (viewDepth < cascadeSplits[1]) cascade = 1;

    // Transform to light space
    vec4 fragPosLightSpace = cascadeMatrices[cascade] * vec4(fragPos, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z > 1.0) {
        return 0.0;
    }

    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.001);
    float currentDepth = projCoords.z - bias;

    // Optimized PCF - fewer samples for distant cascades
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(cascadeShadowMaps, 0).xy);

    if (cascade == 0) {
        // Near cascade: 3x3 PCF for quality (9 samples)
        for (int x = -1; x <= 1; x++) {
            for (int y = -1; y <= 1; y++) {
                vec2 offset = vec2(x, y) * texelSize;
                shadow += texture(cascadeShadowMaps, vec4(projCoords.xy + offset, float(cascade), currentDepth));
            }
        }
        shadow = 1.0 - (shadow / 9.0);
    } else {
        // Distant cascades: 4-tap PCF for performance
        shadow += texture(cascadeShadowMaps, vec4(projCoords.xy + vec2(-0.5, -0.5) * texelSize, float(cascade), currentDepth));
        shadow += texture(cascadeShadowMaps, vec4(projCoords.xy + vec2(0.5, -0.5) * texelSize, float(cascade), currentDepth));
        shadow += texture(cascadeShadowMaps, vec4(projCoords.xy + vec2(-0.5, 0.5) * texelSize, float(cascade), currentDepth));
        shadow += texture(cascadeShadowMaps, vec4(projCoords.xy + vec2(0.5, 0.5) * texelSize, float(cascade), currentDepth));
        shadow = 1.0 - (shadow / 4.0);
    }

    // Distance fade
    float distFade = smoothstep(150.0, 250.0, viewDepth);
    return shadow * shadowStrength * (1.0 - distFade);
}

void main() {
    // Sample G-buffer
    vec4 posAO = texture(gPosition, TexCoords);
    vec4 normalLight = texture(gNormal, TexCoords);
    vec4 albedoEmit = texture(gAlbedo, TexCoords);
    float depth = texture(gDepth, TexCoords).r;

    // Debug visualization modes
    if (debugMode == 1) {
        if (depth >= 0.999) {
            FragColor = vec4(0.0, 1.0, 1.0, 1.0);
        } else if (length(albedoEmit.rgb) < 0.001) {
            FragColor = vec4(1.0, 0.0, 1.0, 1.0);
        } else {
            FragColor = vec4(albedoEmit.rgb, 1.0);
        }
        return;
    } else if (debugMode == 2) {
        if (depth >= 0.999) {
            FragColor = vec4(0.0, 1.0, 1.0, 1.0);
        } else {
            FragColor = vec4(normalLight.xyz * 0.5 + 0.5, 1.0);
        }
        return;
    } else if (debugMode == 3) {
        if (depth >= 0.999) {
            FragColor = vec4(0.0, 1.0, 1.0, 1.0);
        } else {
            vec2 ndc = TexCoords * 2.0 - 1.0;
            vec4 clipPos = vec4(ndc, depth * 2.0 - 1.0, 1.0);
            vec4 worldPos4 = invViewProj * clipPos;
            vec3 debugPos = worldPos4.xyz / worldPos4.w;
            FragColor = vec4(fract(debugPos / 16.0), 1.0);
        }
        return;
    } else if (debugMode == 4) {
        FragColor = vec4(vec3(1.0 - depth), 1.0);
        return;
    } else if (debugMode == 5) {
        if (depth >= 0.999) {
            FragColor = vec4(0.0, 1.0, 1.0, 1.0);
        } else {
            float aoVal = posAO.w;
            FragColor = vec4(aoVal, aoVal, aoVal, 1.0);
        }
        return;
    } else if (debugMode == 6) {
        if (depth >= 0.999) {
            FragColor = vec4(0.0, 1.0, 1.0, 1.0);
        } else {
            float aoVal = posAO.w;
            float ssaoVal = enableSSAO ? texture(ssaoTexture, TexCoords).r : 1.0;
            float combined = aoVal * ssaoVal;
            FragColor = vec4(combined, combined, combined, 1.0);
        }
        return;
    } else if (debugMode == 7) {
        if (depth >= 0.999) {
            FragColor = vec4(0.0, 1.0, 1.0, 1.0);
        } else {
            vec3 normal = normalize(normalLight.xyz);
            vec3 lightDirection = normalize(lightDir);
            float diff = max(dot(normal, lightDirection), 0.0);
            FragColor = vec4(diff, diff, diff, 1.0);
        }
        return;
    } else if (debugMode == 8) {
        if (depth >= 0.999) {
            FragColor = vec4(0.0, 1.0, 1.0, 1.0);
        } else {
            float ll = normalLight.w;
            FragColor = vec4(ll, ll, ll, 1.0);
        }
        return;
    }

    // Early out for sky pixels
    if (depth >= 1.0) {
        FragColor = vec4(skyColor, 1.0);
        return;
    }

    // Reconstruct world position from depth (saves G-buffer bandwidth)
    vec2 ndc = TexCoords * 2.0 - 1.0;
    vec4 clipPos = vec4(ndc, depth * 2.0 - 1.0, 1.0);
    vec4 worldPos4 = invViewProj * clipPos;
    vec3 fragPos = worldPos4.xyz / worldPos4.w;

    float ao = posAO.w;
    vec3 normal = normalize(normalLight.xyz);
    float lightLevel = normalLight.w;
    vec3 albedo = albedoEmit.rgb;
    float emission = albedoEmit.a;

    // Calculate view depth for fog and shadows
    float viewDepth = length(fragPos - cameraPos);

    // SSAO
    float ssao = enableSSAO ? texture(ssaoTexture, TexCoords).r : 1.0;
    ao *= ssao;

    // Shadow
    float shadow = calculateCascadeShadow(fragPos, normal, viewDepth);

    // Lighting calculation
    vec3 lightDirection = normalize(lightDir);
    vec3 ambient = ambientColor * 0.6;
    float diff = max(dot(normal, lightDirection), 0.0);
    vec3 diffuse = diff * lightColor * 0.6 * (1.0 - shadow);
    vec3 pointLight = lightLevel * vec3(1.0, 0.85, 0.6) * 1.2;

    vec3 lighting = (ambient + diffuse) * ao + pointLight;

    vec3 result;
    if (emission > 0.0) {
        vec3 glowColor = albedo * (1.5 + emission * 0.5);
        if (emission < 1.0) {
            float pulse = sin(time * 2.0) * 0.1 + 1.0;
            glowColor *= pulse;
        }
        result = glowColor;
    } else {
        result = albedo * lighting;
    }

    // Distance fog with LOD-hiding enhancement
    if (isUnderwater < 0.5) {
        float heightDensity = getFogDensity(fragPos.y);
        float baseFog = 1.0 - exp(-fogDensity * heightDensity * viewDepth * 0.01);
        float lodStartDist = renderDistanceBlocks * 0.7;
        float lodEndDist = renderDistanceBlocks;
        float lodFogFactor = smoothstep(lodStartDist, lodEndDist, viewDepth);
        float fogFactor = baseFog + lodFogFactor * 0.4 * (1.0 - baseFog);
        fogFactor = clamp(fogFactor, 0.0, 1.0);

        if (emission > 0.0) {
            fogFactor *= (1.0 - emission * 0.7);
        }

        vec3 fogColor = mix(skyColor, lightColor * 0.3, 0.3);
        result = mix(result, fogColor, fogFactor);
    } else {
        float underwaterFog = 1.0 - exp(-fogDensity * 16.0 * viewDepth * viewDepth / 10000.0);
        underwaterFog = clamp(underwaterFog, 0.0, 1.0);
        vec3 underwaterColor = vec3(0.05, 0.2, 0.35);
        result = mix(result, underwaterColor, underwaterFog);
        result = mix(result, result * vec3(0.4, 0.7, 0.9), 0.4);
    }

    FragColor = vec4(result, 1.0);
}
