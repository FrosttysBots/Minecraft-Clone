#version 460 core
layout (location = 0) out vec4 gPosition;  // xyz = world pos, w = AO
layout (location = 1) out vec4 gNormal;    // xyz = normal, w = light level
layout (location = 2) out vec4 gAlbedo;    // rgb = albedo, a = emission

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec2 texSlotBase;
layout(location = 4) in float aoFactor;
layout(location = 5) in float lightLevel;
layout(location = 6) in float viewDepth;

layout(binding = 0) uniform sampler2D texAtlas;
layout(binding = 3) uniform sampler2D normalMap;

uniform bool useNormalMap;

const float ATLAS_SIZE = 16.0;
const float SLOT_SIZE = 1.0 / ATLAS_SIZE;

float getEmission(vec2 uv) {
    int col = int(uv.x / SLOT_SIZE);
    int row = int(uv.y / SLOT_SIZE);
    int slot = row * 16 + col;
    if (slot == 22) return 1.0;  // Glowstone
    if (slot == 23) return 0.95; // Lava
    return 0.0;
}

void main() {
    vec2 tiledUV = texSlotBase + fract(texCoord) * SLOT_SIZE;
    vec4 texColor = texture(texAtlas, tiledUV);

    if (texColor.a < 0.1) discard;

    float emission = getEmission(texSlotBase);

    // Normal mapping with TBN matrix for voxel faces
    vec3 norm = normalize(fragNormal);

    if (useNormalMap) {
        // Sample normal map at the same UV as albedo
        vec3 normalSample = texture(normalMap, tiledUV).rgb;
        normalSample = normalSample * 2.0 - 1.0;  // Convert from [0,1] to [-1,1]

        // Compute TBN matrix for axis-aligned voxel faces
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

    gPosition = vec4(fragPos, aoFactor);
    gNormal = vec4(norm, lightLevel);
    gAlbedo = vec4(texColor.rgb, emission);
}
