#pragma once

#include "Chunk.h"
#include "TerrainGenerator.h"
#include "Block.h"
#include "../render/ChunkMesh.h"
#include "../render/BinaryGreedyMesher.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <functional>
#include <vector>
#include <unordered_set>
#include <iostream>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

// Forward declarations
class World;

// Helper to convert BlockFace enum to faceSlots index
// faceSlots order: front(0), back(1), left(2), right(3), top(4), bottom(5)
inline int getFaceSlotIndex(BlockFace face) {
    switch (face) {
        case BlockFace::FRONT:  return 0;
        case BlockFace::BACK:   return 1;
        case BlockFace::LEFT:   return 2;
        case BlockFace::RIGHT:  return 3;
        case BlockFace::TOP:    return 4;
        case BlockFace::BOTTOM: return 5;
        default: return 0;
    }
}

// Thread-safe chunk generation pool with async mesh support
class ChunkThreadPool {
public:
    // Result of chunk generation
    struct ChunkResult {
        glm::ivec2 position;
        std::unique_ptr<Chunk> chunk;
    };

    // Result of mesh generation (vertex data ready for GPU upload)
    struct MeshResult {
        glm::ivec2 position;
        glm::vec3 worldOffset;

        // Per sub-chunk mesh data with face-orientation buckets for backface culling
        struct SubChunkMeshData {
            // Face buckets for LOD 0: 6 separate vertex arrays by face direction
            // This enables 35% better backface culling by skipping entire face directions
            std::array<std::vector<PackedChunkVertex>, FACE_BUCKET_COUNT> faceBucketVertices;
            
            // Combined vertices for LOD levels 1+ (face culling not used for distant LODs)
            std::array<std::vector<PackedChunkVertex>, LOD_LEVELS> lodVertices;
            
            std::vector<ChunkVertex> waterVertices;
            int subChunkY = 0;
            bool isEmpty = true;
            bool hasWater = false;
            
            // Get total LOD0 vertex count across all face buckets
            size_t getLOD0VertexCount() const {
                size_t total = 0;
                for (const auto& bucket : faceBucketVertices) {
                    total += bucket.size();
                }
                return total;
            }
        };
        std::array<SubChunkMeshData, SUB_CHUNKS_PER_COLUMN> subChunks;
    };

    // Request for mesh generation
    struct MeshRequest {
        glm::ivec2 position;
        Chunk* chunk;  // Pointer to existing chunk data
        // Block getters for neighbor access
        std::function<BlockType(int, int, int)> getWorldBlock;
        std::function<BlockType(int, int, int)> getWaterBlock;
        std::function<BlockType(int, int, int)> getSafeBlock;
        std::function<uint8_t(int, int, int)> getLightLevel;
    };

private:
    // Worker threads
    std::vector<std::thread> workers;

    // Each worker has its own terrain generator (thread-safe noise)
    std::vector<std::unique_ptr<TerrainGenerator>> generators;

    // Pending chunk positions to generate
    std::queue<glm::ivec2> pendingQueue;
    std::mutex pendingMutex;
    std::condition_variable pendingCondition;

    // Completed chunks ready for main thread
    std::queue<ChunkResult> completedQueue;
    std::mutex completedMutex;

    // Track which positions are being processed
    std::unordered_set<glm::ivec2> inProgress;
    std::mutex inProgressMutex;

    // Mesh generation queues
    std::queue<MeshRequest> meshPendingQueue;
    std::mutex meshPendingMutex;
    std::condition_variable meshPendingCondition;

    std::queue<MeshResult> meshCompletedQueue;
    std::mutex meshCompletedMutex;

    std::unordered_set<glm::ivec2> meshInProgress;
    std::mutex meshInProgressMutex;

    // Control
    std::atomic<bool> running{true};
    std::atomic<bool> fastLoadMode{true};  // Skip extra LOD levels during initial load
    int worldSeed;
    int numWorkerThreads = 0;

public:
    ChunkThreadPool(int numThreads, int seed) : worldSeed(seed), numWorkerThreads(numThreads) {
        // Create worker threads for chunk generation
        int chunkThreads = numThreads / 2;  // Half for chunk gen
        int meshThreads = numThreads - chunkThreads;  // Half for mesh gen

        if (chunkThreads < 1) chunkThreads = 1;
        if (meshThreads < 1) meshThreads = 1;

        // Chunk generation threads
        for (int i = 0; i < chunkThreads; i++) {
            generators.push_back(std::make_unique<TerrainGenerator>(seed));
            workers.emplace_back([this, i]() {
                chunkWorkerLoop(i);
            });
        }

        // Mesh generation threads
        for (int i = 0; i < meshThreads; i++) {
            workers.emplace_back([this]() {
                meshWorkerLoop();
            });
        }

        std::cout << "Thread pool: " << chunkThreads << " chunk threads, "
                  << meshThreads << " mesh threads" << std::endl;
    }

    int getThreadCount() const { return numWorkerThreads; }

    // Disable fast load mode (enables full LOD generation)
    void setFastLoadMode(bool enabled) { fastLoadMode = enabled; }
    bool isFastLoadMode() const { return fastLoadMode; }

    ~ChunkThreadPool() {
        shutdown();
    }

    void shutdown() {
        running = false;
        pendingCondition.notify_all();
        meshPendingCondition.notify_all();

        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers.clear();
        generators.clear();
    }

    // Queue a chunk position for generation (thread-safe)
    void queueChunk(glm::ivec2 pos) {
        // Check if already in progress or queued
        {
            std::lock_guard<std::mutex> lock(inProgressMutex);
            if (inProgress.count(pos) > 0) {
                return;  // Already being generated
            }
            inProgress.insert(pos);
        }

        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            pendingQueue.push(pos);
        }
        pendingCondition.notify_one();
    }

    // Check if a position is being generated
    bool isGenerating(glm::ivec2 pos) {
        std::lock_guard<std::mutex> lock(inProgressMutex);
        return inProgress.count(pos) > 0;
    }

    // Get completed chunks (call from main thread)
    std::vector<ChunkResult> getCompletedChunks(int maxCount = 10) {
        std::vector<ChunkResult> results;

        std::lock_guard<std::mutex> lock(completedMutex);
        while (!completedQueue.empty() && results.size() < static_cast<size_t>(maxCount)) {
            results.push_back(std::move(completedQueue.front()));
            completedQueue.pop();
        }

        return results;
    }

    // Get number of pending chunks
    size_t getPendingCount() {
        std::lock_guard<std::mutex> lock(pendingMutex);
        return pendingQueue.size();
    }

    // Get number of completed chunks waiting
    size_t getCompletedCount() {
        std::lock_guard<std::mutex> lock(completedMutex);
        return completedQueue.size();
    }

    // ========== MESH GENERATION METHODS ==========

    // Queue a mesh generation request (thread-safe)
    void queueMesh(MeshRequest request) {
        // Check if already in progress
        {
            std::lock_guard<std::mutex> lock(meshInProgressMutex);
            if (meshInProgress.count(request.position) > 0) {
                return;  // Already being generated
            }
            meshInProgress.insert(request.position);
        }

        {
            std::lock_guard<std::mutex> lock(meshPendingMutex);
            meshPendingQueue.push(std::move(request));
        }
        meshPendingCondition.notify_one();
    }

    // Check if mesh is being generated
    bool isMeshGenerating(glm::ivec2 pos) {
        std::lock_guard<std::mutex> lock(meshInProgressMutex);
        return meshInProgress.count(pos) > 0;
    }

    // Get completed meshes (call from main thread)
    std::vector<MeshResult> getCompletedMeshes(int maxCount = 32) {
        std::vector<MeshResult> results;

        std::lock_guard<std::mutex> lock(meshCompletedMutex);
        while (!meshCompletedQueue.empty() && results.size() < static_cast<size_t>(maxCount)) {
            results.push_back(std::move(meshCompletedQueue.front()));
            meshCompletedQueue.pop();
        }

        return results;
    }

    // Get number of pending mesh requests
    size_t getMeshPendingCount() {
        std::lock_guard<std::mutex> lock(meshPendingMutex);
        return meshPendingQueue.size();
    }

    // Get number of completed meshes waiting
    size_t getMeshCompletedCount() {
        std::lock_guard<std::mutex> lock(meshCompletedMutex);
        return meshCompletedQueue.size();
    }

    // Check if any meshes are pending or in progress
    bool hasPendingMeshes() {
        {
            std::lock_guard<std::mutex> lock(meshPendingMutex);
            if (!meshPendingQueue.empty()) return true;
        }
        {
            std::lock_guard<std::mutex> lock(meshInProgressMutex);
            if (!meshInProgress.empty()) return true;
        }
        return false;
    }

private:
    void chunkWorkerLoop(int threadIndex) {
        TerrainGenerator* generator = generators[threadIndex].get();

        while (running) {
            glm::ivec2 pos;

            // Wait for work
            {
                std::unique_lock<std::mutex> lock(pendingMutex);
                pendingCondition.wait(lock, [this]() {
                    return !pendingQueue.empty() || !running;
                });

                if (!running && pendingQueue.empty()) {
                    break;
                }

                if (pendingQueue.empty()) {
                    continue;
                }

                pos = pendingQueue.front();
                pendingQueue.pop();
            }

            // Generate chunk
            auto chunk = std::make_unique<Chunk>(pos);
            generator->generateChunk(*chunk);

            // Calculate heightmaps for optimization (skip empty Y regions)
            chunk->recalculateHeightmaps();

            // Calculate lighting (chunk-local)
            calculateChunkLighting(*chunk, *generator);

            // Add to completed queue
            {
                std::lock_guard<std::mutex> lock(completedMutex);
                completedQueue.push({pos, std::move(chunk)});
            }

            // Remove from in-progress set
            {
                std::lock_guard<std::mutex> lock(inProgressMutex);
                inProgress.erase(pos);
            }
        }
    }

    // Calculate lighting within a chunk (same logic as World but standalone)
    void calculateChunkLighting(Chunk& chunk, TerrainGenerator& /*gen*/) {
        for (int y = 0; y < CHUNK_SIZE_Y; y++) {
            for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                for (int x = 0; x < CHUNK_SIZE_X; x++) {
                    BlockType block = chunk.getBlock(x, y, z);

                    if (isBlockEmissive(block)) {
                        uint8_t emission = static_cast<uint8_t>(getBlockEmission(block) * 15.0f);
                        propagateLightInChunk(chunk, x, y, z, emission);
                    }
                }
            }
        }
    }

    void propagateLightInChunk(Chunk& chunk, int startX, int startY, int startZ, uint8_t lightLevel) {
        if (lightLevel == 0) return;

        struct LightNode {
            int x, y, z;
            uint8_t level;
        };

        std::vector<LightNode> queue;
        queue.reserve(256);
        queue.push_back({startX, startY, startZ, lightLevel});

        chunk.setLightLevel(startX, startY, startZ, lightLevel);

        size_t head = 0;
        while (head < queue.size()) {
            LightNode node = queue[head++];

            if (node.level <= 1) continue;

            uint8_t newLevel = node.level - 1;

            const int dirs[6][3] = {
                {1, 0, 0}, {-1, 0, 0},
                {0, 1, 0}, {0, -1, 0},
                {0, 0, 1}, {0, 0, -1}
            };

            for (const auto& dir : dirs) {
                int nx = node.x + dir[0];
                int ny = node.y + dir[1];
                int nz = node.z + dir[2];

                if (nx < 0 || nx >= CHUNK_SIZE_X) continue;
                if (ny < 0 || ny >= CHUNK_SIZE_Y) continue;
                if (nz < 0 || nz >= CHUNK_SIZE_Z) continue;

                BlockType neighbor = chunk.getBlock(nx, ny, nz);

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

    // Mesh generation worker loop
    void meshWorkerLoop() {
        while (running) {
            MeshRequest request;

            // Wait for work
            {
                std::unique_lock<std::mutex> lock(meshPendingMutex);
                meshPendingCondition.wait(lock, [this]() {
                    return !meshPendingQueue.empty() || !running;
                });

                if (!running && meshPendingQueue.empty()) {
                    break;
                }

                if (meshPendingQueue.empty()) {
                    continue;
                }

                request = std::move(meshPendingQueue.front());
                meshPendingQueue.pop();
            }

            // Generate mesh vertex data
            MeshResult result;
            result.position = request.position;
            result.worldOffset = glm::vec3(
                request.position.x * CHUNK_SIZE_X,
                0.0f,
                request.position.y * CHUNK_SIZE_Z
            );

            // Generate sub-chunk meshes
            generateMeshData(result, *request.chunk, request.getWorldBlock,
                           request.getWaterBlock, request.getSafeBlock, request.getLightLevel);

            // Add to completed queue
            {
                std::lock_guard<std::mutex> lock(meshCompletedMutex);
                meshCompletedQueue.push(std::move(result));
            }

            // Remove from in-progress set
            {
                std::lock_guard<std::mutex> lock(meshInProgressMutex);
                meshInProgress.erase(request.position);
            }
        }
    }

    // Generate mesh vertex data for all sub-chunks (CPU-only, no GPU upload)
    void generateMeshData(MeshResult& result, const Chunk& chunk,
                         const std::function<BlockType(int, int, int)>& /*getWorldBlock*/,
                         const std::function<BlockType(int, int, int)>& /*getWaterBlock*/,
                         const std::function<BlockType(int, int, int)>& getSafeBlock,
                         const std::function<uint8_t(int, int, int)>& getLightLevel) {

        int baseX = chunk.position.x * CHUNK_SIZE_X;
        int baseZ = chunk.position.y * CHUNK_SIZE_Z;

        // Process each sub-chunk (16 blocks high)
        for (int subY = 0; subY < SUB_CHUNKS_PER_COLUMN; subY++) {
            auto& subData = result.subChunks[subY];
            subData.subChunkY = subY;

            int yStart = subY * SUB_CHUNK_HEIGHT;
            int yEnd = yStart + SUB_CHUNK_HEIGHT - 1;

            // Check if this sub-chunk has any blocks using heightmap
            int effectiveMinY = std::max(yStart, static_cast<int>(chunk.chunkMinY));
            int effectiveMaxY = std::min(yEnd, static_cast<int>(chunk.chunkMaxY));

            if (effectiveMinY > effectiveMaxY) {
                subData.isEmpty = true;
                subData.hasWater = false;
                continue;
            }

            // Generate LOD 0 (full detail) using BINARY GREEDY MESHING (10-50x faster!)
            // Now with FACE-ORIENTATION BUCKETS for 35% better backface culling
            std::vector<ChunkVertex> waterVertices;

            // Use binary greedy mesher for solid geometry
            {
                thread_local BinaryGreedyMesher binaryMesher;
                thread_local BinaryMeshResult binaryResult;

                // Texture getter for binary mesher
                auto getTexture = [](BlockType block, BGMFace face) -> int {
                    BlockTextures textures = getBlockTextures(block);
                    switch (face) {
                        case BGMFace::POS_Z: return textures.faceSlots[0];  // Front
                        case BGMFace::NEG_Z: return textures.faceSlots[1];  // Back
                        case BGMFace::NEG_X: return textures.faceSlots[2];  // Left
                        case BGMFace::POS_X: return textures.faceSlots[3];  // Right
                        case BGMFace::POS_Y: return textures.faceSlots[4];  // Top
                        case BGMFace::NEG_Y: return textures.faceSlots[5];  // Bottom
                        default: return textures.faceSlots[0];
                    }
                };

                binaryMesher.generateMeshForYRange(chunk, getSafeBlock, getTexture, binaryResult,
                                                   baseX, baseZ, yStart, yEnd);

                // Expand to 6 face-orientation buckets for efficient backface culling
                expandFaceBucketsToVertices(binaryResult, subData.faceBucketVertices);
            }

            // Generate water/lava geometry on worker thread (not greedy meshed)
            generateWaterForRange(waterVertices, chunk, baseX, baseZ, getSafeBlock, yStart, yEnd);

            subData.isEmpty = (subData.getLOD0VertexCount() == 0);
            subData.hasWater = !waterVertices.empty();

            if (!waterVertices.empty()) {
                subData.waterVertices = std::move(waterVertices);
            }

            // Generate lower LOD levels for sub-chunk (skip in fast load mode)
            if (!fastLoadMode) {
                for (int lodLevel = 1; lodLevel < LOD_LEVELS; lodLevel++) {
                    generateLODForRange(subData.lodVertices[lodLevel], chunk, baseX, baseZ,
                                       lodLevel, yStart, yEnd);
                }
            }
        }
    }

    // Greedy face generation for a Y range
    void generateGreedyFacesForRange(std::vector<PackedChunkVertex>& vertices,
                                     const Chunk& chunk, int baseX, int baseZ,
                                     const std::function<BlockType(int, int, int)>& getSafeBlock,
                                     const std::function<uint8_t(int, int, int)>& getLightLevel,
                                     BlockFace face, int yStart, int yEnd) {

        // Direction info for each face
        int normalIndex = static_cast<int>(face); (void)normalIndex;
        int dx = 0, dy = 0, dz = 0;

        switch (face) {
            case BlockFace::TOP:    dy = 1; break;
            case BlockFace::BOTTOM: dy = -1; break;
            case BlockFace::FRONT:  dz = 1; break;
            case BlockFace::BACK:   dz = -1; break;
            case BlockFace::RIGHT:  dx = 1; break;
            case BlockFace::LEFT:   dx = -1; break;
        }

        // Iterate through blocks in Y range
        for (int y = yStart; y <= yEnd; y++) {
            for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                for (int x = 0; x < CHUNK_SIZE_X; x++) {
                    BlockType block = chunk.getBlock(x, y, z);

                    // Skip if not a solid, visible block
                    if (block == BlockType::AIR || block == BlockType::WATER ||
                        block == BlockType::LAVA) continue;

                    // Check if face is visible
                    int wx = baseX + x;
                    int wz = baseZ + z;
                    int nx = wx + dx;
                    int ny = y + dy;
                    int nz = wz + dz;

                    // Check bounds - same as original shouldRenderFace
                    if (ny < 0) continue;  // Don't render faces below world
                    bool faceVisible;
                    if (ny >= CHUNK_SIZE_Y) {
                        faceVisible = true;  // Always render faces above world limit
                    } else {
                        BlockType neighbor = getSafeBlock(nx, ny, nz);
                        faceVisible = isBlockTransparent(neighbor);
                    }
                    if (!faceVisible) continue;

                    // Get texture and light
                    // faceSlots order: front(0), back(1), left(2), right(3), top(4), bottom(5)
                    int faceIndex = getFaceSlotIndex(face);
                    int textureSlot = getBlockTextures(block).faceSlots[faceIndex];
                    uint8_t light = getLightLevel(wx, y, wz);
                    uint8_t ao = 255;  // No AO calculation for async (simplified)

                    // Add single-block quad (no greedy merging for async - keep it simple)
                    addPackedQuad(vertices, x, y, z, face, textureSlot, light, ao);
                }
            }
        }
    }

    // Add a packed quad for a single face (matches ChunkMesh.h addGreedyQuad)
    void addPackedQuad(std::vector<PackedChunkVertex>& vertices,
                       int x, int y, int z, BlockFace face,
                       int textureSlot, uint8_t light, uint8_t ao) {

        // Normal index and corner positions - must match ChunkMesh.h exactly
        uint8_t normalIndex;
        std::array<std::array<int16_t, 3>, 4> corners;  // [corner][x,y,z]
        std::array<std::array<uint16_t, 2>, 4> uvs;     // [corner][u,v]

        // For single block: width=1, height=1 in greedy terms
        switch (face) {
            case BlockFace::TOP: { // +Y
                normalIndex = 2;
                corners = {{
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>((z + 1) * 256)},
                    {static_cast<int16_t>((x + 1) * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>((z + 1) * 256)},
                    {static_cast<int16_t>((x + 1) * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>(z * 256)}
                }};
                uvs = {{
                    {0, 256},
                    {256, 256},
                    {256, 0},
                    {0, 0}
                }};
                break;
            }
            case BlockFace::BOTTOM: { // -Y
                normalIndex = 3;
                corners = {{
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>((x + 1) * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>((x + 1) * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>((z + 1) * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>((z + 1) * 256)}
                }};
                uvs = {{
                    {0, 0},
                    {256, 0},
                    {256, 256},
                    {0, 256}
                }};
                break;
            }
            case BlockFace::FRONT: { // +Z
                normalIndex = 4;
                corners = {{
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>((z + 1) * 256)},
                    {static_cast<int16_t>((x + 1) * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>((z + 1) * 256)},
                    {static_cast<int16_t>((x + 1) * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>((z + 1) * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>((z + 1) * 256)}
                }};
                uvs = {{
                    {0, 256},
                    {256, 256},
                    {256, 0},
                    {0, 0}
                }};
                break;
            }
            case BlockFace::BACK: { // -Z
                normalIndex = 5;
                corners = {{
                    {static_cast<int16_t>((x + 1) * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>((x + 1) * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>(z * 256)}
                }};
                uvs = {{
                    {0, 256},
                    {256, 256},
                    {256, 0},
                    {0, 0}
                }};
                break;
            }
            case BlockFace::LEFT: { // -X
                normalIndex = 1;
                corners = {{
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>((z + 1) * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>((z + 1) * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>(z * 256)}
                }};
                uvs = {{
                    {0, 256},
                    {256, 256},
                    {256, 0},
                    {0, 0}
                }};
                break;
            }
            case BlockFace::RIGHT: { // +X
                normalIndex = 0;
                corners = {{
                    {static_cast<int16_t>((x + 1) * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>((z + 1) * 256)},
                    {static_cast<int16_t>((x + 1) * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>((x + 1) * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>((x + 1) * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>((z + 1) * 256)}
                }};
                uvs = {{
                    {0, 256},
                    {256, 256},
                    {256, 0},
                    {0, 0}
                }};
                break;
            }
        }

        // Create vertices for two triangles (0,1,2) and (2,3,0)
        auto makeVertex = [&](int cornerIdx) -> PackedChunkVertex {
            return PackedChunkVertex{
                corners[cornerIdx][0],
                corners[cornerIdx][1],
                corners[cornerIdx][2],
                uvs[cornerIdx][0],
                uvs[cornerIdx][1],
                normalIndex,
                ao,
                light,
                static_cast<uint8_t>(textureSlot),
                0  // padding
            };
        };

        // Triangle 1: 0, 1, 2
        vertices.push_back(makeVertex(0));
        vertices.push_back(makeVertex(1));
        vertices.push_back(makeVertex(2));
        // Triangle 2: 2, 3, 0
        vertices.push_back(makeVertex(2));
        vertices.push_back(makeVertex(3));
        vertices.push_back(makeVertex(0));
    }

    // Note: Water generation is handled synchronously on main thread
    // due to complexity of water levels, texture atlas lookups, etc.

    // Generate LOD mesh for a Y range
    void generateLODForRange(std::vector<PackedChunkVertex>& vertices,
                            const Chunk& chunk, int baseX, int baseZ,
                            int lodLevel, int yStart, int yEnd) {

        if (lodLevel <= 0 || lodLevel >= LOD_LEVELS) return;

        int scale = LOD_SCALES[lodLevel];
        vertices.reserve((yEnd - yStart + 1) * CHUNK_SIZE_X * CHUNK_SIZE_Z / (scale * scale * 2));

        // Sample blocks at LOD scale
        for (int y = yStart; y <= yEnd; y += scale) {
            for (int z = 0; z < CHUNK_SIZE_Z; z += scale) {
                for (int x = 0; x < CHUNK_SIZE_X; x += scale) {
                    BlockType block = chunk.getBlock(x, y, z);

                    if (block == BlockType::AIR || block == BlockType::WATER ||
                        block == BlockType::LAVA) continue;

                    // Add scaled block faces - use top texture for all faces in LOD
                    int textureSlot = getBlockTextures(block).faceSlots[4];  // TOP = 4

                    // Check each face
                    for (int face = 0; face < 6; face++) {
                        int dx = 0, dy = 0, dz = 0;
                        switch (face) {
                            case 0: dy = scale; break;
                            case 1: dy = -1; break;
                            case 2: dz = scale; break;
                            case 3: dz = -1; break;
                            case 4: dx = scale; break;
                            case 5: dx = -1; break;
                        }

                        int nx = x + dx;
                        int ny = y + dy;
                        int nz = z + dz;

                        // Check bounds
                        if (ny < 0 || ny >= CHUNK_SIZE_Y) {
                            addPackedQuadScaled(vertices, x, y, z, scale,
                                               static_cast<BlockFace>(face), textureSlot);
                            continue;
                        }

                        if (nx < 0 || nx >= CHUNK_SIZE_X || nz < 0 || nz >= CHUNK_SIZE_Z) {
                            addPackedQuadScaled(vertices, x, y, z, scale,
                                               static_cast<BlockFace>(face), textureSlot);
                            continue;
                        }

                        BlockType neighbor = chunk.getBlock(
                            std::min(nx, CHUNK_SIZE_X - 1),
                            ny,
                            std::min(nz, CHUNK_SIZE_Z - 1));

                        if (!isBlockSolid(neighbor)) {
                            addPackedQuadScaled(vertices, x, y, z, scale,
                                               static_cast<BlockFace>(face), textureSlot);
                        }
                    }
                }
            }
        }
    }

    // Add a scaled packed quad for LOD (matches ChunkMesh.h addGreedyQuad with scale)
    void addPackedQuadScaled(std::vector<PackedChunkVertex>& vertices,
                             int x, int y, int z, int scale, BlockFace face, int textureSlot) {

        uint8_t normalIndex;
        std::array<std::array<int16_t, 3>, 4> corners;
        std::array<std::array<uint16_t, 2>, 4> uvs;
        int s = scale;

        switch (face) {
            case BlockFace::TOP: { // +Y
                normalIndex = 2;
                corners = {{
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>((y + s) * 256), static_cast<int16_t>((z + s) * 256)},
                    {static_cast<int16_t>((x + s) * 256), static_cast<int16_t>((y + s) * 256), static_cast<int16_t>((z + s) * 256)},
                    {static_cast<int16_t>((x + s) * 256), static_cast<int16_t>((y + s) * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>((y + s) * 256), static_cast<int16_t>(z * 256)}
                }};
                uvs = {{
                    {0, static_cast<uint16_t>(s * 256)},
                    {static_cast<uint16_t>(s * 256), static_cast<uint16_t>(s * 256)},
                    {static_cast<uint16_t>(s * 256), 0},
                    {0, 0}
                }};
                break;
            }
            case BlockFace::BOTTOM: { // -Y
                normalIndex = 3;
                corners = {{
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>((x + s) * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>((x + s) * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>((z + s) * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>((z + s) * 256)}
                }};
                uvs = {{
                    {0, 0},
                    {static_cast<uint16_t>(s * 256), 0},
                    {static_cast<uint16_t>(s * 256), static_cast<uint16_t>(s * 256)},
                    {0, static_cast<uint16_t>(s * 256)}
                }};
                break;
            }
            case BlockFace::FRONT: { // +Z
                normalIndex = 4;
                corners = {{
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>((z + s) * 256)},
                    {static_cast<int16_t>((x + s) * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>((z + s) * 256)},
                    {static_cast<int16_t>((x + s) * 256), static_cast<int16_t>((y + s) * 256), static_cast<int16_t>((z + s) * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>((y + s) * 256), static_cast<int16_t>((z + s) * 256)}
                }};
                uvs = {{
                    {0, static_cast<uint16_t>(s * 256)},
                    {static_cast<uint16_t>(s * 256), static_cast<uint16_t>(s * 256)},
                    {static_cast<uint16_t>(s * 256), 0},
                    {0, 0}
                }};
                break;
            }
            case BlockFace::BACK: { // -Z
                normalIndex = 5;
                corners = {{
                    {static_cast<int16_t>((x + s) * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>((y + s) * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>((x + s) * 256), static_cast<int16_t>((y + s) * 256), static_cast<int16_t>(z * 256)}
                }};
                uvs = {{
                    {0, static_cast<uint16_t>(s * 256)},
                    {static_cast<uint16_t>(s * 256), static_cast<uint16_t>(s * 256)},
                    {static_cast<uint16_t>(s * 256), 0},
                    {0, 0}
                }};
                break;
            }
            case BlockFace::LEFT: { // -X
                normalIndex = 1;
                corners = {{
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>((z + s) * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>((y + s) * 256), static_cast<int16_t>((z + s) * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>((y + s) * 256), static_cast<int16_t>(z * 256)}
                }};
                uvs = {{
                    {0, static_cast<uint16_t>(s * 256)},
                    {static_cast<uint16_t>(s * 256), static_cast<uint16_t>(s * 256)},
                    {static_cast<uint16_t>(s * 256), 0},
                    {0, 0}
                }};
                break;
            }
            case BlockFace::RIGHT: { // +X
                normalIndex = 0;
                corners = {{
                    {static_cast<int16_t>((x + s) * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>((z + s) * 256)},
                    {static_cast<int16_t>((x + s) * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>((x + s) * 256), static_cast<int16_t>((y + s) * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>((x + s) * 256), static_cast<int16_t>((y + s) * 256), static_cast<int16_t>((z + s) * 256)}
                }};
                uvs = {{
                    {0, static_cast<uint16_t>(s * 256)},
                    {static_cast<uint16_t>(s * 256), static_cast<uint16_t>(s * 256)},
                    {static_cast<uint16_t>(s * 256), 0},
                    {0, 0}
                }};
                break;
            }
        }

        auto makeVertex = [&](int cornerIdx) -> PackedChunkVertex {
            return PackedChunkVertex{
                corners[cornerIdx][0],
                corners[cornerIdx][1],
                corners[cornerIdx][2],
                uvs[cornerIdx][0],
                uvs[cornerIdx][1],
                normalIndex,
                255,  // ao
                15,   // light
                static_cast<uint8_t>(textureSlot),
                0     // padding
            };
        };

        vertices.push_back(makeVertex(0));
        vertices.push_back(makeVertex(1));
        vertices.push_back(makeVertex(2));
        vertices.push_back(makeVertex(2));
        vertices.push_back(makeVertex(3));
        vertices.push_back(makeVertex(0));
    }

    // ================================================================
    // WATER GENERATION - Moved to worker threads for better performance
    // ================================================================

    // Generate water vertices for a Y range
    void generateWaterForRange(std::vector<ChunkVertex>& vertices,
                               const Chunk& chunk, int baseX, int baseZ,
                               const std::function<BlockType(int, int, int)>& getBlock,
                               int yStart, int yEnd) {

        // Get water texture slot
        int waterTextureSlot = getBlockTextures(BlockType::WATER).faceSlots[0];
        glm::vec4 uv = TextureAtlas::getUV(waterTextureSlot);
        glm::vec2 texSlotBase(uv.x, uv.y);

        float ao = 1.0f;
        float light = 0.0f;

        // Clamp to chunk bounds
        int effectiveMinY = std::max(yStart, static_cast<int>(chunk.chunkMinY));
        int effectiveMaxY = std::min(yEnd, static_cast<int>(chunk.chunkMaxY));

        for (int y = effectiveMinY; y <= effectiveMaxY; y++) {
            for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                for (int x = 0; x < CHUNK_SIZE_X; x++) {
                    BlockType block = chunk.getBlock(x, y, z);
                    if (block != BlockType::WATER && block != BlockType::LAVA) continue;

                    // Use lava texture if lava
                    if (block == BlockType::LAVA) {
                        int lavaSlot = getBlockTextures(BlockType::LAVA).faceSlots[0];
                        glm::vec4 lavaUv = TextureAtlas::getUV(lavaSlot);
                        texSlotBase = glm::vec2(lavaUv.x, lavaUv.y);
                    }

                    int wx = baseX + x;
                    int wz = baseZ + z;
                    glm::vec3 pos(x, y, z);

                    // Check if water above (submerged)
                    bool waterAbove = (y + 1 < CHUNK_SIZE_Y) &&
                                     (chunk.getBlock(x, y + 1, z) == BlockType::WATER ||
                                      chunk.getBlock(x, y + 1, z) == BlockType::LAVA);

                    // Lambda to check if side should be rendered
                    auto shouldRenderSide = [&](int nx, int nz) -> bool {
                        BlockType neighbor = getBlock(nx, y, nz);
                        return neighbor != BlockType::WATER && neighbor != BlockType::LAVA;
                    };

                    glm::vec3 normal;

                    if (waterAbove) {
                        // Submerged - only render sides, full height
                        float topY = 1.0f;

                        // Front (+Z)
                        if (shouldRenderSide(wx, wz + 1)) {
                            normal = glm::vec3(0, 0, 1);
                            addWaterQuad(vertices, pos, normal, ao, light, texSlotBase,
                                        0,0,1, 1,0,1, 1,topY,1, 0,topY,1);
                        }
                        // Back (-Z)
                        if (shouldRenderSide(wx, wz - 1)) {
                            normal = glm::vec3(0, 0, -1);
                            addWaterQuad(vertices, pos, normal, ao, light, texSlotBase,
                                        1,0,0, 0,0,0, 0,topY,0, 1,topY,0);
                        }
                        // Left (-X)
                        if (shouldRenderSide(wx - 1, wz)) {
                            normal = glm::vec3(-1, 0, 0);
                            addWaterQuad(vertices, pos, normal, ao, light, texSlotBase,
                                        0,0,0, 0,0,1, 0,topY,1, 0,topY,0);
                        }
                        // Right (+X)
                        if (shouldRenderSide(wx + 1, wz)) {
                            normal = glm::vec3(1, 0, 0);
                            addWaterQuad(vertices, pos, normal, ao, light, texSlotBase,
                                        1,0,1, 1,0,0, 1,topY,0, 1,topY,1);
                        }
                    } else {
                        // Surface water - render top with slight lowering + sides
                        float h = 0.875f;  // Slightly below full block

                        // Top face
                        normal = glm::vec3(0, 1, 0);
                        addWaterQuad(vertices, pos, normal, ao, light, texSlotBase,
                                    0,h,1, 1,h,1, 1,h,0, 0,h,0);

                        // Side faces
                        if (shouldRenderSide(wx, wz + 1)) {
                            normal = glm::vec3(0, 0, 1);
                            addWaterQuad(vertices, pos, normal, ao, light, texSlotBase,
                                        0,0,1, 1,0,1, 1,h,1, 0,h,1);
                        }
                        if (shouldRenderSide(wx, wz - 1)) {
                            normal = glm::vec3(0, 0, -1);
                            addWaterQuad(vertices, pos, normal, ao, light, texSlotBase,
                                        1,0,0, 0,0,0, 0,h,0, 1,h,0);
                        }
                        if (shouldRenderSide(wx - 1, wz)) {
                            normal = glm::vec3(-1, 0, 0);
                            addWaterQuad(vertices, pos, normal, ao, light, texSlotBase,
                                        0,0,0, 0,0,1, 0,h,1, 0,h,0);
                        }
                        if (shouldRenderSide(wx + 1, wz)) {
                            normal = glm::vec3(1, 0, 0);
                            addWaterQuad(vertices, pos, normal, ao, light, texSlotBase,
                                        1,0,1, 1,0,0, 1,h,0, 1,h,1);
                        }

                        // Bottom face (if no solid block below)
                        BlockType below = (y > 0) ? chunk.getBlock(x, y - 1, z) : BlockType::STONE;
                        if (!isBlockSolid(below) && below != BlockType::WATER && below != BlockType::LAVA) {
                            normal = glm::vec3(0, -1, 0);
                            addWaterQuad(vertices, pos, normal, ao, light, texSlotBase,
                                        0,0,0, 1,0,0, 1,0,1, 0,0,1);
                        }
                    }
                }
            }
        }
    }

    // Helper to add a water quad with 4 corner positions (x,y,z offsets from pos)
    void addWaterQuad(std::vector<ChunkVertex>& vertices,
                      const glm::vec3& pos, const glm::vec3& normal,
                      float ao, float light, const glm::vec2& texSlotBase,
                      float x0, float y0, float z0,
                      float x1, float y1, float z1,
                      float x2, float y2, float z2,
                      float x3, float y3, float z3) {
        glm::vec3 corners[4] = {
            pos + glm::vec3(x0, y0, z0),
            pos + glm::vec3(x1, y1, z1),
            pos + glm::vec3(x2, y2, z2),
            pos + glm::vec3(x3, y3, z3)
        };

        // UVs
        glm::vec2 uvs[4] = {
            glm::vec2(0.0f, 1.0f), glm::vec2(1.0f, 1.0f),
            glm::vec2(1.0f, 0.0f), glm::vec2(0.0f, 0.0f)
        };

        // Two triangles
        vertices.push_back({ corners[0], uvs[0], normal, ao, light, texSlotBase });
        vertices.push_back({ corners[1], uvs[1], normal, ao, light, texSlotBase });
        vertices.push_back({ corners[2], uvs[2], normal, ao, light, texSlotBase });
        vertices.push_back({ corners[2], uvs[2], normal, ao, light, texSlotBase });
        vertices.push_back({ corners[3], uvs[3], normal, ao, light, texSlotBase });
        vertices.push_back({ corners[0], uvs[0], normal, ao, light, texSlotBase });
    }
};
