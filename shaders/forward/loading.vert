#version 460 core

layout (location = 0) in vec2 aPos;

uniform vec2 uOffset;
uniform vec2 uScale;

void main() {
    vec2 pos = aPos * uScale + uOffset;
    gl_Position = vec4(pos, 0.0, 1.0);
}
