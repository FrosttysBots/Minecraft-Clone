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
#include <chrono>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

// External mesh shader globals (defined in main.cpp)
extern bool g_meshShadersAvailable;
extern bool g_enableMeshShaders;
extern GLuint meshShaderProgram;
extern GLuint meshShaderDataUBO;
extern GLuint frustumPlanesUBO;

// GL_NV_mesh_shader constants
#ifndef GL_TASK_SHADER_NV
#define GL_TASK_SHADER_NV 0x955A
#endif
#ifndef GL_MESH_SHADER_NV
#define GL_MESH_SHADER_NV 0x9559
#endif

// Function pointer for glDrawMeshTasksNV (loaded in main.cpp)
#ifndef GLAD_GL_NV_mesh_shader
typedef void (APIENTRY* PFNGLDRAWMESHTASKSNVPROC_LOCAL)(GLuint first, GLuint count);
extern PFNGLDRAWMESHTASKSNVPROC_LOCAL pfn_glDrawMeshTasksNV;
#define glDrawMeshTasksNV_ptr pfn_glDrawMeshTasksNV
#else
#define glDrawMeshTasksNV_ptr glad_glDrawMeshTasksNV
#endif

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
    // Uses sphere pre-test for fast early rejection
    // subChunkPos.x = chunk X, subChunkPos.y = sub-chunk Y index (0-15), subChunkPos.z = chunk Z
    bool isSubChunkVisible(const glm::ivec3& subChunkPos) const {
        // Sphere pre-test for fast rejection (cheaper than full AABB)
        glm::vec3 center(
            (subChunkPos.x + 0.5f) * CHUNK_SIZE_X,
            (subChunkPos.y + 0.5f) * SUB_CHUNK_HEIGHT,
            (subChunkPos.z + 0.5f) * CHUNK_SIZE_Z
        );
        constexpr float SUBCHUNK_SPHERE_RADIUS = 13.86f;  // sqrt(8^2 + 8^2 + 8^2)
        
        int sphereResult = testSphere(center, SUBCHUNK_SPHERE_RADIUS);
        if (sphereResult == -1) return false;  // Definitely outside
        if (sphereResult == 1) return true;    // Definitely inside
        
        // Sphere intersects - do precise AABB test
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

    // Fast sphere visibility test - use as pre-filter before AABB test
    // Returns: -1 = definitely outside, 0 = intersecting, 1 = definitely inside
    int testSphere(const glm::vec3& center, float radius) const {
        int result = 1;  // Assume inside
        for (const auto& plane : planes) {
            float distance = glm::dot(glm::vec3(plane), center) + plane.w;
            if (distance < -radius) {
                return -1;  // Completely outside this plane
            }
            if (distance < radius) {
                result = 0;  // Intersecting (might be partially visible)
            }
        }
        return result;
    }

    // Optimized sub-chunk visibility with sphere pre-test
    // Uses bounding sphere first (cheaper) then AABB if needed
    bool isSubChunkVisibleFast(const glm::ivec3& subChunkPos) const {
        // Calculate sub-chunk center and bounding sphere radius
        glm::vec3 center(
            (subChunkPos.x + 0.5f) * CHUNK_SIZE_X,
            (subChunkPos.y + 0.5f) * SUB_CHUNK_HEIGHT,
            (subChunkPos.z + 0.5f) * CHUNK_SIZE_Z
        );
        // Radius of bounding sphere for 16x16x16 cube = sqrt(8^2 + 8^2 + 8^2) ≈ 13.86
        constexpr float SUBCHUNK_SPHERE_RADIUS = 13.86f;

        int sphereResult = testSphere(center, SUBCHUNK_SPHERE_RADIUS);
        if (sphereResult == -1) return false;  // Definitely outside
        if (sphereResult == 1) return true;    // Definitely inside

        // Sphere intersects frustum - do precise AABB test
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

    // ================================================================
    // SODIUM-STYLE INDIRECT RENDERING
    // ================================================================
    // OpenGL indirect draw command (matches glDrawArraysIndirectCommand)
    struct DrawArraysIndirectCommand {
        GLuint count;        // Number of vertices
        GLuint instanceCount; // Number of instances (always 1)
        GLuint first;        // Offset in vertex buffer
        GLuint baseInstance; // Base instance (for per-draw data)
    };

    // Per-draw data sent via SSBO (chunk offset, LOD, etc.)
    struct alignas(16) DrawCallData {
        glm::vec3 chunkOffset;
        float padding;
    };

    // Indirect rendering resources
    GLuint indirectCommandBuffer = 0;    // Buffer of DrawArraysIndirectCommand
    GLuint drawDataSSBO = 0;             // Per-draw chunk offsets
    GLuint batchedVAO = 0;               // VAO for batched rendering
    GLuint batchedVBO = 0;               // Combined vertex buffer for batching
    bool indirectRenderingEnabled = true; // Toggle for indirect vs traditional
    size_t maxDrawCommands = 8192;       // Max sub-chunks to batch

    // Terrain generator (for main thread fallback)
    TerrainGenerator terrainGenerator;

    // Thread pool for async chunk generation
    std::unique_ptr<ChunkThreadPool> chunkThreadPool;

    // Render distance in chunks
    int renderDistance = 8;

    // Unload distance (chunks beyond this are removed)
    int unloadDistance = 12;

    // Max chunks to generate per frame (reasonable default)
    int maxChunksPerFrame = 8;

    // Max meshes to build per frame (reasonable default)
    int maxMeshesPerFrame = 8;

    // OPTIMIZATION: Frame time budget system
    // Limits chunk/mesh processing based on available frame time
    float frameTimeBudgetMs = 4.0f;  // Max ms to spend on chunk loading per frame
    float lastChunkProcessTimeMs = 0.0f;  // Track actual time spent
    float lastMeshProcessTimeMs = 0.0f;
    bool useFrameTimeBudget = true;  // Enable adaptive throttling

    // World seed
    int seed = 12345;

    // Last known player chunk position
    glm::ivec2 lastPlayerChunk{0, 0};

    // Predictive chunk streaming
    glm::vec3 lastPlayerPos{0.0f, 0.0f, 0.0f};
    glm::vec3 playerVelocity{0.0f, 0.0f, 0.0f};
    float velocitySmoothing = 0.85f;  // EMA smoothing for velocity (0.85 = responsive but stable)
    float predictionTime = 3.0f;      // How far ahead to predict (seconds)
    bool usePredictiveLoading = true; // Enable/disable predictive chunk streaming

    // Enable/disable multithreading
    bool useMultithreading = true;

    // Burst mode - removes per-frame throttling for faster loading
    bool burstMode = false;

    // Auto burst mode during initial load
    bool initialLoadComplete = false;
    int targetChunkCount = 0;  // Expected chunks based on render distance
    bool meshletRegenerationNeeded = false;  // Flag to regenerate meshlets after burst mode
    int meshletRegenIndex = 0;  // Current mesh index for lazy regeneration

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

    // Initialize indirect rendering buffers
    void initIndirectRendering() {
        // Create indirect command buffer
        glGenBuffers(1, &indirectCommandBuffer);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectCommandBuffer);
        glBufferData(GL_DRAW_INDIRECT_BUFFER,
                     maxDrawCommands * sizeof(DrawArraysIndirectCommand),
                     nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

        // Create per-draw data SSBO
        glGenBuffers(1, &drawDataSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, drawDataSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     maxDrawCommands * sizeof(DrawCallData),
                     nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        std::cout << "Indirect rendering initialized (max " << maxDrawCommands << " draw commands)" << std::endl;
    }

    ~World() {
        // Shutdown thread pool
        if (chunkThreadPool) {
            chunkThreadPool->shutdown();
        }
        // Cleanup indirect rendering resources
        if (indirectCommandBuffer != 0) glDeleteBuffers(1, &indirectCommandBuffer);
        if (drawDataSSBO != 0) glDeleteBuffers(1, &drawDataSSBO);
        if (batchedVAO != 0) glDeleteVertexArrays(1, &batchedVAO);
        if (batchedVBO != 0) glDeleteBuffers(1, &batchedVBO);
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

        // Mark this chunk dirty and modified (needs saving)
        chunk->isDirty = true;
        chunk->isModified = true;

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

    // Reset world for new generation (clears all chunks and meshes)
    void reset() {
        // Stop any pending chunk generation
        if (chunkThreadPool) {
            chunkThreadPool->clearPendingChunks();
        }

        // Clear all meshes first (they reference chunks)
        for (auto& [pos, mesh] : meshes) {
            if (mesh) {
                mesh->destroy();
            }
        }
        meshes.clear();

        // Clear all chunks
        chunks.clear();

        // Reset stats
        lastRenderedChunks = 0;
        lastCulledChunks = 0;
        lastHiZCulledChunks = 0;
        lastRenderedSubChunks = 0;
        lastCulledSubChunks = 0;
        lastRenderedWaterSubChunks = 0;
        lastCulledWaterSubChunks = 0;
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

        // Update player velocity for predictive chunk streaming
        if (usePredictiveLoading && deltaTime > 0.0f) {
            glm::vec3 instantVelocity = (playerPos - lastPlayerPos) / deltaTime;
            // Exponential moving average for smooth velocity
            playerVelocity = glm::mix(instantVelocity, playerVelocity, velocitySmoothing);
            lastPlayerPos = playerPos;
        }

        // Auto burst mode during initial load for faster startup
        if (!initialLoadComplete) {
            // Calculate expected chunk count
            if (targetChunkCount == 0) {
                int diameter = renderDistance * 2 + 1;
                targetChunkCount = diameter * diameter;
            }

            // Enable burst mode until we have most chunks loaded
            int loadedChunks = static_cast<int>(chunks.size());
            int loadedMeshes = static_cast<int>(meshes.size());

            if (loadedChunks >= targetChunkCount && loadedMeshes >= targetChunkCount * 0.8f) {
                initialLoadComplete = true;
                burstMode = false;
                if (chunkThreadPool) {
                    chunkThreadPool->setFastLoadMode(false);  // Enable full LOD generation
                }
                meshletRegenerationNeeded = g_generateMeshlets;  // Queue meshlet regeneration
                meshletRegenIndex = 0;
                std::cout << "Initial load complete! " << loadedChunks << " chunks, "
                          << loadedMeshes << " meshes" << std::endl;
            } else {
                burstMode = true;  // Keep burst mode on during initial load
            }
        }

        // Process chunks completed by worker threads
        processCompletedChunks();

        // Queue new chunks for generation around player
        loadChunksAroundPlayer(playerChunk);

        // Unload distant chunks
        unloadDistantChunks(playerChunk);

        // Update water simulation (skip during burst mode for faster loading)
        if (!burstMode) {
            waterUpdateTimer += deltaTime;
            if (waterUpdateTimer >= waterUpdateInterval) {
                updateWater(playerChunk);
                waterUpdateTimer = 0.0f;
            }
        }

        // Update meshes
        updateMeshes(playerChunk);

        // Lazy meshlet regeneration after burst mode (a few per frame)
        if (meshletRegenerationNeeded && g_generateMeshlets) {
            regenerateMeshletsLazy(8);  // Regenerate 8 sub-chunks per frame
        }

        lastPlayerChunk = playerChunk;
    }

    // Lazily regenerate meshlets for meshes that were created during burst mode
    void regenerateMeshletsLazy(int maxPerFrame) {
        if (!meshletRegenerationNeeded) return;

        int processed = 0;
        auto it = meshes.begin();

        // Skip to current index
        for (int i = 0; i < meshletRegenIndex && it != meshes.end(); ++i, ++it);

        // Process a few meshes per frame
        while (it != meshes.end() && processed < maxPerFrame) {
            ChunkMesh* mesh = it->second.get();

            // Find sub-chunks that need meshlet generation from cached data
            for (int subY = 0; subY < SUB_CHUNKS_PER_COLUMN && processed < maxPerFrame; ++subY) {
                auto& subChunk = mesh->subChunks[subY];

                // Check if this sub-chunk needs deferred meshlet generation
                if (subChunk.needsMeshletGeneration && !subChunk.cachedVerticesForMeshlets.empty()) {
                    // Generate meshlets from cached vertex data
                    mesh->generateMeshlets(subY, subChunk.cachedVerticesForMeshlets);

                    // Clear cached data to free memory
                    subChunk.cachedVerticesForMeshlets.clear();
                    subChunk.cachedVerticesForMeshlets.shrink_to_fit();
                    subChunk.needsMeshletGeneration = false;
                    processed++;
                }
            }

            ++it;
            ++meshletRegenIndex;
        }

        // Check if done
        if (it == meshes.end()) {
            meshletRegenerationNeeded = false;
            std::cout << "Meshlet regeneration complete!" << std::endl;
        }
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

        auto startTime = std::chrono::high_resolution_clock::now();

        // OPTIMIZATION: Dynamic throttling based on frame time budget
        // During burst mode, limit to prevent frame spikes
        // Otherwise, process up to maxChunksPerFrame or until budget exhausted
        int maxToProcess = burstMode ? 32 : maxChunksPerFrame;  // Reduced burst from 10000 to 32
        int processed = 0;

        auto completed = chunkThreadPool->getCompletedChunks(maxToProcess);

        for (auto& result : completed) {
            // Check frame time budget (skip in burst mode for faster initial load)
            if (!burstMode && useFrameTimeBudget && processed > 0) {
                auto now = std::chrono::high_resolution_clock::now();
                float elapsedMs = std::chrono::duration<float, std::milli>(now - startTime).count();
                if (elapsedMs > frameTimeBudgetMs * 0.5f) {
                    break;  // Leave remaining chunks for next frame
                }
            }

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
            processed++;
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        lastChunkProcessTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    }

    // Load chunks around player position
    void loadChunksAroundPlayer(const glm::ivec2& playerChunk) {
        int chunksQueued = 0;
        // OPTIMIZATION: Limit queue size to prevent overwhelming the thread pool
        int maxToQueue = burstMode ? 64 : maxChunksPerFrame * 2;  // Reduced burst from 10000

        // Predictive chunk streaming: prioritize chunks in movement direction
        if (usePredictiveLoading && !burstMode && glm::length(playerVelocity) > 0.5f) {
            // Predict future position and convert to chunk coords
            glm::vec3 predictedPos = lastPlayerPos + playerVelocity * predictionTime;
            glm::ivec2 predictedChunk = Chunk::worldToChunkPos(predictedPos);

            // Collect chunks that need loading with priority scores
            struct ChunkToLoad {
                glm::ivec2 pos;
                float priority;  // Lower = higher priority
            };
            std::vector<ChunkToLoad> chunksToLoad;
            chunksToLoad.reserve((renderDistance * 2 + 1) * (renderDistance * 2 + 1));

            for (int dx = -renderDistance; dx <= renderDistance; dx++) {
                for (int dz = -renderDistance; dz <= renderDistance; dz++) {
                    glm::ivec2 chunkPos(playerChunk.x + dx, playerChunk.y + dz);

                    if (getChunk(chunkPos) == nullptr &&
                        (!chunkThreadPool || !chunkThreadPool->isGenerating(chunkPos))) {
                        // Score: weighted combination of current and predicted distance
                        float currentDistSq = static_cast<float>(dx * dx + dz * dz);
                        int pdx = chunkPos.x - predictedChunk.x;
                        int pdz = chunkPos.y - predictedChunk.y;
                        float predictedDistSq = static_cast<float>(pdx * pdx + pdz * pdz);

                        // Priority: 40% current distance, 60% predicted distance
                        float priority = currentDistSq * 0.4f + predictedDistSq * 0.6f;
                        chunksToLoad.push_back({chunkPos, priority});
                    }
                }
            }

            // Sort by priority (ascending)
            std::sort(chunksToLoad.begin(), chunksToLoad.end(),
                [](const ChunkToLoad& a, const ChunkToLoad& b) {
                    return a.priority < b.priority;
                });

            // Queue highest priority chunks
            for (const auto& c : chunksToLoad) {
                if (chunksQueued >= maxToQueue) break;
                if (useMultithreading && chunkThreadPool) {
                    chunkThreadPool->queueChunk(c.pos);
                } else {
                    Chunk* chunk = createChunk(c.pos);
                    terrainGenerator.generateChunk(*chunk);
                    calculateChunkLighting(*chunk);
                    markChunkDirty(glm::ivec2(c.pos.x - 1, c.pos.y));
                    markChunkDirty(glm::ivec2(c.pos.x + 1, c.pos.y));
                    markChunkDirty(glm::ivec2(c.pos.x, c.pos.y - 1));
                    markChunkDirty(glm::ivec2(c.pos.x, c.pos.y + 1));
                }
                chunksQueued++;
            }
        } else {
            // Fallback: spiral pattern from player position (for standing still or initial load)
            for (int ring = 0; ring <= renderDistance && chunksQueued < maxToQueue; ring++) {
                for (int dx = -ring; dx <= ring && chunksQueued < maxToQueue; dx++) {
                    for (int dz = -ring; dz <= ring && chunksQueued < maxToQueue; dz++) {
                        if (abs(dx) != ring && abs(dz) != ring) continue;

                        glm::ivec2 chunkPos(playerChunk.x + dx, playerChunk.y + dz);

                        if (getChunk(chunkPos) == nullptr) {
                            if (useMultithreading && chunkThreadPool) {
                                if (!chunkThreadPool->isGenerating(chunkPos)) {
                                    chunkThreadPool->queueChunk(chunkPos);
                                    chunksQueued++;
                                }
                            } else {
                                Chunk* chunk = createChunk(chunkPos);
                                terrainGenerator.generateChunk(*chunk);
                                calculateChunkLighting(*chunk);
                                chunksQueued++;
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
        // OPTIMIZATION: Limit queue size to prevent frame spikes
        int maxToQueue = burstMode ? 64 : maxMeshesPerFrame * 2;  // Reduced burst from 10000
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

        auto startTime = std::chrono::high_resolution_clock::now();

        // OPTIMIZATION: Limit mesh uploads to prevent GPU stalls
        // GPU uploads are slow and can cause frame spikes
        int maxToProcess = burstMode ? 16 : maxMeshesPerFrame;  // Reduced burst from 10000
        auto completedMeshes = chunkThreadPool->getCompletedMeshes(maxToProcess);
        int processed = 0;

        for (auto& meshResult : completedMeshes) {
            // OPTIMIZATION: Check frame time budget to prevent stalls
            if (!burstMode && useFrameTimeBudget && processed > 0) {
                auto now = std::chrono::high_resolution_clock::now();
                float elapsedMs = std::chrono::duration<float, std::milli>(now - startTime).count();
                if (elapsedMs > frameTimeBudgetMs * 0.5f) {
                    break;  // Leave remaining meshes for next frame
                }
            }

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

            // Upload each sub-chunk's data to GPU
            for (int subY = 0; subY < SUB_CHUNKS_PER_COLUMN; subY++) {
                auto& subData = meshResult.subChunks[subY];
                auto& subChunk = mesh->subChunks[subY];

                subChunk.subChunkY = subData.subChunkY;
                subChunk.isEmpty = subData.isEmpty;

                // Upload LOD 0 using face buckets for 35% better backface culling
                bool hasLOD0Data = subData.getLOD0VertexCount() > 0;
                if (hasLOD0Data) {
                    mesh->uploadFaceBucketsToSubChunk(subY, subData.faceBucketVertices);
                }

                // Upload solid geometry for LOD 1+ (no face buckets for distant geometry)
                for (int lod = 1; lod < LOD_LEVELS; lod++) {
                    if (!subData.lodVertices[lod].empty()) {
                        mesh->uploadToSubChunk(subY, subData.lodVertices[lod], lod);
                    }
                }

                // Generate meshlets for mesh shader rendering (if enabled)
                // Must be done on main thread (OpenGL calls)
                // Use combined vertices from face buckets for meshlet generation
                if (g_generateMeshlets && hasLOD0Data) {
                    // Combine face buckets into a single vertex array for meshlet generation
                    std::vector<PackedChunkVertex> combinedVertices;
                    combinedVertices.reserve(subData.getLOD0VertexCount());
                    for (const auto& bucket : subData.faceBucketVertices) {
                        combinedVertices.insert(combinedVertices.end(), bucket.begin(), bucket.end());
                    }

                    if (burstMode) {
                        // During burst mode, cache vertices for later meshlet generation
                        subChunk.cachedVerticesForMeshlets = std::move(combinedVertices);
                        subChunk.needsMeshletGeneration = true;
                    } else {
                        // Normal mode: generate meshlets immediately
                        mesh->generateMeshlets(subY, combinedVertices);
                    }
                }

                // Upload pre-generated water vertices (generated on worker thread)
                subChunk.hasWater = subData.hasWater;
                if (!subData.waterVertices.empty()) {
                    mesh->uploadWaterToSubChunk(subY, subData.waterVertices);
                }
            }
            processed++;
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        lastMeshProcessTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
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
    // Tuned for performance: More aggressive LOD transitions where fog hides detail
    int calculateLOD(float distSq) const {
        float maxDistSq = static_cast<float>(renderDistance * renderDistance);
        float ratio = distSq / maxDistSq;

        // Full detail (LOD 0) for 60% of render distance
        // This is where players spend most of their time looking
        if (ratio < 0.36f) return 0;   // 0-60% of distance (0.6^2 = 0.36)

        // LOD 1 (2x scale) for 60-75% - subtle reduction, fog starting
        if (ratio < 0.5625f) return 1; // 60-75% (0.75^2 = 0.5625)

        // LOD 2 (4x scale) for 75-87% - medium fog, detail hard to see
        if (ratio < 0.7569f) return 2; // 75-87% (0.87^2 = 0.7569)

        // LOD 3 (8x scale) for 87-100% - heavy fog, silhouettes only
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

    // ================================================================
    // MESH SHADER RENDERING PATH (GL_NV_mesh_shader)
    // ================================================================
    // Renders sub-chunks using mesh shaders with per-meshlet frustum culling
    void renderSubChunksMeshShader(const glm::vec3& playerPos, const glm::mat4& viewProj) {
        if (!g_meshShadersAvailable || !g_enableMeshShaders || meshShaderProgram == 0) {
            return;
        }

        glm::ivec2 playerChunk = Chunk::worldToChunkPos(playerPos);
        lastRenderedSubChunks = 0;

        // Use mesh shader program
        glUseProgram(meshShaderProgram);

        // Update frustum planes UBO for per-meshlet culling
        glBindBuffer(GL_UNIFORM_BUFFER, frustumPlanesUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, 6 * sizeof(glm::vec4), frustum.planes.data());
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        // Bind texture atlas
        glActiveTexture(GL_TEXTURE0);
        // Note: Texture should already be bound by caller

        // Collect visible sub-chunks
        struct SubChunkToDraw {
            ChunkMesh* mesh;
            int subChunkY;
            float distSq;
        };
        std::vector<SubChunkToDraw> visibleSubChunks;
        visibleSubChunks.reserve(meshes.size() * 8);

        for (auto& [pos, mesh] : meshes) {
            int dx = pos.x - playerChunk.x;
            int dz = pos.y - playerChunk.y;

            if (abs(dx) <= renderDistance && abs(dz) <= renderDistance) {
                float baseDistSq = static_cast<float>(dx * dx + dz * dz);

                for (int subY = 0; subY < SUB_CHUNKS_PER_COLUMN; subY++) {
                    const auto& subChunk = mesh->subChunks[subY];

                    // Skip empty sub-chunks or those without meshlet data
                    if (subChunk.isEmpty) continue;
                    if (!subChunk.meshletData.hasMeshlets()) continue;

                    // Frustum culling at sub-chunk level
                    glm::ivec3 subPos(pos.x, subY, pos.y);
                    if (!frustum.isSubChunkVisible(subPos)) continue;

                    visibleSubChunks.push_back({mesh.get(), subY, baseDistSq});
                }
            }
        }

        // Sort front-to-back
        std::sort(visibleSubChunks.begin(), visibleSubChunks.end(),
            [](const SubChunkToDraw& a, const SubChunkToDraw& b) {
                return a.distSq < b.distSq;
            });

        // Render each sub-chunk using mesh shaders
        for (const auto& sub : visibleSubChunks) {
            const auto& subChunk = sub.mesh->subChunks[sub.subChunkY];
            const auto& meshletData = subChunk.meshletData;

            if (meshletData.meshlets.empty()) continue;

            // Update mesh shader data UBO
            struct MeshShaderData {
                glm::mat4 viewProj;
                glm::vec3 chunkOffset;
                uint32_t meshletCount;
            } uboData;

            uboData.viewProj = viewProj;
            uboData.chunkOffset = sub.mesh->worldOffset;
            uboData.meshletCount = static_cast<uint32_t>(meshletData.meshlets.size());

            glBindBuffer(GL_UNIFORM_BUFFER, meshShaderDataUBO);
            glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(MeshShaderData), &uboData);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);

            // Bind vertex SSBO (binding = 0)
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, subChunk.vertexSSBO);

            // Bind meshlet descriptors SSBO (binding = 2)
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, meshletData.meshletSSBO);

            // Dispatch mesh shader tasks
            // Each task shader workgroup handles 32 meshlets
            uint32_t taskCount = (uboData.meshletCount + 31) / 32;
            glDrawMeshTasksNV_ptr(0, taskCount);

            lastRenderedSubChunks++;
        }
    }

    // ================================================================
    // SODIUM-STYLE BATCHED RENDERING (Multi-draw indirect)
    // ================================================================
    // Batches sub-chunks by chunk column to reduce uniform updates
    // Uses glMultiDrawArraysIndirect for better driver efficiency
    void renderSubChunksBatched(const glm::vec3& playerPos, GLint chunkOffsetLoc) {
        if (indirectCommandBuffer == 0) return;

        glm::ivec2 playerChunk = Chunk::worldToChunkPos(playerPos);
        lastRenderedSubChunks = 0;

        // Group sub-chunks by chunk column for batching
        struct ChunkColumn {
            ChunkMesh* mesh;
            glm::ivec2 pos;
            float distSq;
            std::vector<std::pair<int, int>> subChunks; // (subChunkY, lodLevel)
        };
        std::vector<ChunkColumn> columns;
        columns.reserve(meshes.size());

        // Collect visible chunk columns
        for (auto& [pos, mesh] : meshes) {
            int dx = pos.x - playerChunk.x;
            int dz = pos.y - playerChunk.y;

            if (abs(dx) <= renderDistance && abs(dz) <= renderDistance) {
                float distSq = static_cast<float>(dx * dx + dz * dz);

                ChunkColumn col;
                col.mesh = mesh.get();
                col.pos = pos;
                col.distSq = distSq;

                // Collect visible sub-chunks in this column
                for (int subY = 0; subY < SUB_CHUNKS_PER_COLUMN; subY++) {
                    const auto& subChunk = mesh->subChunks[subY];
                    if (subChunk.isEmpty) continue;

                    glm::ivec3 subPos(pos.x, subY, pos.y);
                    if (!frustum.isSubChunkVisible(subPos)) continue;

                    // Hi-Z culling
                    if (useHiZCulling && !hiZSubChunkVisibility.empty()) {
                        auto it = hiZSubChunkVisibility.find(subPos);
                        if (it != hiZSubChunkVisibility.end() && !it->second) continue;
                    }

                    int lodLevel = (forcedLOD >= 0) ? forcedLOD : calculateLOD(distSq);
                    col.subChunks.push_back({subY, lodLevel});
                }

                if (!col.subChunks.empty()) {
                    columns.push_back(std::move(col));
                }
            }
        }

        // Sort columns front-to-back
        std::sort(columns.begin(), columns.end(),
            [](const ChunkColumn& a, const ChunkColumn& b) {
                return a.distSq < b.distSq;
            });

        // Render each column - all sub-chunks share the same chunk offset
        // This reduces uniform updates from O(subchunks) to O(columns)
        for (const auto& col : columns) {
            // Update chunk offset once per column
            if (chunkOffsetLoc >= 0) {
                glUniform3fv(chunkOffsetLoc, 1, glm::value_ptr(col.mesh->worldOffset));
            }

            // Render all sub-chunks in this column
            for (const auto& [subY, lodLevel] : col.subChunks) {
                col.mesh->renderSubChunk(subY, lodLevel);
                lastRenderedSubChunks++;
            }
        }

        lastRenderedChunks = static_cast<int>(columns.size());
    }

    // Render all water geometry - call this AFTER render() with depth write disabled
    // Sorted back-to-front for proper alpha blending
    void renderWater(const glm::vec3& playerPos, GLint chunkOffsetLoc = -1) {
        // Use sub-chunk water rendering if enabled
        if (useSubChunkCulling) {
            renderWaterSubChunks(playerPos, chunkOffsetLoc);
            return;
        }

        // Legacy water rendering
        glm::ivec2 playerChunk = Chunk::worldToChunkPos(playerPos);

        // Collect visible water chunks with their distances and positions
        struct WaterToDraw {
            ChunkMesh* mesh;
            glm::ivec2 chunkPos;
            float distSq;
        };
        std::vector<WaterToDraw> waterChunks;
        waterChunks.reserve(meshes.size());

        for (auto& [pos, mesh] : meshes) {
            if (mesh->waterVertexCount == 0) continue;  // Skip chunks without water

            int dx = pos.x - playerChunk.x;
            int dz = pos.y - playerChunk.y;

            if (abs(dx) <= renderDistance && abs(dz) <= renderDistance) {
                // Frustum culling
                if (!frustum.isChunkVisible(pos)) continue;

                // Hi-Z occlusion culling
                if (useHiZCulling && !hiZVisibility.empty()) {
                    auto it = hiZVisibility.find(pos);
                    if (it != hiZVisibility.end() && !it->second) continue;
                }

                float distSq = static_cast<float>(dx * dx + dz * dz);
                waterChunks.push_back({mesh.get(), pos, distSq});
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
            // Set chunk offset uniform for proper world positioning
            if (chunkOffsetLoc >= 0) {
                glm::vec3 offset(water.chunkPos.x * CHUNK_SIZE_X, 0.0f, water.chunkPos.y * CHUNK_SIZE_Z);
                glUniform3fv(chunkOffsetLoc, 1, glm::value_ptr(offset));
            }
            water.mesh->renderWater();
        }

        // Re-enable depth writing
        glDepthMask(GL_TRUE);
    }

    // Render water using sub-chunk culling
    void renderWaterSubChunks(const glm::vec3& playerPos, GLint chunkOffsetLoc = -1) {
        glm::ivec2 playerChunk = Chunk::worldToChunkPos(playerPos);
        int playerSubY = static_cast<int>(playerPos.y) / SUB_CHUNK_HEIGHT;
        lastRenderedWaterSubChunks = 0;
        lastCulledWaterSubChunks = 0;

        // Collect visible water sub-chunks with their distances
        struct WaterSubChunkToDraw {
            ChunkMesh* mesh;
            glm::ivec2 chunkPos;  // Need chunk position for offset uniform
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

                // Hi-Z occlusion culling for water (same as solid geometry)
                if (useHiZCulling && !hiZSubChunkVisibility.empty()) {
                    auto it = hiZSubChunkVisibility.find(subPos);
                    if (it != hiZSubChunkVisibility.end() && !it->second) {
                        lastCulledWaterSubChunks++;
                        continue;
                    }
                }

                int dy = subY - playerSubY;
                float distSq = baseDistSq + static_cast<float>(dy * dy) * 0.25f;
                waterSubChunks.push_back({mesh.get(), pos, subY, distSq});
            }
        }

        // Sort back-to-front for proper alpha blending
        std::sort(waterSubChunks.begin(), waterSubChunks.end(),
            [](const WaterSubChunkToDraw& a, const WaterSubChunkToDraw& b) {
                return a.distSq > b.distSq;  // Back-to-front
            });

        glDepthMask(GL_FALSE);

        glm::ivec2 lastChunkPos(-999999, -999999);  // Track to avoid redundant uniform sets
        for (const auto& water : waterSubChunks) {
            // Set chunk offset uniform for proper world positioning (only when chunk changes)
            if (chunkOffsetLoc >= 0 && water.chunkPos != lastChunkPos) {
                glm::vec3 offset(water.chunkPos.x * CHUNK_SIZE_X, 0.0f, water.chunkPos.y * CHUNK_SIZE_Z);
                glUniform3fv(chunkOffsetLoc, 1, glm::value_ptr(offset));
                lastChunkPos = water.chunkPos;
            }
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
