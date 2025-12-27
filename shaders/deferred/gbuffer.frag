#version 460 core
layout (location = 0) out vec4 gPosition;  // xyz = world pos, w = AO
layout (location = 1) out vec4 gNormal;    // xyz = normal, w = light level
layout (location = 2) out vec4 gAlbedo;    // rgb = albedo, a = emission

in vec3 fragPos;
in vec3 fragNormal;
in vec2 texCoord;
in vec2 texSlotBase;
in float aoFactor;
in float lightLevel;
in float viewDepth;

uniform sampler2D texAtlas;

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

    gPosition = vec4(fragPos, aoFactor);
    gNormal = vec4(normalize(fragNormal), lightLevel);
    gAlbedo = vec4(texColor.rgb, emission);
}
