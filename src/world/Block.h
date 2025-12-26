#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <array>

// Block type IDs
enum class BlockType : uint8_t {
    AIR = 0,
    STONE,
    DIRT,
    GRASS,
    COBBLESTONE,
    WOOD_PLANKS,
    WOOD_LOG,
    LEAVES,
    SAND,
    GRAVEL,
    WATER,
    BEDROCK,
    COAL_ORE,
    IRON_ORE,
    GOLD_ORE,
    DIAMOND_ORE,
    GLASS,
    BRICK,
    SNOW_BLOCK,
    CACTUS,
    GLOWSTONE,
    LAVA,

    COUNT // Total number of block types
};

// Block face directions
enum class BlockFace : uint8_t {
    FRONT = 0,  // +Z
    BACK,       // -Z
    LEFT,       // -X
    RIGHT,      // +X
    TOP,        // +Y
    BOTTOM      // -Y
};

// Block properties
struct BlockProperties {
    bool isSolid;       // Does it block movement/visibility?
    bool isTransparent; // Can you see through it?
    bool isLiquid;      // Is it a fluid?

    // Colors per face (temporary until we add textures)
    // Order: front, back, left, right, top, bottom
    std::array<glm::vec3, 6> faceColors;
};

// Get properties for a block type
inline BlockProperties getBlockProperties(BlockType type) {
    switch (type) {
        case BlockType::AIR:
            return { false, true, false, {} };

        case BlockType::STONE:
            return { true, false, false, {{
                {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f},
                {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f},
                {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}
            }}};

        case BlockType::DIRT:
            return { true, false, false, {{
                {0.55f, 0.35f, 0.2f}, {0.55f, 0.35f, 0.2f},
                {0.55f, 0.35f, 0.2f}, {0.55f, 0.35f, 0.2f},
                {0.55f, 0.35f, 0.2f}, {0.55f, 0.35f, 0.2f}
            }}};

        case BlockType::GRASS:
            return { true, false, false, {{
                {0.55f, 0.35f, 0.2f}, {0.55f, 0.35f, 0.2f},  // Front/back (dirt)
                {0.55f, 0.35f, 0.2f}, {0.55f, 0.35f, 0.2f},  // Left/right (dirt)
                {0.3f, 0.7f, 0.2f},   {0.55f, 0.35f, 0.2f}   // Top (grass) / bottom (dirt)
            }}};

        case BlockType::COBBLESTONE:
            return { true, false, false, {{
                {0.4f, 0.4f, 0.4f}, {0.4f, 0.4f, 0.4f},
                {0.4f, 0.4f, 0.4f}, {0.4f, 0.4f, 0.4f},
                {0.4f, 0.4f, 0.4f}, {0.4f, 0.4f, 0.4f}
            }}};

        case BlockType::WOOD_PLANKS:
            return { true, false, false, {{
                {0.7f, 0.5f, 0.3f}, {0.7f, 0.5f, 0.3f},
                {0.7f, 0.5f, 0.3f}, {0.7f, 0.5f, 0.3f},
                {0.7f, 0.5f, 0.3f}, {0.7f, 0.5f, 0.3f}
            }}};

        case BlockType::WOOD_LOG:
            return { true, false, false, {{
                {0.4f, 0.3f, 0.2f}, {0.4f, 0.3f, 0.2f},  // Bark sides
                {0.4f, 0.3f, 0.2f}, {0.4f, 0.3f, 0.2f},
                {0.6f, 0.5f, 0.3f}, {0.6f, 0.5f, 0.3f}   // Top/bottom rings
            }}};

        case BlockType::LEAVES:
            return { true, true, false, {{
                {0.2f, 0.5f, 0.1f}, {0.2f, 0.5f, 0.1f},
                {0.2f, 0.5f, 0.1f}, {0.2f, 0.5f, 0.1f},
                {0.2f, 0.5f, 0.1f}, {0.2f, 0.5f, 0.1f}
            }}};

        case BlockType::SAND:
            return { true, false, false, {{
                {0.9f, 0.85f, 0.6f}, {0.9f, 0.85f, 0.6f},
                {0.9f, 0.85f, 0.6f}, {0.9f, 0.85f, 0.6f},
                {0.9f, 0.85f, 0.6f}, {0.9f, 0.85f, 0.6f}
            }}};

        case BlockType::GRAVEL:
            return { true, false, false, {{
                {0.55f, 0.52f, 0.5f}, {0.55f, 0.52f, 0.5f},
                {0.55f, 0.52f, 0.5f}, {0.55f, 0.52f, 0.5f},
                {0.55f, 0.52f, 0.5f}, {0.55f, 0.52f, 0.5f}
            }}};

        case BlockType::WATER:
            return { false, true, true, {{
                {0.2f, 0.4f, 0.8f}, {0.2f, 0.4f, 0.8f},
                {0.2f, 0.4f, 0.8f}, {0.2f, 0.4f, 0.8f},
                {0.2f, 0.4f, 0.8f}, {0.2f, 0.4f, 0.8f}
            }}};

        case BlockType::BEDROCK:
            return { true, false, false, {{
                {0.2f, 0.2f, 0.2f}, {0.2f, 0.2f, 0.2f},
                {0.2f, 0.2f, 0.2f}, {0.2f, 0.2f, 0.2f},
                {0.2f, 0.2f, 0.2f}, {0.2f, 0.2f, 0.2f}
            }}};

        case BlockType::COAL_ORE:
            return { true, false, false, {{
                {0.4f, 0.4f, 0.4f}, {0.4f, 0.4f, 0.4f},
                {0.4f, 0.4f, 0.4f}, {0.4f, 0.4f, 0.4f},
                {0.4f, 0.4f, 0.4f}, {0.4f, 0.4f, 0.4f}
            }}};

        case BlockType::IRON_ORE:
            return { true, false, false, {{
                {0.6f, 0.55f, 0.5f}, {0.6f, 0.55f, 0.5f},
                {0.6f, 0.55f, 0.5f}, {0.6f, 0.55f, 0.5f},
                {0.6f, 0.55f, 0.5f}, {0.6f, 0.55f, 0.5f}
            }}};

        case BlockType::GOLD_ORE:
            return { true, false, false, {{
                {0.8f, 0.7f, 0.3f}, {0.8f, 0.7f, 0.3f},
                {0.8f, 0.7f, 0.3f}, {0.8f, 0.7f, 0.3f},
                {0.8f, 0.7f, 0.3f}, {0.8f, 0.7f, 0.3f}
            }}};

        case BlockType::DIAMOND_ORE:
            return { true, false, false, {{
                {0.4f, 0.8f, 0.9f}, {0.4f, 0.8f, 0.9f},
                {0.4f, 0.8f, 0.9f}, {0.4f, 0.8f, 0.9f},
                {0.4f, 0.8f, 0.9f}, {0.4f, 0.8f, 0.9f}
            }}};

        case BlockType::GLASS:
            return { true, true, false, {{
                {0.8f, 0.9f, 0.95f}, {0.8f, 0.9f, 0.95f},
                {0.8f, 0.9f, 0.95f}, {0.8f, 0.9f, 0.95f},
                {0.8f, 0.9f, 0.95f}, {0.8f, 0.9f, 0.95f}
            }}};

        case BlockType::BRICK:
            return { true, false, false, {{
                {0.7f, 0.35f, 0.3f}, {0.7f, 0.35f, 0.3f},
                {0.7f, 0.35f, 0.3f}, {0.7f, 0.35f, 0.3f},
                {0.7f, 0.35f, 0.3f}, {0.7f, 0.35f, 0.3f}
            }}};

        case BlockType::SNOW_BLOCK:
            return { true, false, false, {{
                {0.95f, 0.97f, 1.0f}, {0.95f, 0.97f, 1.0f},
                {0.95f, 0.97f, 1.0f}, {0.95f, 0.97f, 1.0f},
                {0.95f, 0.97f, 1.0f}, {0.95f, 0.97f, 1.0f}
            }}};

        case BlockType::CACTUS:
            return { true, false, false, {{
                {0.2f, 0.5f, 0.2f}, {0.2f, 0.5f, 0.2f},
                {0.2f, 0.5f, 0.2f}, {0.2f, 0.5f, 0.2f},
                {0.25f, 0.55f, 0.2f}, {0.25f, 0.55f, 0.2f}
            }}};

        case BlockType::GLOWSTONE:
            return { true, false, false, {{
                {1.0f, 0.9f, 0.5f}, {1.0f, 0.9f, 0.5f},
                {1.0f, 0.9f, 0.5f}, {1.0f, 0.9f, 0.5f},
                {1.0f, 0.9f, 0.5f}, {1.0f, 0.9f, 0.5f}
            }}};

        case BlockType::LAVA:
            return { false, true, true, {{
                {1.0f, 0.4f, 0.1f}, {1.0f, 0.4f, 0.1f},
                {1.0f, 0.4f, 0.1f}, {1.0f, 0.4f, 0.1f},
                {1.0f, 0.4f, 0.1f}, {1.0f, 0.4f, 0.1f}
            }}};

        default:
            return { true, false, false, {{
                {1.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f},
                {1.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f},
                {1.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f}
            }}}; // Magenta for missing
    }
}

// Helper to check if block is solid
inline bool isBlockSolid(BlockType type) {
    return getBlockProperties(type).isSolid;
}

// Helper to check if block is transparent
inline bool isBlockTransparent(BlockType type) {
    return getBlockProperties(type).isTransparent;
}

// Helper to check if block is emissive (glows)
inline bool isBlockEmissive(BlockType type) {
    return type == BlockType::GLOWSTONE || type == BlockType::LAVA;
}

// Get emission strength for emissive blocks (0-1)
inline float getBlockEmission(BlockType type) {
    switch (type) {
        case BlockType::GLOWSTONE: return 1.0f;
        case BlockType::LAVA: return 0.9f;
        default: return 0.0f;
    }
}

// Texture slots in the atlas
// Order: front, back, left, right, top, bottom
struct BlockTextures {
    std::array<int, 6> faceSlots;
};

// Get texture slots for a block type
inline BlockTextures getBlockTextures(BlockType type) {
    switch (type) {
        case BlockType::AIR:
            return {{ 0, 0, 0, 0, 0, 0 }};

        case BlockType::STONE:
            return {{ 0, 0, 0, 0, 0, 0 }};  // Stone texture

        case BlockType::DIRT:
            return {{ 1, 1, 1, 1, 1, 1 }};  // Dirt texture

        case BlockType::GRASS:
            return {{ 3, 3, 3, 3, 2, 1 }};  // Side, side, side, side, grass top, dirt bottom

        case BlockType::COBBLESTONE:
            return {{ 4, 4, 4, 4, 4, 4 }};  // Cobblestone

        case BlockType::WOOD_PLANKS:
            return {{ 5, 5, 5, 5, 5, 5 }};  // Planks

        case BlockType::WOOD_LOG:
            return {{ 6, 6, 6, 6, 7, 7 }};  // Bark sides, log top

        case BlockType::LEAVES:
            return {{ 8, 8, 8, 8, 8, 8 }};  // Leaves

        case BlockType::SAND:
            return {{ 9, 9, 9, 9, 9, 9 }};  // Sand

        case BlockType::GRAVEL:
            return {{ 10, 10, 10, 10, 10, 10 }};  // Gravel

        case BlockType::WATER:
            return {{ 11, 11, 11, 11, 11, 11 }};  // Water

        case BlockType::BEDROCK:
            return {{ 12, 12, 12, 12, 12, 12 }};  // Bedrock

        case BlockType::COAL_ORE:
            return {{ 13, 13, 13, 13, 13, 13 }};  // Coal ore

        case BlockType::IRON_ORE:
            return {{ 14, 14, 14, 14, 14, 14 }};  // Iron ore

        case BlockType::GOLD_ORE:
            return {{ 15, 15, 15, 15, 15, 15 }};  // Gold ore

        case BlockType::DIAMOND_ORE:
            return {{ 16, 16, 16, 16, 16, 16 }};  // Diamond ore (row 1)

        case BlockType::GLASS:
            return {{ 17, 17, 17, 17, 17, 17 }};  // Glass

        case BlockType::BRICK:
            return {{ 18, 18, 18, 18, 18, 18 }};  // Brick

        case BlockType::SNOW_BLOCK:
            return {{ 19, 19, 19, 19, 19, 19 }};  // Snow

        case BlockType::CACTUS:
            return {{ 20, 20, 20, 20, 21, 21 }};  // Cactus side, top

        case BlockType::GLOWSTONE:
            return {{ 22, 22, 22, 22, 22, 22 }};  // Glowstone

        case BlockType::LAVA:
            return {{ 23, 23, 23, 23, 23, 23 }};  // Lava

        default:
            return {{ 0, 0, 0, 0, 0, 0 }};  // Default to stone
    }
}
