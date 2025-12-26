#pragma once

#include <glm/glm.hpp>
#include <cmath>
#include <optional>

// Result of a raycast hit
struct RaycastHit {
    glm::ivec3 blockPos;      // Position of the hit block
    glm::ivec3 normal;        // Face normal (direction to place new block)
    float distance;           // Distance from ray origin
    glm::vec3 hitPoint;       // Exact hit point in world space
};

// DDA (Digital Differential Analyzer) raycast through voxel grid
// This is the standard algorithm for voxel raycasting (used in Minecraft)
class Raycast {
public:
    // Cast a ray and find the first solid block it hits
    // Returns nullopt if no block hit within maxDistance
    template<typename BlockChecker>
    static std::optional<RaycastHit> cast(
        const glm::vec3& origin,
        const glm::vec3& direction,
        float maxDistance,
        BlockChecker isSolid  // Function: bool(int x, int y, int z)
    ) {
        // Normalize direction
        glm::vec3 dir = glm::normalize(direction);

        // Current voxel position
        glm::ivec3 voxel(
            static_cast<int>(floor(origin.x)),
            static_cast<int>(floor(origin.y)),
            static_cast<int>(floor(origin.z))
        );

        // Direction to step in each axis
        glm::ivec3 step(
            (dir.x >= 0) ? 1 : -1,
            (dir.y >= 0) ? 1 : -1,
            (dir.z >= 0) ? 1 : -1
        );

        // Distance along ray to next voxel boundary for each axis
        glm::vec3 tMax;
        glm::vec3 tDelta;

        // Calculate tMax and tDelta for each axis
        for (int i = 0; i < 3; i++) {
            if (std::abs(dir[i]) < 0.0001f) {
                // Ray is parallel to this axis
                tMax[i] = std::numeric_limits<float>::infinity();
                tDelta[i] = std::numeric_limits<float>::infinity();
            } else {
                // Distance to next boundary
                float boundary = (step[i] > 0)
                    ? static_cast<float>(voxel[i] + 1)
                    : static_cast<float>(voxel[i]);
                tMax[i] = (boundary - origin[i]) / dir[i];
                tDelta[i] = static_cast<float>(step[i]) / dir[i];
            }
        }

        // Track which face we entered from
        glm::ivec3 normal(0);
        float distance = 0.0f;

        // DDA loop
        while (distance < maxDistance) {
            // Check current voxel
            if (isSolid(voxel.x, voxel.y, voxel.z)) {
                RaycastHit hit;
                hit.blockPos = voxel;
                hit.normal = normal;
                hit.distance = distance;
                hit.hitPoint = origin + dir * distance;
                return hit;
            }

            // Move to next voxel boundary
            if (tMax.x < tMax.y && tMax.x < tMax.z) {
                distance = tMax.x;
                tMax.x += tDelta.x;
                voxel.x += step.x;
                normal = glm::ivec3(-step.x, 0, 0);
            } else if (tMax.y < tMax.z) {
                distance = tMax.y;
                tMax.y += tDelta.y;
                voxel.y += step.y;
                normal = glm::ivec3(0, -step.y, 0);
            } else {
                distance = tMax.z;
                tMax.z += tDelta.z;
                voxel.z += step.z;
                normal = glm::ivec3(0, 0, -step.z);
            }
        }

        return std::nullopt; // No hit
    }
};
