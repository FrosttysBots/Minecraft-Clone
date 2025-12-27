#version 460 core

in vec2 texCoord;
in vec2 texSlotBase;

uniform sampler2D texAtlas;

const float SLOT_SIZE = 1.0 / 16.0;

void main() {
    vec2 tiledUV = texSlotBase + fract(texCoord) * SLOT_SIZE;
    float alpha = texture(texAtlas, tiledUV).a;
    if (alpha < 0.1) discard;
    // Depth is written automatically
}
