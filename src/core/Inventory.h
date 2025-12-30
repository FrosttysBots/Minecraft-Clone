#pragma once

// Inventory System - ItemStack and Inventory classes
// Handles 36-slot inventory (9 hotbar + 27 main) with stacking, crafting, and armor

#include "../world/Block.h"
#include "Item.h"
#include <array>
#include <algorithm>

// Constants
constexpr int MAX_STACK_SIZE = 64;
constexpr int HOTBAR_SLOTS = 9;
constexpr int INVENTORY_ROWS = 3;
constexpr int INVENTORY_COLS = 9;
constexpr int INVENTORY_SLOTS = INVENTORY_ROWS * INVENTORY_COLS;  // 27
constexpr int TOTAL_SLOTS = HOTBAR_SLOTS + INVENTORY_SLOTS;       // 36
constexpr int CRAFTING_GRID_SIZE = 2;
constexpr int CRAFTING_SLOTS = CRAFTING_GRID_SIZE * CRAFTING_GRID_SIZE;  // 4
constexpr int ARMOR_SLOT_COUNT = 4;  // Helmet, Chestplate, Leggings, Boots

// ==================== STACK TYPE ====================
// Discriminator for what the ItemStack holds
enum class StackType : uint8_t {
    EMPTY = 0,
    BLOCK,
    ITEM
};

// ==================== ITEM STACK ====================
// ItemStack can hold either a block or an item
struct ItemStack {
    StackType stackType = StackType::EMPTY;

    union {
        BlockType blockType;
        ItemType itemType;
    };

    int count = 0;
    int durability = 0;  // For tools/armor: remaining uses

    // ==================== CONSTRUCTORS ====================
    ItemStack() : stackType(StackType::EMPTY), blockType(BlockType::AIR), count(0), durability(0) {}

    // Block constructor
    ItemStack(BlockType bt, int c)
        : stackType(bt == BlockType::AIR ? StackType::EMPTY : StackType::BLOCK)
        , blockType(bt)
        , count(bt == BlockType::AIR ? 0 : c)
        , durability(0) {}

    // Item constructor
    ItemStack(ItemType it, int c, int dur = -1)
        : stackType(it == ItemType::NONE ? StackType::EMPTY : StackType::ITEM)
        , itemType(it)
        , count(it == ItemType::NONE ? 0 : c)
        , durability(0) {
        // Auto-set durability for new tools/armor if not specified
        if (stackType == StackType::ITEM) {
            const ItemProperties& props = getItemProperties(it);
            durability = (dur < 0) ? props.maxDurability : dur;
        }
    }

    // ==================== STATE QUERIES ====================
    bool isEmpty() const {
        return stackType == StackType::EMPTY || count <= 0;
    }

    bool isBlock() const {
        return stackType == StackType::BLOCK && count > 0;
    }

    bool isItem() const {
        return stackType == StackType::ITEM && count > 0;
    }

    BlockType getBlockType() const {
        return isBlock() ? blockType : BlockType::AIR;
    }

    ItemType getItemType() const {
        return isItem() ? itemType : ItemType::NONE;
    }

    // ==================== STACK SIZE ====================
    int getMaxStackSize() const {
        if (isEmpty()) return MAX_STACK_SIZE;
        if (isBlock()) return MAX_STACK_SIZE;  // All blocks stack to 64
        return getItemProperties(itemType).maxStackSize;
    }

    bool isFull() const {
        return count >= getMaxStackSize();
    }

    // ==================== DURABILITY ====================
    bool hasDurability() const {
        if (!isItem()) return false;
        return getItemProperties(itemType).hasDurability();
    }

    int getMaxDurability() const {
        if (!isItem()) return 0;
        return getItemProperties(itemType).maxDurability;
    }

    float getDurabilityPercent() const {
        int maxDur = getMaxDurability();
        if (maxDur <= 0) return 1.0f;
        return static_cast<float>(durability) / maxDur;
    }

    // Use durability, returns true if item broke
    bool useDurability(int amount = 1) {
        if (!hasDurability()) return false;
        durability -= amount;
        if (durability <= 0) {
            clear();
            return true;  // Item broke
        }
        return false;
    }

    // ==================== TOOL/ITEM HELPERS ====================
    bool isTool() const {
        return isItem() && getItemProperties(itemType).isTool();
    }

    bool isArmor() const {
        return isItem() && getItemProperties(itemType).isArmor();
    }

    bool isFood() const {
        return isItem() && getItemProperties(itemType).isFood();
    }

    ToolCategory getToolCategory() const {
        if (!isItem()) return ToolCategory::NONE;
        return getItemProperties(itemType).toolCategory;
    }

    ToolTier getToolTier() const {
        if (!isItem()) return ToolTier::NONE;
        return getItemProperties(itemType).toolTier;
    }

    float getMiningSpeedMultiplier() const {
        if (!isItem()) return MiningSpeed::HAND;
        const ItemProperties& props = getItemProperties(itemType);
        if (props.isTool()) return props.miningSpeedMultiplier;
        return MiningSpeed::HAND;
    }

    int getAttackDamage() const {
        if (!isItem()) return AttackDamage::HAND;
        return getItemProperties(itemType).attackDamage;
    }

    ArmorSlot getArmorSlot() const {
        if (!isItem()) return ArmorSlot::NONE;
        return getItemProperties(itemType).armorSlot;
    }

    int getArmorPoints() const {
        if (!isItem()) return 0;
        return getItemProperties(itemType).armorPoints;
    }

    int getFoodHunger() const {
        if (!isItem()) return 0;
        return getItemProperties(itemType).foodHunger;
    }

    float getFoodSaturation() const {
        if (!isItem()) return 0.0f;
        return getItemProperties(itemType).foodSaturation;
    }

    const char* getName() const {
        if (isEmpty()) return "Empty";
        if (isBlock()) {
            // Simple block name lookup
            switch (blockType) {
                case BlockType::STONE: return "Stone";
                case BlockType::DIRT: return "Dirt";
                case BlockType::GRASS: return "Grass Block";
                case BlockType::COBBLESTONE: return "Cobblestone";
                case BlockType::WOOD_PLANKS: return "Wood Planks";
                case BlockType::WOOD_LOG: return "Wood Log";
                case BlockType::LEAVES: return "Leaves";
                case BlockType::SAND: return "Sand";
                case BlockType::GRAVEL: return "Gravel";
                case BlockType::WATER: return "Water";
                case BlockType::BEDROCK: return "Bedrock";
                case BlockType::COAL_ORE: return "Coal Ore";
                case BlockType::IRON_ORE: return "Iron Ore";
                case BlockType::GOLD_ORE: return "Gold Ore";
                case BlockType::DIAMOND_ORE: return "Diamond Ore";
                case BlockType::GLASS: return "Glass";
                case BlockType::BRICK: return "Bricks";
                case BlockType::SNOW_BLOCK: return "Snow Block";
                case BlockType::CACTUS: return "Cactus";
                case BlockType::GLOWSTONE: return "Glowstone";
                case BlockType::LAVA: return "Lava";
                default: return "Block";
            }
        }
        return getItemProperties(itemType).name;
    }

    // ==================== CLEAR ====================
    void clear() {
        stackType = StackType::EMPTY;
        blockType = BlockType::AIR;
        count = 0;
        durability = 0;
    }

    // ==================== STACK OPERATIONS ====================
    // Add items (for non-empty stacks of same type)
    int add(int amount) {
        // Only reject if truly empty (no type set) - not just count == 0
        if (stackType == StackType::EMPTY) return amount;
        int canAdd = getMaxStackSize() - count;
        int toAdd = std::min(amount, canAdd);
        count += toAdd;
        return amount - toAdd;
    }

    // Remove items, return how many were actually removed
    int remove(int amount) {
        int toRemove = std::min(amount, count);
        count -= toRemove;
        if (count <= 0) clear();
        return toRemove;
    }

    // Add block of specific type (for empty slots)
    int addBlockOfType(BlockType bt, int amount) {
        if (bt == BlockType::AIR) return amount;

        if (isEmpty()) {
            stackType = StackType::BLOCK;
            blockType = bt;
            count = 0;
        } else if (!isBlock() || blockType != bt) {
            return amount;  // Can't mix types
        }
        return add(amount);
    }

    // Add item of specific type (for empty slots)
    int addItemOfType(ItemType it, int amount, int dur = -1) {
        if (it == ItemType::NONE) return amount;

        const ItemProperties& props = getItemProperties(it);

        // Tools/armor don't stack
        if (props.hasDurability()) {
            if (!isEmpty()) return amount;  // Can't stack with anything
            stackType = StackType::ITEM;
            itemType = it;
            count = 1;
            durability = (dur < 0) ? props.maxDurability : dur;
            return amount - 1;
        }

        if (isEmpty()) {
            stackType = StackType::ITEM;
            itemType = it;
            count = 0;
            durability = 0;
        } else if (!isItem() || itemType != it) {
            return amount;  // Can't mix types
        }
        return add(amount);
    }

    // Split the stack, taking up to 'amount' items
    ItemStack split(int amount) {
        ItemStack result;
        if (isEmpty()) return result;

        int toSplit = std::min(amount, count);
        result = *this;
        result.count = toSplit;
        count -= toSplit;
        if (count <= 0) clear();
        return result;
    }

    // Can this stack merge with another?
    bool canMergeWith(const ItemStack& other) const {
        if (isEmpty() || other.isEmpty()) return true;
        if (stackType != other.stackType) return false;

        if (isBlock()) {
            return blockType == other.blockType && !isFull();
        } else {
            // Items: same type, and stackable (durability items can't stack)
            if (itemType != other.itemType) return false;
            if (hasDurability()) return false;  // Tools don't stack
            return !isFull();
        }
    }

    // Merge another stack into this one, return leftover
    ItemStack merge(ItemStack& other) {
        if (other.isEmpty()) return ItemStack();

        if (isEmpty()) {
            // Take all from other
            *this = other;
            other.clear();
            return ItemStack();
        }

        if (!canMergeWith(other)) {
            return other;  // Can't merge
        }

        int overflow = add(other.count);
        if (overflow > 0) {
            other.count = overflow;
            return other;
        }
        other.clear();
        return ItemStack();
    }

    // ==================== COMPARISON ====================
    bool isSameType(const ItemStack& other) const {
        if (stackType != other.stackType) return false;
        if (isEmpty() && other.isEmpty()) return true;
        if (isBlock()) return blockType == other.blockType;
        return itemType == other.itemType;
    }
};

// Forward declaration for crafting
class CraftingRecipeRegistry;

// ==================== INVENTORY CLASS ====================
class Inventory {
public:
    // Storage: hotbar (0-8) + main inventory (9-35)
    std::array<ItemStack, TOTAL_SLOTS> slots;

    // Currently selected hotbar slot (0-8)
    int selectedSlot = 0;

    // Crafting grid (2x2)
    std::array<ItemStack, CRAFTING_SLOTS> craftingGrid;
    ItemStack craftingResult;

    // Item being held by cursor during inventory interaction
    ItemStack cursorStack;

    // Armor slots: 0=Helmet, 1=Chestplate, 2=Leggings, 3=Boots
    std::array<ItemStack, ARMOR_SLOT_COUNT> armorSlots;

    // ==================== SLOT ACCESS ====================
    ItemStack& getSlot(int index) {
        return slots[std::clamp(index, 0, TOTAL_SLOTS - 1)];
    }

    const ItemStack& getSlot(int index) const {
        return slots[std::clamp(index, 0, TOTAL_SLOTS - 1)];
    }

    ItemStack& getHotbarSlot(int index) {
        return slots[std::clamp(index, 0, HOTBAR_SLOTS - 1)];
    }

    ItemStack& getInventorySlot(int index) {
        return slots[HOTBAR_SLOTS + std::clamp(index, 0, INVENTORY_SLOTS - 1)];
    }

    // Get currently held item
    ItemStack& getSelectedStack() {
        return slots[selectedSlot];
    }

    const ItemStack& getSelectedStack() const {
        return slots[selectedSlot];
    }

    // Get currently selected block for placement (only if holding a block)
    BlockType getSelectedBlock() const {
        const ItemStack& stack = slots[selectedSlot];
        return stack.getBlockType();
    }

    // Get count of selected item
    int getSelectedCount() const {
        return slots[selectedSlot].count;
    }

    // ==================== SLOT SELECTION ====================
    void selectSlot(int index) {
        selectedSlot = std::clamp(index, 0, HOTBAR_SLOTS - 1);
    }

    void cycleSlot(int direction) {
        selectedSlot += direction;
        if (selectedSlot < 0) selectedSlot = HOTBAR_SLOTS - 1;
        if (selectedSlot >= HOTBAR_SLOTS) selectedSlot = 0;
    }

    // ==================== ADD ITEMS ====================
    // Add block to inventory (hotbar first, then main)
    int addBlock(BlockType type, int count) {
        if (type == BlockType::AIR || count <= 0) return 0;

        int remaining = count;

        // First pass: try to stack with existing blocks in hotbar
        for (int i = 0; i < HOTBAR_SLOTS && remaining > 0; i++) {
            if (slots[i].isBlock() && slots[i].blockType == type && !slots[i].isFull()) {
                remaining = slots[i].addBlockOfType(type, remaining);
            }
        }

        // Second pass: try to stack with existing blocks in main inventory
        for (int i = HOTBAR_SLOTS; i < TOTAL_SLOTS && remaining > 0; i++) {
            if (slots[i].isBlock() && slots[i].blockType == type && !slots[i].isFull()) {
                remaining = slots[i].addBlockOfType(type, remaining);
            }
        }

        // Third pass: find empty slots in hotbar
        for (int i = 0; i < HOTBAR_SLOTS && remaining > 0; i++) {
            if (slots[i].isEmpty()) {
                remaining = slots[i].addBlockOfType(type, remaining);
            }
        }

        // Fourth pass: find empty slots in main inventory
        for (int i = HOTBAR_SLOTS; i < TOTAL_SLOTS && remaining > 0; i++) {
            if (slots[i].isEmpty()) {
                remaining = slots[i].addBlockOfType(type, remaining);
            }
        }

        return remaining;
    }

    // Add item to inventory
    int addItem(ItemType type, int count, int durability = -1) {
        if (type == ItemType::NONE || count <= 0) return 0;

        const ItemProperties& props = getItemProperties(type);
        int remaining = count;

        // Tools/armor: find empty slots only (don't stack)
        if (props.hasDurability()) {
            for (int i = 0; i < TOTAL_SLOTS && remaining > 0; i++) {
                if (slots[i].isEmpty()) {
                    slots[i] = ItemStack(type, 1, durability);
                    remaining--;
                }
            }
            return remaining;
        }

        // Stackable items: try to stack first
        for (int i = 0; i < HOTBAR_SLOTS && remaining > 0; i++) {
            if (slots[i].isItem() && slots[i].itemType == type && !slots[i].isFull()) {
                remaining = slots[i].addItemOfType(type, remaining);
            }
        }

        for (int i = HOTBAR_SLOTS; i < TOTAL_SLOTS && remaining > 0; i++) {
            if (slots[i].isItem() && slots[i].itemType == type && !slots[i].isFull()) {
                remaining = slots[i].addItemOfType(type, remaining);
            }
        }

        // Then empty slots
        for (int i = 0; i < HOTBAR_SLOTS && remaining > 0; i++) {
            if (slots[i].isEmpty()) {
                remaining = slots[i].addItemOfType(type, remaining);
            }
        }

        for (int i = HOTBAR_SLOTS; i < TOTAL_SLOTS && remaining > 0; i++) {
            if (slots[i].isEmpty()) {
                remaining = slots[i].addItemOfType(type, remaining);
            }
        }

        return remaining;
    }

    // Legacy compatibility: add by BlockType (forwards to addBlock)
    int addItem(BlockType type, int count) {
        return addBlock(type, count);
    }

    // ==================== REMOVE ITEMS ====================
    int removeBlock(BlockType type, int count) {
        int remaining = count;
        for (int i = 0; i < TOTAL_SLOTS && remaining > 0; i++) {
            if (slots[i].isBlock() && slots[i].blockType == type) {
                remaining -= slots[i].remove(remaining);
            }
        }
        return count - remaining;
    }

    int removeItem(ItemType type, int count) {
        int remaining = count;
        for (int i = 0; i < TOTAL_SLOTS && remaining > 0; i++) {
            if (slots[i].isItem() && slots[i].itemType == type) {
                remaining -= slots[i].remove(remaining);
            }
        }
        return count - remaining;
    }

    // Legacy compatibility
    int removeItem(BlockType type, int count) {
        return removeBlock(type, count);
    }

    // ==================== QUERY ITEMS ====================
    bool hasBlock(BlockType type, int count = 1) const {
        return countBlock(type) >= count;
    }

    bool hasItem(ItemType type, int count = 1) const {
        return countItem(type) >= count;
    }

    // Legacy compatibility
    bool hasItem(BlockType type, int count = 1) const {
        return hasBlock(type, count);
    }

    int countBlock(BlockType type) const {
        int total = 0;
        for (const auto& slot : slots) {
            if (slot.isBlock() && slot.blockType == type) {
                total += slot.count;
            }
        }
        return total;
    }

    int countItem(ItemType type) const {
        int total = 0;
        for (const auto& slot : slots) {
            if (slot.isItem() && slot.itemType == type) {
                total += slot.count;
            }
        }
        return total;
    }

    // Legacy compatibility
    int countItem(BlockType type) const {
        return countBlock(type);
    }

    int findBlock(BlockType type) const {
        for (int i = 0; i < TOTAL_SLOTS; i++) {
            if (slots[i].isBlock() && slots[i].blockType == type) {
                return i;
            }
        }
        return -1;
    }

    int findItem(ItemType type) const {
        for (int i = 0; i < TOTAL_SLOTS; i++) {
            if (slots[i].isItem() && slots[i].itemType == type) {
                return i;
            }
        }
        return -1;
    }

    // Legacy compatibility
    int findItem(BlockType type) const {
        return findBlock(type);
    }

    int findEmptySlot() const {
        for (int i = 0; i < TOTAL_SLOTS; i++) {
            if (slots[i].isEmpty()) {
                return i;
            }
        }
        return -1;
    }

    // ==================== SLOT OPERATIONS ====================
    void swapSlots(int index1, int index2) {
        if (index1 >= 0 && index1 < TOTAL_SLOTS &&
            index2 >= 0 && index2 < TOTAL_SLOTS) {
            std::swap(slots[index1], slots[index2]);
        }
    }

    void quickTransfer(int slotIndex) {
        if (slotIndex < 0 || slotIndex >= TOTAL_SLOTS) return;
        if (slots[slotIndex].isEmpty()) return;

        ItemStack& source = slots[slotIndex];
        bool isHotbar = slotIndex < HOTBAR_SLOTS;

        int targetStart = isHotbar ? HOTBAR_SLOTS : 0;
        int targetEnd = isHotbar ? TOTAL_SLOTS : HOTBAR_SLOTS;

        // First try to stack with existing
        for (int i = targetStart; i < targetEnd && !source.isEmpty(); i++) {
            if (slots[i].canMergeWith(source) && slots[i].isSameType(source)) {
                source.count = slots[i].add(source.count);
                if (source.count <= 0) source.clear();
            }
        }

        // Then try empty slots
        for (int i = targetStart; i < targetEnd && !source.isEmpty(); i++) {
            if (slots[i].isEmpty()) {
                slots[i] = source;
                source.clear();
                break;
            }
        }
    }

    // ==================== ARMOR ====================
    ItemStack& getArmorSlot(ArmorSlot slot) {
        int idx = static_cast<int>(slot) - 1;
        return armorSlots[std::clamp(idx, 0, ARMOR_SLOT_COUNT - 1)];
    }

    const ItemStack& getArmorSlot(ArmorSlot slot) const {
        int idx = static_cast<int>(slot) - 1;
        return armorSlots[std::clamp(idx, 0, ARMOR_SLOT_COUNT - 1)];
    }

    // Try to equip armor from inventory slot, returns true if successful
    bool equipArmor(int inventorySlot) {
        if (inventorySlot < 0 || inventorySlot >= TOTAL_SLOTS) return false;

        ItemStack& item = slots[inventorySlot];
        if (!item.isArmor()) return false;

        ArmorSlot slot = item.getArmorSlot();
        int armorIdx = static_cast<int>(slot) - 1;
        if (armorIdx < 0 || armorIdx >= ARMOR_SLOT_COUNT) return false;

        // Swap with current armor in that slot
        std::swap(armorSlots[armorIdx], item);
        return true;
    }

    // Try to equip armor from cursor
    bool equipArmorFromCursor() {
        if (!cursorStack.isArmor()) return false;

        ArmorSlot slot = cursorStack.getArmorSlot();
        int armorIdx = static_cast<int>(slot) - 1;
        if (armorIdx < 0 || armorIdx >= ARMOR_SLOT_COUNT) return false;

        std::swap(armorSlots[armorIdx], cursorStack);
        return true;
    }

    // Calculate total armor points
    int getTotalArmorPoints() const {
        int total = 0;
        for (const auto& armor : armorSlots) {
            total += armor.getArmorPoints();
        }
        return total;
    }

    // Calculate damage reduction (0.0 to 0.8)
    float getDamageReduction() const {
        int armor = getTotalArmorPoints();
        // Each armor point = 4% reduction, max 80%
        return std::min(armor * 0.04f, 0.8f);
    }

    // Damage all armor pieces
    void damageArmor(int damageAmount) {
        int perPiece = std::max(1, damageAmount / 4);
        for (auto& armor : armorSlots) {
            if (armor.hasDurability()) {
                armor.useDurability(perPiece);
            }
        }
    }

    // ==================== CLEAR ====================
    void clear() {
        for (auto& slot : slots) {
            slot.clear();
        }
        for (auto& slot : craftingGrid) {
            slot.clear();
        }
        for (auto& slot : armorSlots) {
            slot.clear();
        }
        craftingResult.clear();
        cursorStack.clear();
        selectedSlot = 0;
    }

    void clearCraftingGrid() {
        for (auto& slot : craftingGrid) {
            if (!slot.isEmpty()) {
                if (slot.isBlock()) {
                    addBlock(slot.blockType, slot.count);
                } else if (slot.isItem()) {
                    addItem(slot.itemType, slot.count, slot.durability);
                }
                slot.clear();
            }
        }
        craftingResult.clear();
    }

    // ==================== CRAFTING ====================
    void updateCraftingResult();  // Implemented after CraftingRecipes.h
    void craftItem();  // Implemented after CraftingRecipes.h

    // ==================== INITIALIZATION ====================
    void initSurvival() {
        clear();
        // Give some starting blocks
        addBlock(BlockType::WOOD_LOG, 8);
        addBlock(BlockType::COBBLESTONE, 16);
        addBlock(BlockType::DIRT, 16);
    }

    void initCreative() {
        clear();
        int slot = 0;

        // Add all blocks
        for (int i = 1; i < static_cast<int>(BlockType::COUNT) && slot < TOTAL_SLOTS; i++) {
            BlockType type = static_cast<BlockType>(i);
            if (type == BlockType::BEDROCK) continue;
            slots[slot] = ItemStack(type, MAX_STACK_SIZE);
            slot++;
        }

        // Add some tools
        if (slot < TOTAL_SLOTS) slots[slot++] = ItemStack(ItemType::DIAMOND_PICKAXE, 1);
        if (slot < TOTAL_SLOTS) slots[slot++] = ItemStack(ItemType::DIAMOND_AXE, 1);
        if (slot < TOTAL_SLOTS) slots[slot++] = ItemStack(ItemType::DIAMOND_SHOVEL, 1);
        if (slot < TOTAL_SLOTS) slots[slot++] = ItemStack(ItemType::DIAMOND_SWORD, 1);
    }

    // Debug: give player tools for testing
    void giveTestItems() {
        addItem(ItemType::WOODEN_PICKAXE, 1);
        addItem(ItemType::STONE_PICKAXE, 1);
        addItem(ItemType::IRON_PICKAXE, 1);
        addItem(ItemType::DIAMOND_PICKAXE, 1);
        addItem(ItemType::WOODEN_SWORD, 1);
        addItem(ItemType::COAL, 16);
        addItem(ItemType::IRON_INGOT, 16);
        addItem(ItemType::DIAMOND, 8);
        addItem(ItemType::APPLE, 16);
        addItem(ItemType::COOKED_BEEF, 16);
    }
};
