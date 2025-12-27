#version 460 core
layout (location = 0) in vec2 aPos;

layout(location = 0) out vec2 screenPos;

void main() {
    screenPos = aPos;
    gl_Position = vec4(aPos, 0.9999, 1.0);  // Far plane
}
