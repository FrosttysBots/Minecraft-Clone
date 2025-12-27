#version 460 core
layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec2 TexCoords;

layout(binding = 0) uniform sampler2D inputTexture;
uniform vec2 texelSize;     // 1.0 / resolution
uniform float sharpness;    // 0.0 = no sharpening, 2.0 = max (default 0.5)

void main() {
    // Sample the center and 4 neighbors (plus pattern)
    vec3 c = texture(inputTexture, TexCoords).rgb;
    vec3 n = texture(inputTexture, TexCoords + vec2(0.0, -texelSize.y)).rgb;
    vec3 s = texture(inputTexture, TexCoords + vec2(0.0, texelSize.y)).rgb;
    vec3 e = texture(inputTexture, TexCoords + vec2(texelSize.x, 0.0)).rgb;
    vec3 w = texture(inputTexture, TexCoords + vec2(-texelSize.x, 0.0)).rgb;

    // Compute min and max of neighborhood (for clamping)
    vec3 minC = min(c, min(min(n, s), min(e, w)));
    vec3 maxC = max(c, max(max(n, s), max(e, w)));

    // Average of neighbors
    vec3 avg = (n + s + e + w) * 0.25;

    // Compute local contrast
    vec3 diff = c - avg;

    // Apply sharpening with contrast-adaptive strength
    // The sharpening is reduced in high-contrast areas to prevent halos
    vec3 contrast = maxC - minC + 0.0001;
    vec3 adaptiveSharp = sharpness / (contrast + 0.5);
    adaptiveSharp = min(adaptiveSharp, vec3(1.0));  // Cap sharpening strength

    // Sharpen
    vec3 result = c + diff * adaptiveSharp;

    // Clamp to neighborhood bounds to prevent ringing/halos
    result = clamp(result, minC, maxC);

    FragColor = vec4(result, 1.0);
}
