#pragma once

#include "../world/Chunk.h"
#include "../world/Block.h"
#include <cstdint>
#include <vector>
#include <array>
#include <algorithm>
#include <bit>

// ============================================================================
// BINARY GREEDY MESHING
// ============================================================================
// Ultra-fast voxel meshing using 64-bit binary operations.
// Processes 64 voxels at a time for face culling and greedy merging.
// Achieves 50-200μs per chunk vs 5ms+ for traditional greedy meshing.
//
// Based on: https://github.com/cgerikj/binary-greedy-meshing
// ============================================================================

// Configuration
constexpr int BGM_CHUNK_SIZE = 32;  // We'll pad our 16x16 chunks to work with this
constexpr int BGM_CHUNK_SIZE_PADDED = BGM_CHUNK_SIZE + 2;  // Include neighbor data

// Compact quad vertex - 8 bytes per quad (not per vertex!)
// Position: x=5 bits (0-31), y=9 bits (0-511), z=5 bits (0-31)
// Size: 6 bits each for width, height (1-64 range, stored as 0-63)
// Normal: 3 bits (0-5 for ±X, ±Y, ±Z)
// Texture: 12 bits for texture slot (0-4095)
// AO: 8 bits packed (2 bits per corner)
struct BinaryQuad {
    // First 32 bits: position and size
    // [4:0] = x (5 bits), [13:5] = y (9 bits), [18:14] = z (5 bits),
    // [24:19] = width-1 (6 bits), [30:25] = height-1 (6 bits), [31] = unused
    uint32_t positionSize;

    // Second 32 bits: normal, texture, AO, light
    // [2:0] = normal index, [14:3] = texture slot, [22:15] = AO (2 bits × 4 corners), [30:23] = light
    uint32_t attributes;

    // Encode position and size - Y now supports 0-511 (enough for 256-tall chunks)
    static uint32_t encodePositionSize(int x, int y, int z, int w, int h) {
        return (x & 0x1F) |                    // 5 bits for x (0-31)
               ((y & 0x1FF) << 5) |            // 9 bits for y (0-511)
               ((z & 0x1F) << 14) |            // 5 bits for z (0-31)
               (((w - 1) & 0x3F) << 19) |      // 6 bits for width-1
               (((h - 1) & 0x3F) << 25);       // 6 bits for height-1
    }

    // Encode attributes
    static uint32_t encodeAttributes(int normalIdx, int texSlot, uint8_t ao, uint8_t light) {
        return (normalIdx & 0x7) |
               ((texSlot & 0xFFF) << 3) |
               ((ao & 0xFF) << 15) |
               ((light & 0xFF) << 23);
    }

    // Decode helpers (for debugging or CPU-side operations)
    int getX() const { return positionSize & 0x1F; }
    int getY() const { return (positionSize >> 5) & 0x1FF; }
    int getZ() const { return (positionSize >> 14) & 0x1F; }
    int getWidth() const { return ((positionSize >> 19) & 0x3F) + 1; }
    int getHeight() const { return ((positionSize >> 25) & 0x3F) + 1; }
    int getNormal() const { return attributes & 0x7; }
    int getTexSlot() const { return (attributes >> 3) & 0xFFF; }
    uint8_t getAO() const { return (attributes >> 15) & 0xFF; }
    uint8_t getLight() const { return (attributes >> 23) & 0xFF; }
};

// Number of face orientation buckets (one per cardinal direction)
constexpr int FACE_BUCKET_COUNT = 6;

// Result of binary greedy meshing with face-orientation buckets
// Separating faces by direction enables 35% better backface culling
struct BinaryMeshResult {
    // Face buckets: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
    std::array<std::vector<BinaryQuad>, FACE_BUCKET_COUNT> faceBuckets;
    int solidQuadCount = 0;
    int waterQuadCount = 0;

    void clear() {
        for (auto& bucket : faceBuckets) {
            bucket.clear();
        }
        solidQuadCount = 0;
        waterQuadCount = 0;
    }

    void reserve(size_t countPerBucket) {
        for (auto& bucket : faceBuckets) {
            bucket.reserve(countPerBucket);
        }
    }

    // Add quad to appropriate bucket based on face direction
    void addQuad(const BinaryQuad& quad) {
        int faceIdx = quad.getNormal();
        if (faceIdx >= 0 && faceIdx < FACE_BUCKET_COUNT) {
            faceBuckets[faceIdx].push_back(quad);
            solidQuadCount++;
        }
    }

    // Get total quad count across all buckets
    size_t getTotalQuadCount() const {
        size_t total = 0;
        for (const auto& bucket : faceBuckets) {
            total += bucket.size();
        }
        return total;
    }
};

// Face direction enum matching our normal indices
enum class BGMFace : int {
    POS_X = 0,  // +X (Right)
    NEG_X = 1,  // -X (Left)
    POS_Y = 2,  // +Y (Top)
    NEG_Y = 3,  // -Y (Bottom)
    POS_Z = 4,  // +Z (Front)
    NEG_Z = 5   // -Z (Back)
};

// Binary Greedy Mesher class
class BinaryGreedyMesher {
public:
    // Block data callback - returns block type at world position
    using BlockGetter = std::function<BlockType(int, int, int)>;
    using TextureGetter = std::function<int(BlockType, BGMFace)>;

    BinaryGreedyMesher() {
        // Pre-allocate work buffers
        m_solidMask.resize(CHUNK_SIZE_Y);
        m_faceMask.resize(CHUNK_SIZE_Y);
        for (auto& row : m_solidMask) {
            row.fill(0);
        }
    }

    // Generate mesh for a chunk using binary greedy meshing
    // Returns quads in ultra-compact format
    void generateMesh(
        const Chunk& chunk,
        const BlockGetter& getBlock,
        const TextureGetter& getTexture,
        BinaryMeshResult& result,
        int baseX, int baseZ
    ) {
        result.clear();
        result.reserve(4096);  // Typical chunk has 1000-4000 quads

        // Process each face direction
        for (int face = 0; face < 6; face++) {
            generateFace(chunk, getBlock, getTexture, result,
                        static_cast<BGMFace>(face), baseX, baseZ);
        }
    }

    // Generate mesh for a Y range (sub-chunk)
    void generateMeshForYRange(
        const Chunk& chunk,
        const BlockGetter& getBlock,
        const TextureGetter& getTexture,
        BinaryMeshResult& result,
        int baseX, int baseZ,
        int yStart, int yEnd
    ) {
        result.clear();
        result.reserve(1024);

        for (int face = 0; face < 6; face++) {
            generateFaceForYRange(chunk, getBlock, getTexture, result,
                                 static_cast<BGMFace>(face), baseX, baseZ, yStart, yEnd);
        }
    }

private:
    // Work buffers - reused across calls to avoid allocation
    // Each row is CHUNK_SIZE_X bits (fits in uint32_t for 16-wide, uint64_t for 32-wide)
    std::vector<std::array<uint64_t, CHUNK_SIZE_Z>> m_solidMask;
    std::vector<std::array<uint64_t, CHUNK_SIZE_Z>> m_faceMask;

    // Helper: count trailing zeros (position of lowest set bit)
    static inline int ctz64(uint64_t x) {
        #if defined(__GNUC__) || defined(__clang__)
            return x ? __builtin_ctzll(x) : 64;
        #elif defined(_MSC_VER)
            unsigned long index;
            return _BitScanForward64(&index, x) ? index : 64;
        #else
            // Fallback
            if (x == 0) return 64;
            int n = 0;
            if ((x & 0xFFFFFFFF) == 0) { n += 32; x >>= 32; }
            if ((x & 0xFFFF) == 0) { n += 16; x >>= 16; }
            if ((x & 0xFF) == 0) { n += 8; x >>= 8; }
            if ((x & 0xF) == 0) { n += 4; x >>= 4; }
            if ((x & 0x3) == 0) { n += 2; x >>= 2; }
            if ((x & 0x1) == 0) { n += 1; }
            return n;
        #endif
    }

    // Helper: count leading zeros
    static inline int clz64(uint64_t x) {
        #if defined(__GNUC__) || defined(__clang__)
            return x ? __builtin_clzll(x) : 64;
        #elif defined(_MSC_VER)
            unsigned long index;
            return _BitScanReverse64(&index, x) ? 63 - index : 64;
        #else
            if (x == 0) return 64;
            int n = 0;
            if ((x >> 32) == 0) { n += 32; x <<= 32; }
            if ((x >> 48) == 0) { n += 16; x <<= 16; }
            if ((x >> 56) == 0) { n += 8; x <<= 8; }
            if ((x >> 60) == 0) { n += 4; x <<= 4; }
            if ((x >> 62) == 0) { n += 2; x <<= 2; }
            if ((x >> 63) == 0) { n += 1; }
            return n;
        #endif
    }

    // Helper: population count
    static inline int popcount64(uint64_t x) {
        #if defined(__GNUC__) || defined(__clang__)
            return __builtin_popcountll(x);
        #elif defined(_MSC_VER)
            return static_cast<int>(__popcnt64(x));
        #else
            x = x - ((x >> 1) & 0x5555555555555555ULL);
            x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
            x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
            return static_cast<int>((x * 0x0101010101010101ULL) >> 56);
        #endif
    }

    // Generate faces for one direction
    void generateFace(
        const Chunk& chunk,
        const BlockGetter& getBlock,
        const TextureGetter& getTexture,
        BinaryMeshResult& result,
        BGMFace face,
        int baseX, int baseZ
    ) {
        generateFaceForYRange(chunk, getBlock, getTexture, result, face,
                             baseX, baseZ, chunk.chunkMinY, chunk.chunkMaxY);
    }

    // Generate faces for one direction within a Y range
    void generateFaceForYRange(
        const Chunk& chunk,
        const BlockGetter& getBlock,
        const TextureGetter& getTexture,
        BinaryMeshResult& result,
        BGMFace face,
        int baseX, int baseZ,
        int yStart, int yEnd
    ) {
        if (yStart > yEnd) return;

        // Clamp to chunk bounds
        yStart = std::max(yStart, static_cast<int>(chunk.chunkMinY));
        yEnd = std::min(yEnd, static_cast<int>(chunk.chunkMaxY));
        if (yStart > yEnd) return;

        // Get axis info based on face direction
        int primaryAxis, secondaryAxis, tertiaryAxis;
        int primaryDir;
        getAxisInfo(face, primaryAxis, secondaryAxis, tertiaryAxis, primaryDir);

        // Process slices perpendicular to the face normal
        if (face == BGMFace::POS_Y || face == BGMFace::NEG_Y) {
            // Y-facing: iterate Y, mask is XZ
            processYFaces(chunk, getBlock, getTexture, result, face, baseX, baseZ, yStart, yEnd);
        } else if (face == BGMFace::POS_Z || face == BGMFace::NEG_Z) {
            // Z-facing: iterate Z, mask is XY
            processZFaces(chunk, getBlock, getTexture, result, face, baseX, baseZ, yStart, yEnd);
        } else {
            // X-facing: iterate X, mask is YZ
            processXFaces(chunk, getBlock, getTexture, result, face, baseX, baseZ, yStart, yEnd);
        }
    }

    void getAxisInfo(BGMFace face, int& primary, int& secondary, int& tertiary, int& dir) {
        switch (face) {
            case BGMFace::POS_X: primary = 0; secondary = 1; tertiary = 2; dir = 1; break;
            case BGMFace::NEG_X: primary = 0; secondary = 1; tertiary = 2; dir = -1; break;
            case BGMFace::POS_Y: primary = 1; secondary = 0; tertiary = 2; dir = 1; break;
            case BGMFace::NEG_Y: primary = 1; secondary = 0; tertiary = 2; dir = -1; break;
            case BGMFace::POS_Z: primary = 2; secondary = 0; tertiary = 1; dir = 1; break;
            case BGMFace::NEG_Z: primary = 2; secondary = 0; tertiary = 1; dir = -1; break;
        }
    }

    // Process Y-facing faces (TOP and BOTTOM)
    void processYFaces(
        const Chunk& chunk,
        const BlockGetter& getBlock,
        const TextureGetter& getTexture,
        BinaryMeshResult& result,
        BGMFace face,
        int baseX, int baseZ,
        int yStart, int yEnd
    ) {
        int yOffset = (face == BGMFace::POS_Y) ? 1 : -1;

        for (int y = yStart; y <= yEnd; y++) {
            // Build binary mask for this Y slice
            // Each bit represents whether a face should be rendered at (x, z)
            std::array<uint32_t, CHUNK_SIZE_Z> faceMask;
            std::array<int, CHUNK_SIZE_X * CHUNK_SIZE_Z> textureMask;
            faceMask.fill(0);
            std::fill(textureMask.begin(), textureMask.end(), -1);

            for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                uint32_t rowMask = 0;
                for (int x = 0; x < CHUNK_SIZE_X; x++) {
                    BlockType block = chunk.getBlock(x, y, z);
                    if (block == BlockType::AIR || block == BlockType::WATER) continue;

                    // Check if neighbor is transparent
                    int ny = y + yOffset;
                    BlockType neighbor = getBlock(baseX + x, ny, baseZ + z);
                    if (!isBlockOpaque(neighbor)) {
                        rowMask |= (1u << x);
                        textureMask[z * CHUNK_SIZE_X + x] = getTexture(block, face);
                    }
                }
                faceMask[z] = rowMask;
            }

            // Greedy merge the mask
            greedyMergeXZ(result, faceMask, textureMask, face, y, chunk, baseX, baseZ, getBlock);
        }
    }

    // Process Z-facing faces (FRONT and BACK)
    void processZFaces(
        const Chunk& chunk,
        const BlockGetter& getBlock,
        const TextureGetter& getTexture,
        BinaryMeshResult& result,
        BGMFace face,
        int baseX, int baseZ,
        int yStart, int yEnd
    ) {
        int zOffset = (face == BGMFace::POS_Z) ? 1 : -1;

        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
            // Build binary mask for this Z slice
            int yRange = yEnd - yStart + 1;
            std::vector<uint32_t> faceMask(yRange, 0);
            std::vector<int> textureMask(CHUNK_SIZE_X * yRange, -1);

            for (int yRel = 0; yRel < yRange; yRel++) {
                int y = yStart + yRel;
                uint32_t rowMask = 0;
                for (int x = 0; x < CHUNK_SIZE_X; x++) {
                    BlockType block = chunk.getBlock(x, y, z);
                    if (block == BlockType::AIR || block == BlockType::WATER) continue;

                    int nz = baseZ + z + zOffset;
                    BlockType neighbor = getBlock(baseX + x, y, nz);
                    if (!isBlockOpaque(neighbor)) {
                        rowMask |= (1u << x);
                        textureMask[yRel * CHUNK_SIZE_X + x] = getTexture(block, face);
                    }
                }
                faceMask[yRel] = rowMask;
            }

            // Greedy merge the mask
            greedyMergeXY(result, faceMask, textureMask, face, z, yStart, chunk, baseX, baseZ, getBlock);
        }
    }

    // Process X-facing faces (LEFT and RIGHT)
    void processXFaces(
        const Chunk& chunk,
        const BlockGetter& getBlock,
        const TextureGetter& getTexture,
        BinaryMeshResult& result,
        BGMFace face,
        int baseX, int baseZ,
        int yStart, int yEnd
    ) {
        int xOffset = (face == BGMFace::POS_X) ? 1 : -1;

        for (int x = 0; x < CHUNK_SIZE_X; x++) {
            // Build binary mask for this X slice
            int yRange = yEnd - yStart + 1;
            std::vector<uint32_t> faceMask(yRange, 0);
            std::vector<int> textureMask(CHUNK_SIZE_Z * yRange, -1);

            for (int yRel = 0; yRel < yRange; yRel++) {
                int y = yStart + yRel;
                uint32_t rowMask = 0;
                for (int z = 0; z < CHUNK_SIZE_Z; z++) {
                    BlockType block = chunk.getBlock(x, y, z);
                    if (block == BlockType::AIR || block == BlockType::WATER) continue;

                    int nx = baseX + x + xOffset;
                    BlockType neighbor = getBlock(nx, y, baseZ + z);
                    if (!isBlockOpaque(neighbor)) {
                        rowMask |= (1u << z);
                        textureMask[yRel * CHUNK_SIZE_Z + z] = getTexture(block, face);
                    }
                }
                faceMask[yRel] = rowMask;
            }

            // Greedy merge the mask
            greedyMergeZY(result, faceMask, textureMask, face, x, yStart, chunk, baseX, baseZ, getBlock);
        }
    }

    // Binary greedy merge for XZ plane (Y-facing faces)
    void greedyMergeXZ(
        BinaryMeshResult& result,
        std::array<uint32_t, CHUNK_SIZE_Z>& faceMask,
        std::array<int, CHUNK_SIZE_X * CHUNK_SIZE_Z>& textureMask,
        BGMFace face,
        int y,
        const Chunk& chunk,
        int baseX, int baseZ,
        const BlockGetter& getBlock
    ) {
        for (int z = 0; z < CHUNK_SIZE_Z; z++) {
            uint32_t row = faceMask[z];
            while (row != 0) {
                // Find first set bit (start of run)
                int x = ctz64(row);
                if (x >= CHUNK_SIZE_X) break;

                int texSlot = textureMask[z * CHUNK_SIZE_X + x];
                if (texSlot < 0) {
                    row &= ~(1u << x);
                    continue;
                }

                // Find width: count consecutive bits with same texture
                int width = 1;
                uint32_t runMask = 1u << x;
                while (x + width < CHUNK_SIZE_X) {
                    if (!(row & (1u << (x + width)))) break;
                    if (textureMask[z * CHUNK_SIZE_X + x + width] != texSlot) break;
                    runMask |= (1u << (x + width));
                    width++;
                }

                // Find height: check subsequent rows
                int height = 1;
                while (z + height < CHUNK_SIZE_Z) {
                    uint32_t nextRow = faceMask[z + height];
                    // Check if all bits in our run are set in next row with same texture
                    if ((nextRow & runMask) != runMask) break;
                    bool sameTexture = true;
                    for (int dx = 0; dx < width; dx++) {
                        if (textureMask[(z + height) * CHUNK_SIZE_X + x + dx] != texSlot) {
                            sameTexture = false;
                            break;
                        }
                    }
                    if (!sameTexture) break;
                    height++;
                }

                // Clear the merged region from all masks
                for (int dz = 0; dz < height; dz++) {
                    faceMask[z + dz] &= ~runMask;
                    for (int dx = 0; dx < width; dx++) {
                        textureMask[(z + dz) * CHUNK_SIZE_X + x + dx] = -1;
                    }
                }

                // Calculate AO and light
                uint8_t ao = calculateAO(chunk, getBlock, baseX, baseZ, x, y, z, width, height, face);
                uint8_t light = calculateLight(chunk, x, y, z);

                // Emit quad
                BinaryQuad quad;
                quad.positionSize = BinaryQuad::encodePositionSize(x, y, z, width, height);
                quad.attributes = BinaryQuad::encodeAttributes(static_cast<int>(face), texSlot, ao, light);
                result.addQuad(quad);

                // Update row for next iteration
                row = faceMask[z];
            }
        }
    }

    // Binary greedy merge for XY plane (Z-facing faces)
    void greedyMergeXY(
        BinaryMeshResult& result,
        std::vector<uint32_t>& faceMask,
        std::vector<int>& textureMask,
        BGMFace face,
        int z,
        int yStart,
        const Chunk& chunk,
        int baseX, int baseZ,
        const BlockGetter& getBlock
    ) {
        int yRange = static_cast<int>(faceMask.size());

        for (int yRel = 0; yRel < yRange; yRel++) {
            uint32_t row = faceMask[yRel];
            while (row != 0) {
                int x = ctz64(row);
                if (x >= CHUNK_SIZE_X) break;

                int texSlot = textureMask[yRel * CHUNK_SIZE_X + x];
                if (texSlot < 0) {
                    row &= ~(1u << x);
                    continue;
                }

                // Find width
                int width = 1;
                uint32_t runMask = 1u << x;
                while (x + width < CHUNK_SIZE_X) {
                    if (!(row & (1u << (x + width)))) break;
                    if (textureMask[yRel * CHUNK_SIZE_X + x + width] != texSlot) break;
                    runMask |= (1u << (x + width));
                    width++;
                }

                // Find height (in Y direction)
                int height = 1;
                while (yRel + height < yRange) {
                    uint32_t nextRow = faceMask[yRel + height];
                    if ((nextRow & runMask) != runMask) break;
                    bool sameTexture = true;
                    for (int dx = 0; dx < width; dx++) {
                        if (textureMask[(yRel + height) * CHUNK_SIZE_X + x + dx] != texSlot) {
                            sameTexture = false;
                            break;
                        }
                    }
                    if (!sameTexture) break;
                    height++;
                }

                // Clear merged region
                for (int dy = 0; dy < height; dy++) {
                    faceMask[yRel + dy] &= ~runMask;
                    for (int dx = 0; dx < width; dx++) {
                        textureMask[(yRel + dy) * CHUNK_SIZE_X + x + dx] = -1;
                    }
                }

                int y = yStart + yRel;
                uint8_t ao = calculateAO(chunk, getBlock, baseX, baseZ, x, y, z, width, height, face);
                uint8_t light = calculateLight(chunk, x, y, z);

                BinaryQuad quad;
                quad.positionSize = BinaryQuad::encodePositionSize(x, y, z, width, height);
                quad.attributes = BinaryQuad::encodeAttributes(static_cast<int>(face), texSlot, ao, light);
                result.addQuad(quad);

                row = faceMask[yRel];
            }
        }
    }

    // Binary greedy merge for ZY plane (X-facing faces)
    void greedyMergeZY(
        BinaryMeshResult& result,
        std::vector<uint32_t>& faceMask,
        std::vector<int>& textureMask,
        BGMFace face,
        int x,
        int yStart,
        const Chunk& chunk,
        int baseX, int baseZ,
        const BlockGetter& getBlock
    ) {
        int yRange = static_cast<int>(faceMask.size());

        for (int yRel = 0; yRel < yRange; yRel++) {
            uint32_t row = faceMask[yRel];
            while (row != 0) {
                int z = ctz64(row);
                if (z >= CHUNK_SIZE_Z) break;

                int texSlot = textureMask[yRel * CHUNK_SIZE_Z + z];
                if (texSlot < 0) {
                    row &= ~(1u << z);
                    continue;
                }

                // Find width (in Z direction)
                int width = 1;
                uint32_t runMask = 1u << z;
                while (z + width < CHUNK_SIZE_Z) {
                    if (!(row & (1u << (z + width)))) break;
                    if (textureMask[yRel * CHUNK_SIZE_Z + z + width] != texSlot) break;
                    runMask |= (1u << (z + width));
                    width++;
                }

                // Find height (in Y direction)
                int height = 1;
                while (yRel + height < yRange) {
                    uint32_t nextRow = faceMask[yRel + height];
                    if ((nextRow & runMask) != runMask) break;
                    bool sameTexture = true;
                    for (int dz = 0; dz < width; dz++) {
                        if (textureMask[(yRel + height) * CHUNK_SIZE_Z + z + dz] != texSlot) {
                            sameTexture = false;
                            break;
                        }
                    }
                    if (!sameTexture) break;
                    height++;
                }

                // Clear merged region
                for (int dy = 0; dy < height; dy++) {
                    faceMask[yRel + dy] &= ~runMask;
                    for (int dz = 0; dz < width; dz++) {
                        textureMask[(yRel + dy) * CHUNK_SIZE_Z + z + dz] = -1;
                    }
                }

                int y = yStart + yRel;
                uint8_t ao = calculateAO(chunk, getBlock, baseX, baseZ, x, y, z, width, height, face);
                uint8_t light = calculateLight(chunk, x, y, z);

                BinaryQuad quad;
                // For X-facing, position is (x, y, z), size is (width in Z, height in Y)
                quad.positionSize = BinaryQuad::encodePositionSize(x, y, z, width, height);
                quad.attributes = BinaryQuad::encodeAttributes(static_cast<int>(face), texSlot, ao, light);
                result.addQuad(quad);

                row = faceMask[yRel];
            }
        }
    }

    // Check if position has solid block for AO calculation
    bool isSolidForAO(const BlockGetter& getBlock, int x, int y, int z) const {
        if (y < 0 || y >= CHUNK_SIZE_Y) return false;
        BlockType block = getBlock(x, y, z);
        return isBlockOpaque(block);
    }

    // Calculate AO value for a single corner (0-3, where 3 = fully lit)
    int cornerAO(bool side1, bool side2, bool corner) const {
        if (side1 && side2) return 0;  // Fully occluded by two adjacent blocks
        return 3 - (side1 ? 1 : 0) - (side2 ? 1 : 0) - (corner ? 1 : 0);
    }

    // Calculate ambient occlusion for all 4 corners of a quad
    // Returns packed AO: 2 bits per corner (corners 0,1,2,3 in bits 0-1, 2-3, 4-5, 6-7)
    uint8_t calculateAO(
        const Chunk& chunk,
        const BlockGetter& getBlock,
        int baseX, int baseZ,
        int x, int y, int z,
        int width, int height,
        BGMFace face
    ) {
        (void)chunk; // Unused, using getBlock instead

        // World coordinates
        int wx = baseX + x;
        int wz = baseZ + z;

        // AO values for 4 corners (default fully lit)
        int ao0 = 3, ao1 = 3, ao2 = 3, ao3 = 3;

        // Calculate AO based on face direction
        // Each corner checks 3 neighbors: two sides and one diagonal
        switch (face) {
            case BGMFace::POS_Y: {
                int fy = y + 1;
                // Corner 0: back-left, Corner 1: back-right, Corner 2: front-right, Corner 3: front-left
                ao0 = cornerAO(isSolidForAO(getBlock, wx-1, fy, wz),
                              isSolidForAO(getBlock, wx, fy, wz-1),
                              isSolidForAO(getBlock, wx-1, fy, wz-1));
                ao1 = cornerAO(isSolidForAO(getBlock, wx+width, fy, wz),
                              isSolidForAO(getBlock, wx+width-1, fy, wz-1),
                              isSolidForAO(getBlock, wx+width, fy, wz-1));
                ao2 = cornerAO(isSolidForAO(getBlock, wx+width, fy, wz+height),
                              isSolidForAO(getBlock, wx+width-1, fy, wz+height+1),
                              isSolidForAO(getBlock, wx+width, fy, wz+height+1));
                ao3 = cornerAO(isSolidForAO(getBlock, wx-1, fy, wz+height),
                              isSolidForAO(getBlock, wx, fy, wz+height+1),
                              isSolidForAO(getBlock, wx-1, fy, wz+height+1));
                break;
            }
            case BGMFace::NEG_Y: {
                int fy = y - 1;
                ao0 = cornerAO(isSolidForAO(getBlock, wx-1, fy, wz),
                              isSolidForAO(getBlock, wx, fy, wz-1),
                              isSolidForAO(getBlock, wx-1, fy, wz-1));
                ao1 = cornerAO(isSolidForAO(getBlock, wx+width, fy, wz),
                              isSolidForAO(getBlock, wx+width-1, fy, wz-1),
                              isSolidForAO(getBlock, wx+width, fy, wz-1));
                ao2 = cornerAO(isSolidForAO(getBlock, wx+width, fy, wz+height),
                              isSolidForAO(getBlock, wx+width-1, fy, wz+height+1),
                              isSolidForAO(getBlock, wx+width, fy, wz+height+1));
                ao3 = cornerAO(isSolidForAO(getBlock, wx-1, fy, wz+height),
                              isSolidForAO(getBlock, wx, fy, wz+height+1),
                              isSolidForAO(getBlock, wx-1, fy, wz+height+1));
                break;
            }
            case BGMFace::POS_Z: {
                int fz = wz + 1;
                ao0 = cornerAO(isSolidForAO(getBlock, wx-1, y, fz),
                              isSolidForAO(getBlock, wx, y-1, fz),
                              isSolidForAO(getBlock, wx-1, y-1, fz));
                ao1 = cornerAO(isSolidForAO(getBlock, wx+width, y, fz),
                              isSolidForAO(getBlock, wx+width-1, y-1, fz),
                              isSolidForAO(getBlock, wx+width, y-1, fz));
                ao2 = cornerAO(isSolidForAO(getBlock, wx+width, y+height, fz),
                              isSolidForAO(getBlock, wx+width-1, y+height+1, fz),
                              isSolidForAO(getBlock, wx+width, y+height+1, fz));
                ao3 = cornerAO(isSolidForAO(getBlock, wx-1, y+height, fz),
                              isSolidForAO(getBlock, wx, y+height+1, fz),
                              isSolidForAO(getBlock, wx-1, y+height+1, fz));
                break;
            }
            case BGMFace::NEG_Z: {
                int fz = wz - 1;
                ao0 = cornerAO(isSolidForAO(getBlock, wx+width, y, fz),
                              isSolidForAO(getBlock, wx+width-1, y-1, fz),
                              isSolidForAO(getBlock, wx+width, y-1, fz));
                ao1 = cornerAO(isSolidForAO(getBlock, wx-1, y, fz),
                              isSolidForAO(getBlock, wx, y-1, fz),
                              isSolidForAO(getBlock, wx-1, y-1, fz));
                ao2 = cornerAO(isSolidForAO(getBlock, wx-1, y+height, fz),
                              isSolidForAO(getBlock, wx, y+height+1, fz),
                              isSolidForAO(getBlock, wx-1, y+height+1, fz));
                ao3 = cornerAO(isSolidForAO(getBlock, wx+width, y+height, fz),
                              isSolidForAO(getBlock, wx+width-1, y+height+1, fz),
                              isSolidForAO(getBlock, wx+width, y+height+1, fz));
                break;
            }
            case BGMFace::NEG_X: {
                int fx = wx - 1;
                ao0 = cornerAO(isSolidForAO(getBlock, fx, y, wz-1),
                              isSolidForAO(getBlock, fx, y-1, wz),
                              isSolidForAO(getBlock, fx, y-1, wz-1));
                ao1 = cornerAO(isSolidForAO(getBlock, fx, y, wz+width),
                              isSolidForAO(getBlock, fx, y-1, wz+width-1),
                              isSolidForAO(getBlock, fx, y-1, wz+width));
                ao2 = cornerAO(isSolidForAO(getBlock, fx, y+height, wz+width),
                              isSolidForAO(getBlock, fx, y+height+1, wz+width-1),
                              isSolidForAO(getBlock, fx, y+height+1, wz+width));
                ao3 = cornerAO(isSolidForAO(getBlock, fx, y+height, wz-1),
                              isSolidForAO(getBlock, fx, y+height+1, wz),
                              isSolidForAO(getBlock, fx, y+height+1, wz-1));
                break;
            }
            case BGMFace::POS_X: {
                int fx = wx + 1;
                ao0 = cornerAO(isSolidForAO(getBlock, fx, y, wz+width),
                              isSolidForAO(getBlock, fx, y-1, wz+width-1),
                              isSolidForAO(getBlock, fx, y-1, wz+width));
                ao1 = cornerAO(isSolidForAO(getBlock, fx, y, wz-1),
                              isSolidForAO(getBlock, fx, y-1, wz),
                              isSolidForAO(getBlock, fx, y-1, wz-1));
                ao2 = cornerAO(isSolidForAO(getBlock, fx, y+height, wz-1),
                              isSolidForAO(getBlock, fx, y+height+1, wz),
                              isSolidForAO(getBlock, fx, y+height+1, wz-1));
                ao3 = cornerAO(isSolidForAO(getBlock, fx, y+height, wz+width),
                              isSolidForAO(getBlock, fx, y+height+1, wz+width-1),
                              isSolidForAO(getBlock, fx, y+height+1, wz+width));
                break;
            }
        }

        // Pack 4 corners into 8 bits (2 bits per corner, values 0-3)
        return static_cast<uint8_t>((ao0 & 0x3) | ((ao1 & 0x3) << 2) |
                                    ((ao2 & 0x3) << 4) | ((ao3 & 0x3) << 6));
    }

    // Get light level at position
    uint8_t calculateLight(const Chunk& chunk, int x, int y, int z) {
        if (y < 0 || y >= CHUNK_SIZE_Y) return 255;
        return chunk.getLightLevel(x, y, z) * 17;  // Scale 0-15 to 0-255
    }

    // Check if block is opaque
    static bool isBlockOpaque(BlockType block) {
        return block != BlockType::AIR &&
               block != BlockType::WATER &&
               block != BlockType::GLASS &&
               block != BlockType::LEAVES;
    }
};

// Utility function to expand a single face bucket to vertices
// Internal helper - use expandFaceBucketsToVertices for the main API
inline void expandSingleBucketToVertices(
    const std::vector<BinaryQuad>& quads,
    std::vector<PackedChunkVertex>& vertices
) {
    vertices.reserve(vertices.size() + quads.size() * 6);  // 6 vertices per quad (2 triangles)

    for (const auto& quad : quads) {
        int x = quad.getX();
        int y = quad.getY();
        int z = quad.getZ();
        int width = quad.getWidth();
        int height = quad.getHeight();
        int normalIdx = quad.getNormal();
        int texSlot = quad.getTexSlot();
        uint8_t light = quad.getLight();
        uint8_t packedAO = quad.getAO();

        // Unpack AO values for each corner (2 bits each, 0-3 range)
        // Maps 0-3 to 140-255 range for visible but subtle AO
        std::array<uint8_t, 4> aoValues;
        for (int i = 0; i < 4; i++) {
            int aoVal = (packedAO >> (i * 2)) & 0x3;
            aoValues[i] = static_cast<uint8_t>(140 + aoVal * 38);
        }

        // Local positions (scaled by 256 for precision)
        std::array<std::array<int16_t, 3>, 4> localCorners;
        // UV coordinates (8.8 fixed point)
        std::array<std::array<uint16_t, 2>, 4> uvCorners;
        uint8_t packedNormalIndex;

        // Calculate corners based on face direction - MUST MATCH ChunkMesh.h exactly!
        switch (static_cast<BGMFace>(normalIdx)) {
            case BGMFace::POS_Y: { // Top face (+Y) - normalIndex = 2
                packedNormalIndex = 2;
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
            case BGMFace::NEG_Y: { // Bottom face (-Y) - normalIndex = 3
                packedNormalIndex = 3;
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
            case BGMFace::POS_Z: { // Front face (+Z) - normalIndex = 4
                packedNormalIndex = 4;
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
            case BGMFace::NEG_Z: { // Back face (-Z) - normalIndex = 5
                packedNormalIndex = 5;
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
            case BGMFace::NEG_X: { // Left face (-X) - normalIndex = 1
                packedNormalIndex = 1;
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
            case BGMFace::POS_X: { // Right face (+X) - normalIndex = 0
                packedNormalIndex = 0;
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

        // Create vertex helper - uses per-corner AO values
        auto makeVertex = [&](int cornerIdx) -> PackedChunkVertex {
            return PackedChunkVertex{
                localCorners[cornerIdx][0],
                localCorners[cornerIdx][1],
                localCorners[cornerIdx][2],
                uvCorners[cornerIdx][0],
                uvCorners[cornerIdx][1],
                packedNormalIndex,
                aoValues[cornerIdx],  // Per-corner AO
                light,
                static_cast<uint8_t>(texSlot),
                0  // padding
            };
        };

        // Emit triangles with correct winding order: (0,1,2) and (2,3,0)
        vertices.push_back(makeVertex(0));
        vertices.push_back(makeVertex(1));
        vertices.push_back(makeVertex(2));
        vertices.push_back(makeVertex(2));
        vertices.push_back(makeVertex(3));
        vertices.push_back(makeVertex(0));
    }
}

// Main API: Expand face buckets to 6 separate vertex arrays
// Each array corresponds to one face direction for efficient culling
// faceBucketVertices[i] contains vertices for face direction i (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z)
inline void expandFaceBucketsToVertices(
    const BinaryMeshResult& result,
    std::array<std::vector<PackedChunkVertex>, FACE_BUCKET_COUNT>& faceBucketVertices
) {
    for (int i = 0; i < FACE_BUCKET_COUNT; i++) {
        faceBucketVertices[i].clear();
        expandSingleBucketToVertices(result.faceBuckets[i], faceBucketVertices[i]);
    }
}

// Backward-compatible wrapper: Expand all face buckets to a single vertex array
// Use this when face-orientation culling is not needed (e.g., deferred rendering)
inline void expandQuadsToVertices(
    const BinaryMeshResult& result,
    std::vector<PackedChunkVertex>& vertices
) {
    vertices.clear();
    size_t totalQuads = result.getTotalQuadCount();
    vertices.reserve(totalQuads * 6);
    
    for (int i = 0; i < FACE_BUCKET_COUNT; i++) {
        expandSingleBucketToVertices(result.faceBuckets[i], vertices);
    }
}

// Utility to determine which face buckets should be rendered based on camera direction
// Returns a bitmask where bit i is set if face bucket i should be rendered
// This achieves ~35% backface culling by skipping entire face directions
inline uint8_t getFaceVisibilityMask(const glm::vec3& cameraToChunk) {
    uint8_t mask = 0;
    
    // +X faces (bucket 0) visible when camera is on -X side of chunk
    if (cameraToChunk.x < 0) mask |= (1 << 0);
    // -X faces (bucket 1) visible when camera is on +X side of chunk
    if (cameraToChunk.x > 0) mask |= (1 << 1);
    // +Y faces (bucket 2) visible when camera is below chunk
    if (cameraToChunk.y < 0) mask |= (1 << 2);
    // -Y faces (bucket 3) visible when camera is above chunk
    if (cameraToChunk.y > 0) mask |= (1 << 3);
    // +Z faces (bucket 4) visible when camera is on -Z side of chunk
    if (cameraToChunk.z < 0) mask |= (1 << 4);
    // -Z faces (bucket 5) visible when camera is on +Z side of chunk
    if (cameraToChunk.z > 0) mask |= (1 << 5);
    
    return mask;
}
