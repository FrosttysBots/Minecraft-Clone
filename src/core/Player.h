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
