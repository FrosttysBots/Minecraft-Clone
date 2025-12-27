#version 460 core
layout (location = 0) in vec3 aPackedPos;
layout (location = 1) in vec2 aPackedTexCoord;  // For alpha testing
layout (location = 2) in uvec4 aPackedData;

out vec2 texCoord;
out vec2 texSlotBase;

uniform mat4 view;
uniform mat4 projection;
uniform vec3 chunkOffset;

const float ATLAS_SIZE = 16.0;
const float SLOT_SIZE = 1.0 / ATLAS_SIZE;

void main() {
    vec3 worldPos = aPackedPos / 256.0 + chunkOffset;
    gl_Position = projection * view * vec4(worldPos, 1.0);

    // Pass texture coords for alpha testing
    texCoord = aPackedTexCoord / 256.0;
    uint texSlot = aPackedData.w;
    float slotX = float(texSlot % 16u);
    float slotY = float(texSlot / 16u);
    texSlotBase = vec2(slotX * SLOT_SIZE, slotY * SLOT_SIZE);
}
