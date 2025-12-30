#version 460 core
// Ray-Box Sprite Renderer - Fragment Shader
// Performs ray-AABB intersection to render cubes from point sprites
// Based on NVIDIA's "Ray-Box Intersection Algorithm and Efficient Dynamic Voxel Rendering"

layout(location = 0) in vec3 vBlockMin;
layout(location = 1) in vec3 vBlockMax;
layout(location = 2) in vec3 vBlockCenter;
layout(location = 3) flat in uint vBlockType;
layout(location = 4) in vec3 vRayOrigin;

layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D texAtlas;

uniform mat4 view;
uniform mat4 projection;
uniform mat4 invView;
uniform mat4 invProjection;
uniform vec3 cameraPos;
uniform vec3 lightDir;
uniform vec3 lightColor;
uniform vec3 ambientColor;
uniform vec3 skyColor;
uniform float fogDensity;
uniform float renderDistanceBlocks;
uniform float time;
uniform vec2 viewportSize;

// Texture atlas constants
const float ATLAS_SIZE = 16.0;
const float SLOT_SIZE = 1.0 / ATLAS_SIZE;

// Normal lookup for cube faces
const vec3 FACE_NORMALS[6] = vec3[6](
    vec3(1, 0, 0),   // +X
    vec3(-1, 0, 0),  // -X
    vec3(0, 1, 0),   // +Y
    vec3(0, -1, 0),  // -Y
    vec3(0, 0, 1),   // +Z
    vec3(0, 0, -1)   // -Z
);

// Ray-AABB intersection using slab method
// Returns vec2(tNear, tFar) - hit if tNear <= tFar and tFar > 0
vec2 intersectAABB(vec3 rayOrigin, vec3 rayDir, vec3 boxMin, vec3 boxMax) {
    vec3 invDir = 1.0 / rayDir;

    vec3 t1 = (boxMin - rayOrigin) * invDir;
    vec3 t2 = (boxMax - rayOrigin) * invDir;

    vec3 tMin = min(t1, t2);
    vec3 tMax = max(t1, t2);

    float tNear = max(max(tMin.x, tMin.y), tMin.z);
    float tFar = min(min(tMax.x, tMax.y), tMax.z);

    return vec2(tNear, tFar);
}

// Determine which face was hit and get its normal
int getFaceIndex(vec3 hitPoint, vec3 boxMin, vec3 boxMax) {
    vec3 center = (boxMin + boxMax) * 0.5;
    vec3 halfSize = (boxMax - boxMin) * 0.5;
    vec3 localHit = (hitPoint - center) / halfSize;

    // Find which axis has the largest absolute component
    vec3 absLocal = abs(localHit);

    if (absLocal.x > absLocal.y && absLocal.x > absLocal.z) {
        return localHit.x > 0.0 ? 0 : 1;  // +X or -X
    } else if (absLocal.y > absLocal.z) {
        return localHit.y > 0.0 ? 2 : 3;  // +Y or -Y
    } else {
        return localHit.z > 0.0 ? 4 : 5;  // +Z or -Z
    }
}

// Get UV coordinates for a face hit
vec2 getFaceUV(vec3 hitPoint, vec3 boxMin, int faceIndex) {
    vec3 localHit = hitPoint - boxMin;

    switch (faceIndex) {
        case 0: return vec2(1.0 - localHit.z, 1.0 - localHit.y); // +X
        case 1: return vec2(localHit.z, 1.0 - localHit.y);       // -X
        case 2: return vec2(localHit.x, localHit.z);             // +Y (top)
        case 3: return vec2(localHit.x, 1.0 - localHit.z);       // -Y (bottom)
        case 4: return vec2(localHit.x, 1.0 - localHit.y);       // +Z
        case 5: return vec2(1.0 - localHit.x, 1.0 - localHit.y); // -Z
    }
    return vec2(0.0);
}

// Convert linear depth to gl_FragDepth
float linearDepthToFragDepth(float linearDepth, mat4 proj) {
    // Reconstruct clip-space z from linear depth
    float near = proj[3][2] / (proj[2][2] - 1.0);
    float far = proj[3][2] / (proj[2][2] + 1.0);

    // Linear to NDC depth
    float ndcDepth = (far + near - 2.0 * near * far / linearDepth) / (far - near);

    // NDC to [0,1] range
    return ndcDepth * 0.5 + 0.5;
}

// Fog calculation (simplified from chunk.frag)
float computeFog(float dist) {
    // Height-independent fog for distant sprites
    float fogFactor = 1.0 - exp(-fogDensity * dist * 0.5);

    // LOD-hiding fog at render distance edge
    float lodStartDist = renderDistanceBlocks * 0.7;
    float lodEndDist = renderDistanceBlocks;
    float lodFogFactor = smoothstep(lodStartDist, lodEndDist, dist);
    fogFactor = max(fogFactor, lodFogFactor * 0.6);

    return clamp(fogFactor, 0.0, 1.0);
}

void main() {
    // Reconstruct ray direction from pixel position
    // gl_PointCoord is [0,1] within the point sprite
    vec2 ndc = gl_PointCoord * 2.0 - 1.0;

    // Get the projected center in clip space
    vec4 clipCenter = projection * view * vec4(vBlockCenter, 1.0);
    vec2 ndcCenter = clipCenter.xy / clipCenter.w;

    // Calculate pixel offset from center
    float pointRadius = gl_PointSize * 0.5;
    vec2 pixelOffset = ndc * pointRadius / viewportSize;

    // Final NDC position for this fragment
    vec2 fragNDC = ndcCenter + pixelOffset * 2.0;

    // Reconstruct world-space ray direction
    vec4 clipPos = vec4(fragNDC, 0.0, 1.0);
    vec4 viewPos = invProjection * clipPos;
    viewPos.z = -1.0;
    viewPos.w = 0.0;
    vec3 rayDir = normalize((invView * viewPos).xyz);

    // Perform ray-box intersection
    vec2 tHit = intersectAABB(vRayOrigin, rayDir, vBlockMin, vBlockMax);

    // Check for miss
    if (tHit.x > tHit.y || tHit.y < 0.0) {
        discard;
    }

    // Use entry point (tNear) for front faces
    float t = max(tHit.x, 0.0);
    vec3 hitPoint = vRayOrigin + rayDir * t;

    // Determine which face was hit
    int faceIndex = getFaceIndex(hitPoint, vBlockMin, vBlockMax);
    vec3 normal = FACE_NORMALS[faceIndex];

    // Get texture coordinates
    vec2 faceUV = getFaceUV(hitPoint, vBlockMin, faceIndex);

    // Calculate texture atlas UV from block type
    uint texSlot = vBlockType;  // Direct mapping for now
    float slotX = float(texSlot % 16u);
    float slotY = float(texSlot / 16u);
    vec2 atlasUV = vec2(slotX, slotY) * SLOT_SIZE + faceUV * SLOT_SIZE;

    // Sample texture
    vec4 texColor = texture(texAtlas, atlasUV);
    if (texColor.a < 0.1) discard;

    // Basic lighting
    vec3 lightDirection = normalize(lightDir);
    float diff = max(dot(normal, lightDirection), 0.0);
    vec3 ambient = ambientColor * 0.5;
    vec3 diffuse = diff * lightColor * 0.5;
    vec3 lighting = ambient + diffuse;

    vec3 result = texColor.rgb * lighting;

    // Apply fog
    float dist = length(hitPoint - cameraPos);
    float fogFactor = computeFog(dist);
    vec3 fogColor = mix(skyColor, lightColor * 0.3, 0.2);
    result = mix(result, fogColor, fogFactor);

    FragColor = vec4(result, 1.0);

    // Write correct depth
    vec4 clipHit = projection * view * vec4(hitPoint, 1.0);
    gl_FragDepth = (clipHit.z / clipHit.w) * 0.5 + 0.5;
}
