#version 460 core

// Bloom combine pass
// Adds bloom texture to original scene

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D sceneTexture;  // Original scene
uniform sampler2D bloomTexture;  // Blurred bloom
uniform float intensity;         // Bloom intensity (default: 0.5)
uniform float exposure;          // Exposure adjustment (default: 1.0)

void main() {
    vec3 scene = texture(sceneTexture, vTexCoord).rgb;
    vec3 bloom = texture(bloomTexture, vTexCoord).rgb;

    // Add bloom with intensity control
    vec3 result = scene + bloom * intensity;

    // Optional exposure adjustment
    result *= exposure;

    FragColor = vec4(result, 1.0);
}
