#pragma once

#include "Block.h"
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <cstdint>

// Chunk dimensions
constexpr int CHUNK_SIZE_X = 16;
constexpr int CHUNK_SIZE_Y = 256;
constexpr int CHUNK_SIZE_Z = 16;
constexpr int CHUNK_VOLUME = CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z;

// Water level constants
constexpr uint8_t WATER_SOURCE = 8;  // Full water source block
constexpr uint8_t WATER_MAX_SPREAD = 7;  // Max horizontal spread distance

class Chunk {
public:
    // Chunk position in chunk coordinates (not world coordinates)
    glm::ivec2 position;

    // Block data stored as flat array for cache efficiency
    // Index = x + z * CHUNK_SIZE_X + y * CHUNK_SIZE_X * CHUNK_SIZE_Z
    std::array<BlockType, CHUNK_VOLUME> blocks;

    // Water level data (0 = no water, 1-7 = flowing, 8 = source)
    std::array<uint8_t, CHUNK_VOLUME> waterLevels;

    // Light levels (0-15, like Minecraft)
    // Stores block light from emissive sources (glowstone, lava, etc.)
    std::array<uint8_t, CHUNK_VOLUME> lightLevels;

    // Heightmap optimization: min/max Y per column to skip empty regions
    // Index = x + z * CHUNK_SIZE_X
    std::array<uint8_t, CHUNK_SIZE_X * CHUNK_SIZE_Z> minY;  // Lowest non-air block
    std::array<uint8_t, CHUNK_SIZE_X * CHUNK_SIZE_Z> maxY;  // Highest non-air block

    // Global min/max for entire chunk (for fast culling)
    uint8_t chunkMinY = 255;
    uint8_t chunkMaxY = 0;

    // Mesh needs rebuilding?
    bool isDirty = true;

    // Has pending water updates?
    bool hasWaterUpdates = false;

    // Does this chunk contain any water? (cached for culling optimization)
    bool hasWater = false;

    // Constructor
    Chunk(glm::ivec2 chunkPos = glm::ivec2(0))
        : position(chunkPos)
    {
        // Initialize all blocks to air
        blocks.fill(BlockType::AIR);
        waterLevels.fill(0);
        lightLevels.fill(0);
        minY.fill(255);  // No blocks yet
        maxY.fill(0);
    }

    // Convert local coords to array index
    static inline int toIndex(int x, int y, int z) {
        return x + z * CHUNK_SIZE_X + y * CHUNK_SIZE_X * CHUNK_SIZE_Z;
    }

    // Check if local coordinates are valid
    static inline bool isValidPosition(int x, int y, int z) {
        return x >= 0 && x < CHUNK_SIZE_X &&
               y >= 0 && y < CHUNK_SIZE_Y &&
               z >= 0 && z < CHUNK_SIZE_Z;
    }

    // Get block at local position
    BlockType getBlock(int x, int y, int z) const {
        if (!isValidPosition(x, y, z)) {
            return BlockType::AIR;
        }
        return blocks[toIndex(x, y, z)];
    }

    // Set block at local position
    void setBlock(int x, int y, int z, BlockType type) {
        if (!isValidPosition(x, y, z)) {
            return;
        }
        int idx = toIndex(x, y, z);
        BlockType oldType = blocks[idx];
        blocks[idx] = type;

        // Update heightmap
        int colIdx = x + z * CHUNK_SIZE_X;
        uint8_t uy = static_cast<uint8_t>(y);

        if (type != BlockType::AIR) {
            // Placing a solid block - update min/max
            if (uy < minY[colIdx]) minY[colIdx] = uy;
            if (uy > maxY[colIdx]) maxY[colIdx] = uy;
            if (uy < chunkMinY) chunkMinY = uy;
            if (uy > chunkMaxY) chunkMaxY = uy;
        } else if (oldType != BlockType::AIR) {
            // Removing a block - may need to recalculate column heights
            if (uy == minY[colIdx] || uy == maxY[colIdx]) {
                recalculateColumnHeight(x, z);
            }
        }

        // If placing water, set it as a source block
        if (type == BlockType::WATER) {
            waterLevels[idx] = WATER_SOURCE;
            hasWaterUpdates = true;
            hasWater = true;
        } else if (oldType == BlockType::WATER) {
            // If replacing water, clear the water level
            waterLevels[idx] = 0;
        }
        isDirty = true;
    }

    // Recalculate height for a single column
    void recalculateColumnHeight(int x, int z) {
        int colIdx = x + z * CHUNK_SIZE_X;
        minY[colIdx] = 255;
        maxY[colIdx] = 0;

        for (int y = 0; y < CHUNK_SIZE_Y; y++) {
            if (blocks[toIndex(x, y, z)] != BlockType::AIR) {
                uint8_t uy = static_cast<uint8_t>(y);
                if (uy < minY[colIdx]) minY[colIdx] = uy;
                maxY[colIdx] = uy;  // Keep updating to get highest
            }
        }
    }

    // Recalculate all heightmaps (call after terrain generation)
    void recalculateHeightmaps() {
        minY.fill(255);
        maxY.fill(0);
        chunkMinY = 255;
        chunkMaxY = 0;

        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
            for (int x = 0; x < CHUNK_SIZE_X; x++) {
                int colIdx = x + z * CHUNK_SIZE_X;
                for (int y = 0; y < CHUNK_SIZE_Y; y++) {
                    if (blocks[toIndex(x, y, z)] != BlockType::AIR) {
                        uint8_t uy = static_cast<uint8_t>(y);
                        if (uy < minY[colIdx]) minY[colIdx] = uy;
                        maxY[colIdx] = uy;
                    }
                }
                // Update chunk-wide min/max
                if (minY[colIdx] < chunkMinY) chunkMinY = minY[colIdx];
                if (maxY[colIdx] > chunkMaxY) chunkMaxY = maxY[colIdx];
            }
        }
    }

    // Get min/max Y for a column (for mesh generation optimization)
    inline uint8_t getColumnMinY(int x, int z) const {
        return minY[x + z * CHUNK_SIZE_X];
    }

    inline uint8_t getColumnMaxY(int x, int z) const {
        return maxY[x + z * CHUNK_SIZE_X];
    }

    // Get water level at local position
    uint8_t getWaterLevel(int x, int y, int z) const {
        if (!isValidPosition(x, y, z)) {
            return 0;
        }
        return waterLevels[toIndex(x, y, z)];
    }

    // Set water level at local position
    void setWaterLevel(int x, int y, int z, uint8_t level) {
        if (!isValidPosition(x, y, z)) {
            return;
        }
        int idx = toIndex(x, y, z);
        waterLevels[idx] = level;

        // Update block type based on water level
        if (level > 0 && blocks[idx] == BlockType::AIR) {
            blocks[idx] = BlockType::WATER;
            isDirty = true;
            hasWater = true;
        } else if (level == 0 && blocks[idx] == BlockType::WATER) {
            blocks[idx] = BlockType::AIR;
            isDirty = true;
        }
    }

    // Get light level at local position
    uint8_t getLightLevel(int x, int y, int z) const {
        if (!isValidPosition(x, y, z)) {
            return 0;
        }
        return lightLevels[toIndex(x, y, z)];
    }

    // Set light level at local position
    void setLightLevel(int x, int y, int z, uint8_t level) {
        if (!isValidPosition(x, y, z)) {
            return;
        }
        lightLevels[toIndex(x, y, z)] = level;
    }

    // Get world position of chunk origin
    glm::vec3 getWorldPosition() const {
        return glm::vec3(
            position.x * CHUNK_SIZE_X,
            0.0f,
            position.y * CHUNK_SIZE_Z
        );
    }

    // Convert world position to local chunk position
    static glm::ivec3 worldToLocal(const glm::vec3& worldPos, const glm::ivec2& chunkPos) {
        return glm::ivec3(
            static_cast<int>(worldPos.x) - chunkPos.x * CHUNK_SIZE_X,
            static_cast<int>(worldPos.y),
            static_cast<int>(worldPos.z) - chunkPos.y * CHUNK_SIZE_Z
        );
    }

    // Get chunk coordinates from world position
    static glm::ivec2 worldToChunkPos(const glm::vec3& worldPos) {
        return glm::ivec2(
            static_cast<int>(floor(worldPos.x / CHUNK_SIZE_X)),
            static_cast<int>(floor(worldPos.z / CHUNK_SIZE_Z))
        );
    }
};
