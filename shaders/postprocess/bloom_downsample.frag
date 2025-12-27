#version 460 core

// Bloom downsample pass with 13-tap box filter
// Reduces resolution while preserving bright features

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D sourceTexture;
uniform vec2 texelSize;  // 1.0 / source texture resolution

void main() {
    // 13-tap downsample filter (anti-aliased box filter)
    // Samples in a pattern that covers a 4x4 area with proper weighting

    vec3 a = texture(sourceTexture, vTexCoord + texelSize * vec2(-2.0, -2.0)).rgb;
    vec3 b = texture(sourceTexture, vTexCoord + texelSize * vec2( 0.0, -2.0)).rgb;
    vec3 c = texture(sourceTexture, vTexCoord + texelSize * vec2( 2.0, -2.0)).rgb;

    vec3 d = texture(sourceTexture, vTexCoord + texelSize * vec2(-2.0,  0.0)).rgb;
    vec3 e = texture(sourceTexture, vTexCoord + texelSize * vec2( 0.0,  0.0)).rgb;
    vec3 f = texture(sourceTexture, vTexCoord + texelSize * vec2( 2.0,  0.0)).rgb;

    vec3 g = texture(sourceTexture, vTexCoord + texelSize * vec2(-2.0,  2.0)).rgb;
    vec3 h = texture(sourceTexture, vTexCoord + texelSize * vec2( 0.0,  2.0)).rgb;
    vec3 i = texture(sourceTexture, vTexCoord + texelSize * vec2( 2.0,  2.0)).rgb;

    vec3 j = texture(sourceTexture, vTexCoord + texelSize * vec2(-1.0, -1.0)).rgb;
    vec3 k = texture(sourceTexture, vTexCoord + texelSize * vec2( 1.0, -1.0)).rgb;
    vec3 l = texture(sourceTexture, vTexCoord + texelSize * vec2(-1.0,  1.0)).rgb;
    vec3 m = texture(sourceTexture, vTexCoord + texelSize * vec2( 1.0,  1.0)).rgb;

    // Weighted average with higher weight on center samples
    vec3 result = e * 0.125;  // Center sample
    result += (a + c + g + i) * 0.03125;  // Corner samples
    result += (b + d + f + h) * 0.0625;   // Edge samples
    result += (j + k + l + m) * 0.125;    // Near-center samples

    FragColor = vec4(result, 1.0);
}
