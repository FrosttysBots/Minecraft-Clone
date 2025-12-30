#version 460 core
// HBAO Blur - Edge-aware bilateral blur
// Preserves edges while smoothing the AO noise

layout(location = 0) out float FragColor;

layout(location = 0) in vec2 TexCoords;

layout(binding = 0) uniform sampler2D hbaoTexture;
layout(binding = 1) uniform sampler2D gDepth;
layout(binding = 2) uniform sampler2D gNormal;

uniform vec2 texelSize;
uniform vec2 blurDirection;  // (1,0) for horizontal, (0,1) for vertical
uniform float sharpness;     // Edge-preservation strength (higher = sharper edges)

// Gaussian weights for 7-tap blur
const float weights[4] = float[](0.324, 0.232, 0.0855, 0.0205);
const int BLUR_RADIUS = 3;

// Compute depth-based weight for bilateral filtering
float computeDepthWeight(float centerDepth, float sampleDepth) {
    float depthDiff = abs(centerDepth - sampleDepth);
    return exp(-depthDiff * sharpness * 100.0);
}

// Compute normal-based weight for bilateral filtering
float computeNormalWeight(vec3 centerNormal, vec3 sampleNormal) {
    float normalDiff = 1.0 - max(dot(centerNormal, sampleNormal), 0.0);
    return exp(-normalDiff * sharpness * 10.0);
}

void main() {
    float centerAO = texture(hbaoTexture, TexCoords).r;
    float centerDepth = texture(gDepth, TexCoords).r;

    // Skip sky pixels
    if (centerDepth >= 1.0) {
        FragColor = 1.0;
        return;
    }

    vec3 centerNormal = normalize(texture(gNormal, TexCoords).xyz);

    float totalAO = centerAO * weights[0];
    float totalWeight = weights[0];

    // Blur in the specified direction
    for (int i = 1; i <= BLUR_RADIUS; i++) {
        vec2 offset = blurDirection * texelSize * float(i);

        // Positive direction
        vec2 sampleUV1 = TexCoords + offset;
        if (sampleUV1.x >= 0.0 && sampleUV1.x <= 1.0 &&
            sampleUV1.y >= 0.0 && sampleUV1.y <= 1.0) {

            float sampleAO1 = texture(hbaoTexture, sampleUV1).r;
            float sampleDepth1 = texture(gDepth, sampleUV1).r;
            vec3 sampleNormal1 = normalize(texture(gNormal, sampleUV1).xyz);

            if (sampleDepth1 < 1.0) {
                float depthWeight1 = computeDepthWeight(centerDepth, sampleDepth1);
                float normalWeight1 = computeNormalWeight(centerNormal, sampleNormal1);
                float weight1 = weights[i] * depthWeight1 * normalWeight1;

                totalAO += sampleAO1 * weight1;
                totalWeight += weight1;
            }
        }

        // Negative direction
        vec2 sampleUV2 = TexCoords - offset;
        if (sampleUV2.x >= 0.0 && sampleUV2.x <= 1.0 &&
            sampleUV2.y >= 0.0 && sampleUV2.y <= 1.0) {

            float sampleAO2 = texture(hbaoTexture, sampleUV2).r;
            float sampleDepth2 = texture(gDepth, sampleUV2).r;
            vec3 sampleNormal2 = normalize(texture(gNormal, sampleUV2).xyz);

            if (sampleDepth2 < 1.0) {
                float depthWeight2 = computeDepthWeight(centerDepth, sampleDepth2);
                float normalWeight2 = computeNormalWeight(centerNormal, sampleNormal2);
                float weight2 = weights[i] * depthWeight2 * normalWeight2;

                totalAO += sampleAO2 * weight2;
                totalWeight += weight2;
            }
        }
    }

    FragColor = totalAO / totalWeight;
}
