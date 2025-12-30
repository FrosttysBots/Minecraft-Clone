#version 460 core
// Deferred Water Vertex Shader
// Handles vertex displacement for wave animation

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in float aAO;
layout (location = 4) in float aLightLevel;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 texCoord;
layout(location = 3) out vec4 clipSpacePos;
layout(location = 4) out vec4 clipSpacePosOld;  // For motion/temporal effects
layout(location = 5) out float waterDepth;

uniform mat4 view;
uniform mat4 projection;
uniform mat4 prevViewProj;  // Previous frame for temporal effects
uniform float time;
uniform vec3 chunkOffset;
uniform vec3 cameraPos;

// Simplex noise for wave displacement
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

// Gerstner wave for more realistic ocean motion
vec3 gerstnerWave(vec2 pos, vec2 dir, float steepness, float wavelength, float t) {
    float k = 2.0 * 3.14159 / wavelength;
    float c = sqrt(9.8 / k);
    vec2 d = normalize(dir);
    float f = k * (dot(d, pos) - c * t);
    float a = steepness / k;

    return vec3(
        d.x * a * cos(f),
        a * sin(f),
        d.y * a * cos(f)
    );
}

void main() {
    vec3 pos = aPos + chunkOffset;
    vec3 originalPos = pos;

    // Only animate top surface of water
    if (aNormal.y > 0.5) {
        vec2 samplePos = pos.xz;

        // Gerstner waves for realistic ocean motion
        vec3 wave1 = gerstnerWave(samplePos, vec2(1.0, 0.3), 0.15, 12.0, time * 0.8);
        vec3 wave2 = gerstnerWave(samplePos, vec2(-0.7, 0.5), 0.12, 8.0, time * 0.6);
        vec3 wave3 = gerstnerWave(samplePos, vec2(0.2, -0.8), 0.08, 5.0, time * 1.0);

        // Combine waves
        vec3 waveOffset = wave1 + wave2 + wave3;

        // Add small noise ripples
        float ripple = snoise(samplePos * 0.5 + time * 0.3) * 0.05;
        ripple += snoise(samplePos * 1.0 + time * 0.5) * 0.03;

        pos += waveOffset;
        pos.y += ripple;
    }

    // Calculate water depth from camera
    waterDepth = length(pos - cameraPos);

    vec4 viewPos = view * vec4(pos, 1.0);
    clipSpacePos = projection * viewPos;
    clipSpacePosOld = prevViewProj * vec4(originalPos, 1.0);

    gl_Position = clipSpacePos;

    fragPos = pos;
    fragNormal = aNormal;
    texCoord = aTexCoord;
}
