#version 460 core

// Bloom upsample pass with tent filter
// Upsamples and blends with previous mip level

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D sourceTexture;  // Lower resolution mip to upsample
uniform sampler2D prevTexture;    // Previous (higher resolution) mip to blend with
uniform vec2 texelSize;           // 1.0 / source texture resolution
uniform float blendFactor;        // How much to blend with previous mip (0.5 default)

void main() {
    // 9-tap tent filter for smooth upsampling
    vec3 a = texture(sourceTexture, vTexCoord + texelSize * vec2(-1.0, -1.0)).rgb;
    vec3 b = texture(sourceTexture, vTexCoord + texelSize * vec2( 0.0, -1.0)).rgb;
    vec3 c = texture(sourceTexture, vTexCoord + texelSize * vec2( 1.0, -1.0)).rgb;

    vec3 d = texture(sourceTexture, vTexCoord + texelSize * vec2(-1.0,  0.0)).rgb;
    vec3 e = texture(sourceTexture, vTexCoord + texelSize * vec2( 0.0,  0.0)).rgb;
    vec3 f = texture(sourceTexture, vTexCoord + texelSize * vec2( 1.0,  0.0)).rgb;

    vec3 g = texture(sourceTexture, vTexCoord + texelSize * vec2(-1.0,  1.0)).rgb;
    vec3 h = texture(sourceTexture, vTexCoord + texelSize * vec2( 0.0,  1.0)).rgb;
    vec3 i = texture(sourceTexture, vTexCoord + texelSize * vec2( 1.0,  1.0)).rgb;

    // Tent filter weights
    vec3 upsampled = (a + c + g + i) * 0.0625;       // Corners: 1/16
    upsampled += (b + d + f + h) * 0.125;            // Edges: 2/16
    upsampled += e * 0.25;                           // Center: 4/16

    // Blend with previous mip level
    vec3 prev = texture(prevTexture, vTexCoord).rgb;
    vec3 result = mix(prev, upsampled, blendFactor);

    FragColor = vec4(result, 1.0);
}
