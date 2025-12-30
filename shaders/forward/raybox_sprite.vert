#version 460 core
// Ray-Box Sprite Renderer - Vertex Shader
// Renders distant blocks as point sprites with ray-traced cube intersection
// Based on NVIDIA's "Ray-Box Intersection Algorithm and Efficient Dynamic Voxel Rendering"

// Input: One vertex per visible distant block
layout(location = 0) in vec3 aBlockPos;      // World position of block (integer coords)
layout(location = 1) in uint aBlockType;     // Block type for texture lookup

// Output to geometry/fragment shader
layout(location = 0) out vec3 vBlockMin;     // AABB min corner (world space)
layout(location = 1) out vec3 vBlockMax;     // AABB max corner (world space)
layout(location = 2) out vec3 vBlockCenter;  // Block center for billboard
layout(location = 3) flat out uint vBlockType;
layout(location = 4) out vec3 vRayOrigin;    // Camera position

uniform mat4 view;
uniform mat4 projection;
uniform vec3 cameraPos;

void main() {
    // Block bounds (unit cube at block position)
    vBlockMin = aBlockPos;
    vBlockMax = aBlockPos + vec3(1.0);
    vBlockCenter = aBlockPos + vec3(0.5);
    vBlockType = aBlockType;
    vRayOrigin = cameraPos;

    // Calculate distance to camera for point size
    vec4 viewPos = view * vec4(vBlockCenter, 1.0);
    float dist = length(viewPos.xyz);

    // Project block center
    gl_Position = projection * viewPos;

    // Calculate point size to cover the projected cube
    // A cube of size 1 at distance d projects to roughly 1/d on screen
    // We need to cover the diagonal (sqrt(3) ~= 1.732) plus some margin
    float projectedSize = 2.0 / dist;  // Size in clip space

    // Convert to pixels (assuming ~1000 pixel viewport height as baseline)
    // This will be adjusted by actual viewport in the application
    float pixelSize = projectedSize * 600.0;

    // Clamp point size to reasonable range
    gl_PointSize = clamp(pixelSize, 1.0, 128.0);
}
