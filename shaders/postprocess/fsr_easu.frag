#version 460 core
out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D inputTexture;
uniform vec2 inputSize;      // Render resolution (e.g., 640x360)
uniform vec2 outputSize;     // Display resolution (e.g., 1280x720)

// FsrEasuConst equivalent parameters
uniform vec4 con0;  // {inputSize.x/outputSize.x, inputSize.y/outputSize.y, 0.5*inputSize.x/outputSize.x - 0.5, 0.5*inputSize.y/outputSize.y - 0.5}
uniform vec4 con1;  // {1.0/inputSize.x, 1.0/inputSize.y, 1.0/inputSize.x, -1.0/inputSize.y}
uniform vec4 con2;  // {-1.0/inputSize.x, 2.0/inputSize.y, 1.0/inputSize.x, 2.0/inputSize.y}
uniform vec4 con3;  // {0.0/inputSize.x, 4.0/inputSize.y, 0, 0}

// Compute edge-directed weights for a 12-tap filter pattern
void main() {
    // Input pixel position (in output space)
    vec2 pos = TexCoords * outputSize;

    // Position in input texture
    vec2 srcPos = pos * con0.xy + con0.zw;
    vec2 srcUV = srcPos / inputSize;

    // Get texel offsets
    vec2 texelSize = 1.0 / inputSize;

    // 12-tap filter: sample in a cross pattern around the target pixel
    // This is a simplified version of FSR EASU's edge-aware sampling

    // Center and immediate neighbors
    vec3 c = texture(inputTexture, srcUV).rgb;
    vec3 n = texture(inputTexture, srcUV + vec2(0.0, -texelSize.y)).rgb;
    vec3 s = texture(inputTexture, srcUV + vec2(0.0, texelSize.y)).rgb;
    vec3 e = texture(inputTexture, srcUV + vec2(texelSize.x, 0.0)).rgb;
    vec3 w = texture(inputTexture, srcUV + vec2(-texelSize.x, 0.0)).rgb;

    // Corner samples
    vec3 nw = texture(inputTexture, srcUV + vec2(-texelSize.x, -texelSize.y)).rgb;
    vec3 ne = texture(inputTexture, srcUV + vec2(texelSize.x, -texelSize.y)).rgb;
    vec3 sw = texture(inputTexture, srcUV + vec2(-texelSize.x, texelSize.y)).rgb;
    vec3 se = texture(inputTexture, srcUV + vec2(texelSize.x, texelSize.y)).rgb;

    // Extended samples for edge detection
    vec3 n2 = texture(inputTexture, srcUV + vec2(0.0, -2.0 * texelSize.y)).rgb;
    vec3 s2 = texture(inputTexture, srcUV + vec2(0.0, 2.0 * texelSize.y)).rgb;
    vec3 e2 = texture(inputTexture, srcUV + vec2(2.0 * texelSize.x, 0.0)).rgb;
    vec3 w2 = texture(inputTexture, srcUV + vec2(-2.0 * texelSize.x, 0.0)).rgb;

    // Compute luminance for edge detection
    float lc = dot(c, vec3(0.299, 0.587, 0.114));
    float ln = dot(n, vec3(0.299, 0.587, 0.114));
    float ls = dot(s, vec3(0.299, 0.587, 0.114));
    float le = dot(e, vec3(0.299, 0.587, 0.114));
    float lw = dot(w, vec3(0.299, 0.587, 0.114));
    float lnw = dot(nw, vec3(0.299, 0.587, 0.114));
    float lne = dot(ne, vec3(0.299, 0.587, 0.114));
    float lsw = dot(sw, vec3(0.299, 0.587, 0.114));
    float lse = dot(se, vec3(0.299, 0.587, 0.114));

    // Detect edges using Sobel-like gradients
    float gradH = abs((lnw + 2.0*lw + lsw) - (lne + 2.0*le + lse));
    float gradV = abs((lnw + 2.0*ln + lne) - (lsw + 2.0*ls + lse));

    // Subpixel offset within the source texel
    vec2 subpix = fract(srcPos) - 0.5;

    // Edge-aware interpolation weights
    // Prefer interpolation along edges, not across them
    float edgeH = 1.0 / (1.0 + gradH * 4.0);
    float edgeV = 1.0 / (1.0 + gradV * 4.0);

    // Bilinear-like weights with edge awareness
    float wx = abs(subpix.x);
    float wy = abs(subpix.y);

    // Lanczos-inspired weights (simplified)
    float wc = (1.0 - wx) * (1.0 - wy);
    float wn = (1.0 - wx) * wy * (subpix.y < 0.0 ? 1.0 : 0.0) * edgeV;
    float ws = (1.0 - wx) * wy * (subpix.y >= 0.0 ? 1.0 : 0.0) * edgeV;
    float we = wx * (1.0 - wy) * (subpix.x >= 0.0 ? 1.0 : 0.0) * edgeH;
    float ww = wx * (1.0 - wy) * (subpix.x < 0.0 ? 1.0 : 0.0) * edgeH;

    // Normalize weights
    float wsum = wc + wn + ws + we + ww + 0.0001;
    wc /= wsum;
    wn /= wsum;
    ws /= wsum;
    we /= wsum;
    ww /= wsum;

    // Final color blend
    vec3 result = c * wc + n * wn + s * ws + e * we + w * ww;

    FragColor = vec4(result, 1.0);
}
