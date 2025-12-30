#pragma once

// Crafting Table UI - 3x3 crafting grid interface
// Opened when right-clicking a crafting table block
// Supports full-size tool and armor recipes

#include "MenuUI.h"
#include "../core/Inventory.h"
#include "../core/Item.h"
#include "../core/CraftingRecipes.h"
#include "../render/TextureAtlas.h"
#include "../render/ItemAtlas.h"
#include "../world/Block.h"
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include <array>
#include <vector>

// External config reference
extern GameConfig g_config;

// ==================== 3x3 CRAFTING RECIPE ====================
struct CraftingRecipe3x3 {
    // 3x3 grid pattern
    // Layout: [0][1][2]
    //         [3][4][5]
    //         [6][7][8]
    std::array<CraftingIngredient, 9> pattern;
    CraftingResult result;
    bool shapeless = false;

    CraftingRecipe3x3() = default;
    CraftingRecipe3x3(std::array<CraftingIngredient, 9> p, CraftingResult r, bool s = false)
        : pattern(p), result(r), shapeless(s) {}

    bool matches(const std::array<ItemStack, 9>& grid) const {
        if (shapeless) {
            return matchesShapeless(grid);
        }
        return matchesShaped(grid);
    }

private:
    bool matchesShaped(const std::array<ItemStack, 9>& grid) const {
        // Direct match
        if (matchesOrientation(grid)) return true;

        // Normalized pattern matching (same as 2x2 but for 3x3)
        int patternMinX = 3, patternMinY = 3;
        int patternMaxX = -1, patternMaxY = -1;
        for (int i = 0; i < 9; i++) {
            if (!pattern[i].isEmpty()) {
                int x = i % 3;
                int y = i / 3;
                patternMinX = std::min(patternMinX, x);
                patternMinY = std::min(patternMinY, y);
                patternMaxX = std::max(patternMaxX, x);
                patternMaxY = std::max(patternMaxY, y);
            }
        }

        int gridMinX = 3, gridMinY = 3;
        int gridMaxX = -1, gridMaxY = -1;
        for (int i = 0; i < 9; i++) {
            if (!grid[i].isEmpty()) {
                int x = i % 3;
                int y = i / 3;
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
        std::array<CraftingIngredient, 9> normalizedPattern{};
        std::array<CraftingIngredient, 9> normalizedGrid{};

        for (int i = 0; i < 9; i++) {
            int x = i % 3;
            int y = i / 3;

            if (!pattern[i].isEmpty()) {
                int nx = x - patternMinX;
                int ny = y - patternMinY;
                if (nx >= 0 && nx < 3 && ny >= 0 && ny < 3) {
                    normalizedPattern[ny * 3 + nx] = pattern[i];
                }
            }

            if (!grid[i].isEmpty()) {
                int nx = x - gridMinX;
                int ny = y - gridMinY;
                if (nx >= 0 && nx < 3 && ny >= 0 && ny < 3) {
                    normalizedGrid[ny * 3 + nx] = CraftingRecipe::ingredientFromStack(grid[i]);
                }
            }
        }

        // Compare normalized patterns
        for (int i = 0; i < 9; i++) {
            if (!(normalizedPattern[i] == normalizedGrid[i])) {
                return false;
            }
        }
        return true;
    }

    bool matchesOrientation(const std::array<ItemStack, 9>& grid) const {
        for (int i = 0; i < 9; i++) {
            if (!pattern[i].matches(grid[i])) return false;
        }
        return true;
    }

    bool matchesShapeless(const std::array<ItemStack, 9>& grid) const {
        std::vector<CraftingIngredient> required;
        for (int i = 0; i < 9; i++) {
            if (!pattern[i].isEmpty()) {
                required.push_back(pattern[i]);
            }
        }

        std::vector<CraftingIngredient> provided;
        for (int i = 0; i < 9; i++) {
            if (!grid[i].isEmpty()) {
                provided.push_back(CraftingRecipe::ingredientFromStack(grid[i]));
            }
        }

        if (required.size() != provided.size()) return false;

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
};

// ==================== 3x3 RECIPE REGISTRY ====================
class CraftingTableRecipeRegistry {
public:
    static CraftingTableRecipeRegistry& getInstance() {
        static CraftingTableRecipeRegistry instance;
        return instance;
    }

    void init() {
        recipes.clear();

        // Helper lambda for empty ingredient
        auto E = []() { return CraftingIngredient(); };

        // === FULL TOOL RECIPES ===

        // Wooden Pickaxe
        addRecipe({
            BlockType::WOOD_PLANKS, BlockType::WOOD_PLANKS, BlockType::WOOD_PLANKS,
            E(), ItemType::STICK, E(),
            E(), ItemType::STICK, E()
        }, {ItemType::WOODEN_PICKAXE, 1});

        // Stone Pickaxe
        addRecipe({
            BlockType::COBBLESTONE, BlockType::COBBLESTONE, BlockType::COBBLESTONE,
            E(), ItemType::STICK, E(),
            E(), ItemType::STICK, E()
        }, {ItemType::STONE_PICKAXE, 1});

        // Iron Pickaxe
        addRecipe({
            ItemType::IRON_INGOT, ItemType::IRON_INGOT, ItemType::IRON_INGOT,
            E(), ItemType::STICK, E(),
            E(), ItemType::STICK, E()
        }, {ItemType::IRON_PICKAXE, 1});

        // Diamond Pickaxe
        addRecipe({
            ItemType::DIAMOND, ItemType::DIAMOND, ItemType::DIAMOND,
            E(), ItemType::STICK, E(),
            E(), ItemType::STICK, E()
        }, {ItemType::DIAMOND_PICKAXE, 1});

        // Wooden Axe
        addRecipe({
            BlockType::WOOD_PLANKS, BlockType::WOOD_PLANKS, E(),
            BlockType::WOOD_PLANKS, ItemType::STICK, E(),
            E(), ItemType::STICK, E()
        }, {ItemType::WOODEN_AXE, 1});

        // Stone Axe
        addRecipe({
            BlockType::COBBLESTONE, BlockType::COBBLESTONE, E(),
            BlockType::COBBLESTONE, ItemType::STICK, E(),
            E(), ItemType::STICK, E()
        }, {ItemType::STONE_AXE, 1});

        // Iron Axe
        addRecipe({
            ItemType::IRON_INGOT, ItemType::IRON_INGOT, E(),
            ItemType::IRON_INGOT, ItemType::STICK, E(),
            E(), ItemType::STICK, E()
        }, {ItemType::IRON_AXE, 1});

        // Diamond Axe
        addRecipe({
            ItemType::DIAMOND, ItemType::DIAMOND, E(),
            ItemType::DIAMOND, ItemType::STICK, E(),
            E(), ItemType::STICK, E()
        }, {ItemType::DIAMOND_AXE, 1});

        // Wooden Shovel
        addRecipe({
            E(), BlockType::WOOD_PLANKS, E(),
            E(), ItemType::STICK, E(),
            E(), ItemType::STICK, E()
        }, {ItemType::WOODEN_SHOVEL, 1});

        // Stone Shovel
        addRecipe({
            E(), BlockType::COBBLESTONE, E(),
            E(), ItemType::STICK, E(),
            E(), ItemType::STICK, E()
        }, {ItemType::STONE_SHOVEL, 1});

        // Iron Shovel
        addRecipe({
            E(), ItemType::IRON_INGOT, E(),
            E(), ItemType::STICK, E(),
            E(), ItemType::STICK, E()
        }, {ItemType::IRON_SHOVEL, 1});

        // Diamond Shovel
        addRecipe({
            E(), ItemType::DIAMOND, E(),
            E(), ItemType::STICK, E(),
            E(), ItemType::STICK, E()
        }, {ItemType::DIAMOND_SHOVEL, 1});

        // Wooden Sword
        addRecipe({
            E(), BlockType::WOOD_PLANKS, E(),
            E(), BlockType::WOOD_PLANKS, E(),
            E(), ItemType::STICK, E()
        }, {ItemType::WOODEN_SWORD, 1});

        // Stone Sword
        addRecipe({
            E(), BlockType::COBBLESTONE, E(),
            E(), BlockType::COBBLESTONE, E(),
            E(), ItemType::STICK, E()
        }, {ItemType::STONE_SWORD, 1});

        // Iron Sword
        addRecipe({
            E(), ItemType::IRON_INGOT, E(),
            E(), ItemType::IRON_INGOT, E(),
            E(), ItemType::STICK, E()
        }, {ItemType::IRON_SWORD, 1});

        // Diamond Sword
        addRecipe({
            E(), ItemType::DIAMOND, E(),
            E(), ItemType::DIAMOND, E(),
            E(), ItemType::STICK, E()
        }, {ItemType::DIAMOND_SWORD, 1});

        // === ARMOR RECIPES ===

        // Iron Helmet
        addRecipe({
            ItemType::IRON_INGOT, ItemType::IRON_INGOT, ItemType::IRON_INGOT,
            ItemType::IRON_INGOT, E(), ItemType::IRON_INGOT,
            E(), E(), E()
        }, {ItemType::IRON_HELMET, 1});

        // Iron Chestplate
        addRecipe({
            ItemType::IRON_INGOT, E(), ItemType::IRON_INGOT,
            ItemType::IRON_INGOT, ItemType::IRON_INGOT, ItemType::IRON_INGOT,
            ItemType::IRON_INGOT, ItemType::IRON_INGOT, ItemType::IRON_INGOT
        }, {ItemType::IRON_CHESTPLATE, 1});

        // Iron Leggings
        addRecipe({
            ItemType::IRON_INGOT, ItemType::IRON_INGOT, ItemType::IRON_INGOT,
            ItemType::IRON_INGOT, E(), ItemType::IRON_INGOT,
            ItemType::IRON_INGOT, E(), ItemType::IRON_INGOT
        }, {ItemType::IRON_LEGGINGS, 1});

        // Iron Boots
        addRecipe({
            E(), E(), E(),
            ItemType::IRON_INGOT, E(), ItemType::IRON_INGOT,
            ItemType::IRON_INGOT, E(), ItemType::IRON_INGOT
        }, {ItemType::IRON_BOOTS, 1});

        // Diamond Helmet
        addRecipe({
            ItemType::DIAMOND, ItemType::DIAMOND, ItemType::DIAMOND,
            ItemType::DIAMOND, E(), ItemType::DIAMOND,
            E(), E(), E()
        }, {ItemType::DIAMOND_HELMET, 1});

        // Diamond Chestplate
        addRecipe({
            ItemType::DIAMOND, E(), ItemType::DIAMOND,
            ItemType::DIAMOND, ItemType::DIAMOND, ItemType::DIAMOND,
            ItemType::DIAMOND, ItemType::DIAMOND, ItemType::DIAMOND
        }, {ItemType::DIAMOND_CHESTPLATE, 1});

        // Diamond Leggings
        addRecipe({
            ItemType::DIAMOND, ItemType::DIAMOND, ItemType::DIAMOND,
            ItemType::DIAMOND, E(), ItemType::DIAMOND,
            ItemType::DIAMOND, E(), ItemType::DIAMOND
        }, {ItemType::DIAMOND_LEGGINGS, 1});

        // Diamond Boots
        addRecipe({
            E(), E(), E(),
            ItemType::DIAMOND, E(), ItemType::DIAMOND,
            ItemType::DIAMOND, E(), ItemType::DIAMOND
        }, {ItemType::DIAMOND_BOOTS, 1});

        // === LEATHER ARMOR ===

        // Leather Helmet
        addRecipe({
            ItemType::LEATHER, ItemType::LEATHER, ItemType::LEATHER,
            ItemType::LEATHER, E(), ItemType::LEATHER,
            E(), E(), E()
        }, {ItemType::LEATHER_HELMET, 1});

        // Leather Chestplate
        addRecipe({
            ItemType::LEATHER, E(), ItemType::LEATHER,
            ItemType::LEATHER, ItemType::LEATHER, ItemType::LEATHER,
            ItemType::LEATHER, ItemType::LEATHER, ItemType::LEATHER
        }, {ItemType::LEATHER_CHESTPLATE, 1});

        // Leather Leggings
        addRecipe({
            ItemType::LEATHER, ItemType::LEATHER, ItemType::LEATHER,
            ItemType::LEATHER, E(), ItemType::LEATHER,
            ItemType::LEATHER, E(), ItemType::LEATHER
        }, {ItemType::LEATHER_LEGGINGS, 1});

        // Leather Boots
        addRecipe({
            E(), E(), E(),
            ItemType::LEATHER, E(), ItemType::LEATHER,
            ItemType::LEATHER, E(), ItemType::LEATHER
        }, {ItemType::LEATHER_BOOTS, 1});

        // === GOLDEN ARMOR ===

        // Golden Helmet
        addRecipe({
            ItemType::GOLD_INGOT, ItemType::GOLD_INGOT, ItemType::GOLD_INGOT,
            ItemType::GOLD_INGOT, E(), ItemType::GOLD_INGOT,
            E(), E(), E()
        }, {ItemType::GOLDEN_HELMET, 1});

        // Golden Chestplate
        addRecipe({
            ItemType::GOLD_INGOT, E(), ItemType::GOLD_INGOT,
            ItemType::GOLD_INGOT, ItemType::GOLD_INGOT, ItemType::GOLD_INGOT,
            ItemType::GOLD_INGOT, ItemType::GOLD_INGOT, ItemType::GOLD_INGOT
        }, {ItemType::GOLDEN_CHESTPLATE, 1});

        // Golden Leggings
        addRecipe({
            ItemType::GOLD_INGOT, ItemType::GOLD_INGOT, ItemType::GOLD_INGOT,
            ItemType::GOLD_INGOT, E(), ItemType::GOLD_INGOT,
            ItemType::GOLD_INGOT, E(), ItemType::GOLD_INGOT
        }, {ItemType::GOLDEN_LEGGINGS, 1});

        // Golden Boots
        addRecipe({
            E(), E(), E(),
            ItemType::GOLD_INGOT, E(), ItemType::GOLD_INGOT,
            ItemType::GOLD_INGOT, E(), ItemType::GOLD_INGOT
        }, {ItemType::GOLDEN_BOOTS, 1});
    }

    const CraftingRecipe3x3* findRecipe(const std::array<ItemStack, 9>& grid) const {
        bool empty = true;
        for (const auto& slot : grid) {
            if (!slot.isEmpty()) {
                empty = false;
                break;
            }
        }
        if (empty) return nullptr;

        for (const auto& recipe : recipes) {
            if (recipe.matches(grid)) {
                return &recipe;
            }
        }
        return nullptr;
    }

    const std::vector<CraftingRecipe3x3>& getAllRecipes() const {
        return recipes;
    }

private:
    std::vector<CraftingRecipe3x3> recipes;

    void addRecipe(std::array<CraftingIngredient, 9> pattern, CraftingResult result, bool shapeless = false) {
        recipes.emplace_back(pattern, result, shapeless);
    }

    CraftingTableRecipeRegistry() = default;
    CraftingTableRecipeRegistry(const CraftingTableRecipeRegistry&) = delete;
    CraftingTableRecipeRegistry& operator=(const CraftingTableRecipeRegistry&) = delete;
};

// ==================== CRAFTING TABLE UI ====================
class CraftingTableUI {
public:
    bool isOpen = false;

    // 3x3 crafting grid storage (when UI is open)
    std::array<ItemStack, 9> craftingGrid;
    ItemStack craftingResult;
    ItemStack cursorStack;  // Item being dragged

    // Colors (same as InventoryUI)
    const glm::vec4 SLOT_BG = {0.55f, 0.55f, 0.55f, 1.0f};
    const glm::vec4 SLOT_HOVER = {0.75f, 0.75f, 0.75f, 1.0f};
    const glm::vec4 SLOT_BORDER = {0.2f, 0.2f, 0.2f, 1.0f};
    const glm::vec4 INVENTORY_BG = {0.75f, 0.75f, 0.75f, 1.0f};
    const glm::vec4 TEXT_WHITE = {1.0f, 1.0f, 1.0f, 1.0f};
    const glm::vec4 TEXT_SHADOW = {0.15f, 0.15f, 0.15f, 0.8f};

    // Dimensions
    static constexpr float BASE_SLOT_SIZE = 40.0f;
    static constexpr float BASE_SLOT_GAP = 2.0f;
    static constexpr float BASE_PADDING = 12.0f;

    float getScale() const { return g_config.guiScale; }
    float getSlotSize() const { return BASE_SLOT_SIZE * getScale(); }
    float getSlotGap() const { return BASE_SLOT_GAP * getScale(); }
    float getPadding() const { return BASE_PADDING * getScale(); }

    void init(MenuUIRenderer* renderer, GLuint blockAtlas, GLuint itemAtlas = 0) {
        ui = renderer;
        textureAtlas = blockAtlas;
        itemTextureAtlas = itemAtlas;

        // Initialize 3x3 recipe registry
        CraftingTableRecipeRegistry::getInstance().init();
    }

    void open() {
        isOpen = true;
        // Clear grid when opening
        for (auto& slot : craftingGrid) {
            slot.clear();
        }
        craftingResult.clear();
    }

    void close(Inventory& inventory) {
        isOpen = false;
        // Return grid items to player inventory
        for (auto& slot : craftingGrid) {
            if (!slot.isEmpty()) {
                if (slot.isBlock()) {
                    inventory.addBlock(slot.blockType, slot.count);
                } else if (slot.isItem()) {
                    inventory.addItem(slot.itemType, slot.count, slot.durability);
                }
                slot.clear();
            }
        }
        // Return cursor item
        if (!cursorStack.isEmpty()) {
            if (cursorStack.isBlock()) {
                inventory.addBlock(cursorStack.blockType, cursorStack.count);
            } else if (cursorStack.isItem()) {
                inventory.addItem(cursorStack.itemType, cursorStack.count, cursorStack.durability);
            }
            cursorStack.clear();
        }
    }

    void render(Inventory& inventory, float mouseX, float mouseY) {
        if (!ui || !ui->initialized || !isOpen) return;

        float scale = getScale();
        float slotSize = getSlotSize();
        float slotGap = getSlotGap();
        float padding = getPadding();

        // Calculate panel dimensions
        float gridWidth = 3 * (slotSize + slotGap) - slotGap;
        float invWidth = 9 * (slotSize + slotGap) - slotGap;
        float panelWidth = std::max(gridWidth * 2 + slotSize + padding * 4, invWidth + padding * 2);
        float panelHeight = padding * 2 + slotSize * 3 + slotGap * 2 + // Crafting grid
                           padding + slotSize * 4 + slotGap * 3 + padding; // Inventory + hotbar

        float panelX = (ui->windowWidth - panelWidth) / 2.0f;
        float panelY = (ui->windowHeight - panelHeight) / 2.0f;

        // Background panel
        ui->drawRect(panelX, panelY, panelWidth, panelHeight, INVENTORY_BG);

        // Title (shadow + text)
        ui->drawText("Crafting", panelX + padding + 1, panelY + padding * 0.5f + 1, TEXT_SHADOW, scale);
        ui->drawText("Crafting", panelX + padding, panelY + padding * 0.5f, TEXT_WHITE, scale);

        float craftingY = panelY + padding + slotSize * 0.5f;

        // === 3x3 CRAFTING GRID ===
        float craftGridX = panelX + padding;
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 3; col++) {
                int idx = row * 3 + col;
                float x = craftGridX + col * (slotSize + slotGap);
                float y = craftingY + row * (slotSize + slotGap);

                bool hover = isMouseInSlot(mouseX, mouseY, x, y, slotSize);
                renderSlot(x, y, slotSize, hover);
                renderItemStack(craftingGrid[idx], x, y, slotSize);
            }
        }

        // Arrow between grid and result (shadow + text)
        float arrowX = craftGridX + gridWidth + padding;
        float arrowY = craftingY + slotSize + slotGap / 2;
        ui->drawText("=>", arrowX + 1, arrowY + 1, TEXT_SHADOW, scale);
        ui->drawText("=>", arrowX, arrowY, TEXT_WHITE, scale);

        // === OUTPUT SLOT ===
        float outputX = arrowX + slotSize;
        float outputY = craftingY + slotSize;
        bool outputHover = isMouseInSlot(mouseX, mouseY, outputX, outputY, slotSize);
        renderSlot(outputX, outputY, slotSize, outputHover);
        renderItemStack(craftingResult, outputX, outputY, slotSize);

        // === PLAYER INVENTORY ===
        float invY = craftingY + 3 * (slotSize + slotGap) + padding;
        float invX = panelX + (panelWidth - invWidth) / 2.0f;

        // Main inventory (3 rows)
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 9; col++) {
                int slotIdx = HOTBAR_SLOTS + row * 9 + col;
                float x = invX + col * (slotSize + slotGap);
                float y = invY + row * (slotSize + slotGap);

                bool hover = isMouseInSlot(mouseX, mouseY, x, y, slotSize);
                renderSlot(x, y, slotSize, hover);
                renderItemStack(inventory.slots[slotIdx], x, y, slotSize);
            }
        }

        // Hotbar
        float hotbarY = invY + 3 * (slotSize + slotGap) + padding * 0.5f;
        for (int i = 0; i < HOTBAR_SLOTS; i++) {
            float x = invX + i * (slotSize + slotGap);
            bool hover = isMouseInSlot(mouseX, mouseY, x, hotbarY, slotSize);
            renderSlot(x, hotbarY, slotSize, hover);
            renderItemStack(inventory.slots[i], x, hotbarY, slotSize);
        }

        // Cursor item
        if (!cursorStack.isEmpty()) {
            renderItemStack(cursorStack, mouseX - slotSize/2, mouseY - slotSize/2, slotSize);
        }
    }

    // Handle mouse click - returns true if click was consumed
    bool handleClick(Inventory& inventory, float mouseX, float mouseY, bool rightClick) {
        if (!isOpen) return false;

        float scale = getScale();
        float slotSize = getSlotSize();
        float slotGap = getSlotGap();
        float padding = getPadding();

        float gridWidth = 3 * (slotSize + slotGap) - slotGap;
        float invWidth = 9 * (slotSize + slotGap) - slotGap;
        float panelWidth = std::max(gridWidth * 2 + slotSize + padding * 4, invWidth + padding * 2);
        float panelHeight = padding * 2 + slotSize * 3 + slotGap * 2 + padding + slotSize * 4 + slotGap * 3 + padding;

        float panelX = (ui->windowWidth - panelWidth) / 2.0f;
        float panelY = (ui->windowHeight - panelHeight) / 2.0f;
        float craftingY = panelY + padding + slotSize * 0.5f;
        float craftGridX = panelX + padding;

        // Check crafting grid
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 3; col++) {
                int idx = row * 3 + col;
                float x = craftGridX + col * (slotSize + slotGap);
                float y = craftingY + row * (slotSize + slotGap);

                if (isMouseInSlot(mouseX, mouseY, x, y, slotSize)) {
                    handleSlotClick(craftingGrid[idx], rightClick);
                    updateCraftingResult();
                    return true;
                }
            }
        }

        // Check output slot
        float arrowX = craftGridX + gridWidth + padding;
        float outputX = arrowX + slotSize;
        float outputY = craftingY + slotSize;

        if (isMouseInSlot(mouseX, mouseY, outputX, outputY, slotSize)) {
            if (!craftingResult.isEmpty()) {
                craftItem();
            }
            return true;
        }

        // Check player inventory
        float invY = craftingY + 3 * (slotSize + slotGap) + padding;
        float invX = panelX + (panelWidth - invWidth) / 2.0f;

        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 9; col++) {
                int slotIdx = HOTBAR_SLOTS + row * 9 + col;
                float x = invX + col * (slotSize + slotGap);
                float y = invY + row * (slotSize + slotGap);

                if (isMouseInSlot(mouseX, mouseY, x, y, slotSize)) {
                    handleSlotClick(inventory.slots[slotIdx], rightClick);
                    return true;
                }
            }
        }

        // Check hotbar
        float hotbarY = invY + 3 * (slotSize + slotGap) + padding * 0.5f;
        for (int i = 0; i < HOTBAR_SLOTS; i++) {
            float x = invX + i * (slotSize + slotGap);
            if (isMouseInSlot(mouseX, mouseY, x, hotbarY, slotSize)) {
                handleSlotClick(inventory.slots[i], rightClick);
                return true;
            }
        }

        return false;
    }

    void updateCraftingResult() {
        const CraftingRecipe3x3* recipe = CraftingTableRecipeRegistry::getInstance().findRecipe(craftingGrid);
        if (recipe && !recipe->result.isEmpty()) {
            craftingResult = recipe->result.toItemStack();
        } else {
            craftingResult.clear();
        }
    }

    void craftItem() {
        if (craftingResult.isEmpty()) return;

        // Check if cursor can accept result
        if (cursorStack.isEmpty()) {
            cursorStack = craftingResult;
        } else if (cursorStack.isSameType(craftingResult) &&
                   cursorStack.count + craftingResult.count <= cursorStack.getMaxStackSize()) {
            cursorStack.count += craftingResult.count;
        } else {
            return;  // Can't pick up result
        }

        // Consume ingredients
        for (auto& slot : craftingGrid) {
            if (!slot.isEmpty()) {
                slot.remove(1);
            }
        }

        updateCraftingResult();
    }

private:
    MenuUIRenderer* ui = nullptr;
    GLuint textureAtlas = 0;
    GLuint itemTextureAtlas = 0;

    bool isMouseInSlot(float mouseX, float mouseY, float slotX, float slotY, float size) {
        return mouseX >= slotX && mouseX < slotX + size &&
               mouseY >= slotY && mouseY < slotY + size;
    }

    void renderSlot(float x, float y, float size, bool hover) {
        glm::vec4 color = hover ? SLOT_HOVER : SLOT_BG;
        ui->drawRect(x, y, size, size, SLOT_BORDER);
        ui->drawRect(x + 1, y + 1, size - 2, size - 2, color);
    }

    // Draw a texture region using UV coordinates
    void drawTextureRegion(GLuint textureID, float x, float y, float w, float h,
                           float u0, float v0, float uSize, float vSize) {
        if (!ui || textureID == 0) return;

        glUseProgram(ui->texShaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(ui->texShaderProgram, "projection"),
                          1, GL_FALSE, &ui->projection[0][0]);

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(x, y, 0.0f));
        model = glm::scale(model, glm::vec3(w, h, 1.0f));
        glUniformMatrix4fv(glGetUniformLocation(ui->texShaderProgram, "model"),
                          1, GL_FALSE, &model[0][0]);

        float vertices[] = {
            0.0f, 0.0f, u0, v0,
            1.0f, 0.0f, u0 + uSize, v0,
            1.0f, 1.0f, u0 + uSize, v0 + vSize,
            0.0f, 0.0f, u0, v0,
            1.0f, 1.0f, u0 + uSize, v0 + vSize,
            0.0f, 1.0f, u0, v0 + vSize
        };

        glBindVertexArray(ui->texVAO);
        glBindBuffer(GL_ARRAY_BUFFER, ui->texVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureID);
        glUniform1i(glGetUniformLocation(ui->texShaderProgram, "tex"), 0);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisable(GL_BLEND);

        glBindVertexArray(0);
    }

    void renderItemStack(const ItemStack& stack, float x, float y, float size) {
        if (stack.isEmpty()) return;

        float iconPad = size * 0.1f;
        float iconSize = size - iconPad * 2;
        float iconX = x + iconPad;
        float iconY = y + iconPad;

        if (stack.isBlock()) {
            // Render block from block atlas using top face
            BlockTextures tex = getBlockTextures(stack.blockType);
            int slot = tex.faceSlots[4];  // Top face
            glm::vec4 uv = TextureAtlas::getUV(slot);
            drawTextureRegion(textureAtlas, iconX, iconY, iconSize, iconSize,
                              uv.x, uv.y, uv.z - uv.x, uv.w - uv.y);
        } else if (stack.isItem() && itemTextureAtlas != 0) {
            // Render item from item atlas
            int slot = ItemAtlas::getTextureSlot(stack.itemType);
            glm::vec4 uv = ItemAtlas::getUV(slot);
            drawTextureRegion(itemTextureAtlas, iconX, iconY, iconSize, iconSize,
                              uv.x, uv.y, uv.z - uv.x, uv.w - uv.y);

            // Durability bar
            if (stack.hasDurability() && stack.durability < stack.getMaxDurability()) {
                float durRatio = static_cast<float>(stack.durability) / stack.getMaxDurability();
                float barWidth = iconSize * 0.8f;
                float barHeight = size * 0.08f;
                float barX = iconX + iconSize * 0.1f;
                float barY = y + size - barHeight - iconPad * 0.5f;

                ui->drawRect(barX, barY, barWidth, barHeight, {0.0f, 0.0f, 0.0f, 0.8f});
                glm::vec4 durColor = durRatio > 0.5f ? glm::vec4(0.0f, 1.0f, 0.0f, 1.0f) :
                                     durRatio > 0.25f ? glm::vec4(1.0f, 1.0f, 0.0f, 1.0f) :
                                     glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                ui->drawRect(barX, barY, barWidth * durRatio, barHeight, durColor);
            }
        }

        // Stack count (shadow + text)
        if (stack.count > 1) {
            std::string countStr = std::to_string(stack.count);
            float countScale = getScale() * 0.7f;
            float countX = x + size - countStr.length() * 7 * countScale - 3 * countScale;
            float countY = y + size - 12 * countScale;
            ui->drawText(countStr, countX + 1, countY + 1, TEXT_SHADOW, countScale);
            ui->drawText(countStr, countX, countY, TEXT_WHITE, countScale);
        }
    }

    void handleSlotClick(ItemStack& slot, bool rightClick) {
        if (rightClick) {
            // Right click - place one or pick up half
            if (cursorStack.isEmpty()) {
                if (!slot.isEmpty() && slot.count > 1) {
                    int half = slot.count / 2;
                    cursorStack = slot;
                    cursorStack.count = slot.count - half;
                    slot.count = half;
                } else if (!slot.isEmpty()) {
                    std::swap(cursorStack, slot);
                }
            } else {
                if (slot.isEmpty()) {
                    slot = cursorStack;
                    slot.count = 1;
                    cursorStack.count--;
                    if (cursorStack.count <= 0) cursorStack.clear();
                } else if (slot.isSameType(cursorStack) && slot.count < slot.getMaxStackSize()) {
                    slot.count++;
                    cursorStack.count--;
                    if (cursorStack.count <= 0) cursorStack.clear();
                }
            }
        } else {
            // Left click - swap or merge
            if (cursorStack.isEmpty()) {
                std::swap(cursorStack, slot);
            } else if (slot.isEmpty()) {
                std::swap(cursorStack, slot);
            } else if (slot.isSameType(cursorStack) && slot.canMergeWith(cursorStack)) {
                int transfer = std::min(cursorStack.count, slot.getMaxStackSize() - slot.count);
                slot.count += transfer;
                cursorStack.count -= transfer;
                if (cursorStack.count <= 0) cursorStack.clear();
            } else {
                std::swap(cursorStack, slot);
            }
        }
    }
};
