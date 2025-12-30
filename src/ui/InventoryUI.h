#pragma once

// Inventory UI - Hotbar HUD and full inventory screen rendering
// Minecraft-style layout: Player area (left) + Crafting (right) + Inventory grid + Hotbar
// Supports both block and item rendering with durability bars

#include "MenuUI.h"
#include "../core/Inventory.h"
#include "../core/CraftingRecipes.h"
#include "../core/Config.h"
#include "../core/Item.h"
#include "../render/TextureAtlas.h"
#include "../render/ItemAtlas.h"
#include "../world/Block.h"
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>

// External config reference
extern GameConfig g_config;

class InventoryUI {
public:
    bool isOpen = false;

    // Animation timer for durability effects
    float animationTime = 0.0f;

    // Tool breaking effect state
    bool showBreakingEffect = false;
    float breakingEffectTimer = 0.0f;
    float breakingEffectX = 0.0f;
    float breakingEffectY = 0.0f;

    // Base UI dimensions (scaled by g_config.guiScale)
    static constexpr float BASE_SLOT_SIZE = 40.0f;
    static constexpr float BASE_SLOT_GAP = 2.0f;
    static constexpr float BASE_PADDING = 12.0f;
    static constexpr float BASE_HOTBAR_SLOT_SIZE = 44.0f;

    // Computed scaled values (call getScale() to get current scale)
    float getScale() const { return g_config.guiScale; }
    float getSlotSize() const { return BASE_SLOT_SIZE * getScale(); }
    float getSlotGap() const { return BASE_SLOT_GAP * getScale(); }
    float getPadding() const { return BASE_PADDING * getScale(); }
    float getHotbarSlotSize() const { return BASE_HOTBAR_SLOT_SIZE * getScale(); }

    // Colors - Minecraft-style palette
    // Panel colors
    const glm::vec4 INVENTORY_BG = {0.78f, 0.78f, 0.78f, 1.0f};       // Main panel gray (198, 198, 198)
    const glm::vec4 PANEL_BORDER_DARK = {0.33f, 0.33f, 0.33f, 1.0f};  // Dark outer border
    const glm::vec4 PANEL_BORDER_LIGHT = {1.0f, 1.0f, 1.0f, 1.0f};    // Light inner highlight

    // 3D Beveled slot colors (Minecraft style)
    const glm::vec4 SLOT_OUTER_DARK = {0.22f, 0.22f, 0.22f, 1.0f};    // Dark top/left edge (55, 55, 55)
    const glm::vec4 SLOT_OUTER_LIGHT = {1.0f, 1.0f, 1.0f, 1.0f};      // Light bottom/right edge
    const glm::vec4 SLOT_INNER_DARK = {0.49f, 0.49f, 0.49f, 1.0f};    // Secondary dark edge (125, 125, 125)
    const glm::vec4 SLOT_INNER_LIGHT = {0.88f, 0.88f, 0.88f, 1.0f};   // Secondary light edge
    const glm::vec4 SLOT_BG = {0.55f, 0.55f, 0.55f, 1.0f};            // Slot interior (139, 139, 139)
    const glm::vec4 SLOT_HOVER = {0.65f, 0.65f, 0.72f, 1.0f};         // Slight blue tint on hover
    const glm::vec4 SLOT_SELECTED = {1.0f, 1.0f, 0.6f, 1.0f};         // Yellow highlight for selected

    // Legacy compatibility
    const glm::vec4 SLOT_BORDER = SLOT_OUTER_DARK;
    const glm::vec4 SLOT_INNER = SLOT_BG;

    // Text colors
    const glm::vec4 TEXT_DARK = {0.25f, 0.25f, 0.25f, 1.0f};
    const glm::vec4 TEXT_WHITE = {1.0f, 1.0f, 1.0f, 1.0f};
    const glm::vec4 TEXT_SHADOW = {0.15f, 0.15f, 0.15f, 0.8f};
    const glm::vec4 TEXT_YELLOW = {1.0f, 1.0f, 0.4f, 1.0f};           // For slot numbers

    // HUD colors
    const glm::vec4 HOTBAR_BG = {0.0f, 0.0f, 0.0f, 0.7f};
    const glm::vec4 HOTBAR_SELECTED_BORDER = {1.0f, 1.0f, 1.0f, 1.0f};

    // Tooltip colors
    const glm::vec4 TOOLTIP_BG = {0.1f, 0.0f, 0.15f, 0.94f};          // Dark purple background
    const glm::vec4 TOOLTIP_BORDER = {0.25f, 0.0f, 0.5f, 1.0f};       // Purple border

    // Hovered item name for tooltip
    std::string hoveredItemName = "";

    void init(MenuUIRenderer* renderer, GLuint blockAtlas, GLuint itemAtlas = 0) {
        ui = renderer;
        textureAtlas = blockAtlas;
        itemTextureAtlas = itemAtlas;
    }

    void setItemAtlas(GLuint itemAtlas) {
        itemTextureAtlas = itemAtlas;
    }

    void open() {
        isOpen = true;
    }

    void close(Inventory& inventory) {
        isOpen = false;
        // Return crafting grid items to inventory
        inventory.clearCraftingGrid();
        // Drop cursor item back to inventory
        if (!inventory.cursorStack.isEmpty()) {
            if (inventory.cursorStack.isBlock()) {
                inventory.addBlock(inventory.cursorStack.blockType, inventory.cursorStack.count);
            } else if (inventory.cursorStack.isItem()) {
                inventory.addItem(inventory.cursorStack.itemType, inventory.cursorStack.count, inventory.cursorStack.durability);
            }
            inventory.cursorStack.clear();
        }
    }

    void toggle(Inventory& inventory) {
        if (isOpen) {
            close(inventory);
        } else {
            open();
        }
    }

    // Update animation timers
    void update(float deltaTime) {
        animationTime += deltaTime;

        // Update breaking effect
        if (showBreakingEffect) {
            breakingEffectTimer -= deltaTime;
            if (breakingEffectTimer <= 0.0f) {
                showBreakingEffect = false;
            }
        }
    }

    // Trigger tool breaking effect at position
    void triggerBreakingEffect(float x, float y) {
        showBreakingEffect = true;
        breakingEffectTimer = 0.5f;
        breakingEffectX = x;
        breakingEffectY = y;
    }

    // Render hotbar only (during gameplay when inventory closed)
    // survivalHudHeight is how much space is ABOVE the hotbar for health/hunger bars
    void renderHotbar(const Inventory& inventory, float /*survivalHudHeight*/ = 0.0f) {
        if (!ui || !ui->initialized) return;

        float scale = getScale();
        float hotbarSlotSize = getHotbarSlotSize();
        float hotbarGap = BASE_SLOT_GAP * scale;
        float hotbarPadding = 8.0f * scale;
        float hotbarWidth = HOTBAR_SLOTS * (hotbarSlotSize + hotbarGap) - hotbarGap;
        float hotbarX = (ui->windowWidth - hotbarWidth) / 2.0f;
        float hotbarY = ui->windowHeight - hotbarSlotSize - hotbarPadding;  // Bottom of screen

        // Hotbar background
        float bgPad = 4.0f * scale;
        ui->drawRect(hotbarX - bgPad, hotbarY - bgPad,
                    hotbarWidth + bgPad * 2, hotbarSlotSize + bgPad * 2,
                    HOTBAR_BG);

        // Render each slot with slot number (1-9)
        for (int i = 0; i < HOTBAR_SLOTS; i++) {
            float x = hotbarX + i * (hotbarSlotSize + hotbarGap);
            bool selected = (i == inventory.selectedSlot);

            renderHotbarSlot(x, hotbarY, hotbarSlotSize, inventory.slots[i], selected, i + 1);
        }
    }

    // Get the Y position where survival HUD should be rendered (above hotbar)
    float getHotbarTop() const {
        float scale = getScale();
        float hotbarSlotSize = getHotbarSlotSize();
        float hotbarPadding = 8.0f * scale;
        return ui ? (ui->windowHeight - hotbarSlotSize - hotbarPadding - hotbarPadding) : 0.0f;  // Above hotbar
    }

    // Render full inventory screen (Minecraft-style layout)
    void render(Inventory& inventory, float mx, float my) {
        if (!ui || !ui->initialized) return;

        // Update mouse position
        this->mouseX = mx;
        this->mouseY = my;

        // Get scaled values
        float scale = getScale();
        float slotSize = getSlotSize();
        float slotGap = getSlotGap();
        float padding = getPadding();
        currentSlotSize = slotSize;  // Store for isMouseInSlot

        // Calculate layout dimensions
        // Minecraft layout: 9 columns wide for inventory
        float gridWidth = INVENTORY_COLS * (slotSize + slotGap) - slotGap;

        // Top section: Player/Armor area (left) + Crafting (right)
        // Must fit 4 armor slots vertically (4 slots + gaps + padding)
        float armorSlotGapCalc = 4 * scale;
        float topSectionHeight = 4 * (slotSize + armorSlotGapCalc) + 16 * scale;  // 4 armor slots + padding

        // Main inventory: 3 rows
        float invHeight = INVENTORY_ROWS * (slotSize + slotGap) - slotGap;

        // Hotbar at bottom
        float hotbarHeight = slotSize;

        // Gaps between sections
        float sectionGap = 8.0f * scale;
        float hotbarGapY = 4.0f * scale;  // Smaller gap before hotbar

        // Total panel dimensions
        float totalWidth = gridWidth + padding * 2;
        float totalHeight = padding + topSectionHeight + sectionGap + invHeight + hotbarGapY + hotbarHeight + padding;

        // Center the panel
        float panelX = (ui->windowWidth - totalWidth) / 2.0f;
        float panelY = (ui->windowHeight - totalHeight) / 2.0f;

        // Draw 3D beveled panel (Minecraft style - raised look, opposite of slots)
        float bevel = 3.0f * scale;

        // Main background
        ui->drawRect(panelX, panelY, totalWidth, totalHeight, INVENTORY_BG);

        // Outer light edge (top and left) - raised look
        ui->drawRect(panelX, panelY, totalWidth, bevel, PANEL_BORDER_LIGHT);
        ui->drawRect(panelX, panelY, bevel, totalHeight, PANEL_BORDER_LIGHT);

        // Outer dark edge (bottom and right)
        ui->drawRect(panelX, panelY + totalHeight - bevel, totalWidth, bevel, PANEL_BORDER_DARK);
        ui->drawRect(panelX + totalWidth - bevel, panelY, bevel, totalHeight, PANEL_BORDER_DARK);

        // Inner bevels for depth
        float innerBevel = 2.0f * scale;
        ui->drawRect(panelX + bevel, panelY + bevel, totalWidth - bevel * 2, innerBevel,
                    glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
        ui->drawRect(panelX + bevel, panelY + bevel, innerBevel, totalHeight - bevel * 2,
                    glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
        ui->drawRect(panelX + bevel, panelY + totalHeight - bevel - innerBevel, totalWidth - bevel * 2, innerBevel,
                    glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
        ui->drawRect(panelX + totalWidth - bevel - innerBevel, panelY + bevel, innerBevel, totalHeight - bevel * 2,
                    glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));

        float contentX = panelX + padding;
        float currentY = panelY + padding;

        // === TOP SECTION: Armor Slots (left) + Crafting (right) ===

        // Armor slots area (left side)
        float playerAreaWidth = gridWidth * 0.45f;
        float playerAreaHeight = topSectionHeight;

        // Draw armor area background
        ui->drawRect(contentX, currentY, playerAreaWidth, playerAreaHeight,
                    glm::vec4(0.5f, 0.5f, 0.5f, 0.5f));
        ui->drawRectOutline(contentX, currentY, playerAreaWidth, playerAreaHeight, SLOT_BORDER, 1.0f);

        // Armor slots - vertical column on left side of player area
        hoveredArmorSlot = -1;
        float armorSlotX = contentX + 8 * scale;
        float armorSlotY = currentY + 8 * scale;
        float armorSlotGap = 4 * scale;

        // Armor slot labels (icons would be better but using text for now)
        const char* armorLabels[] = {"H", "C", "L", "B"};  // Helmet, Chestplate, Leggings, Boots

        for (int i = 0; i < ARMOR_SLOT_COUNT; i++) {
            float slotX = armorSlotX;
            float slotY = armorSlotY + i * (slotSize + armorSlotGap);

            bool hovered = isMouseInSlot(slotX, slotY);
            if (hovered) hoveredArmorSlot = i;

            // Render armor slot with special styling
            glm::vec4 slotBg = hovered ? SLOT_HOVER : glm::vec4(0.4f, 0.35f, 0.35f, 1.0f);
            ui->drawRect(slotX, slotY, slotSize, slotSize, SLOT_BORDER);
            ui->drawRect(slotX + 2 * scale, slotY + 2 * scale, slotSize - 4 * scale, slotSize - 4 * scale, slotBg);

            // Draw armor slot label if empty
            if (inventory.armorSlots[i].isEmpty()) {
                float labelScale = 0.9f * scale;
                float labelX = slotX + slotSize / 2 - 4 * scale;
                float labelY = slotY + slotSize / 2 - 6 * scale;
                ui->drawText(armorLabels[i], labelX, labelY, glm::vec4(0.3f, 0.3f, 0.3f, 0.5f), labelScale);
            } else {
                // Render armor item
                float iconPad = 4.0f * scale;
                renderItemStack(slotX + iconPad, slotY + iconPad, slotSize - iconPad * 2, inventory.armorSlots[i]);
                // Durability bar
                if (inventory.armorSlots[i].hasDurability() &&
                    inventory.armorSlots[i].durability < inventory.armorSlots[i].getMaxDurability()) {
                    renderDurabilityBar(slotX, slotY, slotSize, inventory.armorSlots[i].getDurabilityPercent(), scale);
                }
            }
        }

        // Player model placeholder (to the right of armor slots)
        float modelX = armorSlotX + slotSize + 15 * scale;
        float modelWidth = playerAreaWidth - slotSize - 30 * scale;
        float modelHeight = playerAreaHeight - 16 * scale;
        ui->drawRect(modelX, currentY + 8 * scale, modelWidth, modelHeight, glm::vec4(0.4f, 0.4f, 0.4f, 0.5f));
        ui->drawRectOutline(modelX, currentY + 8 * scale, modelWidth, modelHeight, SLOT_BORDER, 1.0f);
        float textScale = 0.7f * scale;
        ui->drawText("Player", modelX + modelWidth/2 - 22 * scale, currentY + playerAreaHeight/2 - 4 * scale, TEXT_DARK, textScale);

        // === CRAFTING SECTION (right side) ===
        float craftAreaX = contentX + gridWidth - (2 * (slotSize + slotGap) + 30 * scale + slotSize);
        float craftY = currentY + 20 * scale;

        // "Crafting" label
        ui->drawText("Crafting", craftAreaX, currentY, TEXT_DARK, 0.9f * scale);

        // Crafting grid (2x2)
        hoveredCraftingSlot = -1;
        for (int row = 0; row < 2; row++) {
            for (int col = 0; col < 2; col++) {
                int idx = row * 2 + col;
                float x = craftAreaX + col * (slotSize + slotGap);
                float y = craftY + row * (slotSize + slotGap);

                bool hovered = isMouseInSlot(x, y);
                if (hovered) hoveredCraftingSlot = idx;

                renderSlot(x, y, slotSize, inventory.craftingGrid[idx], false, hovered);
            }
        }

        // Arrow pointing to result (draw a proper arrow shape)
        float arrowX = craftAreaX + 2 * (slotSize + slotGap) + 8 * scale;
        float arrowY = craftY + slotSize / 2 + slotGap / 2;
        float arrowWidth = 22 * scale;
        float arrowHeight = 16 * scale;

        // Arrow body (rectangle)
        ui->drawRect(arrowX, arrowY - 3 * scale, arrowWidth * 0.6f, 6 * scale, SLOT_OUTER_DARK);
        // Arrow head (triangle made of rects)
        ui->drawRect(arrowX + arrowWidth * 0.5f, arrowY - arrowHeight / 2, 4 * scale, arrowHeight, SLOT_OUTER_DARK);
        ui->drawRect(arrowX + arrowWidth * 0.55f, arrowY - arrowHeight / 2 + 2 * scale, 3 * scale, arrowHeight - 4 * scale, SLOT_OUTER_DARK);
        ui->drawRect(arrowX + arrowWidth * 0.6f, arrowY - arrowHeight / 2 + 4 * scale, 3 * scale, arrowHeight - 8 * scale, SLOT_OUTER_DARK);
        ui->drawRect(arrowX + arrowWidth * 0.65f, arrowY - 3 * scale, 3 * scale, 6 * scale, SLOT_OUTER_DARK);

        // Result slot (slightly larger with special styling)
        float resultX = arrowX + arrowWidth + 8 * scale;
        float resultY = craftY + slotSize / 2 - slotSize / 2 + slotGap / 2;
        bool resultHovered = isMouseInSlot(resultX, resultY);
        hoveredResultSlot = resultHovered;

        // Glow effect when result is available
        bool hasResult = !inventory.craftingResult.isEmpty();
        if (hasResult) {
            float glowPulse = std::sin(animationTime * 3.0f) * 0.3f + 0.7f;
            glm::vec4 glowColor = glm::vec4(0.3f, 0.8f, 0.3f, glowPulse * 0.5f);
            float glowSize = 6 * scale;
            ui->drawRect(resultX - glowSize, resultY - glowSize,
                        slotSize + glowSize * 2, slotSize + glowSize * 2, glowColor);
        }

        // Draw beveled result slot
        drawBeveledSlot(resultX, resultY, slotSize, resultHovered, false);

        // Green border when result is available
        if (hasResult) {
            ui->drawRectOutline(resultX - 1, resultY - 1, slotSize + 2, slotSize + 2,
                               glm::vec4(0.2f, 0.9f, 0.2f, 1.0f), 2.0f * scale);

            float iconPad = 4.0f * scale;
            renderItemStack(resultX + iconPad, resultY + iconPad, slotSize - iconPad * 2, inventory.craftingResult);
            renderItemCount(resultX, resultY, slotSize, inventory.craftingResult.count, scale);
        }

        currentY += topSectionHeight + sectionGap;

        // === MAIN INVENTORY (3 rows of 9) ===
        hoveredInventorySlot = -1;
        for (int row = 0; row < INVENTORY_ROWS; row++) {
            for (int col = 0; col < INVENTORY_COLS; col++) {
                int slotIdx = HOTBAR_SLOTS + row * INVENTORY_COLS + col;
                float x = contentX + col * (slotSize + slotGap);
                float y = currentY + row * (slotSize + slotGap);

                bool hovered = isMouseInSlot(x, y);
                if (hovered) hoveredInventorySlot = slotIdx;

                renderSlot(x, y, slotSize, inventory.slots[slotIdx], false, hovered);
            }
        }

        currentY += invHeight + hotbarGapY;

        // === HOTBAR (bottom of inventory, separated by small gap) ===
        for (int i = 0; i < HOTBAR_SLOTS; i++) {
            float x = contentX + i * (slotSize + slotGap);
            float y = currentY;

            bool selected = (i == inventory.selectedSlot);
            bool hovered = isMouseInSlot(x, y);
            if (hovered) hoveredInventorySlot = i;

            renderSlot(x, y, slotSize, inventory.slots[i], selected, hovered);
        }

        // === CURSOR ITEM (rendered last, on top) ===
        if (!inventory.cursorStack.isEmpty()) {
            renderCursorItem(inventory.cursorStack, slotSize);
        }

        // === TOOLTIP (rendered very last, on top of everything) ===
        if (inventory.cursorStack.isEmpty()) {
            renderTooltip(inventory, scale);
        }
    }

    // Render tooltip for hovered item
    void renderTooltip(const Inventory& inventory, float scale) {
        const ItemStack* hoveredStack = nullptr;

        // Check what's being hovered
        if (hoveredInventorySlot >= 0 && hoveredInventorySlot < TOTAL_SLOTS) {
            hoveredStack = &inventory.slots[hoveredInventorySlot];
        } else if (hoveredCraftingSlot >= 0 && hoveredCraftingSlot < CRAFTING_SLOTS) {
            hoveredStack = &inventory.craftingGrid[hoveredCraftingSlot];
        } else if (hoveredArmorSlot >= 0 && hoveredArmorSlot < ARMOR_SLOT_COUNT) {
            hoveredStack = &inventory.armorSlots[hoveredArmorSlot];
        } else if (hoveredResultSlot) {
            hoveredStack = &inventory.craftingResult;
        }

        if (!hoveredStack || hoveredStack->isEmpty()) return;

        // Get item name
        std::string itemName = hoveredStack->getName();

        // Calculate tooltip dimensions
        float padding = 6.0f * scale;
        float textScale = 0.8f * scale;
        float textWidth = itemName.length() * 7.0f * scale;
        float tooltipWidth = textWidth + padding * 2;
        float tooltipHeight = 18.0f * scale + padding;

        // Position tooltip near mouse (offset to not cover cursor)
        float tooltipX = mouseX + 12 * scale;
        float tooltipY = mouseY - tooltipHeight - 4 * scale;

        // Keep tooltip on screen
        if (tooltipX + tooltipWidth > ui->windowWidth) {
            tooltipX = mouseX - tooltipWidth - 12 * scale;
        }
        if (tooltipY < 0) {
            tooltipY = mouseY + 20 * scale;
        }

        // Draw tooltip background (dark purple, Minecraft style)
        ui->drawRect(tooltipX, tooltipY, tooltipWidth, tooltipHeight, TOOLTIP_BG);

        // Draw tooltip border (purple outline)
        ui->drawRectOutline(tooltipX, tooltipY, tooltipWidth, tooltipHeight, TOOLTIP_BORDER, 1.0f * scale);

        // Inner highlight for 3D effect
        ui->drawRectOutline(tooltipX + 1, tooltipY + 1, tooltipWidth - 2, tooltipHeight - 2,
                           glm::vec4(0.4f, 0.2f, 0.6f, 0.5f), 1.0f);

        // Draw item name
        ui->drawText(itemName, tooltipX + padding, tooltipY + padding, TEXT_WHITE, textScale);

        // Show durability info for tools
        if (hoveredStack->hasDurability()) {
            std::string durText = "Durability: " + std::to_string(hoveredStack->durability) +
                                 "/" + std::to_string(hoveredStack->getMaxDurability());
            float durY = tooltipY + tooltipHeight;
            float durWidth = durText.length() * 6.0f * scale + padding * 2;
            float durHeight = 16.0f * scale;

            ui->drawRect(tooltipX, durY, std::max(tooltipWidth, durWidth), durHeight, TOOLTIP_BG);
            ui->drawRectOutline(tooltipX, durY, std::max(tooltipWidth, durWidth), durHeight, TOOLTIP_BORDER, 1.0f * scale);
            ui->drawText(durText, tooltipX + padding, durY + 3 * scale, glm::vec4(0.7f, 0.7f, 0.7f, 1.0f), 0.65f * scale);
        }
    }

    // Handle mouse click in inventory
    // Returns true if click was handled
    bool handleMouseClick(int button, bool pressed, Inventory& inventory, bool shiftHeld) {
        if (!isOpen || !pressed) return false;

        // Check crafting result first
        if (hoveredResultSlot && !inventory.craftingResult.isEmpty()) {
            if (button == GLFW_MOUSE_BUTTON_LEFT) {
                // Craft item
                inventory.craftItem();
                return true;
            }
        }

        // Check crafting grid
        if (hoveredCraftingSlot >= 0 && hoveredCraftingSlot < CRAFTING_SLOTS) {
            handleSlotClick(inventory.craftingGrid[hoveredCraftingSlot],
                           inventory.cursorStack, button, shiftHeld);
            inventory.updateCraftingResult();
            return true;
        }

        // Check armor slots
        if (hoveredArmorSlot >= 0 && hoveredArmorSlot < ARMOR_SLOT_COUNT) {
            if (button == GLFW_MOUSE_BUTTON_LEFT) {
                ArmorSlot slotType = static_cast<ArmorSlot>(hoveredArmorSlot + 1);  // +1 because NONE=0
                ItemStack& armorSlot = inventory.armorSlots[hoveredArmorSlot];

                // If cursor has armor that fits this slot, try to equip
                if (!inventory.cursorStack.isEmpty() && inventory.cursorStack.isArmor()) {
                    ArmorSlot cursorArmorType = inventory.cursorStack.getArmorSlot();
                    if (cursorArmorType == slotType) {
                        // Swap cursor with armor slot
                        std::swap(inventory.cursorStack, armorSlot);
                    }
                } else if (inventory.cursorStack.isEmpty() && !armorSlot.isEmpty()) {
                    // Pick up armor from slot
                    std::swap(inventory.cursorStack, armorSlot);
                }
            }
            return true;
        }

        // Check inventory slots
        if (hoveredInventorySlot >= 0 && hoveredInventorySlot < TOTAL_SLOTS) {
            if (shiftHeld && button == GLFW_MOUSE_BUTTON_LEFT) {
                // Quick transfer
                inventory.quickTransfer(hoveredInventorySlot);
            } else {
                handleSlotClick(inventory.slots[hoveredInventorySlot],
                               inventory.cursorStack, button, shiftHeld);
            }
            return true;
        }

        return false;
    }

    int getHoveredSlot() const { return hoveredInventorySlot; }
    int getHoveredCraftingSlot() const { return hoveredCraftingSlot; }
    int getHoveredArmorSlot() const { return hoveredArmorSlot; }
    bool isHoveringResult() const { return hoveredResultSlot; }

    // Render tool breaking particle effect (public for main.cpp access)
    void renderBreakingEffect(float scale) {
        if (!showBreakingEffect || !ui) return;

        float progress = 1.0f - (breakingEffectTimer / 0.5f);

        // Expanding particles
        int numParticles = 8;
        for (int i = 0; i < numParticles; i++) {
            float angle = (static_cast<float>(i) / numParticles) * 6.28318f;
            float distance = progress * 40.0f * scale;
            float px = breakingEffectX + std::cos(angle) * distance;
            float py = breakingEffectY + std::sin(angle) * distance;

            float particleSize = (1.0f - progress) * 6.0f * scale;
            float alpha = (1.0f - progress) * 0.9f;

            // Particle color (brownish for wood tools, grayish for stone, etc.)
            glm::vec4 particleColor = glm::vec4(0.6f, 0.5f, 0.4f, alpha);
            ui->drawRect(px - particleSize / 2, py - particleSize / 2, particleSize, particleSize, particleColor);
        }

        // Central flash
        if (progress < 0.3f) {
            float flashAlpha = (1.0f - progress / 0.3f) * 0.5f;
            float flashSize = 30.0f * scale;
            ui->drawRect(breakingEffectX - flashSize / 2, breakingEffectY - flashSize / 2,
                        flashSize, flashSize, glm::vec4(1.0f, 0.9f, 0.7f, flashAlpha));
        }
    }

private:
    MenuUIRenderer* ui = nullptr;
    GLuint textureAtlas = 0;      // Block textures
    GLuint itemTextureAtlas = 0;  // Item textures

    float mouseX = 0, mouseY = 0;
    mutable float currentSlotSize = BASE_SLOT_SIZE;  // Updated during render
    int hoveredInventorySlot = -1;
    int hoveredCraftingSlot = -1;
    int hoveredArmorSlot = -1;  // 0=helmet, 1=chestplate, 2=leggings, 3=boots
    bool hoveredResultSlot = false;

    bool isMouseInSlot(float x, float y) const {
        return mouseX >= x && mouseX < x + currentSlotSize &&
               mouseY >= y && mouseY < y + currentSlotSize;
    }

    // Render slot for HUD hotbar (slightly different style with slot numbers)
    void renderHotbarSlot(float x, float y, float size, const ItemStack& stack, bool selected, int slotNumber = -1) {
        float scale = getScale();
        float bevel = 2.0f * scale;

        // 3D Beveled slot (same style but with transparency for HUD)
        // Outer dark edge (top and left)
        ui->drawRect(x, y, size, bevel, glm::vec4(0.1f, 0.1f, 0.1f, 0.9f));
        ui->drawRect(x, y, bevel, size, glm::vec4(0.1f, 0.1f, 0.1f, 0.9f));

        // Outer light edge (bottom and right)
        ui->drawRect(x, y + size - bevel, size, bevel, glm::vec4(0.6f, 0.6f, 0.6f, 0.9f));
        ui->drawRect(x + size - bevel, y, bevel, size, glm::vec4(0.6f, 0.6f, 0.6f, 0.9f));

        // Inner background
        glm::vec4 bg = selected ? glm::vec4(0.5f, 0.5f, 0.5f, 0.9f) : glm::vec4(0.25f, 0.25f, 0.25f, 0.85f);
        ui->drawRect(x + bevel, y + bevel, size - bevel * 2, size - bevel * 2, bg);

        // Selection highlight (bright white border)
        if (selected) {
            ui->drawRectOutline(x - 2 * scale, y - 2 * scale, size + 4 * scale, size + 4 * scale, HOTBAR_SELECTED_BORDER, 2.0f * scale);
        }

        // Slot number in top-left corner (1-9)
        if (slotNumber >= 1 && slotNumber <= 9) {
            std::string numStr = std::to_string(slotNumber);
            float numScale = 0.5f * scale;
            float numX = x + 3 * scale;
            float numY = y + 2 * scale;
            // Shadow
            ui->drawText(numStr, numX + 1, numY + 1, glm::vec4(0.0f, 0.0f, 0.0f, 0.7f), numScale);
            // Number (yellow for visibility)
            ui->drawText(numStr, numX, numY, TEXT_YELLOW, numScale);
        }

        // Item
        if (!stack.isEmpty()) {
            float iconPad = 4.0f * scale;
            float iconSize = size - iconPad * 2;
            renderItemStack(x + iconPad, y + iconPad, iconSize, stack);

            // Crack overlay for damaged items
            if (stack.hasDurability()) {
                renderCrackOverlay(x + iconPad, y + iconPad, iconSize, stack.getDurabilityPercent(), scale);
            }

            if (stack.count > 1) {
                renderItemCountHUD(x, y, size, stack.count, scale);
            }
            // Durability bar for tools/armor
            if (stack.hasDurability() && stack.durability < stack.getMaxDurability()) {
                renderDurabilityBar(x, y, size, stack.getDurabilityPercent(), scale);
            }
        }
    }

    // Draw a 3D beveled slot (Minecraft-style inset look)
    void drawBeveledSlot(float x, float y, float size, bool hovered, bool selected) {
        float scale = getScale();
        float bevel = 2.0f * scale;

        // Outer dark edge (top and left) - makes it look recessed
        ui->drawRect(x, y, size, bevel, SLOT_OUTER_DARK);              // Top
        ui->drawRect(x, y, bevel, size, SLOT_OUTER_DARK);              // Left

        // Outer light edge (bottom and right)
        ui->drawRect(x, y + size - bevel, size, bevel, SLOT_OUTER_LIGHT);  // Bottom
        ui->drawRect(x + size - bevel, y, bevel, size, SLOT_OUTER_LIGHT);  // Right

        // Inner dark edge (second layer)
        ui->drawRect(x + bevel, y + bevel, size - bevel * 2, bevel, SLOT_INNER_DARK);        // Top
        ui->drawRect(x + bevel, y + bevel, bevel, size - bevel * 2, SLOT_INNER_DARK);        // Left

        // Inner light edge (second layer)
        ui->drawRect(x + bevel, y + size - bevel * 2, size - bevel * 2, bevel, SLOT_INNER_LIGHT);  // Bottom
        ui->drawRect(x + size - bevel * 2, y + bevel, bevel, size - bevel * 2, SLOT_INNER_LIGHT);  // Right

        // Slot interior
        glm::vec4 bgColor = hovered ? SLOT_HOVER : SLOT_BG;
        ui->drawRect(x + bevel * 2, y + bevel * 2, size - bevel * 4, size - bevel * 4, bgColor);

        // Selection highlight (golden glow)
        if (selected) {
            ui->drawRectOutline(x - 2 * scale, y - 2 * scale, size + 4 * scale, size + 4 * scale, SLOT_SELECTED, 2.0f * scale);
        }
    }

    void renderSlot(float x, float y, float size, const ItemStack& stack, bool selected, bool hovered) {
        float scale = getScale();

        // Draw 3D beveled slot background
        drawBeveledSlot(x, y, size, hovered, selected);

        // Item
        if (!stack.isEmpty()) {
            float iconPad = 4.0f * scale;
            float iconSize = size - iconPad * 2;
            renderItemStack(x + iconPad, y + iconPad, iconSize, stack);

            // Crack overlay for damaged items
            if (stack.hasDurability()) {
                renderCrackOverlay(x + iconPad, y + iconPad, iconSize, stack.getDurabilityPercent(), scale);
            }

            if (stack.count > 1) {
                renderItemCount(x, y, size, stack.count, scale);
            }
            // Durability bar for tools/armor
            if (stack.hasDurability() && stack.durability < stack.getMaxDurability()) {
                renderDurabilityBar(x, y, size, stack.getDurabilityPercent(), scale);
            }
        }
    }

    // Render an ItemStack (can be block or item)
    void renderItemStack(float x, float y, float size, const ItemStack& stack) {
        if (stack.isEmpty()) return;

        if (stack.isBlock()) {
            renderBlockIcon(x, y, size, stack.blockType);
        } else if (stack.isItem()) {
            renderItemTypeIcon(x, y, size, stack.itemType);
        }
    }

    // Render crack overlay for damaged items
    void renderCrackOverlay(float x, float y, float size, float durabilityPercent, float scale) {
        if (durabilityPercent >= 0.5f) return;  // No cracks above 50%

        // Calculate crack intensity (0-1, stronger as durability drops)
        float crackIntensity = 1.0f - (durabilityPercent / 0.5f);

        // Darker tint for damaged items
        float tintAlpha = crackIntensity * 0.3f;
        ui->drawRect(x, y, size, size, glm::vec4(0.0f, 0.0f, 0.0f, tintAlpha));

        // Draw crack lines
        float lineWidth = 1.5f * scale;
        glm::vec4 crackColor = glm::vec4(0.2f, 0.15f, 0.1f, crackIntensity * 0.8f);

        if (durabilityPercent < 0.5f) {
            // First crack - diagonal
            float startX = x + size * 0.3f;
            float startY = y + size * 0.2f;
            float endX = x + size * 0.6f;
            float endY = y + size * 0.7f;
            drawCrackLine(startX, startY, endX, endY, lineWidth, crackColor);
        }

        if (durabilityPercent < 0.3f) {
            // Second crack - crossing
            float startX = x + size * 0.5f;
            float startY = y + size * 0.1f;
            float endX = x + size * 0.25f;
            float endY = y + size * 0.6f;
            drawCrackLine(startX, startY, endX, endY, lineWidth, crackColor);

            // Branch crack
            float startX2 = x + size * 0.35f;
            float startY2 = y + size * 0.4f;
            float endX2 = x + size * 0.7f;
            float endY2 = y + size * 0.5f;
            drawCrackLine(startX2, startY2, endX2, endY2, lineWidth * 0.7f, crackColor);
        }

        if (durabilityPercent < 0.15f) {
            // More cracks when critical
            float startX = x + size * 0.6f;
            float startY = y + size * 0.3f;
            float endX = x + size * 0.85f;
            float endY = y + size * 0.8f;
            drawCrackLine(startX, startY, endX, endY, lineWidth, crackColor);

            float startX2 = x + size * 0.15f;
            float startY2 = y + size * 0.5f;
            float endX2 = x + size * 0.4f;
            float endY2 = y + size * 0.9f;
            drawCrackLine(startX2, startY2, endX2, endY2, lineWidth * 0.8f, crackColor);

            // Red warning flash on critical items
            float flash = std::sin(animationTime * 8.0f) * 0.5f + 0.5f;
            ui->drawRect(x, y, size, size, glm::vec4(1.0f, 0.0f, 0.0f, flash * 0.15f));
        }
    }

    // Draw a crack line (simple rectangle for now)
    void drawCrackLine(float x1, float y1, float x2, float y2, float width, const glm::vec4& color) {
        // Calculate line length and angle
        float dx = x2 - x1;
        float dy = y2 - y1;
        float length = std::sqrt(dx * dx + dy * dy);

        // Draw as series of small rectangles along the line
        int segments = static_cast<int>(length / 2.0f) + 1;
        for (int i = 0; i < segments; i++) {
            float t = static_cast<float>(i) / segments;
            float px = x1 + dx * t;
            float py = y1 + dy * t;
            ui->drawRect(px - width / 2, py - width / 2, width, width, color);
        }
    }

    // Render a block texture (top face)
    void renderBlockIcon(float x, float y, float size, BlockType type) {
        if (type == BlockType::AIR || textureAtlas == 0) return;

        // Get texture UV from atlas (use top face for icon)
        BlockTextures tex = getBlockTextures(type);
        int slot = tex.faceSlots[4];  // Top face

        glm::vec4 uv = TextureAtlas::getUV(slot);
        drawTextureRegion(textureAtlas, x, y, size, size, uv.x, uv.y, uv.z - uv.x, uv.w - uv.y);
    }

    // Render an item texture
    void renderItemTypeIcon(float x, float y, float size, ItemType type) {
        if (type == ItemType::NONE || itemTextureAtlas == 0) return;

        int slot = ItemAtlas::getTextureSlot(type);
        glm::vec4 uv = ItemAtlas::getUV(slot);
        drawTextureRegion(itemTextureAtlas, x, y, size, size, uv.x, uv.y, uv.z - uv.x, uv.w - uv.y);
    }

    // Legacy: render by BlockType (for backwards compatibility)
    void renderItemIcon(float x, float y, float size, BlockType type) {
        renderBlockIcon(x, y, size, type);
    }

    // Render durability bar under item with smooth gradient and critical flashing
    void renderDurabilityBar(float x, float y, float slotSize, float percent, float scale) {
        float barHeight = 2.0f * scale;
        float barWidth = slotSize - 4.0f * scale;
        float barX = x + 2.0f * scale;
        float barY = y + slotSize - barHeight - 2.0f * scale;

        // Background (dark)
        ui->drawRect(barX, barY, barWidth, barHeight, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

        // Smooth gradient color based on durability (green -> yellow -> orange -> red)
        glm::vec4 barColor;
        if (percent > 0.5f) {
            // Green to Yellow (1.0 -> 0.5)
            float t = (percent - 0.5f) * 2.0f;  // 0 to 1
            barColor = glm::vec4(1.0f - t, 1.0f, 0.0f, 1.0f);
        } else if (percent > 0.25f) {
            // Yellow to Orange (0.5 -> 0.25)
            float t = (percent - 0.25f) * 4.0f;  // 0 to 1
            barColor = glm::vec4(1.0f, 0.5f + t * 0.5f, 0.0f, 1.0f);
        } else {
            // Orange to Red (0.25 -> 0)
            float t = percent * 4.0f;  // 0 to 1
            barColor = glm::vec4(1.0f, t * 0.5f, 0.0f, 1.0f);
        }

        // Critical flashing when durability < 10%
        if (percent < 0.1f) {
            float flash = std::sin(animationTime * 10.0f) * 0.5f + 0.5f;
            barColor.r = glm::mix(barColor.r, 1.0f, flash * 0.5f);
            barColor.a = glm::mix(0.7f, 1.0f, flash);
        }

        ui->drawRect(barX, barY, barWidth * percent, barHeight, barColor);

        // Critical warning glow effect
        if (percent < 0.1f) {
            float flash = std::sin(animationTime * 10.0f) * 0.5f + 0.5f;
            glm::vec4 glowColor = glm::vec4(1.0f, 0.2f, 0.2f, flash * 0.3f);
            ui->drawRect(barX - 1, barY - 1, barWidth * percent + 2, barHeight + 2, glowColor);
        }
    }

    void renderItemCount(float x, float y, float size, int count, float scale = 1.0f) {
        if (count <= 1) return;

        std::string countStr = std::to_string(count);
        float textScale = 0.7f * scale;

        // Position at bottom-right of slot
        float textX = x + size - countStr.length() * 7 * scale - 3 * scale;
        float textY = y + size - 14 * scale;

        // Shadow
        ui->drawText(countStr, textX + 1 * scale, textY + 1 * scale, TEXT_SHADOW, textScale);
        // Text
        ui->drawText(countStr, textX, textY, TEXT_WHITE, textScale);
    }

    void renderItemCountHUD(float x, float y, float size, int count, float scale = 1.0f) {
        if (count <= 1) return;

        std::string countStr = std::to_string(count);
        float textScale = 0.75f * scale;

        // Position at bottom-right of slot
        float textX = x + size - countStr.length() * 7 * scale - 4 * scale;
        float textY = y + size - 15 * scale;

        // Shadow
        ui->drawText(countStr, textX + 1 * scale, textY + 1 * scale, TEXT_SHADOW, textScale);
        // Text
        ui->drawText(countStr, textX, textY, TEXT_WHITE, textScale);
    }

    void renderCursorItem(const ItemStack& stack, float slotSize) {
        if (stack.isEmpty()) return;

        float scale = getScale();
        float iconSize = slotSize - 8 * scale;
        float x = mouseX - iconSize / 2;
        float y = mouseY - iconSize / 2;

        renderItemStack(x, y, iconSize, stack);
        if (stack.count > 1) {
            renderItemCount(x - 4, y - 4, iconSize + 8, stack.count);
        }
        // Show durability bar on cursor item too
        if (stack.hasDurability() && stack.durability < stack.getMaxDurability()) {
            renderDurabilityBar(x - 4, y - 4, iconSize + 8, stack.getDurabilityPercent(), scale);
        }
    }

    void handleSlotClick(ItemStack& slot, ItemStack& cursor, int button, bool /*shift*/) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            // Left click: swap or merge
            if (cursor.isEmpty()) {
                // Pick up entire stack
                cursor = slot;
                slot.clear();
            } else if (slot.isEmpty()) {
                // Place entire stack
                slot = cursor;
                cursor.clear();
            } else if (slot.isSameType(cursor) && slot.canMergeWith(cursor)) {
                // Merge stacks (only if same type and can merge)
                int overflow = slot.add(cursor.count);
                if (overflow > 0) {
                    cursor.count = overflow;
                } else {
                    cursor.clear();
                }
            } else {
                // Swap stacks (including tools that don't stack)
                std::swap(slot, cursor);
            }
        } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            // Right click: split or place one
            if (cursor.isEmpty()) {
                // Pick up half (rounded up)
                int half = (slot.count + 1) / 2;
                cursor = slot.split(half);
            } else if (slot.isEmpty()) {
                // Place one item from cursor
                slot = cursor;
                slot.count = 1;
                cursor.remove(1);
            } else if (slot.isSameType(cursor) && slot.canMergeWith(cursor)) {
                // Add one item
                slot.add(1);
                cursor.remove(1);
            }
        }
    }

    // Draw a region of a texture (for atlas sampling)
    void drawTextureRegion(GLuint textureID, float x, float y, float w, float h,
                           float u0, float v0, float uSize, float vSize) {
        if (!ui || textureID == 0) return;

        // Use the texture shader from MenuUIRenderer
        glUseProgram(ui->texShaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(ui->texShaderProgram, "projection"),
                          1, GL_FALSE, &ui->projection[0][0]);

        // Create model matrix
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(x, y, 0.0f));
        model = glm::scale(model, glm::vec3(w, h, 1.0f));
        glUniformMatrix4fv(glGetUniformLocation(ui->texShaderProgram, "model"),
                          1, GL_FALSE, &model[0][0]);

        // Create custom quad with UV coordinates
        float vertices[] = {
            // pos      // uv
            0.0f, 0.0f, u0, v0,
            1.0f, 0.0f, u0 + uSize, v0,
            1.0f, 1.0f, u0 + uSize, v0 + vSize,
            0.0f, 0.0f, u0, v0,
            1.0f, 1.0f, u0 + uSize, v0 + vSize,
            0.0f, 1.0f, u0, v0 + vSize
        };

        // Use texVAO but update buffer data
        glBindVertexArray(ui->texVAO);
        glBindBuffer(GL_ARRAY_BUFFER, ui->texVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureID);
        glUniform1i(glGetUniformLocation(ui->texShaderProgram, "tex"), 0);

        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }
};
