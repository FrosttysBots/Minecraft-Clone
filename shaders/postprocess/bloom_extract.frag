#version 460 core

// Bloom brightness extraction pass
// Extracts pixels brighter than threshold for bloom effect

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D sceneTexture;
uniform float threshold;      // Brightness threshold (default: 1.0)
uniform float softThreshold;  // Soft knee for smooth transition (default: 0.5)

void main() {
    vec3 color = texture(sceneTexture, vTexCoord).rgb;

    // Calculate luminance
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));

    // Soft threshold with smooth knee
    float knee = threshold * softThreshold;
    float soft = brightness - threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 0.00001);

    float contribution = max(soft, brightness - threshold);
    contribution /= max(brightness, 0.00001);

    FragColor = vec4(color * contribution, 1.0);
}
