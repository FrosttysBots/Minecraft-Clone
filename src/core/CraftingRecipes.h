#pragma once

// Crafting Recipe System
// Supports both block and item crafting for 2x2 grid
// For 3x3 recipes, see CraftingTableUI.h

#include "../world/Block.h"
#include "Inventory.h"
#include "Item.h"
#include <array>
#include <vector>

// ==================== CRAFTING INGREDIENT ====================
// Can represent either a block or an item
struct CraftingIngredient {
    StackType type = StackType::EMPTY;
    union {
        BlockType blockType;
        ItemType itemType;
    };

    CraftingIngredient() : type(StackType::EMPTY), blockType(BlockType::AIR) {}
    CraftingIngredient(BlockType bt) : type(bt == BlockType::AIR ? StackType::EMPTY : StackType::BLOCK), blockType(bt) {}
    CraftingIngredient(ItemType it) : type(it == ItemType::NONE ? StackType::EMPTY : StackType::ITEM), itemType(it) {}

    bool isEmpty() const { return type == StackType::EMPTY; }
    bool isBlock() const { return type == StackType::BLOCK; }
    bool isItem() const { return type == StackType::ITEM; }

    bool matches(const ItemStack& stack) const {
        if (isEmpty()) return stack.isEmpty();
        if (stack.isEmpty()) return false;
        if (type != stack.stackType) return false;

        if (isBlock()) return blockType == stack.blockType;
        return itemType == stack.itemType;
    }

    bool operator==(const CraftingIngredient& other) const {
        if (type != other.type) return false;
        if (isEmpty()) return true;
        if (isBlock()) return blockType == other.blockType;
        return itemType == other.itemType;
    }
};

// ==================== CRAFTING RESULT ====================
struct CraftingResult {
    StackType type = StackType::EMPTY;
    union {
        BlockType blockType;
        ItemType itemType;
    };
    int count = 0;

    CraftingResult() : type(StackType::EMPTY), blockType(BlockType::AIR), count(0) {}
    CraftingResult(BlockType bt, int c) : type(bt == BlockType::AIR ? StackType::EMPTY : StackType::BLOCK), blockType(bt), count(c) {}
    CraftingResult(ItemType it, int c) : type(it == ItemType::NONE ? StackType::EMPTY : StackType::ITEM), itemType(it), count(c) {}

    bool isEmpty() const { return type == StackType::EMPTY || count <= 0; }

    ItemStack toItemStack() const {
        if (type == StackType::BLOCK) return ItemStack(blockType, count);
        if (type == StackType::ITEM) return ItemStack(itemType, count);
        return ItemStack();
    }
};

// ==================== CRAFTING RECIPE ====================
struct CraftingRecipe {
    // 2x2 grid pattern
    // Layout: [0][1]
    //         [2][3]
    std::array<CraftingIngredient, 4> pattern;

    // Output
    CraftingResult result;

    // Whether pattern is shapeless (any arrangement works)
    bool shapeless = false;

    CraftingRecipe() = default;

    CraftingRecipe(std::array<CraftingIngredient, 4> p, CraftingResult r, bool s = false)
        : pattern(p), result(r), shapeless(s) {}

    // Check if recipe matches a 2x2 crafting grid
    bool matches(const std::array<ItemStack, 4>& grid) const {
        if (shapeless) {
            return matchesShapeless(grid);
        }
        return matchesShaped(grid);
    }

private:
    // Check shaped recipe with pattern normalization
    bool matchesShaped(const std::array<ItemStack, 4>& grid) const {
        // Try direct match first
        if (matchesOrientation(grid)) return true;

        // Normalize both pattern and grid for position-independent matching
        int patternMinX = 2, patternMinY = 2;
        int patternMaxX = -1, patternMaxY = -1;
        for (int i = 0; i < 4; i++) {
            if (!pattern[i].isEmpty()) {
                int x = i % 2;
                int y = i / 2;
                patternMinX = std::min(patternMinX, x);
                patternMinY = std::min(patternMinY, y);
                patternMaxX = std::max(patternMaxX, x);
                patternMaxY = std::max(patternMaxY, y);
            }
        }

        int gridMinX = 2, gridMinY = 2;
        int gridMaxX = -1, gridMaxY = -1;
        for (int i = 0; i < 4; i++) {
            if (!grid[i].isEmpty()) {
                int x = i % 2;
                int y = i / 2;
                gridMinX = std::min(gridMinX, x);
                gridMinY = std::min(gridMinY, y);
                gridMaxX = std::max(gridMaxX, x);
                gridMaxY = std::max(gridMaxY, y);
            }
        }

        // Check dimensions match
        int patternW = (patternMaxX >= 0) ? patternMaxX - patternMinX + 1 : 0;
        int patternH = (patternMaxY >= 0) ? patternMaxY - patternMinY + 1 : 0;
        int gridW = (gridMaxX >= 0) ? gridMaxX - gridMinX + 1 : 0;
        int gridH = (gridMaxY >= 0) ? gridMaxY - gridMinY + 1 : 0;

        if (patternW != gridW || patternH != gridH) return false;

        // Create normalized patterns
        std::array<CraftingIngredient, 4> normalizedPattern;
        std::array<CraftingIngredient, 4> normalizedGrid;

        for (int i = 0; i < 4; i++) {
            int x = i % 2;
            int y = i / 2;

            if (!pattern[i].isEmpty()) {
                int nx = x - patternMinX;
                int ny = y - patternMinY;
                if (nx >= 0 && nx < 2 && ny >= 0 && ny < 2) {
                    normalizedPattern[ny * 2 + nx] = pattern[i];
                }
            }

            if (!grid[i].isEmpty()) {
                int nx = x - gridMinX;
                int ny = y - gridMinY;
                if (nx >= 0 && nx < 2 && ny >= 0 && ny < 2) {
                    normalizedGrid[ny * 2 + nx] = ingredientFromStack(grid[i]);
                }
            }
        }

        // Compare normalized patterns
        for (int i = 0; i < 4; i++) {
            if (!(normalizedPattern[i] == normalizedGrid[i])) {
                return false;
            }
        }
        return true;
    }

    bool matchesOrientation(const std::array<ItemStack, 4>& grid) const {
        for (int i = 0; i < 4; i++) {
            if (!pattern[i].matches(grid[i])) return false;
        }
        return true;
    }

    // Check shapeless recipe
    bool matchesShapeless(const std::array<ItemStack, 4>& grid) const {
        // Count ingredients in pattern
        std::vector<CraftingIngredient> required;
        for (int i = 0; i < 4; i++) {
            if (!pattern[i].isEmpty()) {
                required.push_back(pattern[i]);
            }
        }

        // Count ingredients in grid
        std::vector<CraftingIngredient> provided;
        for (int i = 0; i < 4; i++) {
            if (!grid[i].isEmpty()) {
                provided.push_back(ingredientFromStack(grid[i]));
            }
        }

        // Must have same number of ingredients
        if (required.size() != provided.size()) return false;

        // Try to match each required ingredient
        std::vector<bool> used(provided.size(), false);
        for (const auto& req : required) {
            bool found = false;
            for (size_t j = 0; j < provided.size(); j++) {
                if (!used[j] && req == provided[j]) {
                    used[j] = true;
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }

        return true;
    }

public:
    // Public helper for converting ItemStack to CraftingIngredient
    static CraftingIngredient ingredientFromStack(const ItemStack& stack) {
        if (stack.isEmpty()) return CraftingIngredient();
        if (stack.isBlock()) return CraftingIngredient(stack.blockType);
        return CraftingIngredient(stack.itemType);
    }
};

// ==================== RECIPE REGISTRY ====================
class CraftingRecipeRegistry {
public:
    static CraftingRecipeRegistry& getInstance() {
        static CraftingRecipeRegistry instance;
        return instance;
    }

    void init() {
        recipes.clear();

        // === BASIC BLOCK RECIPES ===

        // Wood Log -> 4 Wood Planks (shapeless)
        addRecipe({{BlockType::WOOD_LOG, BlockType::AIR, BlockType::AIR, BlockType::AIR}},
                  {BlockType::WOOD_PLANKS, 4}, true);

        // 4 Wood Planks -> Crafting Table
        addRecipe({{BlockType::WOOD_PLANKS, BlockType::WOOD_PLANKS,
                    BlockType::WOOD_PLANKS, BlockType::WOOD_PLANKS}},
                  {BlockType::CRAFTING_TABLE, 1}, false);

        // 4 Cobblestone -> 4 Bricks (simplified)
        addRecipe({{BlockType::COBBLESTONE, BlockType::COBBLESTONE,
                    BlockType::COBBLESTONE, BlockType::COBBLESTONE}},
                  {BlockType::BRICK, 4}, false);

        // 4 Sand -> 4 Glass (simplified smelting)
        addRecipe({{BlockType::SAND, BlockType::SAND,
                    BlockType::SAND, BlockType::SAND}},
                  {BlockType::GLASS, 4}, false);

        // === ITEM RECIPES ===

        // 2 Planks (vertical) -> 4 Sticks
        addRecipe({{BlockType::WOOD_PLANKS, BlockType::AIR,
                    BlockType::WOOD_PLANKS, BlockType::AIR}},
                  {ItemType::STICK, 4}, false);

        // === ORE PROCESSING (simplified - normally needs furnace) ===

        // Coal Ore -> Coal (shapeless, simplified)
        addRecipe({{BlockType::COAL_ORE, BlockType::AIR, BlockType::AIR, BlockType::AIR}},
                  {ItemType::COAL, 1}, true);

        // Iron Ore -> Iron Ingot (shapeless, simplified - normally needs furnace)
        addRecipe({{BlockType::IRON_ORE, BlockType::AIR, BlockType::AIR, BlockType::AIR}},
                  {ItemType::IRON_INGOT, 1}, true);

        // Gold Ore -> Gold Ingot (shapeless, simplified)
        addRecipe({{BlockType::GOLD_ORE, BlockType::AIR, BlockType::AIR, BlockType::AIR}},
                  {ItemType::GOLD_INGOT, 1}, true);

        // === SIMPLIFIED TOOL RECIPES (2x2 versions) ===
        // Note: Real Minecraft uses 3x3 crafting table for tools
        // These are simplified recipes that work in 2x2 inventory grid

        // 2 Planks + 2 Sticks = Wooden Pickaxe (simplified)
        addRecipe({{BlockType::WOOD_PLANKS, BlockType::WOOD_PLANKS,
                    ItemType::STICK, ItemType::STICK}},
                  {ItemType::WOODEN_PICKAXE, 1}, false);

        // 2 Cobblestone + 2 Sticks = Stone Pickaxe (simplified)
        addRecipe({{BlockType::COBBLESTONE, BlockType::COBBLESTONE,
                    ItemType::STICK, ItemType::STICK}},
                  {ItemType::STONE_PICKAXE, 1}, false);

        // 2 Iron Ingots + 2 Sticks = Iron Pickaxe (simplified)
        addRecipe({{ItemType::IRON_INGOT, ItemType::IRON_INGOT,
                    ItemType::STICK, ItemType::STICK}},
                  {ItemType::IRON_PICKAXE, 1}, false);

        // 2 Diamonds + 2 Sticks = Diamond Pickaxe (simplified)
        addRecipe({{ItemType::DIAMOND, ItemType::DIAMOND,
                    ItemType::STICK, ItemType::STICK}},
                  {ItemType::DIAMOND_PICKAXE, 1}, false);

        // Wooden Axe
        addRecipe({{BlockType::WOOD_PLANKS, BlockType::WOOD_PLANKS,
                    BlockType::WOOD_PLANKS, ItemType::STICK}},
                  {ItemType::WOODEN_AXE, 1}, false);

        // Stone Axe
        addRecipe({{BlockType::COBBLESTONE, BlockType::COBBLESTONE,
                    BlockType::COBBLESTONE, ItemType::STICK}},
                  {ItemType::STONE_AXE, 1}, false);

        // Wooden Sword (2 planks vertical + 1 stick)
        addRecipe({{BlockType::WOOD_PLANKS, BlockType::AIR,
                    BlockType::WOOD_PLANKS, ItemType::STICK}},
                  {ItemType::WOODEN_SWORD, 1}, false);

        // Stone Sword
        addRecipe({{BlockType::COBBLESTONE, BlockType::AIR,
                    BlockType::COBBLESTONE, ItemType::STICK}},
                  {ItemType::STONE_SWORD, 1}, false);

        // Wooden Shovel (1 plank + 2 sticks)
        addRecipe({{BlockType::WOOD_PLANKS, BlockType::AIR,
                    ItemType::STICK, ItemType::STICK}},
                  {ItemType::WOODEN_SHOVEL, 1}, false);

        // Stone Shovel
        addRecipe({{BlockType::COBBLESTONE, BlockType::AIR,
                    ItemType::STICK, ItemType::STICK}},
                  {ItemType::STONE_SHOVEL, 1}, false);
    }

    // Find matching recipe for grid
    const CraftingRecipe* findRecipe(const std::array<ItemStack, 4>& grid) const {
        // Check if grid is empty
        bool empty = true;
        for (const auto& slot : grid) {
            if (!slot.isEmpty()) {
                empty = false;
                break;
            }
        }
        if (empty) return nullptr;

        // Find matching recipe
        for (const auto& recipe : recipes) {
            if (recipe.matches(grid)) {
                return &recipe;
            }
        }
        return nullptr;
    }

    const std::vector<CraftingRecipe>& getAllRecipes() const {
        return recipes;
    }

private:
    std::vector<CraftingRecipe> recipes;

    void addRecipe(std::array<CraftingIngredient, 4> pattern,
                   CraftingResult result, bool shapeless = false) {
        recipes.emplace_back(pattern, result, shapeless);
    }

    CraftingRecipeRegistry() = default;
    CraftingRecipeRegistry(const CraftingRecipeRegistry&) = delete;
    CraftingRecipeRegistry& operator=(const CraftingRecipeRegistry&) = delete;
};

// ==================== INVENTORY CRAFTING IMPLEMENTATION ====================
inline void Inventory::updateCraftingResult() {
    const CraftingRecipe* recipe = CraftingRecipeRegistry::getInstance().findRecipe(craftingGrid);
    if (recipe && !recipe->result.isEmpty()) {
        craftingResult = recipe->result.toItemStack();
    } else {
        craftingResult.clear();
    }
}

inline void Inventory::craftItem() {
    if (craftingResult.isEmpty()) return;

    const CraftingRecipe* recipe = CraftingRecipeRegistry::getInstance().findRecipe(craftingGrid);
    if (!recipe) return;

    // Check if cursor can accept result
    if (cursorStack.isEmpty()) {
        cursorStack = craftingResult;
    } else if (cursorStack.isSameType(craftingResult) && cursorStack.canMergeWith(craftingResult)) {
        if (cursorStack.count + craftingResult.count <= cursorStack.getMaxStackSize()) {
            cursorStack.count += craftingResult.count;
        } else {
            return;  // Can't fit result
        }
    } else {
        return;  // Can't pick up result
    }

    // Consume one of each ingredient
    for (auto& slot : craftingGrid) {
        if (!slot.isEmpty()) {
            slot.remove(1);
        }
    }

    // Update result for remaining ingredients
    updateCraftingResult();
}
