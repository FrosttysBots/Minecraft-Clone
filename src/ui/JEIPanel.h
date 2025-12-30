#pragma once

// JEI (Just Enough Items) Panel - Item browser and recipe viewer
// Shows all items/blocks on the right side of inventory
// Click to view recipes, sources, and information
// In creative mode: drag items to inventory
// In survival mode: view-only

#include "MenuUI.h"
#include "../core/Inventory.h"
#include "../core/Item.h"
#include "../core/CraftingRecipes.h"
#include "../render/TextureAtlas.h"
#include "../render/ItemAtlas.h"
#include "../world/Block.h"
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include <algorithm>

// External config reference
extern GameConfig g_config;

// ==================== ITEM ENTRY ====================
struct JEIItemEntry {
    bool isBlock = false;
    BlockType blockType = BlockType::AIR;
    ItemType itemType = ItemType::NONE;
    std::string name;
    int textureSlot = 0;

    JEIItemEntry() = default;
    JEIItemEntry(BlockType bt, const std::string& n, int slot)
        : isBlock(true), blockType(bt), name(n), textureSlot(slot) {}
    JEIItemEntry(ItemType it, const std::string& n, int slot)
        : isBlock(false), itemType(it), name(n), textureSlot(slot) {}

    bool isEmpty() const {
        return isBlock ? blockType == BlockType::AIR : itemType == ItemType::NONE;
    }
};

// ==================== RECIPE INFO ====================
struct RecipeInfo {
    enum class Type { CRAFTING_2X2, CRAFTING_3X3, SMELTING, MINING, MOB_DROP };
    Type type;
    std::string description;

    // For crafting recipes
    std::array<JEIItemEntry, 9> ingredients;  // 3x3 grid (2x2 uses first 4)
    int gridSize = 3;  // 2 or 3

    // For mining/drops
    std::vector<JEIItemEntry> sources;
};

// ==================== JEI PANEL ====================
class JEIPanel {
public:
    bool isVisible = false;
    bool showItemInfo = false;
    JEIItemEntry selectedItem;
    std::vector<RecipeInfo> selectedRecipes;

    // Scroll state
    float scrollOffset = 0.0f;
    int hoveredItemIndex = -1;

    // Panel dimensions
    static constexpr float BASE_SLOT_SIZE = 32.0f;
    static constexpr float BASE_PADDING = 8.0f;
    static constexpr int GRID_COLS = 8;

    float getScale() const { return g_config.guiScale; }
    float getSlotSize() const { return BASE_SLOT_SIZE * getScale(); }
    float getPadding() const { return BASE_PADDING * getScale(); }

    void init(MenuUIRenderer* renderer, GLuint blockAtlas, GLuint itemAtlas) {
        ui = renderer;
        textureAtlas = blockAtlas;
        itemTextureAtlas = itemAtlas;
        buildItemList();
    }

    void show() {
        isVisible = true;
        showItemInfo = false;
        scrollOffset = 0.0f;
    }

    void hide() {
        isVisible = false;
        showItemInfo = false;
    }

    // Build list of all blocks and items
    void buildItemList() {
        items.clear();

        // Add blocks (skip AIR and internal blocks)
        for (int i = 1; i < static_cast<int>(BlockType::COUNT); i++) {
            BlockType bt = static_cast<BlockType>(i);
            // Skip non-obtainable blocks
            if (bt == BlockType::BEDROCK) continue;

            BlockTextures tex = getBlockTextures(bt);
            std::string name = getBlockName(bt);
            items.emplace_back(bt, name, tex.faceSlots[4]);  // Top face
        }

        // Add items
        for (int i = 100; i < static_cast<int>(ItemType::ITEM_TYPE_COUNT); i++) {
            ItemType it = static_cast<ItemType>(i);
            if (it == ItemType::NONE) continue;

            const ItemProperties& props = getItemProperties(it);
            if (props.name[0] == '\0') continue;  // Skip unnamed items

            items.emplace_back(it, props.name, props.textureSlot);
        }
    }

    // Get block name helper
    std::string getBlockName(BlockType type) const {
        switch (type) {
            case BlockType::GRASS: return "Grass Block";
            case BlockType::DIRT: return "Dirt";
            case BlockType::STONE: return "Stone";
            case BlockType::COBBLESTONE: return "Cobblestone";
            case BlockType::WOOD_LOG: return "Oak Log";
            case BlockType::WOOD_PLANKS: return "Oak Planks";
            case BlockType::LEAVES: return "Oak Leaves";
            case BlockType::SAND: return "Sand";
            case BlockType::GRAVEL: return "Gravel";
            case BlockType::WATER: return "Water";
            case BlockType::LAVA: return "Lava";
            case BlockType::COAL_ORE: return "Coal Ore";
            case BlockType::IRON_ORE: return "Iron Ore";
            case BlockType::GOLD_ORE: return "Gold Ore";
            case BlockType::DIAMOND_ORE: return "Diamond Ore";
            case BlockType::GLASS: return "Glass";
            case BlockType::BRICK: return "Bricks";
            case BlockType::SNOW_BLOCK: return "Snow Block";
            case BlockType::CACTUS: return "Cactus";
            case BlockType::GLOWSTONE: return "Glowstone";
            case BlockType::CRAFTING_TABLE: return "Crafting Table";
            case BlockType::BEDROCK: return "Bedrock";
            default: return "Block";
        }
    }

    void render(float mouseX, float mouseY, float panelX, float panelWidth, float panelY, float panelHeight) {
        if (!ui || !isVisible) return;

        float scale = getScale();
        float slotSize = getSlotSize();
        float padding = getPadding();

        // JEI panel on the right side of inventory
        float jeiWidth = GRID_COLS * slotSize + padding * 2;
        float jeiX = panelX + panelWidth + padding;
        float jeiY = panelY;
        float jeiHeight = panelHeight;

        // Clamp to screen bounds
        if (jeiX + jeiWidth > ui->windowWidth - padding) {
            jeiX = ui->windowWidth - jeiWidth - padding;
        }

        // Background
        ui->drawRect(jeiX, jeiY, jeiWidth, jeiHeight, glm::vec4(0.2f, 0.2f, 0.2f, 0.95f));
        ui->drawRectOutline(jeiX, jeiY, jeiWidth, jeiHeight, glm::vec4(0.4f, 0.4f, 0.4f, 1.0f), 2.0f);

        // Title
        ui->drawText("Items", jeiX + padding, jeiY + padding * 0.5f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 0.8f * scale);

        // Scrollable item grid
        float gridY = jeiY + padding * 2 + 12 * scale;
        float gridHeight = jeiHeight - padding * 3 - 12 * scale;
        int visibleRows = static_cast<int>(gridHeight / slotSize);
        int totalRows = (static_cast<int>(items.size()) + GRID_COLS - 1) / GRID_COLS;
        int maxScroll = std::max(0, totalRows - visibleRows);

        // Clamp scroll
        scrollOffset = std::clamp(scrollOffset, 0.0f, static_cast<float>(maxScroll));

        int startRow = static_cast<int>(scrollOffset);
        hoveredItemIndex = -1;

        // Enable scissor for clipping
        glEnable(GL_SCISSOR_TEST);
        glScissor(static_cast<int>(jeiX), static_cast<int>(ui->windowHeight - jeiY - jeiHeight),
                  static_cast<int>(jeiWidth), static_cast<int>(gridHeight + padding));

        for (int row = startRow; row < startRow + visibleRows + 1 && row < totalRows; row++) {
            for (int col = 0; col < GRID_COLS; col++) {
                int idx = row * GRID_COLS + col;
                if (idx >= static_cast<int>(items.size())) break;

                float x = jeiX + padding + col * slotSize;
                float y = gridY + (row - startRow) * slotSize;

                // Check hover
                bool hovered = mouseX >= x && mouseX < x + slotSize &&
                              mouseY >= y && mouseY < y + slotSize;
                if (hovered) hoveredItemIndex = idx;

                // Slot background
                glm::vec4 slotBg = hovered ? glm::vec4(0.5f, 0.5f, 0.5f, 1.0f) : glm::vec4(0.3f, 0.3f, 0.3f, 1.0f);
                ui->drawRect(x, y, slotSize - 1, slotSize - 1, slotBg);

                // Render item
                renderJEIItem(items[idx], x + 2 * scale, y + 2 * scale, slotSize - 4 * scale);
            }
        }

        glDisable(GL_SCISSOR_TEST);

        // Scroll bar
        if (totalRows > visibleRows) {
            float scrollBarHeight = gridHeight * (static_cast<float>(visibleRows) / totalRows);
            float scrollBarY = gridY + (scrollOffset / maxScroll) * (gridHeight - scrollBarHeight);
            float scrollBarX = jeiX + jeiWidth - padding * 0.5f - 4 * scale;

            ui->drawRect(scrollBarX, gridY, 4 * scale, gridHeight, glm::vec4(0.15f, 0.15f, 0.15f, 1.0f));
            ui->drawRect(scrollBarX, scrollBarY, 4 * scale, scrollBarHeight, glm::vec4(0.6f, 0.6f, 0.6f, 1.0f));
        }

        // Tooltip for hovered item
        if (hoveredItemIndex >= 0 && hoveredItemIndex < static_cast<int>(items.size())) {
            renderTooltip(items[hoveredItemIndex], mouseX, mouseY);
        }

        // Item info popup
        if (showItemInfo) {
            renderItemInfo(mouseX, mouseY);
        }
    }

    void renderJEIItem(const JEIItemEntry& item, float x, float y, float size) {
        if (item.isEmpty()) return;

        if (item.isBlock) {
            BlockTextures tex = getBlockTextures(item.blockType);
            int slot = tex.faceSlots[4];  // Top face
            glm::vec4 uv = TextureAtlas::getUV(slot);
            drawTextureRegion(textureAtlas, x, y, size, size, uv.x, uv.y, uv.z - uv.x, uv.w - uv.y);
        } else {
            int slot = ItemAtlas::getTextureSlot(item.itemType);
            glm::vec4 uv = ItemAtlas::getUV(slot);
            drawTextureRegion(itemTextureAtlas, x, y, size, size, uv.x, uv.y, uv.z - uv.x, uv.w - uv.y);
        }
    }

    void renderTooltip(const JEIItemEntry& item, float mouseX, float mouseY) {
        float scale = getScale();
        float padding = 6 * scale;
        float textScale = 0.7f * scale;

        std::string text = item.name;
        float textWidth = text.length() * 7 * textScale;
        float tooltipWidth = textWidth + padding * 2;
        float tooltipHeight = 16 * scale;

        float tooltipX = mouseX + 12 * scale;
        float tooltipY = mouseY - tooltipHeight - 4 * scale;

        // Clamp to screen
        if (tooltipX + tooltipWidth > ui->windowWidth) {
            tooltipX = mouseX - tooltipWidth - 4 * scale;
        }
        if (tooltipY < 0) {
            tooltipY = mouseY + 16 * scale;
        }

        // Background
        ui->drawRect(tooltipX, tooltipY, tooltipWidth, tooltipHeight, glm::vec4(0.1f, 0.0f, 0.2f, 0.95f));
        ui->drawRectOutline(tooltipX, tooltipY, tooltipWidth, tooltipHeight, glm::vec4(0.4f, 0.2f, 0.6f, 1.0f), 1.0f);

        // Text
        ui->drawText(text, tooltipX + padding, tooltipY + padding * 0.5f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), textScale);
    }

    void renderItemInfo(float mouseX, float mouseY) {
        if (selectedItem.isEmpty()) return;

        float scale = getScale();
        float padding = getPadding();
        float slotSize = getSlotSize();

        // Info popup dimensions
        float popupWidth = 280 * scale;
        float popupHeight = 320 * scale;
        float popupX = (ui->windowWidth - popupWidth) / 2;
        float popupY = (ui->windowHeight - popupHeight) / 2;

        // Dark overlay
        ui->drawRect(0, 0, static_cast<float>(ui->windowWidth), static_cast<float>(ui->windowHeight),
                    glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));

        // Popup background
        ui->drawRect(popupX, popupY, popupWidth, popupHeight, glm::vec4(0.15f, 0.15f, 0.18f, 0.98f));
        ui->drawRectOutline(popupX, popupY, popupWidth, popupHeight, glm::vec4(0.5f, 0.4f, 0.6f, 1.0f), 2.0f);

        float contentX = popupX + padding;
        float currentY = popupY + padding;

        // Item icon and name
        renderJEIItem(selectedItem, contentX, currentY, slotSize);
        ui->drawText(selectedItem.name, contentX + slotSize + padding, currentY + slotSize / 2 - 6 * scale,
                    glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 0.9f * scale);

        currentY += slotSize + padding;

        // Divider
        ui->drawRect(contentX, currentY, popupWidth - padding * 2, 2, glm::vec4(0.4f, 0.4f, 0.4f, 1.0f));
        currentY += padding;

        // Recipe section
        if (!selectedRecipes.empty()) {
            for (size_t ri = 0; ri < selectedRecipes.size() && ri < 3; ri++) {
                const RecipeInfo& recipe = selectedRecipes[ri];

                // Recipe type label
                std::string typeLabel;
                switch (recipe.type) {
                    case RecipeInfo::Type::CRAFTING_2X2: typeLabel = "Crafting (2x2)"; break;
                    case RecipeInfo::Type::CRAFTING_3X3: typeLabel = "Crafting (3x3)"; break;
                    case RecipeInfo::Type::SMELTING: typeLabel = "Smelting"; break;
                    case RecipeInfo::Type::MINING: typeLabel = "Mining"; break;
                    case RecipeInfo::Type::MOB_DROP: typeLabel = "Mob Drop"; break;
                }
                ui->drawText(typeLabel, contentX, currentY, glm::vec4(0.8f, 0.8f, 0.5f, 1.0f), 0.75f * scale);
                currentY += 14 * scale;

                // Render recipe grid
                if (recipe.type == RecipeInfo::Type::CRAFTING_2X2 || recipe.type == RecipeInfo::Type::CRAFTING_3X3) {
                    int gridSize = recipe.gridSize;
                    float miniSlot = slotSize * 0.7f;

                    for (int row = 0; row < gridSize; row++) {
                        for (int col = 0; col < gridSize; col++) {
                            int idx = row * 3 + col;
                            float sx = contentX + col * miniSlot;
                            float sy = currentY + row * miniSlot;

                            ui->drawRect(sx, sy, miniSlot - 1, miniSlot - 1, glm::vec4(0.25f, 0.25f, 0.25f, 1.0f));

                            if (!recipe.ingredients[idx].isEmpty()) {
                                renderJEIItem(recipe.ingredients[idx], sx + 2, sy + 2, miniSlot - 4);
                            }
                        }
                    }

                    // Arrow
                    float arrowX = contentX + gridSize * miniSlot + padding;
                    float arrowY = currentY + (gridSize * miniSlot) / 2 - 6 * scale;
                    ui->drawText("=>", arrowX, arrowY, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 0.8f * scale);

                    // Result
                    float resultX = arrowX + 25 * scale;
                    float resultY = currentY + (gridSize * miniSlot) / 2 - miniSlot / 2;
                    ui->drawRect(resultX, resultY, miniSlot, miniSlot, glm::vec4(0.3f, 0.3f, 0.2f, 1.0f));
                    renderJEIItem(selectedItem, resultX + 2, resultY + 2, miniSlot - 4);

                    currentY += gridSize * miniSlot + padding;
                } else if (recipe.type == RecipeInfo::Type::MINING) {
                    // Show source blocks
                    ui->drawText(recipe.description, contentX, currentY, glm::vec4(0.7f, 0.7f, 0.7f, 1.0f), 0.7f * scale);
                    currentY += 14 * scale;

                    float miniSlot = slotSize * 0.6f;
                    for (size_t si = 0; si < recipe.sources.size() && si < 4; si++) {
                        float sx = contentX + si * (miniSlot + 4);
                        ui->drawRect(sx, currentY, miniSlot, miniSlot, glm::vec4(0.25f, 0.25f, 0.25f, 1.0f));
                        renderJEIItem(recipe.sources[si], sx + 2, currentY + 2, miniSlot - 4);
                    }
                    currentY += miniSlot + padding;
                } else {
                    ui->drawText(recipe.description, contentX, currentY, glm::vec4(0.7f, 0.7f, 0.7f, 1.0f), 0.7f * scale);
                    currentY += 14 * scale;
                }

                currentY += padding * 0.5f;
            }
        } else {
            ui->drawText("No recipes found", contentX, currentY, glm::vec4(0.6f, 0.6f, 0.6f, 1.0f), 0.8f * scale);
            currentY += 16 * scale;
        }

        // Close hint
        float closeY = popupY + popupHeight - padding - 12 * scale;
        ui->drawText("Click anywhere to close", popupX + popupWidth / 2 - 70 * scale, closeY,
                    glm::vec4(0.5f, 0.5f, 0.5f, 1.0f), 0.65f * scale);
    }

    // Handle scroll
    void handleScroll(float yOffset) {
        if (!isVisible) return;
        scrollOffset -= yOffset * 2.0f;
    }

    // Handle click - returns true if click was consumed
    // In creative mode, can grab items
    bool handleClick(int button, bool pressed, Inventory& inventory, bool isCreativeMode) {
        if (!isVisible || !pressed) return false;

        // Close item info popup on any click
        if (showItemInfo) {
            showItemInfo = false;
            return true;
        }

        // Check if clicking on an item
        if (hoveredItemIndex >= 0 && hoveredItemIndex < static_cast<int>(items.size())) {
            const JEIItemEntry& item = items[hoveredItemIndex];

            if (button == GLFW_MOUSE_BUTTON_LEFT) {
                if (isCreativeMode) {
                    // Creative mode: grab a stack of the item
                    if (inventory.cursorStack.isEmpty()) {
                        if (item.isBlock) {
                            inventory.cursorStack = ItemStack(item.blockType, 64);
                        } else {
                            const ItemProperties& props = getItemProperties(item.itemType);
                            int count = props.maxStackSize;
                            int durability = props.maxDurability;
                            inventory.cursorStack = ItemStack(item.itemType, count, durability);
                        }
                    }
                } else {
                    // Survival mode: show item info
                    showItemInfoFor(item);
                }
                return true;
            } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
                // Right click always shows info
                showItemInfoFor(item);
                return true;
            }
        }

        return false;
    }

    void showItemInfoFor(const JEIItemEntry& item) {
        selectedItem = item;
        selectedRecipes.clear();
        buildRecipesFor(item);
        showItemInfo = true;
    }

    void buildRecipesFor(const JEIItemEntry& item) {
        // Check 2x2 crafting recipes
        const auto& recipes2x2 = CraftingRecipeRegistry::getInstance().getAllRecipes();
        for (const auto& recipe : recipes2x2) {
            bool matches = false;
            if (item.isBlock && recipe.result.type == StackType::BLOCK && recipe.result.blockType == item.blockType) {
                matches = true;
            } else if (!item.isBlock && recipe.result.type == StackType::ITEM && recipe.result.itemType == item.itemType) {
                matches = true;
            }

            if (matches) {
                RecipeInfo info;
                info.type = RecipeInfo::Type::CRAFTING_2X2;
                info.gridSize = 2;

                for (int i = 0; i < 4; i++) {
                    int row = i / 2;
                    int col = i % 2;
                    int idx3x3 = row * 3 + col;

                    const CraftingIngredient& ing = recipe.pattern[i];
                    if (ing.isBlock()) {
                        info.ingredients[idx3x3] = JEIItemEntry(ing.blockType, "", 0);
                    } else if (ing.isItem()) {
                        info.ingredients[idx3x3] = JEIItemEntry(ing.itemType, "", 0);
                    }
                }

                selectedRecipes.push_back(info);
            }
        }

        // Check 3x3 crafting recipes (CraftingTableRecipeRegistry)
        // We'll add a getAllRecipes method or iterate differently
        build3x3RecipesFor(item);

        // Check mining sources
        buildMiningSourcesFor(item);
    }

    void build3x3RecipesFor(const JEIItemEntry& item);  // Implemented after CraftingTableRecipeRegistry

    void buildMiningSourcesFor(const JEIItemEntry& item) {
        // Items that come from mining
        if (!item.isBlock) {
            RecipeInfo info;
            info.type = RecipeInfo::Type::MINING;

            switch (item.itemType) {
                case ItemType::COAL:
                    info.description = "Mine Coal Ore";
                    info.sources.push_back(JEIItemEntry(BlockType::COAL_ORE, "Coal Ore", 0));
                    selectedRecipes.push_back(info);
                    break;
                case ItemType::DIAMOND:
                    info.description = "Mine Diamond Ore (Iron+ pickaxe)";
                    info.sources.push_back(JEIItemEntry(BlockType::DIAMOND_ORE, "Diamond Ore", 0));
                    selectedRecipes.push_back(info);
                    break;
                case ItemType::IRON_INGOT:
                    info.description = "Smelt Iron Ore";
                    info.sources.push_back(JEIItemEntry(BlockType::IRON_ORE, "Iron Ore", 0));
                    selectedRecipes.push_back(info);
                    break;
                case ItemType::GOLD_INGOT:
                    info.description = "Smelt Gold Ore";
                    info.sources.push_back(JEIItemEntry(BlockType::GOLD_ORE, "Gold Ore", 0));
                    selectedRecipes.push_back(info);
                    break;
                case ItemType::FLINT:
                    info.description = "Mine Gravel (chance drop)";
                    info.sources.push_back(JEIItemEntry(BlockType::GRAVEL, "Gravel", 0));
                    selectedRecipes.push_back(info);
                    break;
                default:
                    break;
            }
        } else {
            // Blocks that come from mining other blocks
            RecipeInfo info;
            info.type = RecipeInfo::Type::MINING;

            switch (item.blockType) {
                case BlockType::COBBLESTONE:
                    info.description = "Mine Stone";
                    info.sources.push_back(JEIItemEntry(BlockType::STONE, "Stone", 0));
                    selectedRecipes.push_back(info);
                    break;
                case BlockType::DIRT:
                    info.description = "Mine Grass or Dirt";
                    info.sources.push_back(JEIItemEntry(BlockType::GRASS, "Grass", 0));
                    info.sources.push_back(JEIItemEntry(BlockType::DIRT, "Dirt", 0));
                    selectedRecipes.push_back(info);
                    break;
                default:
                    break;
            }
        }
    }

    // Check if mouse is over JEI panel
    bool isMouseOver(float mouseX, float mouseY, float panelX, float panelWidth, float panelY, float panelHeight) const {
        float scale = getScale();
        float slotSize = getSlotSize();
        float padding = getPadding();

        float jeiWidth = GRID_COLS * slotSize + padding * 2;
        float jeiX = panelX + panelWidth + padding;

        if (jeiX + jeiWidth > ui->windowWidth - padding) {
            jeiX = ui->windowWidth - jeiWidth - padding;
        }

        return mouseX >= jeiX && mouseX < jeiX + jeiWidth &&
               mouseY >= panelY && mouseY < panelY + panelHeight;
    }

private:
    MenuUIRenderer* ui = nullptr;
    GLuint textureAtlas = 0;
    GLuint itemTextureAtlas = 0;
    std::vector<JEIItemEntry> items;

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
};

// Forward declaration for 3x3 recipe registry
class CraftingTableRecipeRegistry;
struct CraftingRecipe3x3;

// Implementation of build3x3RecipesFor - needs CraftingTableUI.h to be included first
// This is defined inline so it can access CraftingTableRecipeRegistry

#ifndef JEI_PANEL_IMPL
#define JEI_PANEL_IMPL

// Include CraftingTableUI.h for 3x3 recipes - must be included in source file first
#include "CraftingTableUI.h"

inline void JEIPanel::build3x3RecipesFor(const JEIItemEntry& item) {
    const auto& recipes3x3 = CraftingTableRecipeRegistry::getInstance().getAllRecipes();

    for (const auto& recipe : recipes3x3) {
        bool matches = false;
        if (item.isBlock && recipe.result.type == StackType::BLOCK && recipe.result.blockType == item.blockType) {
            matches = true;
        } else if (!item.isBlock && recipe.result.type == StackType::ITEM && recipe.result.itemType == item.itemType) {
            matches = true;
        }

        if (matches) {
            RecipeInfo info;
            info.type = RecipeInfo::Type::CRAFTING_3X3;
            info.gridSize = 3;

            for (int i = 0; i < 9; i++) {
                const CraftingIngredient& ing = recipe.pattern[i];
                if (ing.isBlock()) {
                    info.ingredients[i] = JEIItemEntry(ing.blockType, "", 0);
                } else if (ing.isItem()) {
                    info.ingredients[i] = JEIItemEntry(ing.itemType, "", 0);
                }
            }

            selectedRecipes.push_back(info);
        }
    }
}

#endif // JEI_PANEL_IMPL
