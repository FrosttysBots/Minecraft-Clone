#version 460 core
// Packed vertex format (16 bytes total - 3x smaller than before)
layout (location = 0) in vec3 aPackedPos;     // int16 * 3, scaled by 256
layout (location = 1) in vec2 aPackedTexCoord; // uint16 * 2, 8.8 fixed point
layout (location = 2) in uvec4 aPackedData;   // normalIndex, ao, light, texSlot

out vec2 texCoord;
out vec2 texSlotBase;  // Pass to fragment shader for tiling
out vec3 fragNormal;
out vec3 fragPos;
out float aoFactor;
out float lightLevel;
out float fogDepth;
out vec2 screenPos;
out vec4 fragPosLightSpace;

uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;
uniform vec3 chunkOffset;  // World position of chunk origin

// Normal lookup table (matches CPU-side NORMAL_LOOKUP)
const vec3 NORMALS[6] = vec3[6](
    vec3(1, 0, 0),   // 0: +X
    vec3(-1, 0, 0),  // 1: -X
    vec3(0, 1, 0),   // 2: +Y
    vec3(0, -1, 0),  // 3: -Y
    vec3(0, 0, 1),   // 4: +Z
    vec3(0, 0, -1)   // 5: -Z
);

// Texture atlas constants
const float ATLAS_SIZE = 16.0;
const float SLOT_SIZE = 1.0 / ATLAS_SIZE;

void main() {
    // Decode packed position (divide by 256 to get actual position, add chunk offset)
    vec3 worldPos = aPackedPos / 256.0 + chunkOffset;

    // Decode packed texcoord (8.8 fixed point - divide by 256)
    texCoord = aPackedTexCoord / 256.0;

    // Decode packed data
    uint normalIndex = aPackedData.x;
    uint aoValue = aPackedData.y;
    uint lightValue = aPackedData.z;
    uint texSlot = aPackedData.w;

    // Look up normal from table
    fragNormal = NORMALS[normalIndex];

    // Decode AO and light (0-255 to 0.0-1.0)
    aoFactor = float(aoValue) / 255.0;
    lightLevel = float(lightValue) / 255.0;

    // Calculate texture slot base UV from slot index
    float slotX = float(texSlot % 16u);
    float slotY = float(texSlot / 16u);
    texSlotBase = vec2(slotX * SLOT_SIZE, slotY * SLOT_SIZE);

    // Transform to clip space
    vec4 viewPos = view * vec4(worldPos, 1.0);
    gl_Position = projection * viewPos;

    fragPos = worldPos;
    fogDepth = length(viewPos.xyz);
    screenPos = gl_Position.xy / gl_Position.w;
    fragPosLightSpace = lightSpaceMatrix * vec4(worldPos, 1.0);
}
