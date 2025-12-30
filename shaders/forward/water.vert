#version 460 core
// Optimized water vertex shader - simplified animation

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in float aAO;
layout (location = 4) in float aLightLevel;
layout (location = 5) in vec2 aTexSlotBase;

layout(location = 0) out vec2 texCoord;
layout(location = 1) out vec2 texSlotBase;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragPos;
layout(location = 4) out float aoFactor;
layout(location = 5) out float fogDepth;

uniform mat4 view;
uniform mat4 projection;
uniform float time;
uniform vec3 chunkOffset;
uniform float waterAnimationEnabled;

// Simple sine-based wave animation (much faster than noise)
float waveHeight(vec2 pos, float t) {
    // Two overlapping sine waves for organic motion
    float wave1 = sin(pos.x * 0.3 + pos.y * 0.2 + t * 1.5) * 0.08;
    float wave2 = sin(pos.x * 0.2 - pos.y * 0.3 + t * 1.2) * 0.06;
    float wave3 = sin((pos.x + pos.y) * 0.15 + t * 0.8) * 0.04;
    return wave1 + wave2 + wave3;
}

void main() {
    vec3 pos = aPos + chunkOffset;

    // Only animate the top surface of water (normal pointing up)
    if (aNormal.y > 0.5 && waterAnimationEnabled > 0.5) {
        pos.y += waveHeight(pos.xz, time);
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
