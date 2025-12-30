#pragma once

// Item System - Types, Properties, and Constants
// Items are non-placeable things: tools, materials, food, armor

#include <cstdint>
#include <array>

// ==================== TOOL TIER ====================
enum class ToolTier : uint8_t {
    NONE = 0,   // Not a tool or hand
    WOOD = 1,
    STONE = 2,
    IRON = 3,
    GOLD = 4,   // Fast but weak durability
    DIAMOND = 5
};

// ==================== TOOL CATEGORY ====================
enum class ToolCategory : uint8_t {
    NONE = 0,
    PICKAXE,
    AXE,
    SHOVEL,
    HOE,
    SWORD,
    SHEARS
};

// ==================== ARMOR SLOT ====================
enum class ArmorSlot : uint8_t {
    NONE = 0,
    HELMET = 1,
    CHESTPLATE = 2,
    LEGGINGS = 3,
    BOOTS = 4
};

// ==================== ITEM TYPE ENUMERATION ====================
// Items are categorized by purpose with gaps for future expansion
enum class ItemType : uint16_t {
    NONE = 0,  // Empty/invalid item

    // === MATERIALS (100-199) ===
    STICK = 100,
    COAL,
    CHARCOAL,
    IRON_INGOT,
    GOLD_INGOT,
    DIAMOND,
    FLINT,
    LEATHER,
    STRING,
    FEATHER,
    BONE,
    BRICK_ITEM,     // For crafting bricks block
    CLAY_BALL,

    // === TOOLS - PICKAXES (200-209) ===
    WOODEN_PICKAXE = 200,
    STONE_PICKAXE,
    IRON_PICKAXE,
    GOLDEN_PICKAXE,
    DIAMOND_PICKAXE,

    // === TOOLS - AXES (210-219) ===
    WOODEN_AXE = 210,
    STONE_AXE,
    IRON_AXE,
    GOLDEN_AXE,
    DIAMOND_AXE,

    // === TOOLS - SHOVELS (220-229) ===
    WOODEN_SHOVEL = 220,
    STONE_SHOVEL,
    IRON_SHOVEL,
    GOLDEN_SHOVEL,
    DIAMOND_SHOVEL,

    // === TOOLS - HOES (230-239) ===
    WOODEN_HOE = 230,
    STONE_HOE,
    IRON_HOE,
    GOLDEN_HOE,
    DIAMOND_HOE,

    // === WEAPONS - SWORDS (240-249) ===
    WOODEN_SWORD = 240,
    STONE_SWORD,
    IRON_SWORD,
    GOLDEN_SWORD,
    DIAMOND_SWORD,

    // === ARMOR - HELMETS (300-309) ===
    LEATHER_HELMET = 300,
    IRON_HELMET,
    GOLDEN_HELMET,
    DIAMOND_HELMET,
    CHAINMAIL_HELMET,

    // === ARMOR - CHESTPLATES (310-319) ===
    LEATHER_CHESTPLATE = 310,
    IRON_CHESTPLATE,
    GOLDEN_CHESTPLATE,
    DIAMOND_CHESTPLATE,
    CHAINMAIL_CHESTPLATE,

    // === ARMOR - LEGGINGS (320-329) ===
    LEATHER_LEGGINGS = 320,
    IRON_LEGGINGS,
    GOLDEN_LEGGINGS,
    DIAMOND_LEGGINGS,
    CHAINMAIL_LEGGINGS,

    // === ARMOR - BOOTS (330-339) ===
    LEATHER_BOOTS = 330,
    IRON_BOOTS,
    GOLDEN_BOOTS,
    DIAMOND_BOOTS,
    CHAINMAIL_BOOTS,

    // === FOOD (400-449) ===
    APPLE = 400,
    GOLDEN_APPLE,
    BREAD,
    RAW_PORKCHOP,
    COOKED_PORKCHOP,
    RAW_BEEF,
    COOKED_BEEF,
    RAW_CHICKEN,
    COOKED_CHICKEN,
    RAW_MUTTON,
    COOKED_MUTTON,
    CARROT,
    POTATO,
    BAKED_POTATO,
    MELON_SLICE,
    COOKIE,
    ROTTEN_FLESH,

    // === MISC (500+) ===
    BUCKET = 500,
    WATER_BUCKET,
    LAVA_BUCKET,
    BOWL,
    MUSHROOM_STEW,

    ITEM_TYPE_COUNT
};

// ==================== DURABILITY CONSTANTS ====================
namespace ItemDurability {
    constexpr int WOOD = 59;
    constexpr int STONE = 131;
    constexpr int IRON = 250;
    constexpr int GOLD = 32;
    constexpr int DIAMOND = 1561;

    // Armor base durability (multiplied by slot)
    constexpr int LEATHER_BASE = 55;
    constexpr int IRON_BASE = 165;
    constexpr int GOLD_BASE = 77;
    constexpr int DIAMOND_BASE = 363;
    constexpr int CHAINMAIL_BASE = 165;

    // Armor slot multipliers: helmet, chest, legs, boots
    constexpr std::array<int, 4> SLOT_MULTIPLIERS = {11, 16, 15, 13};

    inline int getArmorDurability(int baseDurability, ArmorSlot slot) {
        if (slot == ArmorSlot::NONE) return 0;
        int idx = static_cast<int>(slot) - 1;
        return baseDurability * SLOT_MULTIPLIERS[idx] / 10;
    }
}

// ==================== MINING SPEED CONSTANTS ====================
namespace MiningSpeed {
    constexpr float HAND = 1.0f;
    constexpr float WOOD = 2.0f;
    constexpr float STONE = 4.0f;
    constexpr float IRON = 6.0f;
    constexpr float GOLD = 12.0f;   // Gold is fastest
    constexpr float DIAMOND = 8.0f;

    inline float getForTier(ToolTier tier) {
        switch (tier) {
            case ToolTier::WOOD:    return WOOD;
            case ToolTier::STONE:   return STONE;
            case ToolTier::IRON:    return IRON;
            case ToolTier::GOLD:    return GOLD;
            case ToolTier::DIAMOND: return DIAMOND;
            default:                return HAND;
        }
    }
}

// ==================== ATTACK DAMAGE CONSTANTS ====================
namespace AttackDamage {
    // Swords
    constexpr int WOODEN_SWORD = 4;
    constexpr int STONE_SWORD = 5;
    constexpr int IRON_SWORD = 6;
    constexpr int GOLDEN_SWORD = 4;
    constexpr int DIAMOND_SWORD = 7;

    // Tools used as weapons (base damage, add tier bonus)
    constexpr int PICKAXE_BASE = 2;
    constexpr int AXE_BASE = 3;
    constexpr int SHOVEL_BASE = 1;
    constexpr int HOE_BASE = 1;
    constexpr int HAND = 1;
}

// ==================== ARMOR POINTS ====================
namespace ArmorPoints {
    // Per piece: helmet, chest, legs, boots
    constexpr std::array<int, 4> LEATHER = {1, 3, 2, 1};    // Total: 7
    constexpr std::array<int, 4> IRON = {2, 6, 5, 2};       // Total: 15
    constexpr std::array<int, 4> GOLD = {2, 5, 3, 1};       // Total: 11
    constexpr std::array<int, 4> DIAMOND = {3, 8, 6, 3};    // Total: 20
    constexpr std::array<int, 4> CHAINMAIL = {2, 5, 4, 1};  // Total: 12
}

// ==================== FOOD VALUES ====================
// Hunger points (half-drumsticks) and saturation
struct FoodValues {
    int hunger;
    float saturation;
};

namespace Food {
    constexpr FoodValues APPLE = {4, 2.4f};
    constexpr FoodValues GOLDEN_APPLE = {4, 9.6f};
    constexpr FoodValues BREAD = {5, 6.0f};
    constexpr FoodValues RAW_PORKCHOP = {3, 1.8f};
    constexpr FoodValues COOKED_PORKCHOP = {8, 12.8f};
    constexpr FoodValues RAW_BEEF = {3, 1.8f};
    constexpr FoodValues COOKED_BEEF = {8, 12.8f};
    constexpr FoodValues RAW_CHICKEN = {2, 1.2f};
    constexpr FoodValues COOKED_CHICKEN = {6, 7.2f};
    constexpr FoodValues RAW_MUTTON = {2, 1.2f};
    constexpr FoodValues COOKED_MUTTON = {6, 9.6f};
    constexpr FoodValues CARROT = {3, 3.6f};
    constexpr FoodValues POTATO = {1, 0.6f};
    constexpr FoodValues BAKED_POTATO = {5, 6.0f};
    constexpr FoodValues MELON_SLICE = {2, 1.2f};
    constexpr FoodValues COOKIE = {2, 0.4f};
    constexpr FoodValues ROTTEN_FLESH = {4, 0.8f};
    constexpr FoodValues MUSHROOM_STEW = {6, 7.2f};
}

// ==================== ITEM PROPERTIES STRUCT ====================
struct ItemProperties {
    const char* name;
    int maxStackSize;           // 1 for tools/armor, 16 for some, 64 for most
    int maxDurability;          // 0 = infinite (non-tool)
    ToolCategory toolCategory;
    ToolTier toolTier;
    float miningSpeedMultiplier;
    int attackDamage;
    ArmorSlot armorSlot;
    int armorPoints;
    int foodHunger;             // Half-drumsticks restored
    float foodSaturation;
    int textureSlot;            // Slot in item texture atlas

    bool isTool() const { return toolCategory != ToolCategory::NONE; }
    bool isArmor() const { return armorSlot != ArmorSlot::NONE; }
    bool isFood() const { return foodHunger > 0; }
    bool isStackable() const { return maxStackSize > 1; }
    bool hasDurability() const { return maxDurability > 0; }
};

// ==================== ITEM PROPERTIES LOOKUP ====================
inline const ItemProperties& getItemProperties(ItemType type) {
    // Default empty properties
    static const ItemProperties EMPTY = {
        "Unknown", 64, 0, ToolCategory::NONE, ToolTier::NONE,
        MiningSpeed::HAND, AttackDamage::HAND, ArmorSlot::NONE, 0, 0, 0.0f, 0
    };

    // Properties table
    static const ItemProperties PROPERTIES[] = {
        // MATERIALS
        /* STICK */         {"Stick", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, 0, 0.0f, 0},
        /* COAL */          {"Coal", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, 0, 0.0f, 1},
        /* CHARCOAL */      {"Charcoal", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, 0, 0.0f, 2},
        /* IRON_INGOT */    {"Iron Ingot", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, 0, 0.0f, 3},
        /* GOLD_INGOT */    {"Gold Ingot", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, 0, 0.0f, 4},
        /* DIAMOND */       {"Diamond", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, 0, 0.0f, 5},
        /* FLINT */         {"Flint", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, 0, 0.0f, 6},
        /* LEATHER */       {"Leather", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, 0, 0.0f, 7},
        /* STRING */        {"String", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, 0, 0.0f, 8},
        /* FEATHER */       {"Feather", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, 0, 0.0f, 9},
        /* BONE */          {"Bone", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, 0, 0.0f, 10},
        /* BRICK_ITEM */    {"Brick", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, 0, 0.0f, 11},
        /* CLAY_BALL */     {"Clay", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, 0, 0.0f, 12},
    };

    // Pickaxes
    static const ItemProperties PICKAXES[] = {
        {"Wooden Pickaxe", 1, ItemDurability::WOOD, ToolCategory::PICKAXE, ToolTier::WOOD, MiningSpeed::WOOD, 2, ArmorSlot::NONE, 0, 0, 0.0f, 16},
        {"Stone Pickaxe", 1, ItemDurability::STONE, ToolCategory::PICKAXE, ToolTier::STONE, MiningSpeed::STONE, 3, ArmorSlot::NONE, 0, 0, 0.0f, 17},
        {"Iron Pickaxe", 1, ItemDurability::IRON, ToolCategory::PICKAXE, ToolTier::IRON, MiningSpeed::IRON, 4, ArmorSlot::NONE, 0, 0, 0.0f, 18},
        {"Golden Pickaxe", 1, ItemDurability::GOLD, ToolCategory::PICKAXE, ToolTier::GOLD, MiningSpeed::GOLD, 2, ArmorSlot::NONE, 0, 0, 0.0f, 19},
        {"Diamond Pickaxe", 1, ItemDurability::DIAMOND, ToolCategory::PICKAXE, ToolTier::DIAMOND, MiningSpeed::DIAMOND, 5, ArmorSlot::NONE, 0, 0, 0.0f, 20},
    };

    // Axes
    static const ItemProperties AXES[] = {
        {"Wooden Axe", 1, ItemDurability::WOOD, ToolCategory::AXE, ToolTier::WOOD, MiningSpeed::WOOD, 3, ArmorSlot::NONE, 0, 0, 0.0f, 21},
        {"Stone Axe", 1, ItemDurability::STONE, ToolCategory::AXE, ToolTier::STONE, MiningSpeed::STONE, 4, ArmorSlot::NONE, 0, 0, 0.0f, 22},
        {"Iron Axe", 1, ItemDurability::IRON, ToolCategory::AXE, ToolTier::IRON, MiningSpeed::IRON, 5, ArmorSlot::NONE, 0, 0, 0.0f, 23},
        {"Golden Axe", 1, ItemDurability::GOLD, ToolCategory::AXE, ToolTier::GOLD, MiningSpeed::GOLD, 3, ArmorSlot::NONE, 0, 0, 0.0f, 24},
        {"Diamond Axe", 1, ItemDurability::DIAMOND, ToolCategory::AXE, ToolTier::DIAMOND, MiningSpeed::DIAMOND, 6, ArmorSlot::NONE, 0, 0, 0.0f, 25},
    };

    // Shovels
    static const ItemProperties SHOVELS[] = {
        {"Wooden Shovel", 1, ItemDurability::WOOD, ToolCategory::SHOVEL, ToolTier::WOOD, MiningSpeed::WOOD, 1, ArmorSlot::NONE, 0, 0, 0.0f, 26},
        {"Stone Shovel", 1, ItemDurability::STONE, ToolCategory::SHOVEL, ToolTier::STONE, MiningSpeed::STONE, 2, ArmorSlot::NONE, 0, 0, 0.0f, 27},
        {"Iron Shovel", 1, ItemDurability::IRON, ToolCategory::SHOVEL, ToolTier::IRON, MiningSpeed::IRON, 3, ArmorSlot::NONE, 0, 0, 0.0f, 28},
        {"Golden Shovel", 1, ItemDurability::GOLD, ToolCategory::SHOVEL, ToolTier::GOLD, MiningSpeed::GOLD, 1, ArmorSlot::NONE, 0, 0, 0.0f, 29},
        {"Diamond Shovel", 1, ItemDurability::DIAMOND, ToolCategory::SHOVEL, ToolTier::DIAMOND, MiningSpeed::DIAMOND, 4, ArmorSlot::NONE, 0, 0, 0.0f, 30},
    };

    // Hoes
    static const ItemProperties HOES[] = {
        {"Wooden Hoe", 1, ItemDurability::WOOD, ToolCategory::HOE, ToolTier::WOOD, 1.0f, 1, ArmorSlot::NONE, 0, 0, 0.0f, 31},
        {"Stone Hoe", 1, ItemDurability::STONE, ToolCategory::HOE, ToolTier::STONE, 1.0f, 1, ArmorSlot::NONE, 0, 0, 0.0f, 32},
        {"Iron Hoe", 1, ItemDurability::IRON, ToolCategory::HOE, ToolTier::IRON, 1.0f, 1, ArmorSlot::NONE, 0, 0, 0.0f, 33},
        {"Golden Hoe", 1, ItemDurability::GOLD, ToolCategory::HOE, ToolTier::GOLD, 1.0f, 1, ArmorSlot::NONE, 0, 0, 0.0f, 34},
        {"Diamond Hoe", 1, ItemDurability::DIAMOND, ToolCategory::HOE, ToolTier::DIAMOND, 1.0f, 1, ArmorSlot::NONE, 0, 0, 0.0f, 35},
    };

    // Swords
    static const ItemProperties SWORDS[] = {
        {"Wooden Sword", 1, ItemDurability::WOOD, ToolCategory::SWORD, ToolTier::WOOD, 1.0f, AttackDamage::WOODEN_SWORD, ArmorSlot::NONE, 0, 0, 0.0f, 36},
        {"Stone Sword", 1, ItemDurability::STONE, ToolCategory::SWORD, ToolTier::STONE, 1.0f, AttackDamage::STONE_SWORD, ArmorSlot::NONE, 0, 0, 0.0f, 37},
        {"Iron Sword", 1, ItemDurability::IRON, ToolCategory::SWORD, ToolTier::IRON, 1.0f, AttackDamage::IRON_SWORD, ArmorSlot::NONE, 0, 0, 0.0f, 38},
        {"Golden Sword", 1, ItemDurability::GOLD, ToolCategory::SWORD, ToolTier::GOLD, 1.0f, AttackDamage::GOLDEN_SWORD, ArmorSlot::NONE, 0, 0, 0.0f, 39},
        {"Diamond Sword", 1, ItemDurability::DIAMOND, ToolCategory::SWORD, ToolTier::DIAMOND, 1.0f, AttackDamage::DIAMOND_SWORD, ArmorSlot::NONE, 0, 0, 0.0f, 40},
    };

    // Helmets
    static const ItemProperties HELMETS[] = {
        {"Leather Cap", 1, ItemDurability::getArmorDurability(ItemDurability::LEATHER_BASE, ArmorSlot::HELMET), ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::HELMET, ArmorPoints::LEATHER[0], 0, 0.0f, 48},
        {"Iron Helmet", 1, ItemDurability::getArmorDurability(ItemDurability::IRON_BASE, ArmorSlot::HELMET), ToolCategory::NONE, ToolTier::IRON, 1.0f, 1, ArmorSlot::HELMET, ArmorPoints::IRON[0], 0, 0.0f, 49},
        {"Golden Helmet", 1, ItemDurability::getArmorDurability(ItemDurability::GOLD_BASE, ArmorSlot::HELMET), ToolCategory::NONE, ToolTier::GOLD, 1.0f, 1, ArmorSlot::HELMET, ArmorPoints::GOLD[0], 0, 0.0f, 50},
        {"Diamond Helmet", 1, ItemDurability::getArmorDurability(ItemDurability::DIAMOND_BASE, ArmorSlot::HELMET), ToolCategory::NONE, ToolTier::DIAMOND, 1.0f, 1, ArmorSlot::HELMET, ArmorPoints::DIAMOND[0], 0, 0.0f, 51},
        {"Chainmail Helmet", 1, ItemDurability::getArmorDurability(ItemDurability::CHAINMAIL_BASE, ArmorSlot::HELMET), ToolCategory::NONE, ToolTier::IRON, 1.0f, 1, ArmorSlot::HELMET, ArmorPoints::CHAINMAIL[0], 0, 0.0f, 52},
    };

    // Chestplates
    static const ItemProperties CHESTPLATES[] = {
        {"Leather Tunic", 1, ItemDurability::getArmorDurability(ItemDurability::LEATHER_BASE, ArmorSlot::CHESTPLATE), ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::CHESTPLATE, ArmorPoints::LEATHER[1], 0, 0.0f, 53},
        {"Iron Chestplate", 1, ItemDurability::getArmorDurability(ItemDurability::IRON_BASE, ArmorSlot::CHESTPLATE), ToolCategory::NONE, ToolTier::IRON, 1.0f, 1, ArmorSlot::CHESTPLATE, ArmorPoints::IRON[1], 0, 0.0f, 54},
        {"Golden Chestplate", 1, ItemDurability::getArmorDurability(ItemDurability::GOLD_BASE, ArmorSlot::CHESTPLATE), ToolCategory::NONE, ToolTier::GOLD, 1.0f, 1, ArmorSlot::CHESTPLATE, ArmorPoints::GOLD[1], 0, 0.0f, 55},
        {"Diamond Chestplate", 1, ItemDurability::getArmorDurability(ItemDurability::DIAMOND_BASE, ArmorSlot::CHESTPLATE), ToolCategory::NONE, ToolTier::DIAMOND, 1.0f, 1, ArmorSlot::CHESTPLATE, ArmorPoints::DIAMOND[1], 0, 0.0f, 56},
        {"Chainmail Chestplate", 1, ItemDurability::getArmorDurability(ItemDurability::CHAINMAIL_BASE, ArmorSlot::CHESTPLATE), ToolCategory::NONE, ToolTier::IRON, 1.0f, 1, ArmorSlot::CHESTPLATE, ArmorPoints::CHAINMAIL[1], 0, 0.0f, 57},
    };

    // Leggings
    static const ItemProperties LEGGINGS[] = {
        {"Leather Pants", 1, ItemDurability::getArmorDurability(ItemDurability::LEATHER_BASE, ArmorSlot::LEGGINGS), ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::LEGGINGS, ArmorPoints::LEATHER[2], 0, 0.0f, 58},
        {"Iron Leggings", 1, ItemDurability::getArmorDurability(ItemDurability::IRON_BASE, ArmorSlot::LEGGINGS), ToolCategory::NONE, ToolTier::IRON, 1.0f, 1, ArmorSlot::LEGGINGS, ArmorPoints::IRON[2], 0, 0.0f, 59},
        {"Golden Leggings", 1, ItemDurability::getArmorDurability(ItemDurability::GOLD_BASE, ArmorSlot::LEGGINGS), ToolCategory::NONE, ToolTier::GOLD, 1.0f, 1, ArmorSlot::LEGGINGS, ArmorPoints::GOLD[2], 0, 0.0f, 60},
        {"Diamond Leggings", 1, ItemDurability::getArmorDurability(ItemDurability::DIAMOND_BASE, ArmorSlot::LEGGINGS), ToolCategory::NONE, ToolTier::DIAMOND, 1.0f, 1, ArmorSlot::LEGGINGS, ArmorPoints::DIAMOND[2], 0, 0.0f, 61},
        {"Chainmail Leggings", 1, ItemDurability::getArmorDurability(ItemDurability::CHAINMAIL_BASE, ArmorSlot::LEGGINGS), ToolCategory::NONE, ToolTier::IRON, 1.0f, 1, ArmorSlot::LEGGINGS, ArmorPoints::CHAINMAIL[2], 0, 0.0f, 62},
    };

    // Boots
    static const ItemProperties BOOTS[] = {
        {"Leather Boots", 1, ItemDurability::getArmorDurability(ItemDurability::LEATHER_BASE, ArmorSlot::BOOTS), ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::BOOTS, ArmorPoints::LEATHER[3], 0, 0.0f, 63},
        {"Iron Boots", 1, ItemDurability::getArmorDurability(ItemDurability::IRON_BASE, ArmorSlot::BOOTS), ToolCategory::NONE, ToolTier::IRON, 1.0f, 1, ArmorSlot::BOOTS, ArmorPoints::IRON[3], 0, 0.0f, 64},
        {"Golden Boots", 1, ItemDurability::getArmorDurability(ItemDurability::GOLD_BASE, ArmorSlot::BOOTS), ToolCategory::NONE, ToolTier::GOLD, 1.0f, 1, ArmorSlot::BOOTS, ArmorPoints::GOLD[3], 0, 0.0f, 65},
        {"Diamond Boots", 1, ItemDurability::getArmorDurability(ItemDurability::DIAMOND_BASE, ArmorSlot::BOOTS), ToolCategory::NONE, ToolTier::DIAMOND, 1.0f, 1, ArmorSlot::BOOTS, ArmorPoints::DIAMOND[3], 0, 0.0f, 66},
        {"Chainmail Boots", 1, ItemDurability::getArmorDurability(ItemDurability::CHAINMAIL_BASE, ArmorSlot::BOOTS), ToolCategory::NONE, ToolTier::IRON, 1.0f, 1, ArmorSlot::BOOTS, ArmorPoints::CHAINMAIL[3], 0, 0.0f, 67},
    };

    // Food
    static const ItemProperties FOODS[] = {
        {"Apple", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, Food::APPLE.hunger, Food::APPLE.saturation, 80},
        {"Golden Apple", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, Food::GOLDEN_APPLE.hunger, Food::GOLDEN_APPLE.saturation, 81},
        {"Bread", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, Food::BREAD.hunger, Food::BREAD.saturation, 82},
        {"Raw Porkchop", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, Food::RAW_PORKCHOP.hunger, Food::RAW_PORKCHOP.saturation, 83},
        {"Cooked Porkchop", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, Food::COOKED_PORKCHOP.hunger, Food::COOKED_PORKCHOP.saturation, 84},
        {"Raw Beef", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, Food::RAW_BEEF.hunger, Food::RAW_BEEF.saturation, 85},
        {"Steak", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, Food::COOKED_BEEF.hunger, Food::COOKED_BEEF.saturation, 86},
        {"Raw Chicken", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, Food::RAW_CHICKEN.hunger, Food::RAW_CHICKEN.saturation, 87},
        {"Cooked Chicken", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, Food::COOKED_CHICKEN.hunger, Food::COOKED_CHICKEN.saturation, 88},
        {"Raw Mutton", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, Food::RAW_MUTTON.hunger, Food::RAW_MUTTON.saturation, 89},
        {"Cooked Mutton", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, Food::COOKED_MUTTON.hunger, Food::COOKED_MUTTON.saturation, 90},
        {"Carrot", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, Food::CARROT.hunger, Food::CARROT.saturation, 91},
        {"Potato", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, Food::POTATO.hunger, Food::POTATO.saturation, 92},
        {"Baked Potato", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, Food::BAKED_POTATO.hunger, Food::BAKED_POTATO.saturation, 93},
        {"Melon Slice", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, Food::MELON_SLICE.hunger, Food::MELON_SLICE.saturation, 94},
        {"Cookie", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, Food::COOKIE.hunger, Food::COOKIE.saturation, 95},
        {"Rotten Flesh", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, Food::ROTTEN_FLESH.hunger, Food::ROTTEN_FLESH.saturation, 96},
    };

    // Misc items
    static const ItemProperties MISC[] = {
        {"Bucket", 16, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, 0, 0.0f, 112},
        {"Water Bucket", 1, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, 0, 0.0f, 113},
        {"Lava Bucket", 1, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, 0, 0.0f, 114},
        {"Bowl", 64, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, 0, 0.0f, 115},
        {"Mushroom Stew", 1, 0, ToolCategory::NONE, ToolTier::NONE, 1.0f, 1, ArmorSlot::NONE, 0, Food::MUSHROOM_STEW.hunger, Food::MUSHROOM_STEW.saturation, 116},
    };

    // Lookup by type
    uint16_t id = static_cast<uint16_t>(type);

    // Materials: 100-112
    if (id >= 100 && id < 113) return PROPERTIES[id - 100];

    // Pickaxes: 200-204
    if (id >= 200 && id < 205) return PICKAXES[id - 200];

    // Axes: 210-214
    if (id >= 210 && id < 215) return AXES[id - 210];

    // Shovels: 220-224
    if (id >= 220 && id < 225) return SHOVELS[id - 220];

    // Hoes: 230-234
    if (id >= 230 && id < 235) return HOES[id - 230];

    // Swords: 240-244
    if (id >= 240 && id < 245) return SWORDS[id - 240];

    // Helmets: 300-304
    if (id >= 300 && id < 305) return HELMETS[id - 300];

    // Chestplates: 310-314
    if (id >= 310 && id < 315) return CHESTPLATES[id - 310];

    // Leggings: 320-324
    if (id >= 320 && id < 325) return LEGGINGS[id - 320];

    // Boots: 330-334
    if (id >= 330 && id < 335) return BOOTS[id - 330];

    // Food: 400-416
    if (id >= 400 && id < 417) return FOODS[id - 400];

    // Misc: 500-504
    if (id >= 500 && id < 505) return MISC[id - 500];

    return EMPTY;
}

// ==================== HELPER FUNCTIONS ====================
inline const char* getItemName(ItemType type) {
    return getItemProperties(type).name;
}

inline int getMaxStackSize(ItemType type) {
    return getItemProperties(type).maxStackSize;
}

inline bool isItemTool(ItemType type) {
    return getItemProperties(type).isTool();
}

inline bool isItemArmor(ItemType type) {
    return getItemProperties(type).isArmor();
}

inline bool isItemFood(ItemType type) {
    return getItemProperties(type).isFood();
}
