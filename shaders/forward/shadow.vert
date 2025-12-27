#version 460 core
layout (location = 0) in vec3 aPackedPos;  // Packed int16 positions
layout (location = 1) in vec2 aPackedTexCoord;  // Not used for shadows
layout (location = 2) in uvec4 aPackedData;  // Not used for shadows

uniform mat4 lightSpaceMatrix;
uniform vec3 chunkOffset;

void main() {
    vec3 worldPos = aPackedPos / 256.0 + chunkOffset;
    gl_Position = lightSpaceMatrix * vec4(worldPos, 1.0);
}
