#pragma once

#include "Chunk.h"
#include "TerrainGenerator.h"
#include "ChunkThreadPool.h"
#include "../render/ChunkMesh.h"
#include <unordered_map>
#include <memory>
#include <vector>
#include <algorithm>
#include <thread>
#include <iostream>
#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

// Frustum culling for chunks
class Frustum {
public:
    std::array<glm::vec4, 6> planes;  // Left, Right, Bottom, Top, Near, Far

    // Extract frustum planes from view-projection matrix
    void update(const glm::mat4& viewProj) {
        // Left plane
        planes[0] = glm::vec4(
            viewProj[0][3] + viewProj[0][0],
            viewProj[1][3] + viewProj[1][0],
            viewProj[2][3] + viewProj[2][0],
            viewProj[3][3] + viewProj[3][0]
        );
        // Right plane
        planes[1] = glm::vec4(
            viewProj[0][3] - viewProj[0][0],
            viewProj[1][3] - viewProj[1][0],
            viewProj[2][3] - viewProj[2][0],
            viewProj[3][3] - viewProj[3][0]
        );
        // Bottom plane
        planes[2] = glm::vec4(
            viewProj[0][3] + viewProj[0][1],
            viewProj[1][3] + viewProj[1][1],
            viewProj[2][3] + viewProj[2][1],
            viewProj[3][3] + viewProj[3][1]
        );
        // Top plane
        planes[3] = glm::vec4(
            viewProj[0][3] - viewProj[0][1],
            viewProj[1][3] - viewProj[1][1],
            viewProj[2][3] - viewProj[2][1],
            viewProj[3][3] - viewProj[3][1]
        );
        // Near plane
        planes[4] = glm::vec4(
            viewProj[0][3] + viewProj[0][2],
            viewProj[1][3] + viewProj[1][2],
            viewProj[2][3] + viewProj[2][2],
            viewProj[3][3] + viewProj[3][2]
        );
        // Far plane
        planes[5] = glm::vec4(
            viewProj[0][3] - viewProj[0][2],
            viewProj[1][3] - viewProj[1][2],
            viewProj[2][3] - viewProj[2][2],
            viewProj[3][3] - viewProj[3][2]
        );

        // Normalize planes
        for (auto& plane : planes) {
            float length = glm::length(glm::vec3(plane));
            plane /= length;
        }
    }

    // Check if an AABB (axis-aligned bounding box) is inside or intersects the frustum
    bool isBoxVisible(const glm::vec3& min, const glm::vec3& max) const {
        for (const auto& plane : planes) {
            glm::vec3 pVertex = min;
            if (plane.x >= 0) pVertex.x = max.x;
            if (plane.y >= 0) pVertex.y = max.y;
            if (plane.z >= 0) pVertex.z = max.z;

            if (glm::dot(glm::vec3(plane), pVertex) + plane.w < 0) {
                return false;  // Box is completely outside this plane
            }
        }
        return true;  // Box is inside or intersecting all planes
    }

    // Check if a chunk is visible
    bool isChunkVisible(const glm::ivec2& chunkPos) const {
        glm::vec3 min(
            chunkPos.x * CHUNK_SIZE_X,
            0.0f,
            chunkPos.y * CHUNK_SIZE_Z
        );
        glm::vec3 max(
            (chunkPos.x + 1) * CHUNK_SIZE_X,
            static_cast<float>(CHUNK_SIZE_Y),
            (chunkPos.y + 1) * CHUNK_SIZE_Z
        );
        return isBoxVisible(min, max);
    }

    // Check if a sub-chunk is visible (16x16x16 section)
    // subChunkPos.x = chunk X, subChunkPos.y = sub-chunk Y index (0-15), subChunkPos.z = chunk Z
    bool isSubChunkVisible(const glm::ivec3& subChunkPos) const {
        glm::vec3 min(
            subChunkPos.x * CHUNK_SIZE_X,
            subChunkPos.y * SUB_CHUNK_HEIGHT,
            subChunkPos.z * CHUNK_SIZE_Z
        );
        glm::vec3 max(
            (subChunkPos.x + 1) * CHUNK_SIZE_X,
            (subChunkPos.y + 1) * SUB_CHUNK_HEIGHT,
            (subChunkPos.z + 1) * CHUNK_SIZE_Z
        );
        return isBoxVisible(min, max);
    }
};

class World {
public:
    // Chunks stored by position
    std::unordered_map<glm::ivec2, std::unique_ptr<Chunk>> chunks;

    // Chunk meshes stored by position
    std::unordered_map<glm::ivec2, std::unique_ptr<ChunkMesh>> meshes;

    // Terrain generator (for main thread fallback)
    TerrainGenerator terrainGenerator;

    // Thread pool for async chunk generation
    std::unique_ptr<ChunkThreadPool> chunkThreadPool;

    // Render distance in chunks
    int renderDistance = 8;

    // Unload distance (chunks beyond this are removed)
    int unloadDistance = 12;

    // Max chunks to generate per frame (high default for better utilization)
    int maxChunksPerFrame = 128;

    // Max meshes to build per frame (high default for better utilization)
    int maxMeshesPerFrame = 64;

    // World seed
    int seed = 12345;

    // Last known player chunk position
    glm::ivec2 lastPlayerChunk{0, 0};

    // Enable/disable multithreading
    bool useMultithreading = true;

    // Burst mode - removes per-frame throttling for faster loading
    bool burstMode = false;

    World(int worldSeed = 12345) : terrainGenerator(worldSeed), seed(worldSeed) {
        // Thread pool will be initialized later via initThreadPool()
    }

    // Initialize thread pool with specific thread counts (call after config is loaded)
    void initThreadPool(int chunkThreads = 0, int meshThreads = 0) {
        // Use defaults if not specified
        int totalCores = std::max(4, static_cast<int>(std::thread::hardware_concurrency()));
        if (chunkThreads <= 0) chunkThreads = totalCores / 2;
        if (meshThreads <= 0) meshThreads = totalCores / 2;

        int totalThreads = chunkThreads + meshThreads;
        chunkThreadPool = std::make_unique<ChunkThreadPool>(totalThreads, seed);
        std::cout << "Thread pool started with " << totalThreads << " total worker threads" << std::endl;
        std::cout << "  Chunk threads: " << chunkThreads << ", Mesh threads: " << meshThreads << std::endl;
    }

    ~World() {
        // Shutdown thread pool
        if (chunkThreadPool) {
            chunkThreadPool->shutdown();
        }
    }

    // Get or create chunk at position
    Chunk* getChunk(glm::ivec2 pos) {
        auto it = chunks.find(pos);
        if (it != chunks.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    // Get chunk at position (const)
    const Chunk* getChunk(glm::ivec2 pos) const {
        auto it = chunks.find(pos);
        if (it != chunks.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    // Create a new chunk at position
    Chunk* createChunk(glm::ivec2 pos) {
        auto chunk = std::make_unique<Chunk>(pos);
        Chunk* ptr = chunk.get();
        chunks[pos] = std::move(chunk);
        return ptr;
    }

    // Get block at world position
    BlockType getBlock(int x, int y, int z) const {
        glm::ivec2 chunkPos(
            static_cast<int>(floor(static_cast<float>(x) / CHUNK_SIZE_X)),
            static_cast<int>(floor(static_cast<float>(z) / CHUNK_SIZE_Z))
        );

        const Chunk* chunk = getChunk(chunkPos);
        if (!chunk) return BlockType::AIR;

        // Convert to local coordinates
        int localX = x - chunkPos.x * CHUNK_SIZE_X;
        int localZ = z - chunkPos.y * CHUNK_SIZE_Z;

        return chunk->getBlock(localX, y, localZ);
    }

    // Get block for water face culling - returns WATER for non-existent chunks
    // This prevents water at chunk boundaries from rendering internal faces
    BlockType getBlockForWater(int x, int y, int z) const {
        glm::ivec2 chunkPos(
            static_cast<int>(floor(static_cast<float>(x) / CHUNK_SIZE_X)),
            static_cast<int>(floor(static_cast<float>(z) / CHUNK_SIZE_Z))
        );

        const Chunk* chunk = getChunk(chunkPos);
        if (!chunk) return BlockType::WATER;  // Assume water continues into unloaded chunks

        // Convert to local coordinates
        int localX = x - chunkPos.x * CHUNK_SIZE_X;
        int localZ = z - chunkPos.y * CHUNK_SIZE_Z;

        return chunk->getBlock(localX, y, localZ);
    }

    // Get block for solid block face culling - returns STONE for non-existent chunks
    // This prevents solid blocks from rendering faces toward unloaded chunks (fixes seams visible through water)
    BlockType getBlockSafe(int x, int y, int z) const {
        glm::ivec2 chunkPos(
            static_cast<int>(floor(static_cast<float>(x) / CHUNK_SIZE_X)),
            static_cast<int>(floor(static_cast<float>(z) / CHUNK_SIZE_Z))
        );

        const Chunk* chunk = getChunk(chunkPos);
        if (!chunk) return BlockType::STONE;  // Assume solid block - don't render face

        // Convert to local coordinates
        int localX = x - chunkPos.x * CHUNK_SIZE_X;
        int localZ = z - chunkPos.y * CHUNK_SIZE_Z;

        return chunk->getBlock(localX, y, localZ);
    }

    // Set block at world position
    void setBlock(int x, int y, int z, BlockType type) {
        glm::ivec2 chunkPos(
            static_cast<int>(floor(static_cast<float>(x) / CHUNK_SIZE_X)),
            static_cast<int>(floor(static_cast<float>(z) / CHUNK_SIZE_Z))
        );

        Chunk* chunk = getChunk(chunkPos);
        if (!chunk) {
            // Generate new chunk if it doesn't exist
            chunk = createChunk(chunkPos);
            terrainGenerator.generateChunk(*chunk);
        }

        // Convert to local coordinates
        int localX = x - chunkPos.x * CHUNK_SIZE_X;
        int localZ = z - chunkPos.y * CHUNK_SIZE_Z;

        chunk->setBlock(localX, y, localZ, type);

        // Mark this chunk dirty
        chunk->isDirty = true;

        // Mark neighboring chunks as dirty if on edge
        if (localX == 0) markChunkDirty(glm::ivec2(chunkPos.x - 1, chunkPos.y));
        if (localX == CHUNK_SIZE_X - 1) markChunkDirty(glm::ivec2(chunkPos.x + 1, chunkPos.y));
        if (localZ == 0) markChunkDirty(glm::ivec2(chunkPos.x, chunkPos.y - 1));
        if (localZ == CHUNK_SIZE_Z - 1) markChunkDirty(glm::ivec2(chunkPos.x, chunkPos.y + 1));
    }

    // Mark chunk as needing mesh rebuild
    void markChunkDirty(glm::ivec2 pos) {
        Chunk* chunk = getChunk(pos);
        if (chunk) {
            chunk->isDirty = true;
        }
    }

    // Generate initial world around spawn
    void generateWorld(int radiusChunks = 4) {
        for (int cx = -radiusChunks; cx <= radiusChunks; cx++) {
            for (int cz = -radiusChunks; cz <= radiusChunks; cz++) {
                glm::ivec2 chunkPos(cx, cz);
                Chunk* chunk = createChunk(chunkPos);
                terrainGenerator.generateChunk(*chunk);
            }
        }
    }

    // Set world seed
    void setSeed(int newSeed) {
        seed = newSeed;
        terrainGenerator.setSeed(newSeed);
    }

    // Water simulation timer
    float waterUpdateTimer = 0.0f;
    float waterUpdateInterval = 0.1f;  // Update water every 100ms

    // Get water level at world position
    uint8_t getWaterLevel(int x, int y, int z) const {
        glm::ivec2 chunkPos(
            static_cast<int>(floor(static_cast<float>(x) / CHUNK_SIZE_X)),
            static_cast<int>(floor(static_cast<float>(z) / CHUNK_SIZE_Z))
        );

        const Chunk* chunk = getChunk(chunkPos);
        if (!chunk) return 0;

        int localX = x - chunkPos.x * CHUNK_SIZE_X;
        int localZ = z - chunkPos.y * CHUNK_SIZE_Z;

        return chunk->getWaterLevel(localX, y, localZ);
    }

    // Set water level at world position
    void setWaterLevel(int x, int y, int z, uint8_t level) {
        glm::ivec2 chunkPos(
            static_cast<int>(floor(static_cast<float>(x) / CHUNK_SIZE_X)),
            static_cast<int>(floor(static_cast<float>(z) / CHUNK_SIZE_Z))
        );

        Chunk* chunk = getChunk(chunkPos);
        if (!chunk) return;

        int localX = x - chunkPos.x * CHUNK_SIZE_X;
        int localZ = z - chunkPos.y * CHUNK_SIZE_Z;

        chunk->setWaterLevel(localX, y, localZ, level);
    }

    // Get light level at world position
    uint8_t getLightLevel(int x, int y, int z) const {
        glm::ivec2 chunkPos(
            static_cast<int>(floor(static_cast<float>(x) / CHUNK_SIZE_X)),
            static_cast<int>(floor(static_cast<float>(z) / CHUNK_SIZE_Z))
        );

        const Chunk* chunk = getChunk(chunkPos);
        if (!chunk) return 0;

        int localX = x - chunkPos.x * CHUNK_SIZE_X;
        int localZ = z - chunkPos.y * CHUNK_SIZE_Z;

        return chunk->getLightLevel(localX, y, localZ);
    }

    // Set light level at world position
    void setLightLevel(int x, int y, int z, uint8_t level) {
        glm::ivec2 chunkPos(
            static_cast<int>(floor(static_cast<float>(x) / CHUNK_SIZE_X)),
            static_cast<int>(floor(static_cast<float>(z) / CHUNK_SIZE_Z))
        );

        Chunk* chunk = getChunk(chunkPos);
        if (!chunk) return;

        int localX = x - chunkPos.x * CHUNK_SIZE_X;
        int localZ = z - chunkPos.y * CHUNK_SIZE_Z;

        chunk->setLightLevel(localX, y, localZ, level);
    }

    // Propagate light within a single chunk only (no cross-chunk propagation)
    // This is much faster than cross-chunk BFS
    void propagateLightInChunk(Chunk& chunk, int startX, int startY, int startZ, uint8_t lightLevel) {
        if (lightLevel == 0) return;

        // Use a simple queue for BFS light propagation
        struct LightNode {
            int x, y, z;
            uint8_t level;
        };

        std::vector<LightNode> queue;
        queue.reserve(256);  // Pre-allocate for performance
        queue.push_back({startX, startY, startZ, lightLevel});

        // Set initial light
        chunk.setLightLevel(startX, startY, startZ, lightLevel);

        size_t head = 0;
        while (head < queue.size()) {
            LightNode node = queue[head++];

            if (node.level <= 1) continue;

            uint8_t newLevel = node.level - 1;

            // Check all 6 directions
            const int dirs[6][3] = {
                {1, 0, 0}, {-1, 0, 0},
                {0, 1, 0}, {0, -1, 0},
                {0, 0, 1}, {0, 0, -1}
            };

            for (const auto& dir : dirs) {
                int nx = node.x + dir[0];
                int ny = node.y + dir[1];
                int nz = node.z + dir[2];

                // Stay within chunk bounds
                if (nx < 0 || nx >= CHUNK_SIZE_X) continue;
                if (ny < 0 || ny >= CHUNK_SIZE_Y) continue;
                if (nz < 0 || nz >= CHUNK_SIZE_Z) continue;

                BlockType neighbor = chunk.getBlock(nx, ny, nz);

                // Light passes through air and transparent blocks
                if (!isBlockSolid(neighbor) || isBlockTransparent(neighbor)) {
                    uint8_t currentLight = chunk.getLightLevel(nx, ny, nz);
                    if (newLevel > currentLight) {
                        chunk.setLightLevel(nx, ny, nz, newLevel);
                        queue.push_back({nx, ny, nz, newLevel});
                    }
                }
            }
        }
    }

    // Calculate lighting for a chunk (called after terrain generation)
    void calculateChunkLighting(Chunk& chunk) {
        // Find all emissive blocks and propagate their light within the chunk
        for (int y = 0; y < CHUNK_SIZE_Y; y++) {
            for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                for (int x = 0; x < CHUNK_SIZE_X; x++) {
                    BlockType block = chunk.getBlock(x, y, z);

                    if (isBlockEmissive(block)) {
                        // Get emission level (15 for glowstone, 14 for lava)
                        uint8_t emission = static_cast<uint8_t>(getBlockEmission(block) * 15.0f);
                        propagateLightInChunk(chunk, x, y, z, emission);
                    }
                }
            }
        }
    }

    // Update world around player - loads new chunks, unloads distant ones
    void update(const glm::vec3& playerPos, float deltaTime = 0.0f) {
        glm::ivec2 playerChunk = Chunk::worldToChunkPos(playerPos);

        // Process chunks completed by worker threads
        processCompletedChunks();

        // Queue new chunks for generation around player
        loadChunksAroundPlayer(playerChunk);

        // Unload distant chunks
        unloadDistantChunks(playerChunk);

        // Update water simulation
        waterUpdateTimer += deltaTime;
        if (waterUpdateTimer >= waterUpdateInterval) {
            updateWater(playerChunk);
            waterUpdateTimer = 0.0f;
        }

        // Update meshes
        updateMeshes(playerChunk);

        lastPlayerChunk = playerChunk;
    }

    // Simulate water flow
    void updateWater(const glm::ivec2& playerChunk) {
        // Process chunks near the player
        for (auto& [pos, chunk] : chunks) {
            // Skip chunks without water (uses cached flag for fast culling)
            if (!chunk->hasWater && !chunk->hasWaterUpdates) continue;

            int dx = abs(pos.x - playerChunk.x);
            int dz = abs(pos.y - playerChunk.y);

            // Only simulate water in nearby chunks AND visible chunks
            if (dx <= 4 && dz <= 4 && frustum.isChunkVisible(pos)) {
                updateChunkWater(*chunk);
            }
        }
    }

    // Update water in a single chunk
    void updateChunkWater(Chunk& chunk) {
        bool anyUpdates = false;

        // Process from top to bottom so water flows down first
        for (int y = CHUNK_SIZE_Y - 1; y >= 0; y--) {
            for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                for (int x = 0; x < CHUNK_SIZE_X; x++) {
                    uint8_t level = chunk.getWaterLevel(x, y, z);
                    if (level == 0) continue;

                    // Get world coordinates
                    int wx = chunk.position.x * CHUNK_SIZE_X + x;
                    int wz = chunk.position.y * CHUNK_SIZE_Z + z;

                    // Try to flow down first
                    BlockType below = getBlock(wx, y - 1, wz);
                    uint8_t belowLevel = getWaterLevel(wx, y - 1, wz);

                    if (y > 0 && !isBlockSolid(below) && below != BlockType::WATER) {
                        // Flow down - water below becomes source-like
                        setWaterLevel(wx, y - 1, wz, WATER_SOURCE);
                        anyUpdates = true;
                    } else if (y > 0 && below == BlockType::WATER && belowLevel < WATER_SOURCE) {
                        // Fill up water below
                        setWaterLevel(wx, y - 1, wz, WATER_SOURCE);
                        anyUpdates = true;
                    }

                    // Spread horizontally if we're a source or have enough level
                    if (level >= 1) {
                        uint8_t spreadLevel = (level == WATER_SOURCE) ? WATER_MAX_SPREAD : level - 1;

                        if (spreadLevel > 0) {
                            // Check if we can spread (not blocked below or on solid ground)
                            bool canSpread = (y == 0) || isBlockSolid(getBlock(wx, y - 1, wz)) ||
                                           getWaterLevel(wx, y - 1, wz) == WATER_SOURCE;

                            if (canSpread) {
                                // Try spreading in each horizontal direction
                                const int dirs[4][2] = {{1,0}, {-1,0}, {0,1}, {0,-1}};
                                for (auto& dir : dirs) {
                                    int nx = wx + dir[0];
                                    int nz = wz + dir[1];

                                    BlockType neighbor = getBlock(nx, y, nz);
                                    uint8_t neighborLevel = getWaterLevel(nx, y, nz);

                                    if (!isBlockSolid(neighbor) && neighborLevel < spreadLevel) {
                                        setWaterLevel(nx, y, nz, spreadLevel);
                                        anyUpdates = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        if (anyUpdates) {
            chunk.isDirty = true;
        }
    }

    // Process completed chunks from thread pool (call from main thread)
    void processCompletedChunks() {
        if (!chunkThreadPool) return;

        // In burst mode, process all available chunks without throttling
        // OPTIMIZATION: Reduced multiplier from 2 to 1 for smoother frame pacing
        int maxToProcess = burstMode ? 10000 : maxChunksPerFrame;
        auto completed = chunkThreadPool->getCompletedChunks(maxToProcess);

        for (auto& result : completed) {
            // Only add if not already present (could have been unloaded while generating)
            if (getChunk(result.position) == nullptr) {
                chunks[result.position] = std::move(result.chunk);
                chunks[result.position]->isDirty = true;

                // Mark neighboring chunks as dirty
                markChunkDirty(glm::ivec2(result.position.x - 1, result.position.y));
                markChunkDirty(glm::ivec2(result.position.x + 1, result.position.y));
                markChunkDirty(glm::ivec2(result.position.x, result.position.y - 1));
                markChunkDirty(glm::ivec2(result.position.x, result.position.y + 1));
            }
        }
    }

    // Load chunks around player position
    void loadChunksAroundPlayer(const glm::ivec2& playerChunk) {
        int chunksQueued = 0;

        // Generate chunks in a spiral pattern from player position
        // This prioritizes closer chunks
        // OPTIMIZATION: Reduced multiplier from 4 to 2 for smoother frame pacing
        int maxToQueue = burstMode ? 10000 : maxChunksPerFrame * 2;
        for (int ring = 0; ring <= renderDistance && chunksQueued < maxToQueue; ring++) {
            for (int dx = -ring; dx <= ring && chunksQueued < maxToQueue; dx++) {
                for (int dz = -ring; dz <= ring && chunksQueued < maxToQueue; dz++) {
                    // Only process the outer edge of this ring
                    if (abs(dx) != ring && abs(dz) != ring) continue;

                    glm::ivec2 chunkPos(playerChunk.x + dx, playerChunk.y + dz);

                    // Check if chunk already exists or is being generated
                    if (getChunk(chunkPos) == nullptr) {
                        if (useMultithreading && chunkThreadPool) {
                            // Queue for async generation
                            if (!chunkThreadPool->isGenerating(chunkPos)) {
                                chunkThreadPool->queueChunk(chunkPos);
                                chunksQueued++;
                            }
                        } else {
                            // Synchronous fallback
                            Chunk* chunk = createChunk(chunkPos);
                            terrainGenerator.generateChunk(*chunk);
                            calculateChunkLighting(*chunk);
                            chunksQueued++;

                            // Mark neighboring chunks as dirty
                            markChunkDirty(glm::ivec2(chunkPos.x - 1, chunkPos.y));
                            markChunkDirty(glm::ivec2(chunkPos.x + 1, chunkPos.y));
                            markChunkDirty(glm::ivec2(chunkPos.x, chunkPos.y - 1));
                            markChunkDirty(glm::ivec2(chunkPos.x, chunkPos.y + 1));
                        }
                    }
                }
            }
        }
    }

    // Unload chunks that are too far from the player
    void unloadDistantChunks(const glm::ivec2& playerChunk) {
        std::vector<glm::ivec2> toRemove;

        // Find chunks to unload
        for (auto& [pos, chunk] : chunks) {
            int dx = abs(pos.x - playerChunk.x);
            int dz = abs(pos.y - playerChunk.y);

            if (dx > unloadDistance || dz > unloadDistance) {
                toRemove.push_back(pos);
            }
        }

        // Remove distant chunks and their meshes
        for (const auto& pos : toRemove) {
            chunks.erase(pos);
            meshes.erase(pos);
        }
    }

    // Update chunk meshes around player - queues async mesh generation
    void updateMeshes(const glm::ivec2& playerChunk) {
        int meshesQueued = 0;

        // First, process any completed meshes from worker threads
        processCompletedMeshes();

        // Collect dirty chunks within render distance, sorted by distance
        std::vector<std::pair<int, glm::ivec2>> dirtyChunks;

        for (auto& [pos, chunk] : chunks) {
            int dx = abs(pos.x - playerChunk.x);
            int dz = abs(pos.y - playerChunk.y);

            if (dx <= renderDistance && dz <= renderDistance) {
                if (chunk->isDirty && !chunkThreadPool->isMeshGenerating(pos)) {
                    int distSq = dx * dx + dz * dz;
                    dirtyChunks.push_back({distSq, pos});
                }
            }
        }

        // Sort by distance (closest first)
        std::sort(dirtyChunks.begin(), dirtyChunks.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        // Queue meshes for async generation
        // OPTIMIZATION: Reduced multiplier from 4 to 2 for smoother frame pacing
        int maxToQueue = burstMode ? 10000 : maxMeshesPerFrame * 2;
        for (auto& [distSq, pos] : dirtyChunks) {
            if (meshesQueued >= maxToQueue) break;  // Queue more since it's async

            Chunk* chunk = getChunk(pos);
            if (!chunk) continue;

            // Only queue if ALL 4 neighboring chunks exist
            bool allNeighborsExist =
                getChunk(glm::ivec2(pos.x - 1, pos.y)) != nullptr &&
                getChunk(glm::ivec2(pos.x + 1, pos.y)) != nullptr &&
                getChunk(glm::ivec2(pos.x, pos.y - 1)) != nullptr &&
                getChunk(glm::ivec2(pos.x, pos.y + 1)) != nullptr;

            if (!allNeighborsExist) {
                continue;
            }

            // Create mesh request with block getters
            // Note: These lambdas capture 'this' - must ensure World outlives mesh generation
            ChunkThreadPool::MeshRequest request;
            request.position = pos;
            request.chunk = chunk;

            request.getWorldBlock = [this](int x, int y, int z) -> BlockType {
                return this->getBlock(x, y, z);
            };
            request.getWaterBlock = [this](int x, int y, int z) -> BlockType {
                return this->getBlockForWater(x, y, z);
            };
            request.getSafeBlock = [this](int x, int y, int z) -> BlockType {
                return this->getBlockSafe(x, y, z);
            };
            request.getLightLevel = [this](int x, int y, int z) -> uint8_t {
                return this->getLightLevel(x, y, z);
            };

            chunkThreadPool->queueMesh(std::move(request));
            chunk->isDirty = false;  // Mark as not dirty so we don't queue again
            meshesQueued++;
        }
    }

    // Process completed meshes from worker threads (upload to GPU)
    void processCompletedMeshes() {
        if (!chunkThreadPool) return;

        // In burst mode, upload all available meshes without throttling
        // OPTIMIZATION: Reduced multiplier from 2 to 1 for smoother frame pacing
        // This spreads mesh uploads over more frames, reducing CPU spikes
        int maxToProcess = burstMode ? 10000 : maxMeshesPerFrame;
        auto completedMeshes = chunkThreadPool->getCompletedMeshes(maxToProcess);

        for (auto& meshResult : completedMeshes) {
            glm::ivec2 pos = meshResult.position;

            // Skip if chunk was unloaded while mesh was generating
            Chunk* chunk = getChunk(pos);
            if (chunk == nullptr) continue;

            // Create or get mesh
            auto it = meshes.find(pos);
            if (it == meshes.end()) {
                meshes[pos] = std::make_unique<ChunkMesh>();
            }

            ChunkMesh* mesh = meshes[pos].get();
            mesh->worldOffset = meshResult.worldOffset;

            int baseX = pos.x * CHUNK_SIZE_X;
            int baseZ = pos.y * CHUNK_SIZE_Z;

            // Block getter for water face culling
            auto getWaterBlock = [this](int x, int y, int z) -> BlockType {
                return this->getBlockForWater(x, y, z);
            };

            // Upload each sub-chunk's data to GPU
            for (int subY = 0; subY < SUB_CHUNKS_PER_COLUMN; subY++) {
                auto& subData = meshResult.subChunks[subY];
                auto& subChunk = mesh->subChunks[subY];

                subChunk.subChunkY = subData.subChunkY;
                subChunk.isEmpty = subData.isEmpty;

                // Upload solid geometry for each LOD level
                for (int lod = 0; lod < LOD_LEVELS; lod++) {
                    if (!subData.lodVertices[lod].empty()) {
                        mesh->uploadToSubChunk(subY, subData.lodVertices[lod], lod);
                    }
                }

                // Generate water on main thread (needs TextureAtlas access)
                int yStart = subY * SUB_CHUNK_HEIGHT;
                int yEnd = yStart + SUB_CHUNK_HEIGHT - 1;

                std::vector<ChunkVertex> waterVertices;
                waterVertices.reserve(256);

                // Only process if sub-chunk has blocks
                if (yStart <= chunk->chunkMaxY && yEnd >= chunk->chunkMinY) {
                    int effectiveMinY = std::max(yStart, static_cast<int>(chunk->chunkMinY));
                    int effectiveMaxY = std::min(yEnd, static_cast<int>(chunk->chunkMaxY));

                    for (int y = effectiveMinY; y <= effectiveMaxY; y++) {
                        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                            for (int x = 0; x < CHUNK_SIZE_X; x++) {
                                BlockType block = chunk->getBlock(x, y, z);
                                if (block == BlockType::WATER || block == BlockType::LAVA) {
                                    int wx = baseX + x;
                                    int wz = baseZ + z;
                                    BlockTextures textures = getBlockTextures(block);
                                    glm::vec3 blockPos = mesh->worldOffset + glm::vec3(x, y, z);
                                    mesh->addWaterBlockPublic(waterVertices, *chunk, x, y, z, blockPos,
                                                             textures.faceSlots[0], getWaterBlock, wx, wz);
                                }
                            }
                        }
                    }
                }

                // Upload water and set flag
                subChunk.hasWater = !waterVertices.empty();
                if (!waterVertices.empty()) {
                    mesh->uploadWaterToSubChunk(subY, waterVertices);
                }
            }
        }
    }

    // Legacy function for compatibility
    void updateMeshes(const glm::vec3& playerPos) {
        glm::ivec2 playerChunk = Chunk::worldToChunkPos(playerPos);
        updateMeshes(playerChunk);
    }

    // Frustum for culling
    Frustum frustum;
    mutable int lastRenderedChunks = 0;  // For debug stats
    mutable int lastCulledChunks = 0;
    mutable int lastHiZCulledChunks = 0; // Chunks culled by Hi-Z occlusion
    mutable int lastRenderedSubChunks = 0;  // Sub-chunks rendered (when using sub-chunk culling)
    mutable int lastCulledSubChunks = 0;    // Sub-chunks frustum culled
    mutable int lastRenderedWaterSubChunks = 0;  // Water sub-chunks rendered
    mutable int lastCulledWaterSubChunks = 0;    // Water sub-chunks culled

    // Hi-Z occlusion visibility map (updated by main.cpp after occlusion culling)
    std::unordered_map<glm::ivec2, bool> hiZVisibility;
    std::unordered_map<glm::ivec3, bool> hiZSubChunkVisibility;  // For sub-chunk Hi-Z culling
    bool useHiZCulling = true;  // Enable/disable Hi-Z culling in render
    bool useSubChunkCulling = true;  // Enable/disable sub-chunk vertical culling

    // Update frustum from view-projection matrix (call before render)
    void updateFrustum(const glm::mat4& viewProj) {
        frustum.update(viewProj);
    }

    // Calculate LOD level based on squared distance and render distance
    // Returns 0-3 (LOD 0 = full detail, LOD 3 = lowest detail)
    // Distant Horizons-style: Full detail for most of render distance,
    // LOD only kicks in at the far edges where fog hides the transition
    int calculateLOD(float distSq) const {
        float maxDistSq = static_cast<float>(renderDistance * renderDistance);
        float ratio = distSq / maxDistSq;

        // Full detail (LOD 0) for 70% of render distance - this is the key!
        // Players rarely notice detail at the far edges due to fog
        if (ratio < 0.49f) return 0;   // 0-70% of distance (0.7^2 = 0.49)

        // LOD 1 (2x scale) for 70-85% - subtle reduction, fog is starting
        if (ratio < 0.7225f) return 1; // 70-85% (0.85^2 = 0.7225)

        // LOD 2 (4x scale) for 85-95% - more reduction, heavy fog
        if (ratio < 0.9025f) return 2; // 85-95% (0.95^2 = 0.9025)

        // LOD 3 (8x scale) for 95-100% - furthest chunks, mostly fog
        return 3;
    }

    // Shadow render distance override (temporarily changes renderDistance for shadow pass)
    int shadowRenderDistance = -1;  // -1 means use default, otherwise use this value

    // Force a specific LOD level for all rendering (used by shadow pass)
    int forcedLOD = -1;  // -1 means use calculated LOD, otherwise force this LOD

    // Render for shadow pass - uses frustum culling but with reduced render distance and fixed LOD
    void renderForShadow(const glm::vec3& centerPos, GLint chunkOffsetLoc, int maxShadowDistance) {
        // Temporarily override settings for shadow pass
        int originalRenderDistance = renderDistance;
        int originalForcedLOD = forcedLOD;

        renderDistance = std::min(maxShadowDistance, renderDistance);
        forcedLOD = 1;  // Use LOD 1 for shadows (silhouettes don't need high detail)

        // Use the normal render path with frustum culling
        render(centerPos, chunkOffsetLoc);

        // Restore original settings
        renderDistance = originalRenderDistance;
        forcedLOD = originalForcedLOD;
    }

    // Render all visible chunks (solid geometry only) with frustum culling + Hi-Z occlusion
    // Sorted front-to-back for early depth rejection
    // Uses LOD (Level of Detail) based on distance for performance
    // chunkOffsetLoc is the uniform location for the chunk offset (for packed vertices)
    void render(const glm::vec3& playerPos, GLint chunkOffsetLoc = -1) {
        // Use sub-chunk rendering if enabled
        if (useSubChunkCulling) {
            renderSubChunks(playerPos, chunkOffsetLoc);
            return;
        }

        // Legacy full-chunk rendering
        glm::ivec2 playerChunk = Chunk::worldToChunkPos(playerPos);
        lastRenderedChunks = 0;
        lastCulledChunks = 0;
        lastHiZCulledChunks = 0;

        // Collect visible chunks with their distances
        struct ChunkToDraw {
            ChunkMesh* mesh;
            float distSq;
        };
        std::vector<ChunkToDraw> visibleChunks;
        visibleChunks.reserve(meshes.size());

        for (auto& [pos, mesh] : meshes) {
            int dx = pos.x - playerChunk.x;
            int dz = pos.y - playerChunk.y;

            if (abs(dx) <= renderDistance && abs(dz) <= renderDistance) {
                // First check frustum culling
                if (!frustum.isChunkVisible(pos)) {
                    lastCulledChunks++;
                    continue;
                }

                // Then check Hi-Z occlusion (if enabled and data available)
                if (useHiZCulling && !hiZVisibility.empty()) {
                    auto it = hiZVisibility.find(pos);
                    if (it != hiZVisibility.end() && !it->second) {
                        // Chunk is occluded by Hi-Z
                        lastHiZCulledChunks++;
                        continue;
                    }
                }

                float distSq = static_cast<float>(dx * dx + dz * dz);
                visibleChunks.push_back({mesh.get(), distSq});
            }
        }

        // Sort front-to-back for early Z rejection
        std::sort(visibleChunks.begin(), visibleChunks.end(),
            [](const ChunkToDraw& a, const ChunkToDraw& b) {
                return a.distSq < b.distSq;
            });

        // Render sorted chunks with LOD based on distance
        for (const auto& chunk : visibleChunks) {
            // Set chunk offset uniform for packed vertex format
            if (chunkOffsetLoc >= 0) {
                glUniform3fv(chunkOffsetLoc, 1, glm::value_ptr(chunk.mesh->worldOffset));
            }
            // Calculate LOD level based on distance (or use forced LOD for shadow pass)
            int lodLevel = (forcedLOD >= 0) ? forcedLOD : calculateLOD(chunk.distSq);
            chunk.mesh->render(lodLevel);
            lastRenderedChunks++;
        }
    }

    // Render using sub-chunk culling (16x16x16 sections)
    // This provides better vertical culling - underground and sky sub-chunks are culled individually
    void renderSubChunks(const glm::vec3& playerPos, GLint chunkOffsetLoc = -1) {
        glm::ivec2 playerChunk = Chunk::worldToChunkPos(playerPos);
        int playerSubY = static_cast<int>(playerPos.y) / SUB_CHUNK_HEIGHT;
        lastRenderedChunks = 0;
        lastCulledChunks = 0;
        lastHiZCulledChunks = 0;
        lastRenderedSubChunks = 0;
        lastCulledSubChunks = 0;

        // Collect visible sub-chunks with their distances
        struct SubChunkToDraw {
            ChunkMesh* mesh;
            int subChunkY;
            float distSq;
        };
        std::vector<SubChunkToDraw> visibleSubChunks;
        visibleSubChunks.reserve(meshes.size() * 8);  // Estimate: avg 8 non-empty sub-chunks per column

        for (auto& [pos, mesh] : meshes) {
            int dx = pos.x - playerChunk.x;
            int dz = pos.y - playerChunk.y;

            if (abs(dx) <= renderDistance && abs(dz) <= renderDistance) {
                float baseDistSq = static_cast<float>(dx * dx + dz * dz);

                // Iterate through sub-chunks in this column
                for (int subY = 0; subY < SUB_CHUNKS_PER_COLUMN; subY++) {
                    const auto& subChunk = mesh->subChunks[subY];

                    // Skip empty sub-chunks
                    if (subChunk.isEmpty) continue;

                    // Create sub-chunk position (chunkX, subChunkY, chunkZ)
                    glm::ivec3 subPos(pos.x, subY, pos.y);

                    // Frustum culling per sub-chunk
                    if (!frustum.isSubChunkVisible(subPos)) {
                        lastCulledSubChunks++;
                        continue;
                    }

                    // Hi-Z occlusion culling per sub-chunk (if enabled)
                    if (useHiZCulling && !hiZSubChunkVisibility.empty()) {
                        auto it = hiZSubChunkVisibility.find(subPos);
                        if (it != hiZSubChunkVisibility.end() && !it->second) {
                            lastHiZCulledChunks++;
                            continue;
                        }
                    }

                    // Add Y distance component for better sorting
                    int dy = subY - playerSubY;
                    float distSq = baseDistSq + static_cast<float>(dy * dy) * 0.25f;
                    visibleSubChunks.push_back({mesh.get(), subY, distSq});
                }
            }
        }

        // Sort front-to-back for early Z rejection
        std::sort(visibleSubChunks.begin(), visibleSubChunks.end(),
            [](const SubChunkToDraw& a, const SubChunkToDraw& b) {
                return a.distSq < b.distSq;
            });

        // Render sorted sub-chunks with LOD based on distance
        ChunkMesh* lastMesh = nullptr;
        for (const auto& sub : visibleSubChunks) {
            // Only update uniform if mesh changed (batching optimization)
            if (sub.mesh != lastMesh) {
                if (chunkOffsetLoc >= 0) {
                    glUniform3fv(chunkOffsetLoc, 1, glm::value_ptr(sub.mesh->worldOffset));
                }
                lastMesh = sub.mesh;
            }

            // Calculate LOD level based on distance (or use forced LOD for shadow pass)
            int lodLevel = (forcedLOD >= 0) ? forcedLOD : calculateLOD(sub.distSq);
            sub.mesh->renderSubChunk(sub.subChunkY, lodLevel);
            lastRenderedSubChunks++;
        }

        // For backwards compatibility, count rendered "chunks" as columns with at least one sub-chunk rendered
        lastRenderedChunks = static_cast<int>(meshes.size());  // Approximate
    }

    // Render all water geometry - call this AFTER render() with depth write disabled
    // Sorted back-to-front for proper alpha blending
    void renderWater(const glm::vec3& playerPos) {
        // Use sub-chunk water rendering if enabled
        if (useSubChunkCulling) {
            renderWaterSubChunks(playerPos);
            return;
        }

        // Legacy water rendering
        glm::ivec2 playerChunk = Chunk::worldToChunkPos(playerPos);

        // Collect visible water chunks with their distances
        struct WaterToDraw {
            ChunkMesh* mesh;
            float distSq;
        };
        std::vector<WaterToDraw> waterChunks;
        waterChunks.reserve(meshes.size());

        for (auto& [pos, mesh] : meshes) {
            if (mesh->waterVertexCount == 0) continue;  // Skip chunks without water

            int dx = pos.x - playerChunk.x;
            int dz = pos.y - playerChunk.y;

            if (abs(dx) <= renderDistance && abs(dz) <= renderDistance) {
                if (frustum.isChunkVisible(pos)) {
                    float distSq = static_cast<float>(dx * dx + dz * dz);
                    waterChunks.push_back({mesh.get(), distSq});
                }
            }
        }

        // Sort back-to-front for proper alpha blending
        std::sort(waterChunks.begin(), waterChunks.end(),
            [](const WaterToDraw& a, const WaterToDraw& b) {
                return a.distSq > b.distSq;  // Back-to-front
            });

        // Disable depth writing so water doesn't occlude objects behind it
        glDepthMask(GL_FALSE);

        // Render sorted water chunks
        for (const auto& water : waterChunks) {
            water.mesh->renderWater();
        }

        // Re-enable depth writing
        glDepthMask(GL_TRUE);
    }

    // Render water using sub-chunk culling
    void renderWaterSubChunks(const glm::vec3& playerPos) {
        glm::ivec2 playerChunk = Chunk::worldToChunkPos(playerPos);
        int playerSubY = static_cast<int>(playerPos.y) / SUB_CHUNK_HEIGHT;
        lastRenderedWaterSubChunks = 0;
        lastCulledWaterSubChunks = 0;

        // Collect visible water sub-chunks with their distances
        struct WaterSubChunkToDraw {
            ChunkMesh* mesh;
            int subChunkY;
            float distSq;
        };
        std::vector<WaterSubChunkToDraw> waterSubChunks;
        waterSubChunks.reserve(meshes.size() * 4);

        for (auto& [pos, mesh] : meshes) {
            int dx = pos.x - playerChunk.x;
            int dz = pos.y - playerChunk.y;

            // Distance culling - skip chunks outside render distance
            if (abs(dx) > renderDistance || abs(dz) > renderDistance) {
                // Count water sub-chunks that would be culled by distance
                for (int subY = 0; subY < SUB_CHUNKS_PER_COLUMN; subY++) {
                    if (mesh->subChunks[subY].hasWater) lastCulledWaterSubChunks++;
                }
                continue;
            }

            float baseDistSq = static_cast<float>(dx * dx + dz * dz);

            for (int subY = 0; subY < SUB_CHUNKS_PER_COLUMN; subY++) {
                const auto& subChunk = mesh->subChunks[subY];

                // Skip sub-chunks without water
                if (!subChunk.hasWater) continue;

                glm::ivec3 subPos(pos.x, subY, pos.y);

                // Frustum culling
                if (!frustum.isSubChunkVisible(subPos)) {
                    lastCulledWaterSubChunks++;
                    continue;
                }

                int dy = subY - playerSubY;
                float distSq = baseDistSq + static_cast<float>(dy * dy) * 0.25f;
                waterSubChunks.push_back({mesh.get(), subY, distSq});
            }
        }

        // Sort back-to-front for proper alpha blending
        std::sort(waterSubChunks.begin(), waterSubChunks.end(),
            [](const WaterSubChunkToDraw& a, const WaterSubChunkToDraw& b) {
                return a.distSq > b.distSq;  // Back-to-front
            });

        glDepthMask(GL_FALSE);

        for (const auto& water : waterSubChunks) {
            water.mesh->renderSubChunkWater(water.subChunkY);
            lastRenderedWaterSubChunks++;
        }

        glDepthMask(GL_TRUE);
    }

    // Get rendered chunk count for stats
    int getRenderedChunkCount() const { return lastRenderedChunks; }
    int getCulledChunkCount() const { return lastCulledChunks; }
    int getRenderedSubChunkCount() const { return lastRenderedSubChunks; }
    int getCulledSubChunkCount() const { return lastCulledSubChunks; }
    int getHiZCulledCount() const { return lastHiZCulledChunks; }

    // Get total vertex count for stats (solid LOD 0 + water)
    int getTotalVertexCount() const {
        int total = 0;
        for (const auto& [pos, mesh] : meshes) {
            total += mesh->getVertexCount(0) + mesh->waterVertexCount;
        }
        return total;
    }

    // Get number of loaded chunks
    size_t getChunkCount() const {
        return chunks.size();
    }

    // Get number of loaded meshes
    size_t getMeshCount() const {
        return meshes.size();
    }
};
