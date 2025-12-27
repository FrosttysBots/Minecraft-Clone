#version 460 core

// Fullscreen quad vertex shader for post-processing passes
// Generates a fullscreen triangle without any vertex input

out vec2 vTexCoord;

void main() {
    // Generate fullscreen triangle vertices
    // Vertex 0: (-1, -1), Vertex 1: (3, -1), Vertex 2: (-1, 3)
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vTexCoord = pos * 0.5 + 0.5;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
