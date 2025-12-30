#pragma once

// Dropped Item Entity System
// Handles 3D items that drop when mining blocks
// Items fall with gravity, bob up and down, spin, and can be picked up

#include "../core/Inventory.h"
#include "../core/Item.h"
#include "../render/TextureAtlas.h"
#include "../render/ItemAtlas.h"
#include "Block.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <random>
#include <cmath>

// Forward declaration
class World;

// ==================== DROPPED ITEM ENTITY ====================
struct DroppedItem {
    // Position and physics
    glm::vec3 position;
    glm::vec3 velocity;

    // Item data
    ItemStack stack;

    // Visual state
    float rotation = 0.0f;          // Y-axis rotation (spinning)
    float bobOffset = 0.0f;         // Vertical bob animation
    float bobPhase = 0.0f;          // Phase offset for bob animation

    // Lifetime
    float lifetime = 300.0f;        // 5 minutes before despawn
    float pickupDelay = 0.5f;       // Can't pickup immediately after spawn
    float mergeDelay = 0.2f;        // Delay before merging with nearby items

    // State flags
    bool onGround = false;
    bool markedForRemoval = false;

    DroppedItem() = default;

    DroppedItem(const glm::vec3& pos, const ItemStack& item)
        : position(pos), velocity(0.0f), stack(item) {
        // Random bob phase so items don't all bob in sync
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_real_distribution<float> phaseDist(0.0f, 6.28318f);
        bobPhase = phaseDist(gen);
    }

    // Spawn with random velocity (like when breaking a block)
    static DroppedItem spawnWithVelocity(const glm::vec3& pos, const ItemStack& item) {
        DroppedItem drop(pos, item);

        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_real_distribution<float> velDist(-2.0f, 2.0f);
        static std::uniform_real_distribution<float> upDist(2.0f, 4.0f);

        drop.velocity.x = velDist(gen);
        drop.velocity.y = upDist(gen);
        drop.velocity.z = velDist(gen);

        return drop;
    }
};

// ==================== DROPPED ITEM MANAGER ====================
class DroppedItemManager {
public:
    static constexpr float GRAVITY = -20.0f;
    static constexpr float DRAG = 0.98f;
    static constexpr float GROUND_FRICTION = 0.7f;
    static constexpr float BOB_SPEED = 2.5f;
    static constexpr float BOB_AMPLITUDE = 0.1f;
    static constexpr float SPIN_SPEED = 2.0f;
    static constexpr float PICKUP_RADIUS = 1.5f;  // ~1.5 block pickup range
    static constexpr float PLAYER_HEIGHT = 1.8f;  // Player is ~1.8 blocks tall
    static constexpr float MERGE_RADIUS = 0.5f;
    static constexpr int MAX_DROPPED_ITEMS = 500;

    std::vector<DroppedItem> items;

    // Spawn a dropped item at position with random velocity
    void spawnDrop(const glm::vec3& position, const ItemStack& stack) {
        if (items.size() >= MAX_DROPPED_ITEMS) {
            // Remove oldest item to make room
            items.erase(items.begin());
        }

        // Offset slightly to center of block
        glm::vec3 spawnPos = position + glm::vec3(0.5f, 0.5f, 0.5f);
        items.push_back(DroppedItem::spawnWithVelocity(spawnPos, stack));
    }

    // Spawn from BlockDrop
    void spawnBlockDrop(const glm::vec3& blockPos, const BlockDrop& drop) {
        if (drop.count <= 0) return;

        ItemStack stack;
        if (drop.isItem) {
            stack = ItemStack(static_cast<ItemType>(drop.typeId), drop.count);
        } else {
            stack = ItemStack(static_cast<BlockType>(drop.typeId), drop.count);
        }

        spawnDrop(blockPos, stack);
    }

    // Update all dropped items
    void update(float deltaTime, World& world, glm::vec3 playerPos, Inventory& inventory) {
        for (auto& item : items) {
            if (item.markedForRemoval) continue;

            // Update lifetime
            item.lifetime -= deltaTime;
            if (item.lifetime <= 0.0f) {
                item.markedForRemoval = true;
                continue;
            }

            // Update pickup delay
            if (item.pickupDelay > 0.0f) {
                item.pickupDelay -= deltaTime;
            }

            // Update merge delay
            if (item.mergeDelay > 0.0f) {
                item.mergeDelay -= deltaTime;
            }

            // Apply gravity
            if (!item.onGround) {
                item.velocity.y += GRAVITY * deltaTime;
            }

            // Apply velocity
            glm::vec3 newPos = item.position + item.velocity * deltaTime;

            // Collision detection with world
            updateCollision(item, newPos, world);

            // Apply drag
            item.velocity *= DRAG;
            if (item.onGround) {
                item.velocity.x *= GROUND_FRICTION;
                item.velocity.z *= GROUND_FRICTION;
            }

            // Update bob animation (only when on ground or slow)
            if (item.onGround || glm::length(item.velocity) < 0.5f) {
                item.bobOffset = std::sin(item.bobPhase) * BOB_AMPLITUDE;
                item.bobPhase += BOB_SPEED * deltaTime;
                if (item.bobPhase > 6.28318f) item.bobPhase -= 6.28318f;
            }

            // Update rotation (spinning)
            item.rotation += SPIN_SPEED * deltaTime;
            if (item.rotation > 6.28318f) item.rotation -= 6.28318f;

            // Check for pickup - simple 3D distance from player center
            if (item.pickupDelay <= 0.0f) {
                glm::vec3 playerCenter = playerPos + glm::vec3(0.0f, PLAYER_HEIGHT * 0.5f, 0.0f);
                float dist = glm::distance(item.position, playerCenter);

                if (dist < PICKUP_RADIUS) {
                    tryPickup(item, inventory);
                }
            }
        }

        // Merge nearby items of same type
        mergeNearbyItems();

        // Remove marked items
        items.erase(
            std::remove_if(items.begin(), items.end(),
                [](const DroppedItem& i) { return i.markedForRemoval; }),
            items.end()
        );
    }

    // Get render position (with bob offset)
    glm::vec3 getRenderPosition(const DroppedItem& item) const {
        return item.position + glm::vec3(0.0f, item.bobOffset, 0.0f);
    }

    size_t getItemCount() const { return items.size(); }

private:
    void updateCollision(DroppedItem& item, glm::vec3& newPos, World& world);

    void tryPickup(DroppedItem& item, Inventory& inventory) {
        // Try to add to inventory
        int originalCount = item.stack.count;

        if (item.stack.isBlock()) {
            int remaining = inventory.addBlock(item.stack.blockType, item.stack.count);
            if (remaining < originalCount) {
                item.stack.count = remaining;
            }
        } else if (item.stack.isItem()) {
            int remaining = inventory.addItem(item.stack.itemType, item.stack.count, item.stack.durability);
            if (remaining < originalCount) {
                item.stack.count = remaining;
            }
        }

        // If fully picked up, remove
        if (item.stack.count <= 0) {
            item.markedForRemoval = true;
        }
    }

    void mergeNearbyItems() {
        for (size_t i = 0; i < items.size(); i++) {
            if (items[i].markedForRemoval || items[i].mergeDelay > 0.0f) continue;

            for (size_t j = i + 1; j < items.size(); j++) {
                if (items[j].markedForRemoval || items[j].mergeDelay > 0.0f) continue;

                // Check if same type and can stack
                if (!canMerge(items[i].stack, items[j].stack)) continue;

                // Check distance
                float dist = glm::distance(items[i].position, items[j].position);
                if (dist > MERGE_RADIUS) continue;

                // Merge j into i
                int maxStack = items[i].stack.getMaxStackSize();
                int space = maxStack - items[i].stack.count;

                if (space > 0) {
                    int toTransfer = std::min(space, items[j].stack.count);
                    items[i].stack.count += toTransfer;
                    items[j].stack.count -= toTransfer;

                    if (items[j].stack.count <= 0) {
                        items[j].markedForRemoval = true;
                    }
                }
            }
        }
    }

    bool canMerge(const ItemStack& a, const ItemStack& b) {
        if (a.stackType != b.stackType) return false;
        if (a.stackType == StackType::EMPTY || b.stackType == StackType::EMPTY) return false;

        if (a.stackType == StackType::BLOCK) {
            return a.blockType == b.blockType;
        } else {
            // Items with durability can't stack
            if (a.hasDurability() || b.hasDurability()) return false;
            return a.itemType == b.itemType;
        }
    }
};

// Collision implementation (needs World access)
inline void DroppedItemManager::updateCollision(DroppedItem& item, glm::vec3& newPos, World& world) {
    // Simple AABB collision - item is ~0.25 units
    const float ITEM_SIZE = 0.25f;
    const float ITEM_HEIGHT = 0.25f;

    // Check Y collision (ground)
    int blockY = static_cast<int>(std::floor(newPos.y - ITEM_HEIGHT));
    int blockX = static_cast<int>(std::floor(item.position.x));
    int blockZ = static_cast<int>(std::floor(item.position.z));

    BlockType belowBlock = world.getBlock(blockX, blockY, blockZ);
    bool solidBelow = isBlockSolid(belowBlock);

    if (item.velocity.y < 0 && solidBelow) {
        // Land on block
        newPos.y = static_cast<float>(blockY + 1) + ITEM_HEIGHT + 0.01f;
        item.velocity.y = 0.0f;
        item.onGround = true;
    } else {
        item.onGround = false;
    }

    // Check X collision
    int newBlockX = static_cast<int>(std::floor(newPos.x));
    if (newBlockX != blockX) {
        BlockType sideBlock = world.getBlock(newBlockX, static_cast<int>(item.position.y), blockZ);
        if (isBlockSolid(sideBlock)) {
            newPos.x = item.position.x;
            item.velocity.x = -item.velocity.x * 0.3f;
        }
    }

    // Check Z collision
    int newBlockZ = static_cast<int>(std::floor(newPos.z));
    if (newBlockZ != blockZ) {
        BlockType sideBlock = world.getBlock(blockX, static_cast<int>(item.position.y), newBlockZ);
        if (isBlockSolid(sideBlock)) {
            newPos.z = item.position.z;
            item.velocity.z = -item.velocity.z * 0.3f;
        }
    }

    // Update position
    item.position = newPos;

    // Keep above bedrock
    if (item.position.y < 1.0f) {
        item.position.y = 1.0f;
        item.velocity.y = 0.0f;
        item.onGround = true;
    }
}

// ==================== DROPPED ITEM RENDERER ====================
class DroppedItemRenderer {
public:
    GLuint blockAtlas = 0;
    GLuint itemAtlas = 0;
    GLuint shaderProgram = 0;
    GLuint vao = 0;
    GLuint vbo = 0;

    // Shader uniform locations
    GLint mvpLoc = -1;
    GLint texLoc = -1;

    bool initialized = false;

    void init(GLuint blockTex, GLuint itemTex) {
        blockAtlas = blockTex;
        itemAtlas = itemTex;

        // Create simple billboard shader
        const char* vertSrc = R"(
            #version 330 core
            layout(location = 0) in vec3 aPos;
            layout(location = 1) in vec2 aTexCoord;

            uniform mat4 mvp;

            out vec2 TexCoord;

            void main() {
                gl_Position = mvp * vec4(aPos, 1.0);
                TexCoord = aTexCoord;
            }
        )";

        const char* fragSrc = R"(
            #version 330 core
            in vec2 TexCoord;
            out vec4 FragColor;

            uniform sampler2D tex;

            void main() {
                vec4 color = texture(tex, TexCoord);
                if (color.a < 0.1) discard;
                FragColor = color;
            }
        )";

        // Compile shader
        GLuint vert = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vert, 1, &vertSrc, nullptr);
        glCompileShader(vert);

        GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(frag, 1, &fragSrc, nullptr);
        glCompileShader(frag);

        shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vert);
        glAttachShader(shaderProgram, frag);
        glLinkProgram(shaderProgram);

        glDeleteShader(vert);
        glDeleteShader(frag);

        mvpLoc = glGetUniformLocation(shaderProgram, "mvp");
        texLoc = glGetUniformLocation(shaderProgram, "tex");

        // Create quad VAO/VBO
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 5 * 6, nullptr, GL_DYNAMIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

        glBindVertexArray(0);

        initialized = true;
    }

    void cleanup() {
        if (vao) glDeleteVertexArrays(1, &vao);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (shaderProgram) glDeleteProgram(shaderProgram);
        initialized = false;
    }

    void render(const DroppedItemManager& manager, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPos) {
        if (!initialized || manager.items.empty()) return;

        glUseProgram(shaderProgram);
        glBindVertexArray(vao);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);

        for (const auto& item : manager.items) {
            if (item.markedForRemoval) continue;

            renderItem(item, manager.getRenderPosition(item), view, projection, cameraPos);
        }

        glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glBindVertexArray(0);
    }

private:
    void renderItem(const DroppedItem& item, const glm::vec3& pos,
                    const glm::mat4& view, const glm::mat4& projection,
                    const glm::vec3& cameraPos) {
        const float SIZE = 0.35f;  // Item display size

        // Get texture and UV
        GLuint texture;
        glm::vec4 uv;

        if (item.stack.isBlock()) {
            texture = blockAtlas;
            BlockTextures tex = getBlockTextures(item.stack.blockType);
            uv = TextureAtlas::getUV(tex.faceSlots[4]);  // Top face
        } else {
            texture = itemAtlas;
            int slot = ItemAtlas::getTextureSlot(item.stack.itemType);
            uv = ItemAtlas::getUV(slot);
        }

        // Billboard facing camera (Y-axis rotation only for items)
        glm::vec3 toCamera = cameraPos - pos;
        toCamera.y = 0;
        float angle = std::atan2(toCamera.x, toCamera.z);

        // Add item's spin rotation
        angle += item.rotation;

        // Create model matrix
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, pos);
        model = glm::rotate(model, angle, glm::vec3(0, 1, 0));
        model = glm::scale(model, glm::vec3(SIZE));

        glm::mat4 mvp = projection * view * model;

        // Build quad vertices (pos.xyz, tex.uv)
        float vertices[] = {
            -0.5f, 0.0f, 0.0f,  uv.x, uv.w,
             0.5f, 0.0f, 0.0f,  uv.z, uv.w,
             0.5f, 1.0f, 0.0f,  uv.z, uv.y,
            -0.5f, 0.0f, 0.0f,  uv.x, uv.w,
             0.5f, 1.0f, 0.0f,  uv.z, uv.y,
            -0.5f, 1.0f, 0.0f,  uv.x, uv.y
        };

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, &mvp[0][0]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(texLoc, 0);

        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
};
