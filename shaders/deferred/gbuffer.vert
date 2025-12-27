#version 460 core
layout (location = 0) in vec3 aPackedPos;
layout (location = 1) in vec2 aPackedTexCoord;
layout (location = 2) in uvec4 aPackedData;

out vec3 fragPos;
out vec3 fragNormal;
out vec2 texCoord;
out vec2 texSlotBase;
out float aoFactor;
out float lightLevel;
out float viewDepth;

uniform mat4 view;
uniform mat4 projection;
uniform vec3 chunkOffset;

const vec3 NORMALS[6] = vec3[6](
    vec3(1, 0, 0), vec3(-1, 0, 0),
    vec3(0, 1, 0), vec3(0, -1, 0),
    vec3(0, 0, 1), vec3(0, 0, -1)
);

const float ATLAS_SIZE = 16.0;
const float SLOT_SIZE = 1.0 / ATLAS_SIZE;

void main() {
    vec3 worldPos = aPackedPos / 256.0 + chunkOffset;
    texCoord = aPackedTexCoord / 256.0;

    uint normalIndex = aPackedData.x;
    uint aoValue = aPackedData.y;
    uint lightValue = aPackedData.z;
    uint texSlot = aPackedData.w;

    fragNormal = NORMALS[normalIndex];
    aoFactor = float(aoValue) / 255.0;
    lightLevel = float(lightValue) / 255.0;

    float slotX = float(texSlot % 16u);
    float slotY = float(texSlot / 16u);
    texSlotBase = vec2(slotX * SLOT_SIZE, slotY * SLOT_SIZE);

    vec4 viewPos = view * vec4(worldPos, 1.0);
    gl_Position = projection * viewPos;
    fragPos = worldPos;
    viewDepth = -viewPos.z;  // Positive depth in view space
}
