#pragma once

#include "../world/Chunk.h"
#include "../world/Block.h"
#include "TextureAtlas.h"
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <vector>
#include <array>
#include <functional>
#include <cstring>
#include <cfloat>
#include <cmath>

// Face mask entry for greedy meshing
struct FaceMask {
    BlockType blockType = BlockType::AIR;
    int textureSlot = 0;
    bool valid = false;  // Whether this face should be rendered
};

// Packed vertex structure for efficient memory usage (16 bytes vs 48 bytes)
// Reduces memory bandwidth by 3x for significant performance gains
struct PackedChunkVertex {
    // Position relative to chunk origin (0-16 for X/Z, 0-256 for Y)
    // Using int16 for sub-block precision (multiply by 1/256 in shader)
    int16_t x, y, z;       // 6 bytes

    // Texture coordinates for greedy meshing (0-16 range, 8.8 fixed point)
    uint16_t u, v;         // 4 bytes

    // Normal direction index (0-5 for +X,-X,+Y,-Y,+Z,-Z)
    uint8_t normalIndex;   // 1 byte

    // AO factor (0-255 maps to 0.0-1.0)
    uint8_t ao;            // 1 byte

    // Light level (0-255 maps to 0.0-1.0)
    uint8_t light;         // 1 byte

    // Texture slot index in atlas (0-255)
    uint8_t texSlot;       // 1 byte

    // Biome data for grass/foliage tinting (0-255 maps to 0.0-1.0)
    uint8_t biomeTemp;     // 1 byte - temperature for colormap sampling
    uint8_t biomeHumid;    // 1 byte - humidity for colormap sampling

    // Total: 16 bytes (3x smaller than original 48 bytes)
};

// Legacy vertex structure for water (keeps smooth normals for water effects)
struct ChunkVertex {
    glm::vec3 position;
    glm::vec2 texCoord;    // Local UV coords (0 to quadWidth, 0 to quadHeight for tiling)
    glm::vec3 normal;
    float aoFactor;        // Smooth ambient occlusion factor (0-1)
    float lightLevel;      // Block light level (0-1, from emissive blocks)
    glm::vec2 texSlotBase; // Base UV of texture slot in atlas (for greedy meshing tiling)
};

// Include BinaryGreedyMesher.h for FACE_BUCKET_COUNT constant
// Must be included after PackedChunkVertex is defined (no circular dependency)
#include "BinaryGreedyMesher.h"

// ============================================================
// MESH SHADER STRUCTURES (GL_NV_mesh_shader)
// ============================================================

// Meshlet configuration - Tuned for better GPU occupancy
// AMD recommends 128 vertices/256 triangles for mesh shaders
// Using 128/256 as a balanced choice for both vendors
constexpr int MESHLET_MAX_VERTICES = 128;
constexpr int MESHLET_MAX_TRIANGLES = 256;
constexpr int MESHLET_MAX_INDICES = MESHLET_MAX_TRIANGLES * 3;

// GPU-side meshlet descriptor (matches mesh shader layout)
// Packed for efficient GPU access
struct alignas(16) MeshletDescriptor {
    uint32_t vertexOffset;      // Offset into vertex buffer
    uint32_t vertexCount;       // Number of vertices in this meshlet
    uint32_t triangleOffset;    // Offset into index buffer (in triangles)
    uint32_t triangleCount;     // Number of triangles

    // Bounding sphere for frustum culling (in local chunk coordinates)
    float centerX, centerY, centerZ;  // Center of bounding sphere
    float radius;                      // Radius of bounding sphere
};

// Meshlet data for a sub-chunk (used by mesh shaders)
struct MeshletData {
    std::vector<MeshletDescriptor> meshlets;  // List of meshlet descriptors
    std::vector<uint32_t> indices;            // Local indices (relative to meshlet vertex offset)

    // GPU buffer handles (uploaded separately for mesh shader usage)
    GLuint meshletSSBO = 0;   // SSBO for meshlet descriptors
    GLuint indexSSBO = 0;     // SSBO for meshlet indices

    void destroy() {
        if (meshletSSBO != 0) { glDeleteBuffers(1, &meshletSSBO); meshletSSBO = 0; }
        if (indexSSBO != 0) { glDeleteBuffers(1, &indexSSBO); indexSSBO = 0; }
        meshlets.clear();
        indices.clear();
    }

    bool hasMeshlets() const {
        return !meshlets.empty() && meshletSSBO != 0;
    }

    size_t getMeshletCount() const {
        return meshlets.size();
    }
};

// Global flag to enable meshlet generation (set from main based on GPU support)
inline bool g_generateMeshlets = false;

// Normal lookup table (used by shader to decode normal index)
// 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
constexpr glm::vec3 NORMAL_LOOKUP[6] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
};

// Helper to encode normal to index
inline uint8_t encodeNormal(const glm::vec3& normal) {
    if (normal.x > 0.5f) return 0;      // +X
    if (normal.x < -0.5f) return 1;     // -X
    if (normal.y > 0.5f) return 2;      // +Y
    if (normal.y < -0.5f) return 3;     // -Y
    if (normal.z > 0.5f) return 4;      // +Z
    return 5;                            // -Z
}

// Global flag to enable/disable persistent mapped buffers
// Set to false to fall back to traditional glBufferSubData
inline bool g_usePersistentMapping = true;

// LOD mesh storage for a single level of detail
struct LODMesh {
    GLuint VAO = 0;
    GLuint VBO = 0;
    int vertexCount = 0;
    GLsizeiptr capacity = 0;  // Current VBO capacity in bytes

    // Persistent mapping support
    void* mappedPtr = nullptr;      // Persistent mapped pointer (nullptr if not mapped)
    GLsync fence = nullptr;         // Sync fence for this buffer

    void destroy() {
        // Wait for any pending GPU operations before destroying
        // Use short timeout to avoid blocking - GPU should be done by now
        if (fence != nullptr) {
            glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000); // 1ms timeout (was 1 second!)
            glDeleteSync(fence);
            fence = nullptr;
        }
        // Unmap before deleting
        if (mappedPtr != nullptr && VBO != 0) {
            glBindBuffer(GL_ARRAY_BUFFER, VBO);
            glUnmapBuffer(GL_ARRAY_BUFFER);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            mappedPtr = nullptr;
        }
        if (VBO != 0) { glDeleteBuffers(1, &VBO); VBO = 0; }
        if (VAO != 0) { glDeleteVertexArrays(1, &VAO); VAO = 0; }
        vertexCount = 0;
        capacity = 0;
    }

    // Check if GPU is done with this buffer (non-blocking)
    // Returns true if GPU is ready and fence has been cleared
    bool isGPUReady() {
        if (fence == nullptr) return true;

        GLenum result = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
        if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED) {
            glDeleteSync(fence);
            fence = nullptr;
            return true;
        }
        return false;  // GPU still using buffer
    }

    // Wait for GPU to finish using this buffer (blocking - use sparingly!)
    // Returns true if GPU is ready, false if timeout/failure occurred
    // OPTIMIZATION: Reduced timeout to prevent long frame stalls
    bool waitForGPU() {
        if (fence == nullptr) return true;

        // Short wait to avoid long stalls - 3 attempts × 1ms = 3ms max
        // If GPU is more than 3ms behind, we bail out and handle it
        for (int attempt = 0; attempt < 3; attempt++) {
            GLenum result = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000);  // 1ms per attempt
            if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED) {
                glDeleteSync(fence);
                fence = nullptr;
                return true;
            }
            if (result == GL_WAIT_FAILED) {
                // Sync object is invalid - clean up and report failure
                glDeleteSync(fence);
                fence = nullptr;
                return false;
            }
            // GL_TIMEOUT_EXPIRED - try again
        }

        // All attempts timed out - GPU is severely behind
        // Don't delete fence, let caller handle this
        return false;
    }

    // Signal that CPU is done writing (call after memcpy)
    void signalCPUDone() {
        if (fence != nullptr) {
            glDeleteSync(fence);
        }
        fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }
};

// LOD configuration
constexpr int LOD_LEVELS = 4;
constexpr int LOD_SCALES[LOD_LEVELS] = {1, 2, 4, 8};  // Block sampling scale for each LOD

// Sub-chunk configuration for vertical culling
constexpr int SUB_CHUNK_HEIGHT = 16;                       // Height of each sub-chunk in blocks
constexpr int SUB_CHUNKS_PER_COLUMN = CHUNK_SIZE_Y / SUB_CHUNK_HEIGHT;  // 256/16 = 16 sub-chunks

// Sub-chunk mesh - contains LOD meshes for a 16x16x16 section
// Uses consolidated VBO with glMultiDrawArrays for batched rendering
struct SubChunkMesh {
    // Consolidated face bucket storage - single VAO/VBO for all 6 face directions
    // Uses glMultiDrawArrays to draw all faces in one call (reduces VAO binds by 6x)
    GLuint consolidatedVAO = 0;
    GLuint consolidatedVBO = 0;
    GLsizeiptr consolidatedCapacity = 0;

    // MultiDraw arrays for batched rendering
    std::array<GLint, FACE_BUCKET_COUNT> faceBucketOffsets = {};   // Vertex offset per face
    std::array<GLsizei, FACE_BUCKET_COUNT> faceBucketCounts = {};  // Vertex count per face
    int activeBucketCount = 0;  // Number of non-empty buckets

    // Legacy separate face buckets (kept for compatibility, TODO: remove)
    std::array<GLuint, FACE_BUCKET_COUNT> faceBucketVAOs = {};
    std::array<GLuint, FACE_BUCKET_COUNT> faceBucketVBOs = {};
    std::array<int, FACE_BUCKET_COUNT> faceBucketVertexCounts = {};
    std::array<GLsizeiptr, FACE_BUCKET_COUNT> faceBucketCapacities = {};
    
    std::array<LODMesh, LOD_LEVELS> lodMeshes;  // LOD 0-3 for this sub-chunk (LOD 0 unused if buckets active)

    // Water geometry for this sub-chunk
    GLuint waterVAO = 0;
    GLuint waterVBO = 0;
    int waterVertexCount = 0;
    GLsizeiptr waterVboCapacity = 0;

    // Mesh shader data (for GL_NV_mesh_shader rendering path)
    MeshletData meshletData;
    GLuint vertexSSBO = 0;  // SSBO for vertex data (mesh shaders read from SSBO, not VBO)

    // Cached vertex data for deferred meshlet generation (used during burst mode)
    std::vector<PackedChunkVertex> cachedVerticesForMeshlets;
    bool needsMeshletGeneration = false;  // Flag for deferred meshlet generation

    // Cached vertex data for RHI renderer (Vulkan backend)
    std::vector<PackedChunkVertex> cachedVertices;
    std::vector<ChunkVertex> cachedWaterVertices;

    int subChunkY = 0;     // Y index (0-15)
    bool isEmpty = true;   // Skip rendering if no geometry
    bool hasWater = false; // Quick check for water rendering pass
    bool useFaceBuckets = true;  // Use face buckets for LOD 0 (can disable for debugging)

    void destroy() {
        // Clean up consolidated face bucket buffer
        if (consolidatedVBO != 0) { glDeleteBuffers(1, &consolidatedVBO); consolidatedVBO = 0; }
        if (consolidatedVAO != 0) { glDeleteVertexArrays(1, &consolidatedVAO); consolidatedVAO = 0; }
        consolidatedCapacity = 0;
        activeBucketCount = 0;
        for (int i = 0; i < FACE_BUCKET_COUNT; i++) {
            faceBucketOffsets[i] = 0;
            faceBucketCounts[i] = 0;
        }

        // Clean up legacy separate face buckets
        for (int i = 0; i < FACE_BUCKET_COUNT; i++) {
            if (faceBucketVBOs[i] != 0) { glDeleteBuffers(1, &faceBucketVBOs[i]); faceBucketVBOs[i] = 0; }
            if (faceBucketVAOs[i] != 0) { glDeleteVertexArrays(1, &faceBucketVAOs[i]); faceBucketVAOs[i] = 0; }
            faceBucketVertexCounts[i] = 0;
            faceBucketCapacities[i] = 0;
        }
        for (auto& lod : lodMeshes) {
            lod.destroy();
        }
        if (waterVBO != 0) { glDeleteBuffers(1, &waterVBO); waterVBO = 0; }
        if (waterVAO != 0) { glDeleteVertexArrays(1, &waterVAO); waterVAO = 0; }
        waterVertexCount = 0;
        waterVboCapacity = 0;
        // Clean up mesh shader resources
        meshletData.destroy();
        if (vertexSSBO != 0) { glDeleteBuffers(1, &vertexSSBO); vertexSSBO = 0; }
        isEmpty = true;
        hasWater = false;
    }

    // Check if this sub-chunk has any geometry at any LOD level
    bool hasGeometry() const {
        if (!isEmpty) return true;
        // Check face buckets
        for (int i = 0; i < FACE_BUCKET_COUNT; i++) {
            if (faceBucketVertexCounts[i] > 0) return true;
        }
        for (const auto& lod : lodMeshes) {
            if (lod.vertexCount > 0) return true;
        }
        return false;
    }

    // Get total vertex count at LOD 0 (sum of all face buckets)
    int getLOD0VertexCount() const {
        int total = 0;
        for (int i = 0; i < FACE_BUCKET_COUNT; i++) {
            total += faceBucketVertexCounts[i];
        }
        return total;
    }

    // Get vertex count at specified LOD
    int getVertexCount(int lodLevel = 0) const {
        lodLevel = std::max(0, std::min(lodLevel, LOD_LEVELS - 1));
        if (lodLevel == 0 && useFaceBuckets) {
            return getLOD0VertexCount();
        }
        return lodMeshes[lodLevel].vertexCount;
    }
    
    // Check if a specific face bucket has vertices
    bool hasFaceBucket(int bucketIndex) const {
        if (bucketIndex < 0 || bucketIndex >= FACE_BUCKET_COUNT) return false;
        return faceBucketVertexCounts[bucketIndex] > 0 && faceBucketVAOs[bucketIndex] != 0;
    }
};

class ChunkMesh {
public:
    // Sub-chunks: 16 vertical sections, each 16x16x16 blocks
    std::array<SubChunkMesh, SUB_CHUNKS_PER_COLUMN> subChunks;

    // Legacy: Keep single LOD meshes for backwards compatibility during transition
    // TODO: Remove once sub-chunk system is fully working
    std::array<LODMesh, LOD_LEVELS> lodMeshes;

    // Water geometry (no LOD - only rendered at close range)
    GLuint waterVAO = 0;
    GLuint waterVBO = 0;
    int waterVertexCount = 0;
    GLsizeiptr waterVboCapacity = 0;

    // 3D lightmap texture for smooth lighting across greedy-meshed quads
    // Stores light values per-block, sampled in fragment shader using world position
    // Size: 16 x 256 x 16 (CHUNK_SIZE_X x CHUNK_SIZE_Y x CHUNK_SIZE_Z)
    GLuint lightmapTexture = 0;

    glm::ivec2 chunkPosition;

    // World position of chunk origin (needed for shader to reconstruct world positions)
    glm::vec3 worldOffset;

    ChunkMesh() = default;

    ~ChunkMesh() {
        destroy();
    }

    // Move constructor
    ChunkMesh(ChunkMesh&& other) noexcept
        : subChunks(std::move(other.subChunks)),
          lodMeshes(std::move(other.lodMeshes)),
          waterVAO(other.waterVAO), waterVBO(other.waterVBO), waterVertexCount(other.waterVertexCount),
          waterVboCapacity(other.waterVboCapacity), lightmapTexture(other.lightmapTexture),
          chunkPosition(other.chunkPosition), worldOffset(other.worldOffset)
    {
        other.lightmapTexture = 0;
        // Reset moved-from sub-chunks
        for (auto& sub : other.subChunks) {
            for (auto& lod : sub.lodMeshes) {
                lod.VAO = 0;
                lod.VBO = 0;
                lod.vertexCount = 0;
                lod.capacity = 0;
            }
            sub.waterVAO = 0;
            sub.waterVBO = 0;
            sub.waterVertexCount = 0;
            sub.isEmpty = true;
            sub.hasWater = false;
        }
        // Reset moved-from LOD meshes
        for (auto& lod : other.lodMeshes) {
            lod.VAO = 0;
            lod.VBO = 0;
            lod.vertexCount = 0;
            lod.capacity = 0;
        }
        other.waterVAO = 0;
        other.waterVBO = 0;
        other.waterVertexCount = 0;
        other.waterVboCapacity = 0;
    }

    // Move assignment
    ChunkMesh& operator=(ChunkMesh&& other) noexcept {
        if (this != &other) {
            destroy();
            subChunks = std::move(other.subChunks);
            lodMeshes = std::move(other.lodMeshes);
            waterVAO = other.waterVAO;
            waterVBO = other.waterVBO;
            waterVertexCount = other.waterVertexCount;
            waterVboCapacity = other.waterVboCapacity;
            lightmapTexture = other.lightmapTexture;
            chunkPosition = other.chunkPosition;
            worldOffset = other.worldOffset;
            other.lightmapTexture = 0;
            // Reset moved-from sub-chunks
            for (auto& sub : other.subChunks) {
                for (auto& lod : sub.lodMeshes) {
                    lod.VAO = 0;
                    lod.VBO = 0;
                    lod.vertexCount = 0;
                    lod.capacity = 0;
                }
                sub.waterVAO = 0;
                sub.waterVBO = 0;
                sub.waterVertexCount = 0;
                sub.isEmpty = true;
                sub.hasWater = false;
            }
            // Reset moved-from LOD meshes
            for (auto& lod : other.lodMeshes) {
                lod.VAO = 0;
                lod.VBO = 0;
                lod.vertexCount = 0;
                lod.capacity = 0;
            }
            other.waterVAO = 0;
            other.waterVBO = 0;
            other.waterVertexCount = 0;
            other.waterVboCapacity = 0;
        }
        return *this;
    }

    // No copy
    ChunkMesh(const ChunkMesh&) = delete;
    ChunkMesh& operator=(const ChunkMesh&) = delete;

    void destroy() {
        // Clean up all sub-chunk meshes
        for (auto& sub : subChunks) {
            sub.destroy();
        }

        // Clean up legacy LOD meshes
        for (auto& lod : lodMeshes) {
            lod.destroy();
        }

        // Clean up water resources
        if (waterVBO != 0) {
            glDeleteBuffers(1, &waterVBO);
            waterVBO = 0;
        }
        if (waterVAO != 0) {
            glDeleteVertexArrays(1, &waterVAO);
            waterVAO = 0;
        }
        waterVertexCount = 0;
        waterVboCapacity = 0;

        // Clean up 3D lightmap texture
        if (lightmapTexture != 0) {
            glDeleteTextures(1, &lightmapTexture);
            lightmapTexture = 0;
        }
    }

    // Create or update the 3D lightmap texture from chunk light data
    // This enables smooth lighting across greedy-meshed quads by sampling
    // light values per-pixel in the fragment shader instead of per-vertex
    void updateLightmap(const Chunk& chunk) {
        // Allocate texture data (16 x 256 x 16 = 65536 bytes)
        std::vector<uint8_t> lightData(CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z);

        // Fill with light values from chunk
        // 3D texture layout: X varies fastest, then Z, then Y
        for (int y = 0; y < CHUNK_SIZE_Y; y++) {
            for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                for (int x = 0; x < CHUNK_SIZE_X; x++) {
                    int index = x + z * CHUNK_SIZE_X + y * CHUNK_SIZE_X * CHUNK_SIZE_Z;
                    // Get light level (0-15) and scale to 0-255
                    uint8_t light = chunk.getLightLevel(x, y, z);
                    lightData[index] = light * 17;  // Scale 0-15 to 0-255
                }
            }
        }

        // Create texture if it doesn't exist
        if (lightmapTexture == 0) {
            glGenTextures(1, &lightmapTexture);
            glBindTexture(GL_TEXTURE_3D, lightmapTexture);

            // Set texture parameters for smooth interpolation
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

            // Allocate storage
            glTexImage3D(GL_TEXTURE_3D, 0, GL_R8,
                         CHUNK_SIZE_X, CHUNK_SIZE_Z, CHUNK_SIZE_Y,  // width, height, depth
                         0, GL_RED, GL_UNSIGNED_BYTE, lightData.data());
        } else {
            // Update existing texture
            glBindTexture(GL_TEXTURE_3D, lightmapTexture);
            glTexSubImage3D(GL_TEXTURE_3D, 0,
                            0, 0, 0,  // offset
                            CHUNK_SIZE_X, CHUNK_SIZE_Z, CHUNK_SIZE_Y,  // size
                            GL_RED, GL_UNSIGNED_BYTE, lightData.data());
        }

        glBindTexture(GL_TEXTURE_3D, 0);
    }

    // Bind the lightmap texture to a specific texture unit
    void bindLightmap(int textureUnit = 2) const {
        if (lightmapTexture != 0) {
            glActiveTexture(GL_TEXTURE0 + textureUnit);
            glBindTexture(GL_TEXTURE_3D, lightmapTexture);
        }
    }

    // Block getter function type - takes world coordinates, returns block type
    using BlockGetter = std::function<BlockType(int, int, int)>;

    // Light getter function type - takes world coordinates, returns light level (0-15)
    using LightGetter = std::function<uint8_t(int, int, int)>;

    // Generate mesh from chunk data (legacy - no cross-chunk awareness)
    void generate(const Chunk& chunk) {
        // Create a simple block getter that only looks at this chunk
        auto localGetter = [&chunk](int wx, int wy, int wz) -> BlockType {
            int localX = wx - chunk.position.x * CHUNK_SIZE_X;
            int localZ = wz - chunk.position.y * CHUNK_SIZE_Z;
            if (localX < 0 || localX >= CHUNK_SIZE_X ||
                localZ < 0 || localZ >= CHUNK_SIZE_Z ||
                wy < 0 || wy >= CHUNK_SIZE_Y) {
                return BlockType::AIR;
            }
            return chunk.getBlock(localX, wy, localZ);
        };
        auto localLightGetter = [&chunk](int wx, int wy, int wz) -> uint8_t {
            int localX = wx - chunk.position.x * CHUNK_SIZE_X;
            int localZ = wz - chunk.position.y * CHUNK_SIZE_Z;
            if (localX < 0 || localX >= CHUNK_SIZE_X ||
                localZ < 0 || localZ >= CHUNK_SIZE_Z ||
                wy < 0 || wy >= CHUNK_SIZE_Y) {
                return 0;
            }
            return chunk.getLightLevel(localX, wy, localZ);
        };
        generate(chunk, localGetter, localGetter, localGetter, localLightGetter);
    }

    // Generate mesh with single world-aware block getter (for compatibility)
    void generate(const Chunk& chunk, const BlockGetter& getWorldBlock) {
        auto defaultLightGetter = [](int, int, int) -> uint8_t { return 0; };
        generate(chunk, getWorldBlock, getWorldBlock, getWorldBlock, defaultLightGetter);
    }

    // Generate mesh with world-aware block getter (fixes chunk seams)
    // Uses greedy meshing to merge adjacent faces of same type
    void generate(const Chunk& chunk, const BlockGetter& getWorldBlock, const BlockGetter& getWaterBlock, const BlockGetter& getSafeBlock, const LightGetter& getLightLevel) {
        std::vector<PackedChunkVertex> solidVertices;
        std::vector<ChunkVertex> waterVertices;
        solidVertices.reserve(CHUNK_VOLUME);
        waterVertices.reserve(CHUNK_VOLUME / 8);

        chunkPosition = chunk.position;
        glm::vec3 chunkWorldPos = chunk.getWorldPosition();
        worldOffset = chunkWorldPos;  // Store for shader
        int baseX = chunk.position.x * CHUNK_SIZE_X;
        int baseZ = chunk.position.y * CHUNK_SIZE_Z;

        // Process water blocks separately (no greedy meshing for water)
        // Use heightmaps to skip empty Y regions - major optimization
        int minY = chunk.chunkMinY;
        int maxY = chunk.chunkMaxY;

        // Only iterate if chunk has content
        if (minY <= maxY) {
            for (int y = minY; y <= maxY; y++) {
                for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                    for (int x = 0; x < CHUNK_SIZE_X; x++) {
                        BlockType block = chunk.getBlock(x, y, z);
                        if (block == BlockType::WATER) {
                            int wx = baseX + x;
                            int wz = baseZ + z;
                            BlockTextures textures = getBlockTextures(block);
                            glm::vec3 blockPos = chunkWorldPos + glm::vec3(x, y, z);
                            addWaterBlock(waterVertices, chunk, x, y, z, blockPos, textures.faceSlots[0], getWaterBlock, wx, wz);
                        }
                    }
                }
            }
        }

        // Greedy meshing for each face direction
        // Process TOP faces (+Y) - most common large flat surface
        generateGreedyFaces(solidVertices, chunk, chunkWorldPos, baseX, baseZ,
                           getWorldBlock, getSafeBlock, getLightLevel, BlockFace::TOP);

        // Process BOTTOM faces (-Y)
        generateGreedyFaces(solidVertices, chunk, chunkWorldPos, baseX, baseZ,
                           getWorldBlock, getSafeBlock, getLightLevel, BlockFace::BOTTOM);

        // Process FRONT faces (+Z)
        generateGreedyFaces(solidVertices, chunk, chunkWorldPos, baseX, baseZ,
                           getWorldBlock, getSafeBlock, getLightLevel, BlockFace::FRONT);

        // Process BACK faces (-Z)
        generateGreedyFaces(solidVertices, chunk, chunkWorldPos, baseX, baseZ,
                           getWorldBlock, getSafeBlock, getLightLevel, BlockFace::BACK);

        // Process LEFT faces (-X)
        generateGreedyFaces(solidVertices, chunk, chunkWorldPos, baseX, baseZ,
                           getWorldBlock, getSafeBlock, getLightLevel, BlockFace::LEFT);

        // Process RIGHT faces (+X)
        generateGreedyFaces(solidVertices, chunk, chunkWorldPos, baseX, baseZ,
                           getWorldBlock, getSafeBlock, getLightLevel, BlockFace::RIGHT);

        // Upload solid geometry to LOD 0 (legacy - kept for backwards compatibility)
        uploadToGPU(solidVertices, 0);
        // Upload water geometry to separate water VAO (legacy)
        uploadWaterToGPU(waterVertices);

        // Generate lower LOD levels (1, 2, 3) for distance rendering (legacy)
        generateAllLODs(chunk, getWorldBlock, getSafeBlock, getLightLevel);

        // Generate sub-chunk meshes for vertical culling
        generateSubChunkMeshes(chunk, chunkWorldPos, baseX, baseZ, getWorldBlock, getWaterBlock, getSafeBlock, getLightLevel);
    }

    // Generate meshes for each of the 16 sub-chunks (16x16x16 sections)
    void generateSubChunkMeshes(const Chunk& chunk, const glm::vec3& chunkWorldPos, int baseX, int baseZ,
                                const BlockGetter& getWorldBlock, const BlockGetter& getWaterBlock,
                                const BlockGetter& getSafeBlock, const LightGetter& getLightLevel) {
        // Process each sub-chunk (Y section 0-15)
        for (int subY = 0; subY < SUB_CHUNKS_PER_COLUMN; subY++) {
            int yStart = subY * SUB_CHUNK_HEIGHT;
            int yEnd = yStart + SUB_CHUNK_HEIGHT - 1;

            // Check if this sub-chunk is empty using heightmaps
            if (yEnd < chunk.chunkMinY || yStart > chunk.chunkMaxY) {
                // Sub-chunk is entirely empty
                subChunks[subY].isEmpty = true;
                subChunks[subY].hasWater = false;
                subChunks[subY].subChunkY = subY;
                continue;
            }

            // Generate mesh for this sub-chunk
            std::vector<PackedChunkVertex> solidVertices;
            std::vector<ChunkVertex> waterVertices;
            solidVertices.reserve(SUB_CHUNK_HEIGHT * CHUNK_SIZE_X * CHUNK_SIZE_Z / 2);
            waterVertices.reserve(1024);

            // Process water and lava blocks in this Y range (transparent liquids)
            for (int y = std::max(yStart, static_cast<int>(chunk.chunkMinY)); y <= std::min(yEnd, static_cast<int>(chunk.chunkMaxY)); y++) {
                for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                    for (int x = 0; x < CHUNK_SIZE_X; x++) {
                        BlockType block = chunk.getBlock(x, y, z);
                        if (block == BlockType::WATER || block == BlockType::LAVA) {
                            int wx = baseX + x;
                            int wz = baseZ + z;
                            BlockTextures textures = getBlockTextures(block);
                            glm::vec3 blockPos = chunkWorldPos + glm::vec3(x, y, z);
                            addWaterBlock(waterVertices, chunk, x, y, z, blockPos, textures.faceSlots[0], getWaterBlock, wx, wz);
                        }
                    }
                }
            }

            // Greedy meshing for this sub-chunk's Y range
            generateGreedyFacesForSubChunk(solidVertices, chunk, chunkWorldPos, baseX, baseZ,
                                           getWorldBlock, getSafeBlock, getLightLevel, yStart, yEnd);

            // Upload to this sub-chunk
            uploadToSubChunk(subY, solidVertices, 0);
            uploadWaterToSubChunk(subY, waterVertices);

            // Generate meshlets for mesh shader rendering (if enabled)
            if (g_generateMeshlets && !solidVertices.empty()) {
                generateMeshlets(subY, solidVertices);
            }

            // Generate LODs for this sub-chunk
            generateSubChunkLODs(subY, chunk, yStart, yEnd, getSafeBlock);
        }
    }

    // Render solid (opaque) geometry at specified LOD level
    // Falls back to lower LOD if requested level isn't available
    void render(int lodLevel = 0) const {
        lodLevel = std::max(0, std::min(lodLevel, LOD_LEVELS - 1));

        // Try requested LOD, fall back to lower LODs if not available
        for (int level = lodLevel; level >= 0; level--) {
            const auto& lod = lodMeshes[level];
            if (lod.vertexCount > 0 && lod.VAO != 0) {
                glBindVertexArray(lod.VAO);
                glDrawArrays(GL_TRIANGLES, 0, lod.vertexCount);
                return;
            }
        }
    }

    // Get vertex count for a specific LOD level
    int getVertexCount(int lodLevel = 0) const {
        lodLevel = std::max(0, std::min(lodLevel, LOD_LEVELS - 1));
        return lodMeshes[lodLevel].vertexCount;
    }

    // Check if mesh has geometry at any LOD level
    bool hasGeometry() const {
        for (const auto& lod : lodMeshes) {
            if (lod.vertexCount > 0) return true;
        }
        return false;
    }

    // Check if any sub-chunk has geometry
    bool hasSubChunkGeometry() const {
        for (const auto& sub : subChunks) {
            if (!sub.isEmpty) return true;
        }
        return false;
    }

    // Render a specific sub-chunk at the given LOD level
    // For LOD 0: Renders all 6 face buckets (use renderSubChunkWithFaceCulling for directional culling)
    // For LOD 1+: Uses pre-baked LOD meshes
    void renderSubChunk(int subChunkY, int lodLevel = 0) const {
        if (subChunkY < 0 || subChunkY >= SUB_CHUNKS_PER_COLUMN) return;
        const auto& sub = subChunks[subChunkY];
        if (sub.isEmpty) return;

        lodLevel = std::max(0, std::min(lodLevel, LOD_LEVELS - 1));

        // LOD 0: Use face buckets (original separate VAO approach for debugging)
        if (lodLevel == 0 && sub.useFaceBuckets) {
            bool rendered = false;
            for (int bucketIdx = 0; bucketIdx < FACE_BUCKET_COUNT; bucketIdx++) {
                if (sub.faceBucketVertexCounts[bucketIdx] > 0 && sub.faceBucketVAOs[bucketIdx] != 0) {
                    glBindVertexArray(sub.faceBucketVAOs[bucketIdx]);
                    glDrawArrays(GL_TRIANGLES, 0, sub.faceBucketVertexCounts[bucketIdx]);
                    rendered = true;
                }
            }
            if (rendered) return;
        }

        // Try requested LOD, fall back to lower LODs if not available
        for (int level = lodLevel; level >= 0; level--) {
            const auto& lod = sub.lodMeshes[level];
            if (lod.vertexCount > 0 && lod.VAO != 0) {
                glBindVertexArray(lod.VAO);
                glDrawArrays(GL_TRIANGLES, 0, lod.vertexCount);
                return;
            }
        }
    }

    // Render water for a specific sub-chunk
    void renderSubChunkWater(int subChunkY) const {
        if (subChunkY < 0 || subChunkY >= SUB_CHUNKS_PER_COLUMN) return;
        const auto& sub = subChunks[subChunkY];
        if (!sub.hasWater || sub.waterVertexCount == 0 || sub.waterVAO == 0) return;

        glBindVertexArray(sub.waterVAO);
        glDrawArrays(GL_TRIANGLES, 0, sub.waterVertexCount);
    }

    // Get the sub-chunk for a given Y index
    const SubChunkMesh& getSubChunk(int subChunkY) const {
        return subChunks[std::clamp(subChunkY, 0, SUB_CHUNKS_PER_COLUMN - 1)];
    }

    // Render water (transparent) geometry - call this AFTER all solid geometry
    void renderWater() const {
        if (waterVertexCount == 0 || waterVAO == 0) return;

        glBindVertexArray(waterVAO);
        glDrawArrays(GL_TRIANGLES, 0, waterVertexCount);
    }

    // Check if this chunk has water to render
    bool hasWater() const {
        return waterVertexCount > 0 && waterVAO != 0;
    }

    // Generate all LOD levels for this chunk
    void generateAllLODs(const Chunk& chunk, const BlockGetter& getWorldBlock,
                         const BlockGetter& getSafeBlock, const LightGetter& getLightLevel) {
        // LOD 0 is already generated by the main generate() call
        // Generate LOD 1, 2, 3 with reduced detail
        for (int lod = 1; lod < LOD_LEVELS; lod++) {
            generateLODMesh(chunk, lod, getWorldBlock, getSafeBlock, getLightLevel);
        }
    }

private:
    // Generate greedy mesh for a specific Y range (for sub-chunk generation)
    void generateGreedyFacesForSubChunk(std::vector<PackedChunkVertex>& vertices, const Chunk& chunk,
                                        const glm::vec3& chunkWorldPos, int baseX, int baseZ,
                                        const BlockGetter& getWorldBlock, const BlockGetter& getSafeBlock,
                                        const LightGetter& getLightLevel, int yStart, int yEnd) {
        // Generate all 6 face directions within this Y range
        for (int faceIdx = 0; faceIdx < 6; faceIdx++) {
            BlockFace face = static_cast<BlockFace>(faceIdx);
            generateGreedyFacesInYRange(vertices, chunk, chunkWorldPos, baseX, baseZ,
                                        getWorldBlock, getSafeBlock, getLightLevel, face, yStart, yEnd);
        }
    }

    // Generate greedy faces for a specific direction within a Y range
    void generateGreedyFacesInYRange(std::vector<PackedChunkVertex>& vertices, const Chunk& chunk,
                                     const glm::vec3& chunkWorldPos, int baseX, int baseZ,
                                     const BlockGetter& getWorldBlock, const BlockGetter& getSafeBlock,
                                     const LightGetter& getLightLevel, BlockFace face, int yStart, int yEnd) {
        // Face visibility mask
        std::array<int, CHUNK_SIZE_X * CHUNK_SIZE_Z> maskXZ;
        std::array<int, CHUNK_SIZE_X * SUB_CHUNK_HEIGHT> maskXY;
        std::array<int, SUB_CHUNK_HEIGHT * CHUNK_SIZE_Z> maskYZ;

        auto getNeighborOffset = [](BlockFace f) -> glm::ivec3 {
            switch (f) {
                case BlockFace::TOP:    return {0, 1, 0};
                case BlockFace::BOTTOM: return {0, -1, 0};
                case BlockFace::FRONT:  return {0, 0, 1};
                case BlockFace::BACK:   return {0, 0, -1};
                case BlockFace::LEFT:   return {-1, 0, 0};
                case BlockFace::RIGHT:  return {1, 0, 0};
                default: return {0, 0, 0};
            }
        };

        glm::ivec3 nOffset = getNeighborOffset(face);
        int effectiveMinY = std::max(yStart, static_cast<int>(chunk.chunkMinY));
        int effectiveMaxY = std::min(yEnd, static_cast<int>(chunk.chunkMaxY));
        if (effectiveMinY > effectiveMaxY) return;

        if (face == BlockFace::TOP || face == BlockFace::BOTTOM) {
            // Iterate through Y slices within this sub-chunk
            for (int y = effectiveMinY; y <= effectiveMaxY; y++) {
                maskXZ.fill(-1);
                for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                    for (int x = 0; x < CHUNK_SIZE_X; x++) {
                        BlockType block = chunk.getBlock(x, y, z);
                        if (block == BlockType::AIR || block == BlockType::WATER) continue;

                        int wx = baseX + x;
                        int wz = baseZ + z;
                        int ny = y + nOffset.y;

                        if (shouldRenderFace(getSafeBlock, wx, ny, wz)) {
                            BlockTextures textures = getBlockTextures(block);
                            int slot = (face == BlockFace::TOP) ? textures.faceSlots[4] : textures.faceSlots[5];
                            maskXZ[z * CHUNK_SIZE_X + x] = slot;
                        }
                    }
                }

                // Greedy merge
                for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                    for (int x = 0; x < CHUNK_SIZE_X; ) {
                        int slot = maskXZ[z * CHUNK_SIZE_X + x];
                        if (slot < 0) { x++; continue; }

                        int width = 1;
                        while (x + width < CHUNK_SIZE_X && maskXZ[z * CHUNK_SIZE_X + x + width] == slot) width++;

                        int height = 1;
                        bool canExtend = true;
                        while (z + height < CHUNK_SIZE_Z && canExtend) {
                            for (int dx = 0; dx < width; dx++) {
                                if (maskXZ[(z + height) * CHUNK_SIZE_X + x + dx] != slot) {
                                    canExtend = false;
                                    break;
                                }
                            }
                            if (canExtend) height++;
                        }

                        for (int dz = 0; dz < height; dz++) {
                            for (int dx = 0; dx < width; dx++) {
                                maskXZ[(z + dz) * CHUNK_SIZE_X + x + dx] = -1;
                            }
                        }

                        addGreedyQuad(vertices, getWorldBlock, getLightLevel, baseX, baseZ,
                                     chunkWorldPos, face, slot, x, y, z, width, height);
                        x += width;
                    }
                }
            }
        }
        else if (face == BlockFace::FRONT || face == BlockFace::BACK) {
            for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                maskXY.fill(-1);
                for (int y = effectiveMinY; y <= effectiveMaxY; y++) {
                    int localY = y - yStart;
                    for (int x = 0; x < CHUNK_SIZE_X; x++) {
                        BlockType block = chunk.getBlock(x, y, z);
                        if (block == BlockType::AIR || block == BlockType::WATER) continue;

                        int wx = baseX + x;
                        int wz = baseZ + z;
                        int nz = wz + nOffset.z;

                        if (shouldRenderFace(getSafeBlock, wx, y, nz)) {
                            BlockTextures textures = getBlockTextures(block);
                            int slot = (face == BlockFace::FRONT) ? textures.faceSlots[0] : textures.faceSlots[1];
                            maskXY[localY * CHUNK_SIZE_X + x] = slot;
                        }
                    }
                }

                for (int y = effectiveMinY; y <= effectiveMaxY; y++) {
                    int localY = y - yStart;
                    for (int x = 0; x < CHUNK_SIZE_X; ) {
                        int slot = maskXY[localY * CHUNK_SIZE_X + x];
                        if (slot < 0) { x++; continue; }

                        int width = 1;
                        while (x + width < CHUNK_SIZE_X && maskXY[localY * CHUNK_SIZE_X + x + width] == slot) width++;

                        int height = 1;
                        bool canExtend = true;
                        while (y + height <= effectiveMaxY && canExtend) {
                            int nextLocalY = (y + height) - yStart;
                            for (int dx = 0; dx < width; dx++) {
                                if (maskXY[nextLocalY * CHUNK_SIZE_X + x + dx] != slot) {
                                    canExtend = false;
                                    break;
                                }
                            }
                            if (canExtend) height++;
                        }

                        for (int dy = 0; dy < height; dy++) {
                            int clearLocalY = (y + dy) - yStart;
                            for (int dx = 0; dx < width; dx++) {
                                maskXY[clearLocalY * CHUNK_SIZE_X + x + dx] = -1;
                            }
                        }

                        addGreedyQuad(vertices, getWorldBlock, getLightLevel, baseX, baseZ,
                                     chunkWorldPos, face, slot, x, y, z, width, height);
                        x += width;
                    }
                }
            }
        }
        else { // LEFT or RIGHT
            for (int x = 0; x < CHUNK_SIZE_X; x++) {
                maskYZ.fill(-1);
                for (int y = effectiveMinY; y <= effectiveMaxY; y++) {
                    int localY = y - yStart;
                    for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                        BlockType block = chunk.getBlock(x, y, z);
                        if (block == BlockType::AIR || block == BlockType::WATER) continue;

                        int wx = baseX + x;
                        int wz = baseZ + z;
                        int nx = wx + nOffset.x;

                        if (shouldRenderFace(getSafeBlock, nx, y, wz)) {
                            BlockTextures textures = getBlockTextures(block);
                            int slot = (face == BlockFace::LEFT) ? textures.faceSlots[2] : textures.faceSlots[3];
                            maskYZ[localY * CHUNK_SIZE_Z + z] = slot;
                        }
                    }
                }

                for (int y = effectiveMinY; y <= effectiveMaxY; y++) {
                    int localY = y - yStart;
                    for (int z = 0; z < CHUNK_SIZE_Z; ) {
                        int slot = maskYZ[localY * CHUNK_SIZE_Z + z];
                        if (slot < 0) { z++; continue; }

                        int width = 1;
                        while (z + width < CHUNK_SIZE_Z && maskYZ[localY * CHUNK_SIZE_Z + z + width] == slot) width++;

                        int height = 1;
                        bool canExtend = true;
                        while (y + height <= effectiveMaxY && canExtend) {
                            int nextLocalY = (y + height) - yStart;
                            for (int dz = 0; dz < width; dz++) {
                                if (maskYZ[nextLocalY * CHUNK_SIZE_Z + z + dz] != slot) {
                                    canExtend = false;
                                    break;
                                }
                            }
                            if (canExtend) height++;
                        }

                        for (int dy = 0; dy < height; dy++) {
                            int clearLocalY = (y + dy) - yStart;
                            for (int dz = 0; dz < width; dz++) {
                                maskYZ[clearLocalY * CHUNK_SIZE_Z + z + dz] = -1;
                            }
                        }

                        addGreedyQuad(vertices, getWorldBlock, getLightLevel, baseX, baseZ,
                                     chunkWorldPos, face, slot, x, y, z, width, height);
                        z += width;
                    }
                }
            }
        }
    }

    // Generate LOD meshes for a specific sub-chunk
    void generateSubChunkLODs(int subChunkY, const Chunk& chunk, int yStart, int yEnd,
                              const BlockGetter& getSafeBlock) {
        for (int lodLevel = 1; lodLevel < LOD_LEVELS; lodLevel++) {
            int scale = LOD_SCALES[lodLevel];
            std::vector<PackedChunkVertex> vertices;
            vertices.reserve(SUB_CHUNK_HEIGHT * CHUNK_SIZE_X * CHUNK_SIZE_Z / (scale * scale * 2));

            int effectiveMinY = std::max(yStart, static_cast<int>(chunk.chunkMinY));
            int effectiveMaxY = std::min(yEnd, static_cast<int>(chunk.chunkMaxY));
            if (effectiveMinY > effectiveMaxY) {
                uploadToSubChunk(subChunkY, vertices, lodLevel);
                continue;
            }

            // Sample blocks at reduced resolution
            for (int lodZ = 0; lodZ < CHUNK_SIZE_Z; lodZ += scale) {
                for (int lodX = 0; lodX < CHUNK_SIZE_X; lodX += scale) {
                    for (int y = effectiveMinY; y <= effectiveMaxY; y++) {
                        BlockType dominant = getDominantBlock(chunk, lodX, lodZ, y, scale);
                        if (dominant == BlockType::AIR) continue;

                        BlockTextures textures = getBlockTextures(dominant);

                        if (shouldRenderLODFace(chunk, getSafeBlock, lodX, lodZ, y, y + 1, scale, BlockFace::TOP))
                            addLODQuad(vertices, lodX, y, lodZ, scale, BlockFace::TOP, textures.faceSlots[4]);
                        if (shouldRenderLODFace(chunk, getSafeBlock, lodX, lodZ, y, y - 1, scale, BlockFace::BOTTOM))
                            addLODQuad(vertices, lodX, y, lodZ, scale, BlockFace::BOTTOM, textures.faceSlots[5]);
                        if (shouldRenderLODFace(chunk, getSafeBlock, lodX, lodZ, y, y, scale, BlockFace::FRONT))
                            addLODQuad(vertices, lodX, y, lodZ, scale, BlockFace::FRONT, textures.faceSlots[0]);
                        if (shouldRenderLODFace(chunk, getSafeBlock, lodX, lodZ, y, y, scale, BlockFace::BACK))
                            addLODQuad(vertices, lodX, y, lodZ, scale, BlockFace::BACK, textures.faceSlots[1]);
                        if (shouldRenderLODFace(chunk, getSafeBlock, lodX, lodZ, y, y, scale, BlockFace::LEFT))
                            addLODQuad(vertices, lodX, y, lodZ, scale, BlockFace::LEFT, textures.faceSlots[2]);
                        if (shouldRenderLODFace(chunk, getSafeBlock, lodX, lodZ, y, y, scale, BlockFace::RIGHT))
                            addLODQuad(vertices, lodX, y, lodZ, scale, BlockFace::RIGHT, textures.faceSlots[3]);
                    }
                }
            }

            uploadToSubChunk(subChunkY, vertices, lodLevel);
        }
    }

    // Get the dominant (most common) solid block type in an NxN region at given Y
    // Used for LOD downsampling - returns the most visually representative block
    BlockType getDominantBlock(const Chunk& chunk, int startX, int startZ, int y, int scale) {
        // Count occurrences of each block type
        std::array<int, 256> counts{};  // Assuming BlockType fits in uint8
        int solidCount = 0;

        for (int dz = 0; dz < scale && startZ + dz < CHUNK_SIZE_Z; dz++) {
            for (int dx = 0; dx < scale && startX + dx < CHUNK_SIZE_X; dx++) {
                BlockType block = chunk.getBlock(startX + dx, y, startZ + dz);
                if (block != BlockType::AIR && block != BlockType::WATER) {
                    counts[static_cast<int>(block)]++;
                    solidCount++;
                }
            }
        }

        if (solidCount == 0) return BlockType::AIR;

        // Find the most common block
        int maxCount = 0;
        BlockType dominant = BlockType::STONE;  // Default fallback
        for (int i = 0; i < 256; i++) {
            if (counts[i] > maxCount) {
                maxCount = counts[i];
                dominant = static_cast<BlockType>(i);
            }
        }
        return dominant;
    }

    // Generate a mesh at a specific LOD level
    // Scale factor: LOD_SCALES[lodLevel] (1, 2, 4, or 8)
    void generateLODMesh(const Chunk& chunk, int lodLevel, const BlockGetter& /*getWorldBlock*/,
                         const BlockGetter& getSafeBlock, const LightGetter& /*getLightLevel*/) {
        if (lodLevel <= 0 || lodLevel >= LOD_LEVELS) return;

        int scale = LOD_SCALES[lodLevel];
        std::vector<PackedChunkVertex> vertices;
        vertices.reserve(CHUNK_VOLUME / (scale * scale));  // Rough estimate

        int baseX = chunk.position.x * CHUNK_SIZE_X;
        int baseZ = chunk.position.y * CHUNK_SIZE_Z;

        // Sample blocks at reduced resolution
        // For scale=2: sample at (0,2,4,6,8,10,12,14) in X and Z
        // For scale=4: sample at (0,4,8,12)
        // For scale=8: sample at (0,8)

        int minY = chunk.chunkMinY;
        int maxY = chunk.chunkMaxY;
        if (minY > maxY) {
            uploadToGPU(vertices, lodLevel);
            return;
        }

        // Process each LOD block (scaled block covering scale x scale area)
        for (int lodZ = 0; lodZ < CHUNK_SIZE_Z; lodZ += scale) {
            for (int lodX = 0; lodX < CHUNK_SIZE_X; lodX += scale) {
                // Find min/max Y for this column region
                int colMinY = 255, colMaxY = 0;
                for (int dz = 0; dz < scale && lodZ + dz < CHUNK_SIZE_Z; dz++) {
                    for (int dx = 0; dx < scale && lodX + dx < CHUNK_SIZE_X; dx++) {
                        int cmy = chunk.getColumnMinY(lodX + dx, lodZ + dz);
                        int cmx = chunk.getColumnMaxY(lodX + dx, lodZ + dz);
                        if (cmy < colMinY) colMinY = cmy;
                        if (cmx > colMaxY) colMaxY = cmx;
                    }
                }

                if (colMinY > colMaxY) continue;

                for (int y = colMinY; y <= colMaxY; y++) {
                    BlockType dominant = getDominantBlock(chunk, lodX, lodZ, y, scale);
                    if (dominant == BlockType::AIR) continue;

                    // Get texture for this block
                    BlockTextures textures = getBlockTextures(dominant);

                    // World coordinates for neighbor checks
                    int wx = baseX + lodX;
                    int wz = baseZ + lodZ;
                (void)wx; (void)wz; // Suppress warnings

                    // Check each face - for LOD, we use scaled block size
                    // TOP face (+Y)
                    if (shouldRenderLODFace(chunk, getSafeBlock, lodX, lodZ, y, y + 1, scale, BlockFace::TOP)) {
                        addLODQuad(vertices, lodX, y, lodZ, scale, BlockFace::TOP, textures.faceSlots[4]);
                    }

                    // BOTTOM face (-Y)
                    if (shouldRenderLODFace(chunk, getSafeBlock, lodX, lodZ, y, y - 1, scale, BlockFace::BOTTOM)) {
                        addLODQuad(vertices, lodX, y, lodZ, scale, BlockFace::BOTTOM, textures.faceSlots[5]);
                    }

                    // FRONT face (+Z)
                    if (shouldRenderLODFace(chunk, getSafeBlock, lodX, lodZ, y, y, scale, BlockFace::FRONT)) {
                        addLODQuad(vertices, lodX, y, lodZ, scale, BlockFace::FRONT, textures.faceSlots[0]);
                    }

                    // BACK face (-Z)
                    if (shouldRenderLODFace(chunk, getSafeBlock, lodX, lodZ, y, y, scale, BlockFace::BACK)) {
                        addLODQuad(vertices, lodX, y, lodZ, scale, BlockFace::BACK, textures.faceSlots[1]);
                    }

                    // LEFT face (-X)
                    if (shouldRenderLODFace(chunk, getSafeBlock, lodX, lodZ, y, y, scale, BlockFace::LEFT)) {
                        addLODQuad(vertices, lodX, y, lodZ, scale, BlockFace::LEFT, textures.faceSlots[2]);
                    }

                    // RIGHT face (+X)
                    if (shouldRenderLODFace(chunk, getSafeBlock, lodX, lodZ, y, y, scale, BlockFace::RIGHT)) {
                        addLODQuad(vertices, lodX, y, lodZ, scale, BlockFace::RIGHT, textures.faceSlots[3]);
                    }
                }
            }
        }

        uploadToGPU(vertices, lodLevel);
    }

    // Check if an LOD face should be rendered (neighbor region is mostly air/transparent)
    bool shouldRenderLODFace(const Chunk& chunk, const BlockGetter& getSafeBlock,
                             int lodX, int lodZ, int y, int neighborY, int scale, BlockFace face) {
        int baseX = chunk.position.x * CHUNK_SIZE_X;
        int baseZ = chunk.position.y * CHUNK_SIZE_Z;

        // Determine neighbor offset based on face
        int nx = lodX, nz = lodZ;
        switch (face) {
            case BlockFace::TOP:
            case BlockFace::BOTTOM:
                // For top/bottom, check neighbor Y
                if (neighborY < 0 || neighborY >= CHUNK_SIZE_Y)
                    return face == BlockFace::TOP;  // Render top at world top, not bottom
                break;
            case BlockFace::FRONT:  nz = lodZ + scale; break;  // +Z
            case BlockFace::BACK:   nz = lodZ - scale; break;  // -Z
            case BlockFace::LEFT:   nx = lodX - scale; break;  // -X
            case BlockFace::RIGHT:  nx = lodX + scale; break;  // +X
            default: break;
        }

        // For Y faces, check if neighbor layer is mostly transparent
        if (face == BlockFace::TOP || face == BlockFace::BOTTOM) {
            int airCount = 0;
            int total = scale * scale;
            for (int dz = 0; dz < scale && lodZ + dz < CHUNK_SIZE_Z; dz++) {
                for (int dx = 0; dx < scale && lodX + dx < CHUNK_SIZE_X; dx++) {
                    BlockType neighbor = chunk.getBlock(lodX + dx, neighborY, lodZ + dz);
                    if (isBlockTransparent(neighbor)) airCount++;
                }
            }
            return airCount > total / 2;  // Render if more than half is transparent
        }

        // For horizontal faces, check neighbor region
        int airCount = 0;
        int total = scale;  // Check full height at edge
        for (int dy = 0; dy < 1; dy++) {  // Just check same Y level
            // Check if neighbor position is outside chunk
            if (nx < 0 || nx >= CHUNK_SIZE_X || nz < 0 || nz >= CHUNK_SIZE_Z) {
                // Use world block getter for cross-chunk check
                int wx = baseX + nx;
                int wz = baseZ + nz;
                // Sample a few points in the neighbor region
                for (int s = 0; s < scale; s += std::max(1, scale/2)) {
                    int sampleX = (face == BlockFace::LEFT || face == BlockFace::RIGHT) ? wx : wx + s;
                    int sampleZ = (face == BlockFace::FRONT || face == BlockFace::BACK) ? wz : wz + s;
                    BlockType neighbor = getSafeBlock(sampleX, y, sampleZ);
                    if (isBlockTransparent(neighbor)) airCount++;
                    total++;
                }
            } else {
                // Check within chunk
                for (int s = 0; s < scale; s++) {
                    int sampleX = (face == BlockFace::FRONT || face == BlockFace::BACK) ? lodX + s : nx;
                    int sampleZ = (face == BlockFace::LEFT || face == BlockFace::RIGHT) ? lodZ + s : nz;
                    if (sampleX >= 0 && sampleX < CHUNK_SIZE_X && sampleZ >= 0 && sampleZ < CHUNK_SIZE_Z) {
                        BlockType neighbor = chunk.getBlock(sampleX, y, sampleZ);
                        if (isBlockTransparent(neighbor)) airCount++;
                    }
                    total++;
                }
            }
        }
        return airCount > 0;  // Render if any neighbor is transparent
    }

    // Add a quad for LOD rendering (scaled block)
    void addLODQuad(std::vector<PackedChunkVertex>& vertices, int x, int y, int z,
                    int scale, BlockFace face, int textureSlot) {
        uint8_t normalIndex;
        std::array<std::array<int16_t, 3>, 4> localCorners;
        std::array<std::array<uint16_t, 2>, 4> uvCorners;

        switch (face) {
            case BlockFace::TOP: { // +Y
                normalIndex = 2;
                localCorners = {{
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>((z + scale) * 256)},
                    {static_cast<int16_t>((x + scale) * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>((z + scale) * 256)},
                    {static_cast<int16_t>((x + scale) * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>(z * 256)}
                }};
                uvCorners = {{
                    {0, static_cast<uint16_t>(scale * 256)},
                    {static_cast<uint16_t>(scale * 256), static_cast<uint16_t>(scale * 256)},
                    {static_cast<uint16_t>(scale * 256), 0},
                    {0, 0}
                }};
                break;
            }
            case BlockFace::BOTTOM: { // -Y
                normalIndex = 3;
                localCorners = {{
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>((x + scale) * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>((x + scale) * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>((z + scale) * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>((z + scale) * 256)}
                }};
                uvCorners = {{
                    {0, 0},
                    {static_cast<uint16_t>(scale * 256), 0},
                    {static_cast<uint16_t>(scale * 256), static_cast<uint16_t>(scale * 256)},
                    {0, static_cast<uint16_t>(scale * 256)}
                }};
                break;
            }
            case BlockFace::FRONT: { // +Z
                normalIndex = 4;
                localCorners = {{
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>((z + scale) * 256)},
                    {static_cast<int16_t>((x + scale) * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>((z + scale) * 256)},
                    {static_cast<int16_t>((x + scale) * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>((z + scale) * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>((z + scale) * 256)}
                }};
                uvCorners = {{
                    {0, 256},
                    {static_cast<uint16_t>(scale * 256), 256},
                    {static_cast<uint16_t>(scale * 256), 0},
                    {0, 0}
                }};
                break;
            }
            case BlockFace::BACK: { // -Z
                normalIndex = 5;
                localCorners = {{
                    {static_cast<int16_t>((x + scale) * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>((x + scale) * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>(z * 256)}
                }};
                uvCorners = {{
                    {0, 256},
                    {static_cast<uint16_t>(scale * 256), 256},
                    {static_cast<uint16_t>(scale * 256), 0},
                    {0, 0}
                }};
                break;
            }
            case BlockFace::LEFT: { // -X
                normalIndex = 1;
                localCorners = {{
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>((z + scale) * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>((z + scale) * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>(z * 256)}
                }};
                uvCorners = {{
                    {0, 256},
                    {static_cast<uint16_t>(scale * 256), 256},
                    {static_cast<uint16_t>(scale * 256), 0},
                    {0, 0}
                }};
                break;
            }
            case BlockFace::RIGHT: { // +X
                normalIndex = 0;
                localCorners = {{
                    {static_cast<int16_t>((x + scale) * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>((z + scale) * 256)},
                    {static_cast<int16_t>((x + scale) * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>((x + scale) * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>((x + scale) * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>((z + scale) * 256)}
                }};
                uvCorners = {{
                    {0, 256},
                    {static_cast<uint16_t>(scale * 256), 256},
                    {static_cast<uint16_t>(scale * 256), 0},
                    {0, 0}
                }};
                break;
            }
        }

        uint8_t packedTexSlot = static_cast<uint8_t>(textureSlot);
        uint8_t ao = 230;   // Default AO
        uint8_t light = 0;  // No light

        auto makeVertex = [&](int cornerIdx) -> PackedChunkVertex {
            return PackedChunkVertex{
                localCorners[cornerIdx][0],
                localCorners[cornerIdx][1],
                localCorners[cornerIdx][2],
                uvCorners[cornerIdx][0],
                uvCorners[cornerIdx][1],
                normalIndex,
                ao,
                light,
                packedTexSlot,
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
    // Greedy meshing for a specific face direction (produces packed vertices)
    // Merges adjacent faces with the same texture into larger quads
    void generateGreedyFaces(std::vector<PackedChunkVertex>& vertices, const Chunk& chunk,
                             const glm::vec3& chunkWorldPos, int baseX, int baseZ,
                             const BlockGetter& getWorldBlock, const BlockGetter& getSafeBlock,
                             const LightGetter& getLightLevel, BlockFace face) {

        // Determine iteration order based on face direction
        // For TOP/BOTTOM: iterate XZ slices at each Y
        // For FRONT/BACK: iterate XY slices at each Z
        // For LEFT/RIGHT: iterate YZ slices at each X

        // Face visibility mask - stores texture slot for each position, -1 if not visible
        // Using a 2D mask for each slice perpendicular to the face normal
        std::array<int, CHUNK_SIZE_X * CHUNK_SIZE_Z> maskXZ;
        std::array<int, CHUNK_SIZE_X * CHUNK_SIZE_Y> maskXY;
        std::array<int, CHUNK_SIZE_Y * CHUNK_SIZE_Z> maskYZ;

        auto getNeighborOffset = [](BlockFace f) -> glm::ivec3 {
            switch (f) {
                case BlockFace::TOP:    return {0, 1, 0};
                case BlockFace::BOTTOM: return {0, -1, 0};
                case BlockFace::FRONT:  return {0, 0, 1};
                case BlockFace::BACK:   return {0, 0, -1};
                case BlockFace::LEFT:   return {-1, 0, 0};
                case BlockFace::RIGHT:  return {1, 0, 0};
                default: return {0, 0, 0};
            }
        };

        glm::ivec3 nOffset = getNeighborOffset(face);

        // Process based on face direction
        if (face == BlockFace::TOP || face == BlockFace::BOTTOM) {
            // Iterate through Y slices - use heightmaps to skip empty regions
            int startY = (chunk.chunkMinY <= chunk.chunkMaxY) ? chunk.chunkMinY : 0;
            int endY = (chunk.chunkMinY <= chunk.chunkMaxY) ? chunk.chunkMaxY : -1;
            for (int y = startY; y <= endY; y++) {
                // Build mask for this slice
                maskXZ.fill(-1);
                for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                    for (int x = 0; x < CHUNK_SIZE_X; x++) {
                        BlockType block = chunk.getBlock(x, y, z);
                        if (block == BlockType::AIR || block == BlockType::WATER) continue;

                        int wx = baseX + x;
                        int wz = baseZ + z;
                        int ny = y + nOffset.y;

                        if (shouldRenderFace(getSafeBlock, wx, ny, wz)) {
                            BlockTextures textures = getBlockTextures(block);
                            int slot = (face == BlockFace::TOP) ? textures.faceSlots[4] : textures.faceSlots[5];
                            maskXZ[z * CHUNK_SIZE_X + x] = slot;
                        }
                    }
                }

                // Greedy merge the mask - find runs along X, then extend in Z
                for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                    for (int x = 0; x < CHUNK_SIZE_X; ) {
                        int slot = maskXZ[z * CHUNK_SIZE_X + x];
                        if (slot < 0) { x++; continue; }

                        // Find run length in X direction
                        int width = 1;
                        while (x + width < CHUNK_SIZE_X &&
                               maskXZ[z * CHUNK_SIZE_X + x + width] == slot) {
                            width++;
                        }

                        // Find run length in Z direction (same width strip)
                        int height = 1;
                        bool canExtend = true;
                        while (z + height < CHUNK_SIZE_Z && canExtend) {
                            for (int dx = 0; dx < width; dx++) {
                                if (maskXZ[(z + height) * CHUNK_SIZE_X + x + dx] != slot) {
                                    canExtend = false;
                                    break;
                                }
                            }
                            if (canExtend) height++;
                        }

                        // Clear the merged region from mask
                        for (int dz = 0; dz < height; dz++) {
                            for (int dx = 0; dx < width; dx++) {
                                maskXZ[(z + dz) * CHUNK_SIZE_X + x + dx] = -1;
                            }
                        }

                        // Add merged quad
                        addGreedyQuad(vertices, getWorldBlock, getLightLevel, baseX, baseZ,
                                     chunkWorldPos, face, slot, x, y, z, width, height);

                        x += width;
                    }
                }
            }
        }
        else if (face == BlockFace::FRONT || face == BlockFace::BACK) {
            // Iterate through Z slices - use heightmaps
            int startY = (chunk.chunkMinY <= chunk.chunkMaxY) ? chunk.chunkMinY : 0;
            int endY = (chunk.chunkMinY <= chunk.chunkMaxY) ? chunk.chunkMaxY : -1;
            for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                // Build mask for this slice
                maskXY.fill(-1);
                for (int y = startY; y <= endY; y++) {
                    for (int x = 0; x < CHUNK_SIZE_X; x++) {
                        BlockType block = chunk.getBlock(x, y, z);
                        if (block == BlockType::AIR || block == BlockType::WATER) continue;

                        int wx = baseX + x;
                        int wz = baseZ + z;
                        int nz = wz + nOffset.z;

                        if (shouldRenderFace(getSafeBlock, wx, y, nz)) {
                            BlockTextures textures = getBlockTextures(block);
                            int slot = (face == BlockFace::FRONT) ? textures.faceSlots[0] : textures.faceSlots[1];
                            maskXY[y * CHUNK_SIZE_X + x] = slot;
                        }
                    }
                }

                // Greedy merge - find runs along X, extend in Y - use heightmaps
                for (int y = startY; y <= endY; y++) {
                    for (int x = 0; x < CHUNK_SIZE_X; ) {
                        int slot = maskXY[y * CHUNK_SIZE_X + x];
                        if (slot < 0) { x++; continue; }

                        int width = 1;
                        while (x + width < CHUNK_SIZE_X &&
                               maskXY[y * CHUNK_SIZE_X + x + width] == slot) {
                            width++;
                        }

                        int height = 1;
                        bool canExtend = true;
                        while (y + height <= endY && canExtend) {
                            for (int dx = 0; dx < width; dx++) {
                                if (maskXY[(y + height) * CHUNK_SIZE_X + x + dx] != slot) {
                                    canExtend = false;
                                    break;
                                }
                            }
                            if (canExtend) height++;
                        }

                        for (int dy = 0; dy < height; dy++) {
                            for (int dx = 0; dx < width; dx++) {
                                maskXY[(y + dy) * CHUNK_SIZE_X + x + dx] = -1;
                            }
                        }

                        addGreedyQuad(vertices, getWorldBlock, getLightLevel, baseX, baseZ,
                                     chunkWorldPos, face, slot, x, y, z, width, height);

                        x += width;
                    }
                }
            }
        }
        else { // LEFT or RIGHT
            // Iterate through X slices - use heightmaps
            int startY = (chunk.chunkMinY <= chunk.chunkMaxY) ? chunk.chunkMinY : 0;
            int endY = (chunk.chunkMinY <= chunk.chunkMaxY) ? chunk.chunkMaxY : -1;
            for (int x = 0; x < CHUNK_SIZE_X; x++) {
                // Build mask for this slice
                maskYZ.fill(-1);
                for (int y = startY; y <= endY; y++) {
                    for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                        BlockType block = chunk.getBlock(x, y, z);
                        if (block == BlockType::AIR || block == BlockType::WATER) continue;

                        int wx = baseX + x;
                        int wz = baseZ + z;
                        int nx = wx + nOffset.x;

                        if (shouldRenderFace(getSafeBlock, nx, y, wz)) {
                            BlockTextures textures = getBlockTextures(block);
                            int slot = (face == BlockFace::LEFT) ? textures.faceSlots[2] : textures.faceSlots[3];
                            maskYZ[y * CHUNK_SIZE_Z + z] = slot;
                        }
                    }
                }

                // Greedy merge - find runs along Z, extend in Y - use heightmaps
                for (int y = startY; y <= endY; y++) {
                    for (int z = 0; z < CHUNK_SIZE_Z; ) {
                        int slot = maskYZ[y * CHUNK_SIZE_Z + z];
                        if (slot < 0) { z++; continue; }

                        int width = 1;
                        while (z + width < CHUNK_SIZE_Z &&
                               maskYZ[y * CHUNK_SIZE_Z + z + width] == slot) {
                            width++;
                        }

                        int height = 1;
                        bool canExtend = true;
                        while (y + height <= endY && canExtend) {
                            for (int dz = 0; dz < width; dz++) {
                                if (maskYZ[(y + height) * CHUNK_SIZE_Z + z + dz] != slot) {
                                    canExtend = false;
                                    break;
                                }
                            }
                            if (canExtend) height++;
                        }

                        for (int dy = 0; dy < height; dy++) {
                            for (int dz = 0; dz < width; dz++) {
                                maskYZ[(y + dy) * CHUNK_SIZE_Z + z + dz] = -1;
                            }
                        }

                        addGreedyQuad(vertices, getWorldBlock, getLightLevel, baseX, baseZ,
                                     chunkWorldPos, face, slot, x, y, z, width, height);

                        z += width;
                    }
                }
            }
        }
    }

    // Add a merged quad for greedy meshing (produces packed vertices)
    void addGreedyQuad(std::vector<PackedChunkVertex>& vertices, const BlockGetter& /*getWorldBlock*/,
                       const LightGetter& /*getLightLevel*/, int /*baseX*/, int /*baseZ*/,
                       const glm::vec3& /*chunkWorldPos*/, BlockFace face, int textureSlot,
                       int x, int y, int z, int width, int height) {

        // Normal index for packed format
        uint8_t normalIndex;

        // Local positions relative to chunk origin (scaled by 256 for precision)
        // Each corner has x, y, z components
        std::array<std::array<int16_t, 3>, 4> localCorners;

        // UV coordinates (8.8 fixed point - value * 256)
        std::array<std::array<uint16_t, 2>, 4> uvCorners;

        // Calculate corners based on face direction
        switch (face) {
            case BlockFace::TOP: { // +Y
                normalIndex = 2;
                localCorners = {{
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>((z + height) * 256)},
                    {static_cast<int16_t>((x + width) * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>((z + height) * 256)},
                    {static_cast<int16_t>((x + width) * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>((y + 1) * 256), static_cast<int16_t>(z * 256)}
                }};
                uvCorners = {{
                    {0, static_cast<uint16_t>(height * 256)},
                    {static_cast<uint16_t>(width * 256), static_cast<uint16_t>(height * 256)},
                    {static_cast<uint16_t>(width * 256), 0},
                    {0, 0}
                }};
                break;
            }
            case BlockFace::BOTTOM: { // -Y
                normalIndex = 3;
                localCorners = {{
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>((x + width) * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>((x + width) * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>((z + height) * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>((z + height) * 256)}
                }};
                uvCorners = {{
                    {0, 0},
                    {static_cast<uint16_t>(width * 256), 0},
                    {static_cast<uint16_t>(width * 256), static_cast<uint16_t>(height * 256)},
                    {0, static_cast<uint16_t>(height * 256)}
                }};
                break;
            }
            case BlockFace::FRONT: { // +Z
                normalIndex = 4;
                localCorners = {{
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>((z + 1) * 256)},
                    {static_cast<int16_t>((x + width) * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>((z + 1) * 256)},
                    {static_cast<int16_t>((x + width) * 256), static_cast<int16_t>((y + height) * 256), static_cast<int16_t>((z + 1) * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>((y + height) * 256), static_cast<int16_t>((z + 1) * 256)}
                }};
                uvCorners = {{
                    {0, static_cast<uint16_t>(height * 256)},
                    {static_cast<uint16_t>(width * 256), static_cast<uint16_t>(height * 256)},
                    {static_cast<uint16_t>(width * 256), 0},
                    {0, 0}
                }};
                break;
            }
            case BlockFace::BACK: { // -Z
                normalIndex = 5;
                localCorners = {{
                    {static_cast<int16_t>((x + width) * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>((y + height) * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>((x + width) * 256), static_cast<int16_t>((y + height) * 256), static_cast<int16_t>(z * 256)}
                }};
                uvCorners = {{
                    {0, static_cast<uint16_t>(height * 256)},
                    {static_cast<uint16_t>(width * 256), static_cast<uint16_t>(height * 256)},
                    {static_cast<uint16_t>(width * 256), 0},
                    {0, 0}
                }};
                break;
            }
            case BlockFace::LEFT: { // -X
                normalIndex = 1;
                localCorners = {{
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>((z + width) * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>((y + height) * 256), static_cast<int16_t>((z + width) * 256)},
                    {static_cast<int16_t>(x * 256), static_cast<int16_t>((y + height) * 256), static_cast<int16_t>(z * 256)}
                }};
                uvCorners = {{
                    {0, static_cast<uint16_t>(height * 256)},
                    {static_cast<uint16_t>(width * 256), static_cast<uint16_t>(height * 256)},
                    {static_cast<uint16_t>(width * 256), 0},
                    {0, 0}
                }};
                break;
            }
            case BlockFace::RIGHT: { // +X
                normalIndex = 0;
                localCorners = {{
                    {static_cast<int16_t>((x + 1) * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>((z + width) * 256)},
                    {static_cast<int16_t>((x + 1) * 256), static_cast<int16_t>(y * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>((x + 1) * 256), static_cast<int16_t>((y + height) * 256), static_cast<int16_t>(z * 256)},
                    {static_cast<int16_t>((x + 1) * 256), static_cast<int16_t>((y + height) * 256), static_cast<int16_t>((z + width) * 256)}
                }};
                uvCorners = {{
                    {0, static_cast<uint16_t>(height * 256)},
                    {static_cast<uint16_t>(width * 256), static_cast<uint16_t>(height * 256)},
                    {static_cast<uint16_t>(width * 256), 0},
                    {0, 0}
                }};
                break;
            }
        }

        // Pack the texture slot
        uint8_t packedTexSlot = static_cast<uint8_t>(textureSlot);

        // AO and light (0-255 range) - use defaults for greedy merged quads
        uint8_t ao = 230;   // Slightly darker than max (0.9 * 255)
        uint8_t light = 0;  // No light for now

        // Create 6 packed vertices (2 triangles)
        auto makeVertex = [&](int cornerIdx) -> PackedChunkVertex {
            return PackedChunkVertex{
                localCorners[cornerIdx][0],
                localCorners[cornerIdx][1],
                localCorners[cornerIdx][2],
                uvCorners[cornerIdx][0],
                uvCorners[cornerIdx][1],
                normalIndex,
                ao,
                light,
                packedTexSlot,
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

    // Check if we should render a face (neighbor is air or transparent) - world coordinates
    bool shouldRenderFace(const BlockGetter& getBlock, int wx, int wy, int wz) const {
        if (wy < 0) return false;
        if (wy >= CHUNK_SIZE_Y) return true;

        BlockType neighbor = getBlock(wx, wy, wz);
        return isBlockTransparent(neighbor);
    }

    // Add water block with variable height based on water level - uses world coordinates
    // SIMPLE RULE: Only render faces at the actual boundary of the water body
    // - TOP: render if no water above (this is the visible water surface)
    // - SIDES: render only if neighbor is AIR (edge of water body)
    // - BOTTOM: render only if below is AIR (rare, like floating water)
    // - NEVER render faces between adjacent water blocks (no internal geometry)
    // Helper to get water height at a world position for smooth interpolation
    float getWaterHeightAt(const BlockGetter& getBlock, int wx, int wy, int wz) {
        BlockType block = getBlock(wx, wy, wz);
        if (block == BlockType::WATER) {
            // Check if there's water above - if so, this is submerged (full height)
            BlockType above = getBlock(wx, wy + 1, wz);
            if (above == BlockType::WATER) {
                return 1.0f;
            }
            return 0.875f;  // Surface water slightly below full
        }
        // Check if there's water below (we're above a water block)
        BlockType below = getBlock(wx, wy - 1, wz);
        if (below == BlockType::WATER) {
            return 0.0f;  // Transition point
        }
        return -1.0f;  // No water here
    }

    void addWaterBlock(std::vector<ChunkVertex>& vertices, const Chunk& chunk,
                       int bx, int by, int bz, const glm::vec3& pos, int textureSlot,
                       const BlockGetter& getBlock, int wx, int wz) {

        uint8_t waterLevel = chunk.getWaterLevel(bx, by, bz);
        if (waterLevel == 0) {
            waterLevel = WATER_SOURCE;
        }

        // Get UV coordinates
        glm::vec4 uv = TextureAtlas::getUV(textureSlot);
        glm::vec2 texSlotBase(uv.x, uv.y);

        glm::vec3 normal;
        float ao = 1.0f;
        float light = 0.0f;

        // Check water neighbors for smooth height interpolation
        bool waterAbove = (by + 1 < CHUNK_SIZE_Y) && (chunk.getBlock(bx, by + 1, bz) == BlockType::WATER);
        bool waterBelow = (by > 0) && (chunk.getBlock(bx, by - 1, bz) == BlockType::WATER);
                (void)waterBelow; // May be used in future for water column optimization

        // If water above, we're submerged - use full height
        if (waterAbove) {
            // Submerged water - no top face, sides go full height
            float topY = 1.0f;

            auto shouldRenderWaterSide = [&](int worldNeighborX, int worldNeighborZ) -> bool {
                BlockType neighbor = getBlock(worldNeighborX, by, worldNeighborZ);
                return neighbor != BlockType::WATER;
            };

            // Render side faces for submerged water
            if (shouldRenderWaterSide(wx, wz + 1)) {
                normal = glm::vec3(0, 0, 1);
                glm::vec3 corners[4] = {
                    pos + glm::vec3(0, 0, 1), pos + glm::vec3(1, 0, 1),
                    pos + glm::vec3(1, topY, 1), pos + glm::vec3(0, topY, 1)
                };
                vertices.push_back({ corners[0], {0.0f, 1.0f}, normal, ao, light, texSlotBase });
                vertices.push_back({ corners[1], {1.0f, 1.0f}, normal, ao, light, texSlotBase });
                vertices.push_back({ corners[2], {1.0f, 0.0f}, normal, ao, light, texSlotBase });
                vertices.push_back({ corners[2], {1.0f, 0.0f}, normal, ao, light, texSlotBase });
                vertices.push_back({ corners[3], {0.0f, 0.0f}, normal, ao, light, texSlotBase });
                vertices.push_back({ corners[0], {0.0f, 1.0f}, normal, ao, light, texSlotBase });
            }
            if (shouldRenderWaterSide(wx, wz - 1)) {
                normal = glm::vec3(0, 0, -1);
                glm::vec3 corners[4] = {
                    pos + glm::vec3(1, 0, 0), pos + glm::vec3(0, 0, 0),
                    pos + glm::vec3(0, topY, 0), pos + glm::vec3(1, topY, 0)
                };
                vertices.push_back({ corners[0], {0.0f, 1.0f}, normal, ao, light, texSlotBase });
                vertices.push_back({ corners[1], {1.0f, 1.0f}, normal, ao, light, texSlotBase });
                vertices.push_back({ corners[2], {1.0f, 0.0f}, normal, ao, light, texSlotBase });
                vertices.push_back({ corners[2], {1.0f, 0.0f}, normal, ao, light, texSlotBase });
                vertices.push_back({ corners[3], {0.0f, 0.0f}, normal, ao, light, texSlotBase });
                vertices.push_back({ corners[0], {0.0f, 1.0f}, normal, ao, light, texSlotBase });
            }
            if (shouldRenderWaterSide(wx - 1, wz)) {
                normal = glm::vec3(-1, 0, 0);
                glm::vec3 corners[4] = {
                    pos + glm::vec3(0, 0, 0), pos + glm::vec3(0, 0, 1),
                    pos + glm::vec3(0, topY, 1), pos + glm::vec3(0, topY, 0)
                };
                vertices.push_back({ corners[0], {0.0f, 1.0f}, normal, ao, light, texSlotBase });
                vertices.push_back({ corners[1], {1.0f, 1.0f}, normal, ao, light, texSlotBase });
                vertices.push_back({ corners[2], {1.0f, 0.0f}, normal, ao, light, texSlotBase });
                vertices.push_back({ corners[2], {1.0f, 0.0f}, normal, ao, light, texSlotBase });
                vertices.push_back({ corners[3], {0.0f, 0.0f}, normal, ao, light, texSlotBase });
                vertices.push_back({ corners[0], {0.0f, 1.0f}, normal, ao, light, texSlotBase });
            }
            if (shouldRenderWaterSide(wx + 1, wz)) {
                normal = glm::vec3(1, 0, 0);
                glm::vec3 corners[4] = {
                    pos + glm::vec3(1, 0, 1), pos + glm::vec3(1, 0, 0),
                    pos + glm::vec3(1, topY, 0), pos + glm::vec3(1, topY, 1)
                };
                vertices.push_back({ corners[0], {0.0f, 1.0f}, normal, ao, light, texSlotBase });
                vertices.push_back({ corners[1], {1.0f, 1.0f}, normal, ao, light, texSlotBase });
                vertices.push_back({ corners[2], {1.0f, 0.0f}, normal, ao, light, texSlotBase });
                vertices.push_back({ corners[2], {1.0f, 0.0f}, normal, ao, light, texSlotBase });
                vertices.push_back({ corners[3], {0.0f, 0.0f}, normal, ao, light, texSlotBase });
                vertices.push_back({ corners[0], {0.0f, 1.0f}, normal, ao, light, texSlotBase });
            }
            return;
        }

        // ============================================
        // SURFACE WATER - Calculate smooth sloped heights per corner
        // ============================================

        // Sample water presence at each corner and neighbors for smooth interpolation
        // Corner order: (-X,-Z), (+X,-Z), (+X,+Z), (-X,+Z) relative to block center

        // Get heights at the 4 corners by averaging neighboring blocks
        auto getCornerHeight = [&](int cornerX, int cornerZ) -> float {
            // cornerX/Z are 0 or 1 (local block coords)
            // We sample the 4 blocks that share this corner
            int worldCornerX = wx + cornerX;
            int worldCornerZ = wz + cornerZ;

            float totalHeight = 0.0f;
            int waterCount = 0;
            bool hasWaterBelow = false;

            // Sample the 4 blocks sharing this corner
            for (int dx = -1; dx <= 0; dx++) {
                for (int dz = -1; dz <= 0; dz++) {
                    int sampleX = worldCornerX + dx;
                    int sampleZ = worldCornerZ + dz;

                    BlockType block = getBlock(sampleX, by, sampleZ);
                    if (block == BlockType::WATER) {
                        // Check if this water has water above (submerged)
                        BlockType above = getBlock(sampleX, by + 1, sampleZ);
                        if (above == BlockType::WATER) {
                            totalHeight += 1.0f;
                        } else {
                            totalHeight += 0.9f;  // Surface water
                        }
                        waterCount++;
                    } else if (block == BlockType::AIR) {
                        // Check if there's water below - creates slope
                        BlockType below = getBlock(sampleX, by - 1, sampleZ);
                        if (below == BlockType::WATER) {
                            hasWaterBelow = true;
                        }
                    }
                }
            }

            if (waterCount == 0) {
                // No water at this corner - check if we should slope down
                if (hasWaterBelow) {
                    return 0.0f;  // Slope down to water below
                }
                return 0.9f;  // Default surface height
            }

            // Average the heights, but if some neighbors are air, blend towards lower
            float avgHeight = totalHeight / waterCount;

            // If not all 4 corners have water, slope down slightly
            if (waterCount < 4) {
                avgHeight = avgHeight * (0.5f + 0.5f * (waterCount / 4.0f));
            }

            return avgHeight;
        };

        // Calculate height at each corner
        // Corners: 0=(-X,-Z), 1=(+X,-Z), 2=(+X,+Z), 3=(-X,+Z)
        float h00 = getCornerHeight(0, 0);  // -X, -Z corner
        float h10 = getCornerHeight(1, 0);  // +X, -Z corner
        float h11 = getCornerHeight(1, 1);  // +X, +Z corner
        float h01 = getCornerHeight(0, 1);  // -X, +Z corner

        // Clamp heights
        h00 = std::max(0.1f, std::min(1.0f, h00));
        h10 = std::max(0.1f, std::min(1.0f, h10));
        h11 = std::max(0.1f, std::min(1.0f, h11));
        h01 = std::max(0.1f, std::min(1.0f, h01));

        // Calculate normal from the slope
        glm::vec3 v1 = glm::vec3(1, h10 - h00, 0);
        glm::vec3 v2 = glm::vec3(0, h01 - h00, 1);
        normal = glm::normalize(glm::cross(v2, v1));

        // TOP face with smooth heights
        glm::vec3 corners[4] = {
            pos + glm::vec3(0, h01, 1),  // -X, +Z
            pos + glm::vec3(1, h11, 1),  // +X, +Z
            pos + glm::vec3(1, h10, 0),  // +X, -Z
            pos + glm::vec3(0, h00, 0)   // -X, -Z
        };

        vertices.push_back({ corners[0], {0.0f, 1.0f}, normal, ao, light, texSlotBase });
        vertices.push_back({ corners[1], {1.0f, 1.0f}, normal, ao, light, texSlotBase });
        vertices.push_back({ corners[2], {1.0f, 0.0f}, normal, ao, light, texSlotBase });
        vertices.push_back({ corners[2], {1.0f, 0.0f}, normal, ao, light, texSlotBase });
        vertices.push_back({ corners[3], {0.0f, 0.0f}, normal, ao, light, texSlotBase });
        vertices.push_back({ corners[0], {0.0f, 1.0f}, normal, ao, light, texSlotBase });

        // SIDE FACES - use the sloped heights at edges
        auto shouldRenderWaterSide = [&](int worldNeighborX, int worldNeighborZ) -> bool {
            BlockType neighbor = getBlock(worldNeighborX, by, worldNeighborZ);
            return neighbor != BlockType::WATER;
        };

        // Front face (+Z)
        if (shouldRenderWaterSide(wx, wz + 1)) {
            normal = glm::vec3(0, 0, 1);
            glm::vec3 sideCorners[4] = {
                pos + glm::vec3(0, 0, 1),
                pos + glm::vec3(1, 0, 1),
                pos + glm::vec3(1, h11, 1),
                pos + glm::vec3(0, h01, 1)
            };
            vertices.push_back({ sideCorners[0], {0.0f, 1.0f}, normal, ao, light, texSlotBase });
            vertices.push_back({ sideCorners[1], {1.0f, 1.0f}, normal, ao, light, texSlotBase });
            vertices.push_back({ sideCorners[2], {1.0f, 0.0f}, normal, ao, light, texSlotBase });
            vertices.push_back({ sideCorners[2], {1.0f, 0.0f}, normal, ao, light, texSlotBase });
            vertices.push_back({ sideCorners[3], {0.0f, 0.0f}, normal, ao, light, texSlotBase });
            vertices.push_back({ sideCorners[0], {0.0f, 1.0f}, normal, ao, light, texSlotBase });
        }

        // Back face (-Z)
        if (shouldRenderWaterSide(wx, wz - 1)) {
            normal = glm::vec3(0, 0, -1);
            glm::vec3 sideCorners[4] = {
                pos + glm::vec3(1, 0, 0),
                pos + glm::vec3(0, 0, 0),
                pos + glm::vec3(0, h00, 0),
                pos + glm::vec3(1, h10, 0)
            };
            vertices.push_back({ sideCorners[0], {0.0f, 1.0f}, normal, ao, light, texSlotBase });
            vertices.push_back({ sideCorners[1], {1.0f, 1.0f}, normal, ao, light, texSlotBase });
            vertices.push_back({ sideCorners[2], {1.0f, 0.0f}, normal, ao, light, texSlotBase });
            vertices.push_back({ sideCorners[2], {1.0f, 0.0f}, normal, ao, light, texSlotBase });
            vertices.push_back({ sideCorners[3], {0.0f, 0.0f}, normal, ao, light, texSlotBase });
            vertices.push_back({ sideCorners[0], {0.0f, 1.0f}, normal, ao, light, texSlotBase });
        }

        // Left face (-X)
        if (shouldRenderWaterSide(wx - 1, wz)) {
            normal = glm::vec3(-1, 0, 0);
            glm::vec3 sideCorners[4] = {
                pos + glm::vec3(0, 0, 0),
                pos + glm::vec3(0, 0, 1),
                pos + glm::vec3(0, h01, 1),
                pos + glm::vec3(0, h00, 0)
            };
            vertices.push_back({ sideCorners[0], {0.0f, 1.0f}, normal, ao, light, texSlotBase });
            vertices.push_back({ sideCorners[1], {1.0f, 1.0f}, normal, ao, light, texSlotBase });
            vertices.push_back({ sideCorners[2], {1.0f, 0.0f}, normal, ao, light, texSlotBase });
            vertices.push_back({ sideCorners[2], {1.0f, 0.0f}, normal, ao, light, texSlotBase });
            vertices.push_back({ sideCorners[3], {0.0f, 0.0f}, normal, ao, light, texSlotBase });
            vertices.push_back({ sideCorners[0], {0.0f, 1.0f}, normal, ao, light, texSlotBase });
        }

        // Right face (+X)
        if (shouldRenderWaterSide(wx + 1, wz)) {
            normal = glm::vec3(1, 0, 0);
            glm::vec3 sideCorners[4] = {
                pos + glm::vec3(1, 0, 1),
                pos + glm::vec3(1, 0, 0),
                pos + glm::vec3(1, h10, 0),
                pos + glm::vec3(1, h11, 1)
            };
            vertices.push_back({ sideCorners[0], {0.0f, 1.0f}, normal, ao, light, texSlotBase });
            vertices.push_back({ sideCorners[1], {1.0f, 1.0f}, normal, ao, light, texSlotBase });
            vertices.push_back({ sideCorners[2], {1.0f, 0.0f}, normal, ao, light, texSlotBase });
            vertices.push_back({ sideCorners[2], {1.0f, 0.0f}, normal, ao, light, texSlotBase });
            vertices.push_back({ sideCorners[3], {0.0f, 0.0f}, normal, ao, light, texSlotBase });
            vertices.push_back({ sideCorners[0], {0.0f, 1.0f}, normal, ao, light, texSlotBase });
        }
    }

    // Check if a block position is solid (for AO calculation) - world coordinates
    bool isSolidForAO(const BlockGetter& getBlock, int wx, int wy, int wz) const {
        if (wy < 0) return true;  // Below world is solid
        if (wy >= CHUNK_SIZE_Y) return false;  // Above world is air
        return isBlockSolid(getBlock(wx, wy, wz));
    }

    // Calculate vertex AO based on 3 neighbors (side1, side2, corner)
    // Returns value from 0 (darkest) to 3 (brightest)
    int calculateVertexAO(bool side1, bool side2, bool corner) const {
        if (side1 && side2) {
            return 0; // Both sides blocked = darkest
        }
        return 3 - (side1 + side2 + corner);
    }

    // Convert AO level (0-3) to factor (0.0-1.0)
    float aoToFactor(int ao) const {
        // AO values: 0=darkest, 3=brightest
        static const float aoFactors[4] = { 0.4f, 0.6f, 0.8f, 1.0f };
        return aoFactors[ao];
    }

    // Add a face with per-vertex ambient occlusion and light level - uses world coordinates
    void addFaceWithAO(std::vector<ChunkVertex>& vertices, const BlockGetter& getBlock,
                       const LightGetter& getLight, int wx, int wy, int wz, const glm::vec3& pos,
                       BlockFace face, int textureSlot) {

        // Get UV coordinates from atlas - now we store base separately for greedy meshing tiling
        glm::vec4 uv = TextureAtlas::getUV(textureSlot);
        glm::vec2 texSlotBase(uv.x, uv.y);  // Base UV of this texture slot
        // Local UVs go from 0 to 1 for a single block face (tiled by shader for merged quads)

        glm::vec3 normal;
        std::array<glm::vec3, 4> corners;  // 4 corners of the face
        std::array<float, 4> aoFactors;    // AO for each corner
        std::array<float, 4> lightLevels;  // Light level for each corner

        // Helper to get light at a position (simplified - just sample the air block)
        auto getSmoothLight = [&getLight](int x, int y, int z) -> float {
            // Just sample the single air block at this position - much faster
            return getLight(x, y, z) / 15.0f;
        };

        // Calculate AO and vertices based on face direction
        // Corner order: bottom-left, bottom-right, top-right, top-left
        switch (face) {
            case BlockFace::FRONT: { // +Z
                normal = glm::vec3(0, 0, 1);
                int nz = wz + 1;
                corners = {{
                    pos + glm::vec3(0, 0, 1),  // bottom-left
                    pos + glm::vec3(1, 0, 1),  // bottom-right
                    pos + glm::vec3(1, 1, 1),  // top-right
                    pos + glm::vec3(0, 1, 1)   // top-left
                }};
                // AO for each corner - check neighbors in the +Z plane
                aoFactors[0] = aoToFactor(calculateVertexAO(
                    isSolidForAO(getBlock, wx-1, wy, nz), isSolidForAO(getBlock, wx, wy-1, nz),
                    isSolidForAO(getBlock, wx-1, wy-1, nz)));
                aoFactors[1] = aoToFactor(calculateVertexAO(
                    isSolidForAO(getBlock, wx+1, wy, nz), isSolidForAO(getBlock, wx, wy-1, nz),
                    isSolidForAO(getBlock, wx+1, wy-1, nz)));
                aoFactors[2] = aoToFactor(calculateVertexAO(
                    isSolidForAO(getBlock, wx+1, wy, nz), isSolidForAO(getBlock, wx, wy+1, nz),
                    isSolidForAO(getBlock, wx+1, wy+1, nz)));
                aoFactors[3] = aoToFactor(calculateVertexAO(
                    isSolidForAO(getBlock, wx-1, wy, nz), isSolidForAO(getBlock, wx, wy+1, nz),
                    isSolidForAO(getBlock, wx-1, wy+1, nz)));
                // Light levels for each corner
                lightLevels[0] = getSmoothLight(wx, wy, nz);
                lightLevels[1] = getSmoothLight(wx+1, wy, nz);
                lightLevels[2] = getSmoothLight(wx+1, wy+1, nz);
                lightLevels[3] = getSmoothLight(wx, wy+1, nz);
                break;
            }

            case BlockFace::BACK: { // -Z
                normal = glm::vec3(0, 0, -1);
                int nz = wz - 1;
                corners = {{
                    pos + glm::vec3(1, 0, 0),  // bottom-left (from back view)
                    pos + glm::vec3(0, 0, 0),  // bottom-right
                    pos + glm::vec3(0, 1, 0),  // top-right
                    pos + glm::vec3(1, 1, 0)   // top-left
                }};
                aoFactors[0] = aoToFactor(calculateVertexAO(
                    isSolidForAO(getBlock, wx+1, wy, nz), isSolidForAO(getBlock, wx, wy-1, nz),
                    isSolidForAO(getBlock, wx+1, wy-1, nz)));
                aoFactors[1] = aoToFactor(calculateVertexAO(
                    isSolidForAO(getBlock, wx-1, wy, nz), isSolidForAO(getBlock, wx, wy-1, nz),
                    isSolidForAO(getBlock, wx-1, wy-1, nz)));
                aoFactors[2] = aoToFactor(calculateVertexAO(
                    isSolidForAO(getBlock, wx-1, wy, nz), isSolidForAO(getBlock, wx, wy+1, nz),
                    isSolidForAO(getBlock, wx-1, wy+1, nz)));
                aoFactors[3] = aoToFactor(calculateVertexAO(
                    isSolidForAO(getBlock, wx+1, wy, nz), isSolidForAO(getBlock, wx, wy+1, nz),
                    isSolidForAO(getBlock, wx+1, wy+1, nz)));
                lightLevels[0] = getSmoothLight(wx+1, wy, nz);
                lightLevels[1] = getSmoothLight(wx, wy, nz);
                lightLevels[2] = getSmoothLight(wx, wy+1, nz);
                lightLevels[3] = getSmoothLight(wx+1, wy+1, nz);
                break;
            }

            case BlockFace::LEFT: { // -X
                normal = glm::vec3(-1, 0, 0);
                int nx = wx - 1;
                corners = {{
                    pos + glm::vec3(0, 0, 0),  // bottom-left
                    pos + glm::vec3(0, 0, 1),  // bottom-right
                    pos + glm::vec3(0, 1, 1),  // top-right
                    pos + glm::vec3(0, 1, 0)   // top-left
                }};
                aoFactors[0] = aoToFactor(calculateVertexAO(
                    isSolidForAO(getBlock, nx, wy, wz-1), isSolidForAO(getBlock, nx, wy-1, wz),
                    isSolidForAO(getBlock, nx, wy-1, wz-1)));
                aoFactors[1] = aoToFactor(calculateVertexAO(
                    isSolidForAO(getBlock, nx, wy, wz+1), isSolidForAO(getBlock, nx, wy-1, wz),
                    isSolidForAO(getBlock, nx, wy-1, wz+1)));
                aoFactors[2] = aoToFactor(calculateVertexAO(
                    isSolidForAO(getBlock, nx, wy, wz+1), isSolidForAO(getBlock, nx, wy+1, wz),
                    isSolidForAO(getBlock, nx, wy+1, wz+1)));
                aoFactors[3] = aoToFactor(calculateVertexAO(
                    isSolidForAO(getBlock, nx, wy, wz-1), isSolidForAO(getBlock, nx, wy+1, wz),
                    isSolidForAO(getBlock, nx, wy+1, wz-1)));
                lightLevels[0] = getSmoothLight(nx, wy, wz);
                lightLevels[1] = getSmoothLight(nx, wy, wz+1);
                lightLevels[2] = getSmoothLight(nx, wy+1, wz+1);
                lightLevels[3] = getSmoothLight(nx, wy+1, wz);
                break;
            }

            case BlockFace::RIGHT: { // +X
                normal = glm::vec3(1, 0, 0);
                int nx = wx + 1;
                corners = {{
                    pos + glm::vec3(1, 0, 1),  // bottom-left (from right view)
                    pos + glm::vec3(1, 0, 0),  // bottom-right
                    pos + glm::vec3(1, 1, 0),  // top-right
                    pos + glm::vec3(1, 1, 1)   // top-left
                }};
                aoFactors[0] = aoToFactor(calculateVertexAO(
                    isSolidForAO(getBlock, nx, wy, wz+1), isSolidForAO(getBlock, nx, wy-1, wz),
                    isSolidForAO(getBlock, nx, wy-1, wz+1)));
                aoFactors[1] = aoToFactor(calculateVertexAO(
                    isSolidForAO(getBlock, nx, wy, wz-1), isSolidForAO(getBlock, nx, wy-1, wz),
                    isSolidForAO(getBlock, nx, wy-1, wz-1)));
                aoFactors[2] = aoToFactor(calculateVertexAO(
                    isSolidForAO(getBlock, nx, wy, wz-1), isSolidForAO(getBlock, nx, wy+1, wz),
                    isSolidForAO(getBlock, nx, wy+1, wz-1)));
                aoFactors[3] = aoToFactor(calculateVertexAO(
                    isSolidForAO(getBlock, nx, wy, wz+1), isSolidForAO(getBlock, nx, wy+1, wz),
                    isSolidForAO(getBlock, nx, wy+1, wz+1)));
                lightLevels[0] = getSmoothLight(nx, wy, wz+1);
                lightLevels[1] = getSmoothLight(nx, wy, wz);
                lightLevels[2] = getSmoothLight(nx, wy+1, wz);
                lightLevels[3] = getSmoothLight(nx, wy+1, wz+1);
                break;
            }

            case BlockFace::TOP: { // +Y
                normal = glm::vec3(0, 1, 0);
                int ny = wy + 1;
                corners = {{
                    pos + glm::vec3(0, 1, 1),  // front-left
                    pos + glm::vec3(1, 1, 1),  // front-right
                    pos + glm::vec3(1, 1, 0),  // back-right
                    pos + glm::vec3(0, 1, 0)   // back-left
                }};
                aoFactors[0] = aoToFactor(calculateVertexAO(
                    isSolidForAO(getBlock, wx-1, ny, wz), isSolidForAO(getBlock, wx, ny, wz+1),
                    isSolidForAO(getBlock, wx-1, ny, wz+1)));
                aoFactors[1] = aoToFactor(calculateVertexAO(
                    isSolidForAO(getBlock, wx+1, ny, wz), isSolidForAO(getBlock, wx, ny, wz+1),
                    isSolidForAO(getBlock, wx+1, ny, wz+1)));
                aoFactors[2] = aoToFactor(calculateVertexAO(
                    isSolidForAO(getBlock, wx+1, ny, wz), isSolidForAO(getBlock, wx, ny, wz-1),
                    isSolidForAO(getBlock, wx+1, ny, wz-1)));
                aoFactors[3] = aoToFactor(calculateVertexAO(
                    isSolidForAO(getBlock, wx-1, ny, wz), isSolidForAO(getBlock, wx, ny, wz-1),
                    isSolidForAO(getBlock, wx-1, ny, wz-1)));
                lightLevels[0] = getSmoothLight(wx, ny, wz+1);
                lightLevels[1] = getSmoothLight(wx+1, ny, wz+1);
                lightLevels[2] = getSmoothLight(wx+1, ny, wz);
                lightLevels[3] = getSmoothLight(wx, ny, wz);
                break;
            }

            case BlockFace::BOTTOM: { // -Y
                normal = glm::vec3(0, -1, 0);
                int ny = wy - 1;
                corners = {{
                    pos + glm::vec3(0, 0, 0),  // back-left
                    pos + glm::vec3(1, 0, 0),  // back-right
                    pos + glm::vec3(1, 0, 1),  // front-right
                    pos + glm::vec3(0, 0, 1)   // front-left
                }};
                aoFactors[0] = aoToFactor(calculateVertexAO(
                    isSolidForAO(getBlock, wx-1, ny, wz), isSolidForAO(getBlock, wx, ny, wz-1),
                    isSolidForAO(getBlock, wx-1, ny, wz-1)));
                aoFactors[1] = aoToFactor(calculateVertexAO(
                    isSolidForAO(getBlock, wx+1, ny, wz), isSolidForAO(getBlock, wx, ny, wz-1),
                    isSolidForAO(getBlock, wx+1, ny, wz-1)));
                aoFactors[2] = aoToFactor(calculateVertexAO(
                    isSolidForAO(getBlock, wx+1, ny, wz), isSolidForAO(getBlock, wx, ny, wz+1),
                    isSolidForAO(getBlock, wx+1, ny, wz+1)));
                aoFactors[3] = aoToFactor(calculateVertexAO(
                    isSolidForAO(getBlock, wx-1, ny, wz), isSolidForAO(getBlock, wx, ny, wz+1),
                    isSolidForAO(getBlock, wx-1, ny, wz+1)));
                lightLevels[0] = getSmoothLight(wx, ny, wz);
                lightLevels[1] = getSmoothLight(wx+1, ny, wz);
                lightLevels[2] = getSmoothLight(wx+1, ny, wz+1);
                lightLevels[3] = getSmoothLight(wx, ny, wz+1);
                break;
            }
        }

        // Local UV coordinates for the 4 corners (0-1 range, tiled by shader for merged quads)
        // Corner order: bottom-left, bottom-right, top-right, top-left
        std::array<glm::vec2, 4> uvCorners = {{
            {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}
        }};

        // Fix anisotropy - flip diagonal if needed to avoid visual artifacts
        // This ensures AO gradients look correct across the quad
        bool flipDiagonal = (aoFactors[0] + aoFactors[2]) < (aoFactors[1] + aoFactors[3]);

        if (flipDiagonal) {
            // Triangle 1: 1, 2, 3
            vertices.push_back({ corners[1], uvCorners[1], normal, aoFactors[1], lightLevels[1], texSlotBase });
            vertices.push_back({ corners[2], uvCorners[2], normal, aoFactors[2], lightLevels[2], texSlotBase });
            vertices.push_back({ corners[3], uvCorners[3], normal, aoFactors[3], lightLevels[3], texSlotBase });
            // Triangle 2: 3, 0, 1
            vertices.push_back({ corners[3], uvCorners[3], normal, aoFactors[3], lightLevels[3], texSlotBase });
            vertices.push_back({ corners[0], uvCorners[0], normal, aoFactors[0], lightLevels[0], texSlotBase });
            vertices.push_back({ corners[1], uvCorners[1], normal, aoFactors[1], lightLevels[1], texSlotBase });
        } else {
            // Triangle 1: 0, 1, 2
            vertices.push_back({ corners[0], uvCorners[0], normal, aoFactors[0], lightLevels[0], texSlotBase });
            vertices.push_back({ corners[1], uvCorners[1], normal, aoFactors[1], lightLevels[1], texSlotBase });
            vertices.push_back({ corners[2], uvCorners[2], normal, aoFactors[2], lightLevels[2], texSlotBase });
            // Triangle 2: 2, 3, 0
            vertices.push_back({ corners[2], uvCorners[2], normal, aoFactors[2], lightLevels[2], texSlotBase });
            vertices.push_back({ corners[3], uvCorners[3], normal, aoFactors[3], lightLevels[3], texSlotBase });
            vertices.push_back({ corners[0], uvCorners[0], normal, aoFactors[0], lightLevels[0], texSlotBase });
        }
    }

    // Upload solid geometry (packed format) to GPU for a specific LOD level
    // Uses capacity-based approach: reuses buffer if data fits, otherwise allocates with headroom
    void uploadToGPU(const std::vector<PackedChunkVertex>& vertices, int lodLevel = 0) {
        lodLevel = std::max(0, std::min(lodLevel, LOD_LEVELS - 1));
        LODMesh& lod = lodMeshes[lodLevel];

        if (vertices.empty()) {
            lod.vertexCount = 0;
            return;
        }

        lod.vertexCount = static_cast<int>(vertices.size());
        GLsizeiptr dataSize = vertices.size() * sizeof(PackedChunkVertex);

        // ============================================================
        // PERSISTENT MAPPED BUFFER PATH - non-blocking
        // ============================================================
        if (g_usePersistentMapping && lod.mappedPtr != nullptr && dataSize <= lod.capacity) {
            // Non-blocking check - if GPU isn't ready, wait (can't orphan glBufferStorage)
            if (!lod.isGPUReady()) {
                if (!lod.waitForGPU()) {
                    // GPU severely behind - fall through to recreate buffer
                    goto recreate_buffer_gpu;
                }
            }
            std::memcpy(lod.mappedPtr, vertices.data(), dataSize);
            lod.signalCPUDone();
            return;
        }

    recreate_buffer_gpu:
        // ============================================================
        // BUFFER REALLOCATION NEEDED
        // ============================================================
        // Need larger buffer or first-time creation

        // Clean up old persistent mapping if exists
        if (lod.mappedPtr != nullptr && lod.VBO != 0) {
            if (!lod.waitForGPU()) {
                // GPU severely behind - force cleanup anyway
                if (lod.fence != nullptr) {
                    glDeleteSync(lod.fence);
                    lod.fence = nullptr;
                }
            }
            glBindBuffer(GL_ARRAY_BUFFER, lod.VBO);
            glUnmapBuffer(GL_ARRAY_BUFFER);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            lod.mappedPtr = nullptr;
        }

        // Delete old buffer if exists (can't resize glBufferStorage)
        if (lod.VBO != 0) {
            glDeleteBuffers(1, &lod.VBO);
            lod.VBO = 0;
        }

        // Calculate capacity with headroom
        GLsizeiptr newCapacity = static_cast<GLsizeiptr>(dataSize * 1.5);  // 50% headroom for growth

        // Generate new buffer
        glGenBuffers(1, &lod.VBO);
        glBindBuffer(GL_ARRAY_BUFFER, lod.VBO);

        if (g_usePersistentMapping) {
            // Use glBufferStorage for persistent mapping
            GLbitfield storageFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
            glBufferStorage(GL_ARRAY_BUFFER, newCapacity, nullptr, storageFlags);

            // Get persistent mapped pointer
            GLbitfield mapFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
            lod.mappedPtr = glMapBufferRange(GL_ARRAY_BUFFER, 0, newCapacity, mapFlags);

            if (lod.mappedPtr != nullptr) {
                // Copy data via persistent mapping
                std::memcpy(lod.mappedPtr, vertices.data(), dataSize);
                lod.signalCPUDone();
            } else {
                // Fallback if mapping failed
                glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, vertices.data());
            }
        } else {
            // Traditional path
            glBufferData(GL_ARRAY_BUFFER, newCapacity, nullptr, GL_DYNAMIC_DRAW);
            glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, vertices.data());
        }

        lod.capacity = newCapacity;

        // Setup VAO if needed
        if (lod.VAO == 0) {
            glGenVertexArrays(1, &lod.VAO);
        }

        glBindVertexArray(lod.VAO);
        glBindBuffer(GL_ARRAY_BUFFER, lod.VBO);

        // Packed vertex layout (16 bytes total):
        // Position: 3 x int16 at offset 0 (6 bytes) - scaled by 1/256 in shader
        glVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, sizeof(PackedChunkVertex),
                              (void*)offsetof(PackedChunkVertex, x));
        glEnableVertexAttribArray(0);

        // TexCoord: 2 x uint16 at offset 6 (4 bytes) - 8.8 fixed point
        glVertexAttribPointer(1, 2, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(PackedChunkVertex),
                              (void*)offsetof(PackedChunkVertex, u));
        glEnableVertexAttribArray(1);

        // Packed data: normalIndex, ao, light, texSlot as 4 bytes (passed as uvec4)
        glVertexAttribIPointer(2, 4, GL_UNSIGNED_BYTE, sizeof(PackedChunkVertex),
                               (void*)offsetof(PackedChunkVertex, normalIndex));
        glEnableVertexAttribArray(2);

        // Biome data: 2 unsigned bytes (biomeTemp, biomeHumid) for grass/foliage tinting
        glVertexAttribIPointer(3, 2, GL_UNSIGNED_BYTE, sizeof(PackedChunkVertex),
                               (void*)offsetof(PackedChunkVertex, biomeTemp));
        glEnableVertexAttribArray(3);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    // Upload water geometry to GPU with smart buffer reuse
    void uploadWaterToGPU(const std::vector<ChunkVertex>& vertices) {
        if (vertices.empty()) {
            // Don't delete buffers - they might be reused
            waterVertexCount = 0;
            return;
        }

        waterVertexCount = static_cast<int>(vertices.size());
        GLsizeiptr dataSize = vertices.size() * sizeof(ChunkVertex);

        // Reuse existing buffer if data fits within capacity
        if (waterVAO != 0 && waterVBO != 0 && dataSize <= waterVboCapacity) {
            glBindBuffer(GL_ARRAY_BUFFER, waterVBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, vertices.data());
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            return;
        }

        // Need to reallocate - use orphaning with 20% headroom
        if (waterVAO != 0 && waterVBO != 0) {
            GLsizeiptr newCapacity = static_cast<GLsizeiptr>(dataSize * 1.2);
            glBindBuffer(GL_ARRAY_BUFFER, waterVBO);
            glBufferData(GL_ARRAY_BUFFER, newCapacity, nullptr, GL_DYNAMIC_DRAW);
            glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, vertices.data());
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            waterVboCapacity = newCapacity;
            return;
        }

        // First time creation - allocate with 20% headroom
        GLsizeiptr initialCapacity = static_cast<GLsizeiptr>(dataSize * 1.2);
        glGenVertexArrays(1, &waterVAO);
        glGenBuffers(1, &waterVBO);

        glBindVertexArray(waterVAO);
        glBindBuffer(GL_ARRAY_BUFFER, waterVBO);
        glBufferData(GL_ARRAY_BUFFER, initialCapacity, nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, vertices.data());
        waterVboCapacity = initialCapacity;

        // Position attribute (location 0)
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
                              (void*)offsetof(ChunkVertex, position));
        glEnableVertexAttribArray(0);

        // TexCoord attribute (location 1)
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
                              (void*)offsetof(ChunkVertex, texCoord));
        glEnableVertexAttribArray(1);

        // Normal attribute (location 2)
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
                              (void*)offsetof(ChunkVertex, normal));
        glEnableVertexAttribArray(2);

        // AO factor attribute (location 3)
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
                              (void*)offsetof(ChunkVertex, aoFactor));
        glEnableVertexAttribArray(3);

        // Light level attribute (location 4)
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
                              (void*)offsetof(ChunkVertex, lightLevel));
        glEnableVertexAttribArray(4);

        // Texture slot base UV attribute (location 5) - for greedy meshing tiling
        glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
                              (void*)offsetof(ChunkVertex, texSlotBase));
        glEnableVertexAttribArray(5);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

public:
    // Public upload methods for async mesh generation
    // Upload solid geometry to a specific sub-chunk's LOD level
    void uploadToSubChunk(int subChunkY, const std::vector<PackedChunkVertex>& vertices, int lodLevel = 0) {
        if (subChunkY < 0 || subChunkY >= SUB_CHUNKS_PER_COLUMN) return;

        SubChunkMesh& sub = subChunks[subChunkY];
        sub.subChunkY = subChunkY;
        lodLevel = std::max(0, std::min(lodLevel, LOD_LEVELS - 1));
        LODMesh& lod = sub.lodMeshes[lodLevel];

        if (vertices.empty()) {
            lod.vertexCount = 0;
            sub.isEmpty = true;
            for (int l = 0; l < LOD_LEVELS; l++) {
                if (sub.lodMeshes[l].vertexCount > 0) {
                    sub.isEmpty = false;
                    break;
                }
            }
            return;
        }

        sub.isEmpty = false;
        lod.vertexCount = static_cast<int>(vertices.size());
        GLsizeiptr dataSize = vertices.size() * sizeof(PackedChunkVertex);

        // ============================================================
        // PERSISTENT MAPPED BUFFER PATH (non-blocking)
        // ============================================================
        if (g_usePersistentMapping && lod.mappedPtr != nullptr && dataSize <= lod.capacity) {
            // Non-blocking check - if GPU isn't ready, just wait (can't orphan glBufferStorage)
            // The isGPUReady() call is fast and usually succeeds
            if (!lod.isGPUReady()) {
                // GPU still using buffer - wait briefly for it to finish
                if (!lod.waitForGPU()) {
                    // GPU severely behind - fall through to recreate buffer
                    // This prevents writing to a buffer GPU is still using
                    goto recreate_buffer;
                }
            }
            std::memcpy(lod.mappedPtr, vertices.data(), dataSize);
            lod.signalCPUDone();
            return;
        }

    recreate_buffer:
        // ============================================================
        // BUFFER REALLOCATION NEEDED
        // ============================================================
        if (lod.mappedPtr != nullptr && lod.VBO != 0) {
            // Must wait here because we're unmapping the buffer
            if (!lod.waitForGPU()) {
                // GPU severely behind - force cleanup anyway (may cause visual glitch)
                // Better than crashing
                if (lod.fence != nullptr) {
                    glDeleteSync(lod.fence);
                    lod.fence = nullptr;
                }
            }
            glBindBuffer(GL_ARRAY_BUFFER, lod.VBO);
            glUnmapBuffer(GL_ARRAY_BUFFER);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            lod.mappedPtr = nullptr;
        }

        if (lod.VBO != 0) {
            glDeleteBuffers(1, &lod.VBO);
            lod.VBO = 0;
        }

        GLsizeiptr newCapacity = static_cast<GLsizeiptr>(dataSize * 1.5);
        glGenBuffers(1, &lod.VBO);
        glBindBuffer(GL_ARRAY_BUFFER, lod.VBO);

        if (g_usePersistentMapping) {
            GLbitfield storageFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
            glBufferStorage(GL_ARRAY_BUFFER, newCapacity, nullptr, storageFlags);

            GLbitfield mapFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
            lod.mappedPtr = glMapBufferRange(GL_ARRAY_BUFFER, 0, newCapacity, mapFlags);

            if (lod.mappedPtr != nullptr) {
                std::memcpy(lod.mappedPtr, vertices.data(), dataSize);
                lod.signalCPUDone();
            } else {
                glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, vertices.data());
            }
        } else {
            glBufferData(GL_ARRAY_BUFFER, newCapacity, nullptr, GL_DYNAMIC_DRAW);
            glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, vertices.data());
        }

        lod.capacity = newCapacity;

        if (lod.VAO == 0) {
            glGenVertexArrays(1, &lod.VAO);
        }

        glBindVertexArray(lod.VAO);
        glBindBuffer(GL_ARRAY_BUFFER, lod.VBO);

        glVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, sizeof(PackedChunkVertex),
                              (void*)offsetof(PackedChunkVertex, x));
        glEnableVertexAttribArray(0);

        glVertexAttribPointer(1, 2, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(PackedChunkVertex),
                              (void*)offsetof(PackedChunkVertex, u));
        glEnableVertexAttribArray(1);

        glVertexAttribIPointer(2, 4, GL_UNSIGNED_BYTE, sizeof(PackedChunkVertex),
                               (void*)offsetof(PackedChunkVertex, normalIndex));
        glEnableVertexAttribArray(2);

        // Biome data: 2 unsigned bytes (biomeTemp, biomeHumid) for grass/foliage tinting
        glVertexAttribIPointer(3, 2, GL_UNSIGNED_BYTE, sizeof(PackedChunkVertex),
                               (void*)offsetof(PackedChunkVertex, biomeTemp));
        glEnableVertexAttribArray(3);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

        // Cache vertices for Vulkan/RHI rendering path (LOD 0 only)
        if (lodLevel == 0) {
            sub.cachedVertices = vertices;
        }
    }

    // Upload face-orientation buckets to a specific sub-chunk
    // Each bucket contains faces for one cardinal direction, enabling 35% better backface culling
    void uploadFaceBucketsToSubChunk(int subChunkY,
                                      const std::array<std::vector<PackedChunkVertex>, FACE_BUCKET_COUNT>& faceBuckets) {
        if (subChunkY < 0 || subChunkY >= SUB_CHUNKS_PER_COLUMN) return;

        SubChunkMesh& sub = subChunks[subChunkY];
        sub.subChunkY = subChunkY;
        sub.useFaceBuckets = true;

        // Track if any bucket has data
        bool hasAnyData = false;

        for (int bucketIdx = 0; bucketIdx < FACE_BUCKET_COUNT; bucketIdx++) {
            const auto& vertices = faceBuckets[bucketIdx];

            if (vertices.empty()) {
                sub.faceBucketVertexCounts[bucketIdx] = 0;
                continue;
            }

            hasAnyData = true;
            sub.faceBucketVertexCounts[bucketIdx] = static_cast<int>(vertices.size());
            GLsizeiptr dataSize = vertices.size() * sizeof(PackedChunkVertex);

            // Reuse buffer if data fits (but always recreate VAO to ensure correct attributes)
            if (sub.faceBucketVBOs[bucketIdx] != 0 &&
                dataSize <= sub.faceBucketCapacities[bucketIdx]) {
                glBindBuffer(GL_ARRAY_BUFFER, sub.faceBucketVBOs[bucketIdx]);
                glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, vertices.data());

                // Recreate VAO to ensure all vertex attributes are set up correctly
                if (sub.faceBucketVAOs[bucketIdx] != 0) {
                    glDeleteVertexArrays(1, &sub.faceBucketVAOs[bucketIdx]);
                }
                glGenVertexArrays(1, &sub.faceBucketVAOs[bucketIdx]);
                glBindVertexArray(sub.faceBucketVAOs[bucketIdx]);

                glVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, sizeof(PackedChunkVertex),
                                      (void*)offsetof(PackedChunkVertex, x));
                glEnableVertexAttribArray(0);

                glVertexAttribPointer(1, 2, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(PackedChunkVertex),
                                      (void*)offsetof(PackedChunkVertex, u));
                glEnableVertexAttribArray(1);

                glVertexAttribIPointer(2, 4, GL_UNSIGNED_BYTE, sizeof(PackedChunkVertex),
                                       (void*)offsetof(PackedChunkVertex, normalIndex));
                glEnableVertexAttribArray(2);

                glVertexAttribIPointer(3, 2, GL_UNSIGNED_BYTE, sizeof(PackedChunkVertex),
                                       (void*)offsetof(PackedChunkVertex, biomeTemp));
                glEnableVertexAttribArray(3);

                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glBindVertexArray(0);
                continue;
            }

            // Reallocate with headroom
            if (sub.faceBucketVBOs[bucketIdx] != 0) {
                glDeleteBuffers(1, &sub.faceBucketVBOs[bucketIdx]);
            }
            if (sub.faceBucketVAOs[bucketIdx] != 0) {
                glDeleteVertexArrays(1, &sub.faceBucketVAOs[bucketIdx]);
            }

            GLsizeiptr newCapacity = static_cast<GLsizeiptr>(dataSize * 1.5);

            glGenBuffers(1, &sub.faceBucketVBOs[bucketIdx]);
            glBindBuffer(GL_ARRAY_BUFFER, sub.faceBucketVBOs[bucketIdx]);
            glBufferData(GL_ARRAY_BUFFER, newCapacity, nullptr, GL_DYNAMIC_DRAW);
            glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, vertices.data());
            sub.faceBucketCapacities[bucketIdx] = newCapacity;

            glGenVertexArrays(1, &sub.faceBucketVAOs[bucketIdx]);
            glBindVertexArray(sub.faceBucketVAOs[bucketIdx]);

            // Same vertex format as regular sub-chunk mesh
            glVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, sizeof(PackedChunkVertex),
                                  (void*)offsetof(PackedChunkVertex, x));
            glEnableVertexAttribArray(0);

            glVertexAttribPointer(1, 2, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(PackedChunkVertex),
                                  (void*)offsetof(PackedChunkVertex, u));
            glEnableVertexAttribArray(1);

            glVertexAttribIPointer(2, 4, GL_UNSIGNED_BYTE, sizeof(PackedChunkVertex),
                                   (void*)offsetof(PackedChunkVertex, normalIndex));
            glEnableVertexAttribArray(2);

            // Biome data: 2 unsigned bytes (biomeTemp, biomeHumid) for grass/foliage tinting
            glVertexAttribIPointer(3, 2, GL_UNSIGNED_BYTE, sizeof(PackedChunkVertex),
                                   (void*)offsetof(PackedChunkVertex, biomeTemp));
            glEnableVertexAttribArray(3);

            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);
        }

        sub.isEmpty = !hasAnyData;

        // Cache vertices for Vulkan/RHI rendering path
        // Combine all face buckets into a single cachedVertices vector
        sub.cachedVertices.clear();
        if (hasAnyData) {
            size_t totalVertices = 0;
            for (int bucketIdx = 0; bucketIdx < FACE_BUCKET_COUNT; bucketIdx++) {
                totalVertices += faceBuckets[bucketIdx].size();
            }
            sub.cachedVertices.reserve(totalVertices);
            for (int bucketIdx = 0; bucketIdx < FACE_BUCKET_COUNT; bucketIdx++) {
                sub.cachedVertices.insert(sub.cachedVertices.end(),
                    faceBuckets[bucketIdx].begin(), faceBuckets[bucketIdx].end());
            }
        }
    }

    // Render a specific face bucket of a sub-chunk (for face-orientation culling)
    void renderSubChunkFaceBucket(int subChunkY, int bucketIndex) const {
        if (subChunkY < 0 || subChunkY >= SUB_CHUNKS_PER_COLUMN) return;
        if (bucketIndex < 0 || bucketIndex >= FACE_BUCKET_COUNT) return;

        const auto& sub = subChunks[subChunkY];
        if (sub.faceBucketCounts[bucketIndex] == 0) return;
        if (sub.consolidatedVAO == 0) return;

        // Use consolidated VAO with offset for this specific bucket
        glBindVertexArray(sub.consolidatedVAO);
        glDrawArrays(GL_TRIANGLES, sub.faceBucketOffsets[bucketIndex], sub.faceBucketCounts[bucketIndex]);
    }

    // Render sub-chunk with face-orientation culling based on camera position
    // visibilityMask is a bitmask where bit i is set if face bucket i should be rendered
    void renderSubChunkWithFaceCulling(int subChunkY, uint8_t visibilityMask) const {
        if (subChunkY < 0 || subChunkY >= SUB_CHUNKS_PER_COLUMN) return;

        const auto& sub = subChunks[subChunkY];
        if (sub.isEmpty || sub.consolidatedVAO == 0) return;

        // Build arrays for visible buckets only
        GLint visibleOffsets[FACE_BUCKET_COUNT];
        GLsizei visibleCounts[FACE_BUCKET_COUNT];
        int visibleCount = 0;

        for (int bucketIdx = 0; bucketIdx < FACE_BUCKET_COUNT; bucketIdx++) {
            if ((visibilityMask & (1 << bucketIdx)) == 0) continue;  // Skip culled bucket
            if (sub.faceBucketCounts[bucketIdx] == 0) continue;

            visibleOffsets[visibleCount] = sub.faceBucketOffsets[bucketIdx];
            visibleCounts[visibleCount] = sub.faceBucketCounts[bucketIdx];
            visibleCount++;
        }

        if (visibleCount == 0) return;

        // Single VAO bind + glMultiDrawArrays for all visible buckets
        glBindVertexArray(sub.consolidatedVAO);
        glMultiDrawArrays(GL_TRIANGLES, visibleOffsets, visibleCounts, visibleCount);
    }

    // Upload water geometry to a specific sub-chunk
    void uploadWaterToSubChunk(int subChunkY, const std::vector<ChunkVertex>& vertices) {
        if (subChunkY < 0 || subChunkY >= SUB_CHUNKS_PER_COLUMN) return;

        SubChunkMesh& sub = subChunks[subChunkY];

        if (vertices.empty()) {
            sub.waterVertexCount = 0;
            sub.hasWater = false;
            return;
        }

        sub.hasWater = true;
        sub.waterVertexCount = static_cast<int>(vertices.size());
        GLsizeiptr dataSize = vertices.size() * sizeof(ChunkVertex);

        // Reuse existing buffer if data fits
        if (sub.waterVAO != 0 && sub.waterVBO != 0 && dataSize <= sub.waterVboCapacity) {
            glBindBuffer(GL_ARRAY_BUFFER, sub.waterVBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, vertices.data());
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            return;
        }

        // Reallocate with headroom
        if (sub.waterVAO != 0 && sub.waterVBO != 0) {
            GLsizeiptr newCapacity = static_cast<GLsizeiptr>(dataSize * 1.2);
            glBindBuffer(GL_ARRAY_BUFFER, sub.waterVBO);
            glBufferData(GL_ARRAY_BUFFER, newCapacity, nullptr, GL_DYNAMIC_DRAW);
            glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, vertices.data());
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            sub.waterVboCapacity = newCapacity;
            return;
        }

        // First time creation
        GLsizeiptr initialCapacity = static_cast<GLsizeiptr>(dataSize * 1.2);
        glGenVertexArrays(1, &sub.waterVAO);
        glGenBuffers(1, &sub.waterVBO);

        glBindVertexArray(sub.waterVAO);
        glBindBuffer(GL_ARRAY_BUFFER, sub.waterVBO);
        glBufferData(GL_ARRAY_BUFFER, initialCapacity, nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, vertices.data());
        sub.waterVboCapacity = initialCapacity;

        // Same layout as regular water
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
                              (void*)offsetof(ChunkVertex, position));
        glEnableVertexAttribArray(0);

        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
                              (void*)offsetof(ChunkVertex, texCoord));
        glEnableVertexAttribArray(1);

        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
                              (void*)offsetof(ChunkVertex, normal));
        glEnableVertexAttribArray(2);

        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
                              (void*)offsetof(ChunkVertex, aoFactor));
        glEnableVertexAttribArray(3);

        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
                              (void*)offsetof(ChunkVertex, lightLevel));
        glEnableVertexAttribArray(4);

        glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
                              (void*)offsetof(ChunkVertex, texSlotBase));
        glEnableVertexAttribArray(5);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    // ============================================================
    // MESH SHADER - Meshlet Generation
    // ============================================================

    // Generate meshlets from vertex data for mesh shader rendering
    // Divides vertices into groups of up to MESHLET_MAX_VERTICES vertices
    void generateMeshlets(int subChunkY, const std::vector<PackedChunkVertex>& vertices) {
        if (subChunkY < 0 || subChunkY >= SUB_CHUNKS_PER_COLUMN) return;
        if (vertices.empty()) return;

        SubChunkMesh& sub = subChunks[subChunkY];
        MeshletData& meshlets = sub.meshletData;

        // Clean up existing meshlet data
        meshlets.destroy();
        if (sub.vertexSSBO != 0) {
            glDeleteBuffers(1, &sub.vertexSSBO);
            sub.vertexSSBO = 0;
        }

        // Create meshlets - each holds up to MESHLET_MAX_VERTICES vertices
        // Since we use triangle lists (non-indexed), every 3 vertices = 1 triangle
        size_t vertexCount = vertices.size();
        size_t triangleCount = vertexCount / 3;

        // Calculate number of meshlets needed
        // For non-indexed geometry: MESHLET_MAX_VERTICES vertices = MESHLET_MAX_VERTICES/3 triangles
        size_t maxTrianglesPerMeshlet = std::min((size_t)MESHLET_MAX_TRIANGLES, (size_t)MESHLET_MAX_VERTICES / 3);
        size_t meshletCount = (triangleCount + maxTrianglesPerMeshlet - 1) / maxTrianglesPerMeshlet;

        meshlets.meshlets.reserve(meshletCount);

        size_t currentVertex = 0;
        while (currentVertex < vertexCount) {
            MeshletDescriptor desc = {};

            // Calculate vertices for this meshlet (must be multiple of 3 for triangles)
            size_t remainingVertices = vertexCount - currentVertex;
            size_t meshletVertices = std::min(remainingVertices, (size_t)MESHLET_MAX_VERTICES);
            meshletVertices = (meshletVertices / 3) * 3;  // Round down to triangle boundary

            if (meshletVertices == 0) break;

            desc.vertexOffset = static_cast<uint32_t>(currentVertex);
            desc.vertexCount = static_cast<uint32_t>(meshletVertices);
            desc.triangleOffset = static_cast<uint32_t>(currentVertex / 3);
            desc.triangleCount = static_cast<uint32_t>(meshletVertices / 3);

            // Calculate bounding sphere for frustum culling
            // Position is stored as int16 with 8.8 fixed point (multiply by 1/256 for world coords)
            float minX = FLT_MAX, minY = FLT_MAX, minZ = FLT_MAX;
            float maxX = -FLT_MAX, maxY = -FLT_MAX, maxZ = -FLT_MAX;

            for (size_t i = 0; i < meshletVertices; i++) {
                const PackedChunkVertex& v = vertices[currentVertex + i];
                float x = static_cast<float>(v.x) / 256.0f;
                float y = static_cast<float>(v.y) / 256.0f;
                float z = static_cast<float>(v.z) / 256.0f;
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                minZ = std::min(minZ, z);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
                maxZ = std::max(maxZ, z);
            }

            // Bounding sphere center and radius
            desc.centerX = (minX + maxX) * 0.5f;
            desc.centerY = (minY + maxY) * 0.5f;
            desc.centerZ = (minZ + maxZ) * 0.5f;

            float dx = maxX - minX;
            float dy = maxY - minY;
            float dz = maxZ - minZ;
            desc.radius = sqrtf(dx*dx + dy*dy + dz*dz) * 0.5f;

            meshlets.meshlets.push_back(desc);
            currentVertex += meshletVertices;
        }

        if (meshlets.meshlets.empty()) return;

        // Upload vertex data to SSBO (mesh shaders read from SSBO, not VBO)
        glGenBuffers(1, &sub.vertexSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, sub.vertexSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     vertices.size() * sizeof(PackedChunkVertex),
                     vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        // Upload meshlet descriptors to SSBO
        glGenBuffers(1, &meshlets.meshletSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, meshlets.meshletSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     meshlets.meshlets.size() * sizeof(MeshletDescriptor),
                     meshlets.meshlets.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        // No index SSBO needed for non-indexed triangle lists
        // Mesh shader will directly output triangles from vertex data
    }

    // Public wrapper for water block generation (used by World for async mesh completion)
    void addWaterBlockPublic(std::vector<ChunkVertex>& vertices, const Chunk& chunk,
                             int bx, int by, int bz, const glm::vec3& pos, int textureSlot,
                             const BlockGetter& getBlock, int wx, int wz) {
        addWaterBlock(vertices, chunk, bx, by, bz, pos, textureSlot, getBlock, wx, wz);
    }
};
