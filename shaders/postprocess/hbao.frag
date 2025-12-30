#version 460 core
// HBAO (Horizon-Based Ambient Occlusion)
// Based on NVIDIA's HBAO+ algorithm
// More accurate than standard SSAO with better contact shadows

layout(location = 0) out float FragColor;

layout(location = 0) in vec2 TexCoords;

layout(binding = 1) uniform sampler2D gPosition;
layout(binding = 2) uniform sampler2D gNormal;
layout(binding = 3) uniform sampler2D gDepth;
layout(binding = 4) uniform sampler2D noiseTexture;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 invProjection;
uniform vec2 noiseScale;
uniform vec2 resolution;
uniform float radius;           // World-space radius
uniform float intensity;        // AO intensity multiplier
uniform float bias;             // Angle bias to reduce self-occlusion
uniform float maxRadiusPixels;  // Maximum radius in pixels (for performance)
uniform float nearPlane;
uniform float farPlane;

// HBAO parameters
const int NUM_DIRECTIONS = 8;     // Number of ray directions
const int NUM_STEPS = 4;          // Steps per direction
const float PI = 3.14159265359;
const float TWO_PI = 6.28318530718;

// Convert depth buffer value to linear view-space depth
float linearizeDepth(float depth) {
    float z = depth * 2.0 - 1.0;
    return (2.0 * nearPlane * farPlane) / (farPlane + nearPlane - z * (farPlane - nearPlane));
}

// Reconstruct view-space position from depth
vec3 getViewPos(vec2 uv) {
    float depth = texture(gDepth, uv).r;
    if (depth >= 1.0) return vec3(0.0);

    // Get world position and transform to view space
    vec3 worldPos = texture(gPosition, uv).xyz;
    return (view * vec4(worldPos, 1.0)).xyz;
}

// Get view-space position using depth reconstruction (faster)
vec3 getViewPosFromDepth(vec2 uv, float depth) {
    vec2 ndc = uv * 2.0 - 1.0;
    vec4 clipPos = vec4(ndc, depth * 2.0 - 1.0, 1.0);
    vec4 viewPos = invProjection * clipPos;
    return viewPos.xyz / viewPos.w;
}

// Compute the horizon angle for a single direction
float computeHorizonAngle(vec3 viewPos, vec3 viewNormal, vec2 rayDir, float jitter) {
    float horizonAngle = -1.0;  // -1 = no horizon found (looking at sky)

    // Convert radius to screen-space pixels
    float radiusPixels = min(radius / abs(viewPos.z) * projection[0][0] * resolution.x * 0.5, maxRadiusPixels);

    // Step size in pixels
    float stepSize = radiusPixels / float(NUM_STEPS);

    vec2 texelSize = 1.0 / resolution;
    vec2 stepDir = rayDir * texelSize * stepSize;

    // Apply jitter to reduce banding
    vec2 sampleUV = TexCoords + stepDir * jitter;

    for (int step = 1; step <= NUM_STEPS; step++) {
        sampleUV += stepDir;

        // Skip if outside screen
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
            continue;

        // Get sample depth
        float sampleDepth = texture(gDepth, sampleUV).r;
        if (sampleDepth >= 1.0) continue;

        // Reconstruct sample view position
        vec3 sampleViewPos = getViewPosFromDepth(sampleUV, sampleDepth);

        // Vector from current pixel to sample
        vec3 horizonVec = sampleViewPos - viewPos;
        float horizonDist = length(horizonVec);

        // Skip if too far (falloff)
        if (horizonDist > radius) continue;

        // Normalize
        horizonVec /= horizonDist;

        // Calculate horizon angle (angle above the tangent plane)
        float angle = dot(horizonVec, viewNormal);

        // Apply distance-based falloff
        float falloff = 1.0 - (horizonDist / radius);
        falloff = falloff * falloff;

        // Update maximum horizon angle
        horizonAngle = max(horizonAngle, angle * falloff);
    }

    return horizonAngle;
}

// Compute AO from horizon angles
float computeAO(float horizonAngle, float tangentAngle) {
    // The occlusion is based on how much the horizon is above the tangent plane
    // If horizon is below tangent (negative), no occlusion
    // If horizon is above tangent, proportional occlusion

    float ao = 0.0;

    if (horizonAngle > tangentAngle + bias) {
        // Integrate occlusion over the angle difference
        float theta = acos(clamp(tangentAngle, -1.0, 1.0));
        float phi = acos(clamp(horizonAngle, -1.0, 1.0));

        // Approximate integral of cos over the angle range
        ao = sin(theta) - sin(phi);
        ao = max(ao, 0.0);
    }

    return ao;
}

void main() {
    float depth = texture(gDepth, TexCoords).r;

    // Skip sky pixels
    if (depth >= 1.0) {
        FragColor = 1.0;
        return;
    }

    // Get view-space position and normal
    vec3 viewPos = getViewPosFromDepth(TexCoords, depth);
    vec3 worldNormal = normalize(texture(gNormal, TexCoords).xyz);
    vec3 viewNormal = normalize((view * vec4(worldNormal, 0.0)).xyz);

    // Get random rotation for this pixel (reduces banding)
    vec2 noise = texture(noiseTexture, TexCoords * noiseScale).xy;
    float randomAngle = noise.x * TWO_PI;
    float jitter = noise.y;

    // Tangent angle (angle of the surface normal from the view direction)
    // For a surface facing the camera, this is close to 0
    // For a surface parallel to the view, this is close to 1
    float tangentAngle = dot(viewNormal, normalize(-viewPos));

    float totalAO = 0.0;

    // Sample in multiple directions around the pixel
    for (int dir = 0; dir < NUM_DIRECTIONS; dir++) {
        // Direction angle (evenly spaced + random offset)
        float angle = (float(dir) / float(NUM_DIRECTIONS)) * TWO_PI + randomAngle;

        // Ray direction in screen space
        vec2 rayDir = vec2(cos(angle), sin(angle));

        // Compute horizon angle for this direction
        float horizonAngle = computeHorizonAngle(viewPos, viewNormal, rayDir, jitter);

        // Compute AO contribution from this direction
        if (horizonAngle > -0.99) {
            totalAO += computeAO(horizonAngle, tangentAngle);
        }
    }

    // Normalize by number of directions
    totalAO /= float(NUM_DIRECTIONS);

    // Apply intensity
    totalAO *= intensity;

    // Convert to visibility (1 = fully visible, 0 = fully occluded)
    float visibility = 1.0 - clamp(totalAO, 0.0, 1.0);

    // Increase contrast for more visible effect
    visibility = pow(visibility, 1.5);

    FragColor = visibility;
}
