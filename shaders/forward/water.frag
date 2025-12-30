#version 460 core
// Optimized water fragment shader - performance focused
// Simplified noise and removed FBM loops for better FPS

layout(location = 0) in vec2 texCoord;
layout(location = 1) in vec2 texSlotBase;
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
uniform float waterLodDistance;

// ============================================================
// Optimized noise - single function, no FBM loops
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

void main() {
    vec2 pos = fragPos.xz;

    // Distance-based LOD
    float distToCamera = length(fragPos - cameraPos);
    float lodFactor = clamp(distToCamera / waterLodDistance, 0.0, 1.0);

    // ============================================================
    // Simplified wave pattern - just 2-3 noise samples instead of FBM
    // This is MUCH faster than the old FBM with 5+ octaves
    // ============================================================
    float wave1 = snoise(pos * 0.1 + time * vec2(0.15, 0.1)) * 0.5;
    float wave2 = snoise(pos * 0.2 - time * vec2(0.1, 0.15)) * 0.3;

    // Add fine detail only for close water (skip for distant)
    float detail = (lodFactor < 0.4) ? snoise(pos * 0.5 + time * 0.3) * 0.15 : 0.0;

    float wavePattern = (wave1 + wave2 + detail) * 0.5 + 0.5;
    wavePattern = clamp(wavePattern, 0.0, 1.0);

    // ============================================================
    // Water coloring - simplified gradient
    // ============================================================
    vec3 waterDeep = vec3(0.05, 0.20, 0.45);
    vec3 waterSurface = vec3(0.20, 0.45, 0.70);
    vec3 waterColor = mix(waterDeep, waterSurface, wavePattern);

    // Simple sparkle for close water only
    if (lodFactor < 0.3 && wavePattern > 0.75) {
        waterColor += vec3(0.1) * (wavePattern - 0.75) * 4.0;
    }

    vec4 texColor = vec4(waterColor, 0.78);

    // Lighting calculation
    vec3 norm = normalize(fragNormal);
    vec3 lightDirection = normalize(lightDir);
    vec3 ambient = ambientColor * 0.6;
    float diff = max(dot(norm, lightDirection), 0.0);
    vec3 diffuse = diff * lightColor * 0.5;
    vec3 lighting = (ambient + diffuse) * aoFactor;

    vec3 result = texColor.rgb * lighting;

    // ============================================================
    // Simplified fog - no volumetric ray marching (huge perf win)
    // ============================================================
    if (isUnderwater > 0.5) {
        // Underwater fog
        float underwaterFog = 1.0 - exp(-fogDensity * 12.0 * fogDepth * fogDepth);
        underwaterFog = clamp(underwaterFog, 0.0, 0.95);
        vec3 underwaterColor = vec3(0.05, 0.18, 0.32);
        result = mix(result, underwaterColor, underwaterFog);

        // Blue color grading
        result *= vec3(0.6, 0.8, 1.0);

        // Simple caustics (just one noise sample instead of 4+)
        float caustic = snoise(pos * 0.2 + time * 0.03) * 0.5 + 0.5;
        caustic = smoothstep(0.3, 0.7, caustic);
        float causticFade = exp(-fogDepth * 0.02);
        result *= mix(0.9, 1.1, caustic * causticFade);
    } else {
        // Above water - simple exponential fog (no ray marching)
        float fogFactor = 1.0 - exp(-fogDensity * fogDepth * 0.8);
        fogFactor = clamp(fogFactor, 0.0, 0.9);

        // Sun contribution to fog color
        float sunUp = max(lightDirection.y, 0.0);
        vec3 fogColor = mix(skyColor, lightColor * 0.5 + vec3(0.3, 0.3, 0.25), sunUp * 0.3);

        result = mix(result, fogColor, fogFactor);
    }

    FragColor = vec4(result, texColor.a);
}
