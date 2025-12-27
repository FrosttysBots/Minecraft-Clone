#version 460 core
layout(location = 0) out float FragColor;

layout(location = 0) in vec2 TexCoords;

layout(binding = 1) uniform sampler2D gPosition;
layout(binding = 2) uniform sampler2D gNormal;
layout(binding = 3) uniform sampler2D gDepth;
layout(binding = 4) uniform sampler2D noiseTexture;

// OPTIMIZATION: Use UBO for kernel samples (uploaded once, not per-frame)
// Reduced from 32 to 16 samples for 2x performance
layout(std140, binding = 0) uniform SSAOKernel {
    vec4 samples[16];  // vec4 for std140 alignment (only xyz used)
};

uniform mat4 projection;
uniform mat4 view;
uniform vec2 noiseScale;
uniform float radius;
uniform float bias;

void main() {
    vec3 fragPos = texture(gPosition, TexCoords).xyz;
    vec3 normal = normalize(texture(gNormal, TexCoords).xyz);
    float depth = texture(gDepth, TexCoords).r;

    // Skip sky pixels
    if (depth >= 1.0) {
        FragColor = 1.0;
        return;
    }

    // Transform to view space
    vec3 fragPosView = (view * vec4(fragPos, 1.0)).xyz;
    vec3 normalView = normalize((view * vec4(normal, 0.0)).xyz);

    // Random rotation from noise texture
    vec3 randomVec = normalize(texture(noiseTexture, TexCoords * noiseScale).xyz);

    // Gram-Schmidt to create TBN matrix
    vec3 tangent = normalize(randomVec - normalView * dot(randomVec, normalView));
    vec3 bitangent = cross(normalView, tangent);
    mat3 TBN = mat3(tangent, bitangent, normalView);

    float occlusion = 0.0;
    // OPTIMIZATION: Reduced from 32 to 16 samples
    for (int i = 0; i < 16; i++) {
        // Get sample position (samples stored as vec4 for std140 alignment)
        vec3 sampleDir = TBN * samples[i].xyz;
        vec3 samplePos = fragPosView + sampleDir * radius;

        // Project sample to screen space
        vec4 offset = projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;

        // Sample depth at this position
        float sampleDepth = texture(gDepth, offset.xy).r;

        // Reconstruct view-space position of sampled point
        vec3 sampledPos = texture(gPosition, offset.xy).xyz;
        float sampledDepth = (view * vec4(sampledPos, 1.0)).z;

        // Range check and compare
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPosView.z - sampledDepth));
        occlusion += (sampledDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    occlusion = 1.0 - (occlusion / 16.0);
    FragColor = pow(occlusion, 2.0);  // Increase contrast
}
