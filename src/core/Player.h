#pragma once

#include "Camera.h"
#include "../world/World.h"
#include <glm/glm.hpp>
#include <algorithm>

class Player {
public:
    // Position and movement
    glm::vec3 position;
    glm::vec3 velocity{0.0f};

    // Player dimensions (hitbox)
    static constexpr float WIDTH = 0.6f;      // Player width/depth
    static constexpr float HEIGHT = 1.8f;     // Player height
    static constexpr float EYE_HEIGHT = 1.62f; // Eye level from feet

    // Physics constants
    static constexpr float GRAVITY = 28.0f;
    static constexpr float JUMP_VELOCITY = 9.0f;
    static constexpr float TERMINAL_VELOCITY = 50.0f;
    static constexpr float GROUND_FRICTION = 12.0f;
    static constexpr float AIR_FRICTION = 2.0f;

    // Movement speeds
    static constexpr float WALK_SPEED = 4.3f;
    static constexpr float SPRINT_SPEED = 5.6f;
    static constexpr float FLY_SPEED = 10.0f;
    static constexpr float SWIM_SPEED = 2.0f;
    static constexpr float SWIM_SPRINT_SPEED = 3.0f;

    // Water physics
    static constexpr float WATER_GRAVITY = 4.0f;      // Reduced gravity in water
    static constexpr float WATER_BUOYANCY = 6.0f;     // Upward force in water
    static constexpr float WATER_FRICTION = 8.0f;     // Drag in water
    static constexpr float SWIM_UP_SPEED = 3.5f;      // Speed when swimming up

    // State
    bool onGround = false;
    bool isFlying = false;
    bool isNoclip = false;       // Noclip mode - fly through blocks
    bool isSprinting = false;
    bool isInWater = false;      // Feet in water
    bool isUnderwater = false;   // Head underwater

    // ===== SURVIVAL MECHANICS =====
    // Survival constants
    static constexpr int MAX_HEALTH = 20;           // 10 hearts (2 HP each)
    static constexpr int MAX_HUNGER = 20;           // 10 drumsticks
    static constexpr int MAX_AIR = 300;             // 15 seconds underwater (20 ticks/sec)
    static constexpr int FALL_DAMAGE_THRESHOLD = 3; // Blocks before taking damage
    static constexpr int REGEN_HUNGER_THRESHOLD = 18; // Need 9+ drumsticks to heal
    static constexpr int DROWN_DAMAGE = 2;          // Per second when out of air
    static constexpr int LAVA_DAMAGE = 4;           // Per 0.5 seconds
    static constexpr int STARVATION_DAMAGE = 1;     // Per 4 seconds at 0 hunger
    static constexpr float DAMAGE_IMMUNITY_TIME = 0.5f;  // Seconds after taking damage

    // Survival stats
    int health = MAX_HEALTH;
    int hunger = MAX_HUNGER;
    int air = MAX_AIR;
    float saturation = 5.0f;     // Hidden hunger buffer

    // Fall tracking
    float fallStartY = 0.0f;
    bool wasFalling = false;

    // Hazard state
    bool isInLava = false;

    // Timers
    float regenTimer = 0.0f;
    float starvationTimer = 0.0f;
    float drownTimer = 0.0f;
    float hungerDecayTimer = 0.0f;
    float damageImmunityTimer = 0.0f;
    float lavaTimer = 0.0f;

    // Death state
    bool isDead = false;
    float deathTimer = 0.0f;
    glm::vec3 spawnPoint = glm::vec3(0, 80, 0);

    // Eating state
    bool isEating = false;
    float eatingTimer = 0.0f;
    static constexpr float EATING_DURATION = 1.6f;  // 32 ticks in MC
    int eatingFoodHunger = 0;      // Hunger to restore when finished
    float eatingFoodSaturation = 0.0f;  // Saturation to restore

    // Reference to camera for view direction
    Camera* camera = nullptr;

    Player(const glm::vec3& startPos) : position(startPos) {}

    void attachCamera(Camera* cam) {
        camera = cam;
        updateCameraPosition();
    }

    void updateCameraPosition() {
        if (camera) {
            camera->position = position + glm::vec3(0.0f, EYE_HEIGHT, 0.0f);
        }
    }

    // Process movement input and return desired movement vector
    glm::vec3 getMovementInput(bool forward, bool backward, bool left, bool right) {
        if (!camera) return glm::vec3(0.0f);

        glm::vec3 moveDir(0.0f);

        // Get horizontal forward/right vectors (ignore pitch)
        glm::vec3 flatFront = glm::normalize(glm::vec3(camera->front.x, 0.0f, camera->front.z));
        glm::vec3 flatRight = glm::normalize(glm::cross(flatFront, glm::vec3(0.0f, 1.0f, 0.0f)));

        if (forward) moveDir += flatFront;
        if (backward) moveDir -= flatFront;
        if (right) moveDir += flatRight;
        if (left) moveDir -= flatRight;

        if (glm::length(moveDir) > 0.0f) {
            moveDir = glm::normalize(moveDir);
        }

        return moveDir;
    }

    void update(float deltaTime, World& world,
                bool forward, bool backward, bool left, bool right,
                bool jump, bool descend, bool sprint) {

        isSprinting = sprint && forward && !backward;

        // Check water status
        checkWaterStatus(world);

        if (isFlying) {
            updateFlying(deltaTime, world, forward, backward, left, right, jump, descend);
        } else if (isInWater) {
            updateSwimming(deltaTime, world, forward, backward, left, right, jump, descend);
        } else {
            updateWalking(deltaTime, world, forward, backward, left, right, jump);
        }

        updateCameraPosition();
    }

    // Check if player is in water
    void checkWaterStatus(World& world) {
        // Check at feet level
        int feetX = static_cast<int>(std::floor(position.x));
        int feetY = static_cast<int>(std::floor(position.y + 0.1f));
        int feetZ = static_cast<int>(std::floor(position.z));

        isInWater = world.getBlock(feetX, feetY, feetZ) == BlockType::WATER;

        // Check at eye level
        int eyeY = static_cast<int>(std::floor(position.y + EYE_HEIGHT));
        isUnderwater = world.getBlock(feetX, eyeY, feetZ) == BlockType::WATER;
    }

    void toggleFlying() {
        isFlying = !isFlying;
        if (isFlying) {
            velocity = glm::vec3(0.0f);
        }
    }

    void toggleNoclip() {
        isNoclip = !isNoclip;
        if (isNoclip) {
            isFlying = true;  // Noclip requires flying
            velocity = glm::vec3(0.0f);
        }
    }

    // ===== SURVIVAL METHODS =====

    // Take damage with optional armor reduction
    // armorReduction: 0.0 to 0.8 (from Inventory::getDamageReduction())
    // Returns actual damage taken (for damaging armor)
    int takeDamage(int amount, float armorReduction = 0.0f) {
        if (isDead || damageImmunityTimer > 0.0f) return 0;

        // Apply armor reduction
        int reducedDamage = static_cast<int>(std::ceil(amount * (1.0f - armorReduction)));
        reducedDamage = std::max(1, reducedDamage);  // Always take at least 1 damage

        health -= reducedDamage;
        damageImmunityTimer = DAMAGE_IMMUNITY_TIME;

        if (health <= 0) {
            health = 0;
            isDead = true;
            deathTimer = 0.0f;
        }

        return reducedDamage;  // Caller can use this to damage armor
    }

    void heal(int amount) {
        if (isDead) return;
        health = std::min(health + amount, MAX_HEALTH);
    }

    // ===== EATING METHODS =====

    // Start eating food (called when right-click held with food item)
    // Returns true if eating can start
    bool startEating(int foodHunger, float foodSaturation) {
        if (isDead || isEating) return false;
        // Can only eat if not full (Minecraft behavior)
        if (hunger >= MAX_HUNGER) return false;

        isEating = true;
        eatingTimer = 0.0f;
        eatingFoodHunger = foodHunger;
        eatingFoodSaturation = foodSaturation;
        return true;
    }

    // Update eating progress (call every frame while right mouse held)
    // Returns true if finished eating
    bool updateEating(float deltaTime) {
        if (!isEating) return false;

        eatingTimer += deltaTime;
        if (eatingTimer >= EATING_DURATION) {
            // Finished eating - consume the food
            eat(eatingFoodHunger, eatingFoodSaturation);
            isEating = false;
            eatingTimer = 0.0f;
            return true;
        }
        return false;
    }

    // Cancel eating (called when right-click released early)
    void cancelEating() {
        isEating = false;
        eatingTimer = 0.0f;
        eatingFoodHunger = 0;
        eatingFoodSaturation = 0.0f;
    }

    // Get eating progress (0.0 to 1.0) for UI
    float getEatingProgress() const {
        if (!isEating) return 0.0f;
        return std::min(eatingTimer / EATING_DURATION, 1.0f);
    }

    // Actually consume the food (restore hunger/saturation)
    void eat(int foodHunger, float foodSaturation) {
        hunger = std::min(hunger + foodHunger, MAX_HUNGER);
        // Saturation can't exceed hunger level
        saturation = std::min(saturation + foodSaturation, static_cast<float>(hunger));
    }

    void respawn() {
        health = MAX_HEALTH;
        hunger = MAX_HUNGER;
        air = MAX_AIR;
        saturation = 5.0f;
        isDead = false;
        deathTimer = 0.0f;
        damageImmunityTimer = 0.0f;
        regenTimer = 0.0f;
        starvationTimer = 0.0f;
        drownTimer = 0.0f;
        hungerDecayTimer = 0.0f;
        lavaTimer = 0.0f;
        wasFalling = false;
        isInLava = false;
        isEating = false;
        eatingTimer = 0.0f;
        velocity = glm::vec3(0.0f);
        position = spawnPoint;
        updateCameraPosition();
    }

    void checkLavaStatus(World& world) {
        // Check at feet level for lava
        int feetX = static_cast<int>(std::floor(position.x));
        int feetY = static_cast<int>(std::floor(position.y + 0.1f));
        int feetZ = static_cast<int>(std::floor(position.z));

        isInLava = world.getBlock(feetX, feetY, feetZ) == BlockType::LAVA;
    }

    // armorReduction: 0.0 to 0.8, from Inventory::getDamageReduction()
    // Returns total damage taken (for damaging armor)
    int updateSurvival(float deltaTime, World& world, float armorReduction = 0.0f) {
        int totalDamageTaken = 0;

        // Skip survival in creative-like modes
        if (isFlying || isNoclip || isDead) return 0;

        // Update immunity timer
        if (damageImmunityTimer > 0.0f) {
            damageImmunityTimer -= deltaTime;
        }

        // Check hazards
        checkLavaStatus(world);

        // ===== FALL DAMAGE =====
        bool isFalling = velocity.y < -0.1f && !onGround && !isInWater;

        if (isFalling && !wasFalling) {
            // Started falling - record start height
            fallStartY = position.y;
        }

        if (wasFalling && (onGround || isInWater)) {
            // Landed - calculate fall damage
            if (!isInWater) { // Water cancels fall damage
                float fallDistance = fallStartY - position.y;
                int damage = static_cast<int>(std::floor(fallDistance)) - FALL_DAMAGE_THRESHOLD;
                if (damage > 0) {
                    totalDamageTaken += takeDamage(damage, armorReduction);
                }
            }
        }
        wasFalling = isFalling;

        // ===== DROWNING =====
        if (isUnderwater) {
            drownTimer += deltaTime;
            if (drownTimer >= 0.05f) { // 20 ticks per second
                drownTimer -= 0.05f;
                air--;
                if (air <= 0) {
                    air = 0;
                    // Deal drowning damage every second
                    static float drownDamageTimer = 0.0f;
                    drownDamageTimer += 0.05f;
                    if (drownDamageTimer >= 1.0f) {
                        drownDamageTimer -= 1.0f;
                        totalDamageTaken += takeDamage(DROWN_DAMAGE, armorReduction);
                    }
                }
            }
        } else {
            // Surfaced - restore air quickly
            drownTimer += deltaTime;
            if (drownTimer >= 0.0166f) { // ~60 per second
                drownTimer -= 0.0166f;
                air = std::min(air + 1, MAX_AIR);
            }
        }

        // ===== LAVA DAMAGE =====
        if (isInLava) {
            lavaTimer += deltaTime;
            if (lavaTimer >= 0.5f) {
                lavaTimer -= 0.5f;
                totalDamageTaken += takeDamage(LAVA_DAMAGE, armorReduction);
            }
        } else {
            lavaTimer = 0.0f;
        }

        // ===== HUNGER DECAY =====
        hungerDecayTimer += deltaTime;
        float decayRate = 80.0f; // Base: lose 1 hunger every 80 seconds
        if (isSprinting) decayRate *= 0.5f; // Sprint: 2x decay
        if (isInWater) decayRate *= 0.67f;  // Swim: 1.5x decay

        if (hungerDecayTimer >= decayRate) {
            hungerDecayTimer -= decayRate;
            if (saturation > 0.0f) {
                saturation -= 1.0f;
                if (saturation < 0.0f) saturation = 0.0f;
            } else if (hunger > 0) {
                hunger--;
            }
        }

        // ===== NATURAL REGENERATION =====
        if (hunger >= REGEN_HUNGER_THRESHOLD && health < MAX_HEALTH) {
            regenTimer += deltaTime;
            if (regenTimer >= 0.5f) { // Heal every 0.5 seconds
                regenTimer -= 0.5f;
                heal(1);
                // Costs saturation/hunger
                if (saturation >= 1.5f) {
                    saturation -= 1.5f;
                } else {
                    saturation = 0.0f;
                    hunger = std::max(0, hunger - 1);
                }
            }
        } else {
            regenTimer = 0.0f;
        }

        // ===== STARVATION =====
        if (hunger == 0 && health > 1) {
            starvationTimer += deltaTime;
            if (starvationTimer >= 4.0f) { // Damage every 4 seconds
                starvationTimer -= 4.0f;
                // Don't kill player with starvation (stop at 1 HP)
                if (health > 1) {
                    health--;
                }
            }
        } else {
            starvationTimer = 0.0f;
        }

        return totalDamageTaken;
    }

private:
    void updateFlying(float deltaTime, World& world,
                      bool forward, bool backward, bool left, bool right,
                      bool up, bool down) {

        glm::vec3 moveDir = getMovementInput(forward, backward, left, right);

        // Vertical movement in fly mode
        if (up) moveDir.y += 1.0f;
        if (down) moveDir.y -= 1.0f;

        if (glm::length(moveDir) > 0.0f) {
            moveDir = glm::normalize(moveDir);
        }

        float speed = isSprinting ? FLY_SPEED * 2.0f : FLY_SPEED;
        glm::vec3 targetVelocity = moveDir * speed;

        // Smooth acceleration
        float accel = 15.0f * deltaTime;
        velocity.x = glm::mix(velocity.x, targetVelocity.x, accel);
        velocity.y = glm::mix(velocity.y, targetVelocity.y, accel);
        velocity.z = glm::mix(velocity.z, targetVelocity.z, accel);

        if (isNoclip) {
            // Noclip mode - move freely without collision
            position += velocity * deltaTime;
        } else {
            // Normal fly mode with collision
            moveWithCollision(deltaTime, world);
        }
    }

    void updateSwimming(float deltaTime, World& world,
                        bool forward, bool backward, bool left, bool right,
                        bool swimUp, bool /*swimDown*/) {

        // Get movement direction (horizontal only, like Minecraft)
        glm::vec3 moveDir(0.0f);

        if (camera) {
            // Horizontal movement only - use flat vectors
            glm::vec3 flatFront = glm::normalize(glm::vec3(camera->front.x, 0.0f, camera->front.z));
            glm::vec3 flatRight = glm::normalize(glm::cross(flatFront, glm::vec3(0.0f, 1.0f, 0.0f)));

            if (forward) moveDir += flatFront;
            if (backward) moveDir -= flatFront;
            if (right) moveDir += flatRight;
            if (left) moveDir -= flatRight;
        }

        if (glm::length(moveDir) > 0.0f) {
            moveDir = glm::normalize(moveDir);
        }

        // Calculate target horizontal velocity
        float speed = isSprinting ? SWIM_SPRINT_SPEED : SWIM_SPEED;
        glm::vec3 targetVelocity = moveDir * speed;

        // Apply water friction (high drag)
        float t = 1.0f - std::exp(-WATER_FRICTION * deltaTime);
        velocity.x = glm::mix(velocity.x, targetVelocity.x, t);
        velocity.z = glm::mix(velocity.z, targetVelocity.z, t);

        // Vertical physics - player sinks unless pressing space
        if (swimUp) {
            // Swimming upward - only way to rise
            velocity.y = glm::mix(velocity.y, SWIM_UP_SPEED, t);
        } else {
            // Sink in water - apply water gravity (slower than air)
            velocity.y -= WATER_GRAVITY * deltaTime;

            // Apply water drag to slow the sinking
            velocity.y = glm::mix(velocity.y, -2.0f, t * 0.3f);  // Terminal sink speed ~2 m/s
        }

        // Clamp vertical velocity in water
        velocity.y = std::clamp(velocity.y, -3.0f, SWIM_UP_SPEED);

        // Move with collision
        moveWithCollision(deltaTime, world);

        // Reset onGround since we're swimming
        onGround = false;
    }

    void updateWalking(float deltaTime, World& world,
                       bool forward, bool backward, bool left, bool right,
                       bool jump) {

        // Horizontal movement
        glm::vec3 moveDir = getMovementInput(forward, backward, left, right);
        float speed = isSprinting ? SPRINT_SPEED : WALK_SPEED;

        glm::vec3 targetHorizontalVel = moveDir * speed;

        // Apply friction/acceleration
        float friction = onGround ? GROUND_FRICTION : AIR_FRICTION;
        float t = 1.0f - std::exp(-friction * deltaTime);

        velocity.x = glm::mix(velocity.x, targetHorizontalVel.x, t);
        velocity.z = glm::mix(velocity.z, targetHorizontalVel.z, t);

        // Jumping
        if (jump && onGround) {
            velocity.y = JUMP_VELOCITY;
            onGround = false;
        }

        // Apply gravity
        velocity.y -= GRAVITY * deltaTime;
        velocity.y = std::max(velocity.y, -TERMINAL_VELOCITY);

        // Move with collision
        moveWithCollision(deltaTime, world);
    }

    void moveWithCollision(float deltaTime, World& world) {
        // Split movement into smaller steps for better collision
        float remainingTime = deltaTime;
        int maxSteps = 4;

        while (remainingTime > 0.0001f && maxSteps > 0) {
            float stepTime = std::min(remainingTime, 0.02f);
            glm::vec3 displacement = velocity * stepTime;

            // Move on each axis separately for sliding collision
            // Y axis first (gravity)
            if (std::abs(displacement.y) > 0.0001f) {
                float newY = position.y + displacement.y;
                if (!checkCollision(world, position.x, newY, position.z)) {
                    position.y = newY;
                } else {
                    // Hit floor or ceiling
                    if (velocity.y < 0) {
                        // Hit floor - find exact ground position
                        position.y = std::floor(position.y) + 0.001f;
                        onGround = true;
                    }
                    velocity.y = 0;
                }
            }

            // X axis
            if (std::abs(displacement.x) > 0.0001f) {
                float newX = position.x + displacement.x;
                if (!checkCollision(world, newX, position.y, position.z)) {
                    position.x = newX;
                } else {
                    velocity.x = 0;
                }
            }

            // Z axis
            if (std::abs(displacement.z) > 0.0001f) {
                float newZ = position.z + displacement.z;
                if (!checkCollision(world, position.x, position.y, newZ)) {
                    position.z = newZ;
                } else {
                    velocity.z = 0;
                }
            }

            remainingTime -= stepTime;
            maxSteps--;
        }

        // Check if still on ground (for next frame)
        if (!isFlying && velocity.y <= 0) {
            onGround = checkCollision(world, position.x, position.y - 0.05f, position.z);
        }
    }

    // Check if player AABB collides with any solid blocks
    bool checkCollision(World& world, float x, float y, float z) {
        // Player AABB bounds
        float halfWidth = WIDTH / 2.0f;
        float minX = x - halfWidth;
        float maxX = x + halfWidth;
        float minY = y;
        float maxY = y + HEIGHT;
        float minZ = z - halfWidth;
        float maxZ = z + halfWidth;

        // Check all blocks the player could intersect
        int blockMinX = static_cast<int>(std::floor(minX));
        int blockMaxX = static_cast<int>(std::floor(maxX));
        int blockMinY = static_cast<int>(std::floor(minY));
        int blockMaxY = static_cast<int>(std::floor(maxY));
        int blockMinZ = static_cast<int>(std::floor(minZ));
        int blockMaxZ = static_cast<int>(std::floor(maxZ));

        for (int by = blockMinY; by <= blockMaxY; by++) {
            for (int bz = blockMinZ; bz <= blockMaxZ; bz++) {
                for (int bx = blockMinX; bx <= blockMaxX; bx++) {
                    BlockType block = world.getBlock(bx, by, bz);
                    if (isBlockSolid(block)) {
                        // AABB vs AABB intersection
                        if (minX < bx + 1 && maxX > bx &&
                            minY < by + 1 && maxY > by &&
                            minZ < bz + 1 && maxZ > bz) {
                            return true;
                        }
                    }
                }
            }
        }

        return false;
    }
};
