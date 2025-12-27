#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in float aAO;
layout (location = 4) in float aLightLevel;
layout (location = 5) in vec2 aTexSlotBase;  // Base UV of texture slot for greedy meshing

layout(location = 0) out vec2 texCoord;
layout(location = 1) out vec2 texSlotBase;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragPos;
layout(location = 4) out float aoFactor;
layout(location = 5) out float fogDepth;

uniform mat4 view;
uniform mat4 projection;
uniform float time;
uniform vec3 chunkOffset;  // Transform local to world coordinates
uniform float waterAnimationEnabled;  // 1.0 = enabled, 0.0 = disabled

// ============================================================
// Simplex 2D Noise for vertex displacement
// Simplified version for vertex shader performance
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

// Simplified FBM for vertex shader (fewer octaves for performance)
float fbmVertex(vec2 p, float t) {
    float value = 0.0;
    float amplitude = 0.5;
    mat2 rot = mat2(0.80, 0.60, -0.60, 0.80);

    // 3 octaves for vertex displacement
    for (int i = 0; i < 3; i++) {
        float speed = 0.4 + float(i) * 0.2;
        float dir = (mod(float(i), 2.0) == 0.0) ? 1.0 : -0.6;
        vec2 animP = p + t * speed * dir * vec2(0.3, 0.2);
        value += amplitude * snoise(animP);
        p = rot * p * 2.03;
        amplitude *= 0.5;
    }
    return value;
}

void main() {
    vec3 pos = aPos + chunkOffset;  // Transform to world coordinates

    // Only animate the top surface of water (normal pointing up) if animation enabled
    if (aNormal.y > 0.5 && waterAnimationEnabled > 0.5) {
        vec2 samplePos = pos.xz;

        // Large gentle waves (slow, big motion)
        float largeWave = fbmVertex(samplePos * 0.06, time * 0.3) * 0.18;

        // Medium waves (different direction/speed)
        float medWave = snoise(samplePos * 0.12 + time * vec2(-0.2, 0.35)) * 0.10;

        // Small choppy waves (faster, adds detail)
        float smallWave = snoise(samplePos * 0.3 + time * vec2(0.5, -0.3)) * 0.05;

        // Very fine ripples
        float ripples = snoise(samplePos * 0.8 + time * vec2(-0.4, 0.6)) * 0.02;

        // Combine all wave layers
        pos.y += largeWave + medWave + smallWave + ripples;
    }

    vec4 viewPos = view * vec4(pos, 1.0);
    gl_Position = projection * viewPos;
    texCoord = aTexCoord;
    texSlotBase = aTexSlotBase;
    fragNormal = aNormal;
    fragPos = pos;
    aoFactor = aAO;
    fogDepth = length(viewPos.xyz);
}
