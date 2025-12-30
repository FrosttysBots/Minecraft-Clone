#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <array>

// Forward declarations for tool system
enum class ToolCategory : uint8_t;
enum class ToolTier : uint8_t;

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
    CRAFTING_TABLE,

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

        case BlockType::CRAFTING_TABLE:
            return { true, false, false, {{
                {0.55f, 0.35f, 0.2f}, {0.55f, 0.35f, 0.2f},  // Front/Back (side)
                {0.55f, 0.35f, 0.2f}, {0.55f, 0.35f, 0.2f},  // Left/Right (side)
                {0.45f, 0.3f, 0.15f}, {0.5f, 0.32f, 0.18f}   // Top (crafting grid) / Bottom
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

// Get block hardness (time in seconds to mine with bare hands)
// -1 means unbreakable (bedrock)
inline float getBlockHardness(BlockType type) {
    switch (type) {
        case BlockType::AIR:           return 0.0f;
        case BlockType::BEDROCK:       return -1.0f;  // Unbreakable
        case BlockType::WATER:         return -1.0f;  // Can't mine water
        case BlockType::LAVA:          return -1.0f;  // Can't mine lava

        // Instant/very fast
        case BlockType::LEAVES:        return 0.3f;
        case BlockType::GLASS:         return 0.4f;
        case BlockType::GLOWSTONE:     return 0.4f;

        // Soft blocks
        case BlockType::DIRT:          return 0.6f;
        case BlockType::GRASS:         return 0.7f;
        case BlockType::SAND:          return 0.6f;
        case BlockType::GRAVEL:        return 0.7f;
        case BlockType::SNOW_BLOCK:    return 0.3f;

        // Wood
        case BlockType::WOOD_LOG:      return 1.5f;
        case BlockType::WOOD_PLANKS:   return 1.2f;
        case BlockType::CRAFTING_TABLE: return 1.0f;
        case BlockType::CACTUS:        return 0.5f;

        // Stone/ores (slower without pickaxe)
        case BlockType::STONE:         return 2.0f;
        case BlockType::COBBLESTONE:   return 2.5f;
        case BlockType::BRICK:         return 2.5f;
        case BlockType::COAL_ORE:      return 2.5f;
        case BlockType::IRON_ORE:      return 3.0f;
        case BlockType::GOLD_ORE:      return 3.5f;
        case BlockType::DIAMOND_ORE:   return 4.0f;

        default:                       return 1.0f;
    }
}

// Get what block type drops when mined
// Returns AIR if the block drops nothing
inline BlockType getBlockDrop(BlockType type) {
    switch (type) {
        case BlockType::STONE:         return BlockType::COBBLESTONE;  // Stone drops cobblestone
        case BlockType::GRASS:         return BlockType::DIRT;         // Grass drops dirt
        case BlockType::LEAVES:        return BlockType::AIR;          // Leaves drop nothing (no saplings yet)
        case BlockType::GLASS:         return BlockType::AIR;          // Glass breaks, drops nothing

        // Ores drop themselves (would drop items like coal/diamonds with proper item system)
        case BlockType::COAL_ORE:      return BlockType::COAL_ORE;
        case BlockType::IRON_ORE:      return BlockType::IRON_ORE;
        case BlockType::GOLD_ORE:      return BlockType::GOLD_ORE;
        case BlockType::DIAMOND_ORE:   return BlockType::DIAMOND_ORE;

        // Unbreakable blocks drop nothing
        case BlockType::BEDROCK:       return BlockType::AIR;
        case BlockType::WATER:         return BlockType::AIR;
        case BlockType::LAVA:          return BlockType::AIR;

        // Everything else drops itself
        default:                       return type;
    }
}

// Check if a block can be broken in survival
inline bool isBlockBreakable(BlockType type) {
    float hardness = getBlockHardness(type);
    return hardness >= 0.0f && type != BlockType::AIR;
}

// ==================== TOOL INTEGRATION ====================
// These functions require Item.h to be included for full functionality
// Forward-declared enums allow this header to compile standalone

// Note: ToolCategory and ToolTier are defined in Item.h
// NONE = 0, PICKAXE = 1, AXE = 2, SHOVEL = 3, HOE = 4, SWORD = 5, SHEARS = 6

// Get what tool type is most effective for this block
inline int getEffectiveToolCategory(BlockType type) {
    // Returns integer matching ToolCategory enum
    // 0=NONE, 1=PICKAXE, 2=AXE, 3=SHOVEL, 4=HOE, 5=SWORD, 6=SHEARS
    switch (type) {
        // Pickaxe blocks (stone, ores, brick)
        case BlockType::STONE:
        case BlockType::COBBLESTONE:
        case BlockType::COAL_ORE:
        case BlockType::IRON_ORE:
        case BlockType::GOLD_ORE:
        case BlockType::DIAMOND_ORE:
        case BlockType::BRICK:
        case BlockType::BEDROCK:
        case BlockType::GLOWSTONE:
            return 1;  // PICKAXE

        // Axe blocks (wood)
        case BlockType::WOOD_LOG:
        case BlockType::WOOD_PLANKS:
        case BlockType::CRAFTING_TABLE:
            return 2;  // AXE

        // Shovel blocks (soft ground)
        case BlockType::DIRT:
        case BlockType::GRASS:
        case BlockType::SAND:
        case BlockType::GRAVEL:
        case BlockType::SNOW_BLOCK:
            return 3;  // SHOVEL

        // Shears effective
        case BlockType::LEAVES:
            return 6;  // SHEARS

        // Sword effective
        case BlockType::CACTUS:
            return 5;  // SWORD

        default:
            return 0;  // NONE - any tool works equally
    }
}

// Get minimum tool tier required to harvest this block (get drops)
// Returns integer matching ToolTier enum
// 0=NONE (hand works), 1=WOOD, 2=STONE, 3=IRON, 4=GOLD, 5=DIAMOND
inline int getMinimumToolTier(BlockType type) {
    switch (type) {
        // Stone tier required
        case BlockType::IRON_ORE:
            return 2;  // STONE

        // Iron tier required
        case BlockType::GOLD_ORE:
        case BlockType::DIAMOND_ORE:
            return 3;  // IRON

        // These drop nothing regardless of tool
        case BlockType::BEDROCK:
        case BlockType::WATER:
        case BlockType::LAVA:
            return 99;  // Unharvestabl

        // Everything else can be harvested by hand
        default:
            return 0;  // NONE - hand works
    }
}

// Check if a block will drop items with the given tool
// toolCategory and toolTier should be cast from ToolCategory and ToolTier enums
inline bool canHarvestBlock(BlockType block, int toolCategory, int toolTier) {
    int requiredCategory = getEffectiveToolCategory(block);
    int requiredTier = getMinimumToolTier(block);

    // If block requires no special tool, always harvestable
    if (requiredTier == 0) return true;

    // If block is unharvestable (bedrock, etc.)
    if (requiredTier >= 99) return false;

    // If block requires specific tool category
    if (requiredCategory != 0) {
        // Must use correct tool type with sufficient tier
        if (toolCategory != requiredCategory) return false;
        return toolTier >= requiredTier;
    }

    // Tool tier check only
    return toolTier >= requiredTier;
}

// Check if using the correct tool for bonus speed
inline bool isCorrectToolForBlock(BlockType block, int toolCategory) {
    int effective = getEffectiveToolCategory(block);
    return effective != 0 && effective == toolCategory;
}

// ==================== ITEM DROP SYSTEM ====================
// Structure to represent what drops from a block
struct BlockDrop {
    bool isItem;       // true = item drop, false = block drop
    int typeId;        // BlockType or ItemType cast to int
    int count;         // How many drop

    BlockDrop() : isItem(false), typeId(0), count(0) {}
    BlockDrop(BlockType block, int cnt = 1) : isItem(false), typeId(static_cast<int>(block)), count(cnt) {}

    // For item drops - use ItemType values directly
    static BlockDrop item(int itemTypeId, int cnt = 1) {
        BlockDrop d;
        d.isItem = true;
        d.typeId = itemTypeId;
        d.count = cnt;
        return d;
    }

    bool isEmpty() const { return count <= 0 || typeId == 0; }
};

// Get what a block drops when mined (supports both block and item drops)
// ItemType values: COAL=101, DIAMOND=105 (from Item.h)
inline BlockDrop getBlockDropNew(BlockType type) {
    switch (type) {
        case BlockType::STONE:
            return BlockDrop(BlockType::COBBLESTONE, 1);

        case BlockType::GRASS:
            return BlockDrop(BlockType::DIRT, 1);

        case BlockType::LEAVES:
            return BlockDrop();  // Nothing (could add sapling chance)

        case BlockType::GLASS:
            return BlockDrop();  // Glass breaks

        // Ores that drop items
        case BlockType::COAL_ORE:
            return BlockDrop::item(101, 1);  // COAL = 101

        case BlockType::DIAMOND_ORE:
            return BlockDrop::item(105, 1);  // DIAMOND = 105

        // Ores that drop themselves (need smelting)
        case BlockType::IRON_ORE:
            return BlockDrop(BlockType::IRON_ORE, 1);

        case BlockType::GOLD_ORE:
            return BlockDrop(BlockType::GOLD_ORE, 1);

        // Unbreakable blocks
        case BlockType::BEDROCK:
        case BlockType::WATER:
        case BlockType::LAVA:
            return BlockDrop();

        // Everything else drops itself
        default:
            return BlockDrop(type, 1);
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
