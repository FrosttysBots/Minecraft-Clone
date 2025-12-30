#pragma once

// Survival HUD - Health, Hunger, Air bars and Death screen
// Renders Minecraft-style survival UI elements

#include "MenuUI.h"
#include "../core/Player.h"
#include "../core/Config.h"
#include <glm/glm.hpp>
#include <string>
#include <cmath>

// External config reference
extern GameConfig g_config;

class SurvivalHUD {
public:
    // Base HUD layout constants (scaled by g_config.guiScale)
    static constexpr float BASE_ICON_SIZE = 18.0f;         // Size of each heart/hunger icon
    static constexpr float BASE_ICON_SPACING = 2.0f;       // Space between icons
    static constexpr float BASE_HUD_OFFSET_Y = 72.0f;      // Distance from bottom of screen (above hotbar)
    static constexpr float BASE_BAR_SPACING = 20.0f;       // Space between health and hunger bars
    static constexpr float BASE_AIR_OFFSET_Y = 24.0f;      // Air bar above hunger

    // Computed scaled values
    float getScale() const { return g_config.guiScale; }
    float getIconSize() const { return BASE_ICON_SIZE * getScale(); }
    float getIconSpacing() const { return BASE_ICON_SPACING * getScale(); }
    float getHudOffsetY() const { return BASE_HUD_OFFSET_Y * getScale(); }
    float getBarSpacing() const { return BASE_BAR_SPACING * getScale(); }
    float getAirOffsetY() const { return BASE_AIR_OFFSET_Y * getScale(); }

    // Colors
    const glm::vec4 HEART_FULL = {0.85f, 0.15f, 0.15f, 1.0f};      // Red heart
    const glm::vec4 HEART_EMPTY = {0.25f, 0.08f, 0.08f, 0.8f};     // Dark red empty
    const glm::vec4 HEART_OUTLINE = {0.4f, 0.1f, 0.1f, 1.0f};      // Heart border

    const glm::vec4 HUNGER_FULL = {0.65f, 0.45f, 0.20f, 1.0f};     // Brown drumstick
    const glm::vec4 HUNGER_EMPTY = {0.20f, 0.15f, 0.08f, 0.8f};    // Dark brown empty
    const glm::vec4 HUNGER_OUTLINE = {0.35f, 0.25f, 0.12f, 1.0f};  // Hunger border

    const glm::vec4 AIR_FULL = {0.3f, 0.6f, 0.9f, 1.0f};           // Blue bubble
    const glm::vec4 AIR_EMPTY = {0.1f, 0.2f, 0.3f, 0.5f};          // Dark blue empty
    const glm::vec4 AIR_OUTLINE = {0.2f, 0.4f, 0.6f, 1.0f};        // Bubble border

    const glm::vec4 DEATH_OVERLAY = {0.5f, 0.0f, 0.0f, 0.6f};      // Red death overlay
    const glm::vec4 DEATH_TEXT = {1.0f, 1.0f, 1.0f, 1.0f};         // White death text
    const glm::vec4 RESPAWN_TEXT = {0.8f, 0.8f, 0.8f, 1.0f};       // Gray respawn prompt

    // Damage flash effect
    float damageFlashTimer = 0.0f;
    int lastHealth = 20;

    void render(Player& player, MenuUIRenderer& ui) {
        if (!ui.initialized) return;

        // Check for damage flash
        if (player.health < lastHealth) {
            damageFlashTimer = 0.3f;
        }
        lastHealth = player.health;

        // Skip HUD in flying/noclip modes (creative-like)
        if (player.isFlying || player.isNoclip) {
            return;
        }

        // Death screen takes over everything
        if (player.isDead) {
            renderDeathScreen(player, ui);
            return;
        }

        // Get scaled values
        float iconSize = getIconSize();
        float iconSpacing = getIconSpacing();
        float hudOffsetY = getHudOffsetY();
        float barSpacing = getBarSpacing();
        float airOffsetY = getAirOffsetY();

        // Calculate HUD position (bottom center of screen)
        float screenCenterX = ui.windowWidth / 2.0f;
        float hudY = ui.windowHeight - hudOffsetY;

        // Damage flash overlay
        if (damageFlashTimer > 0.0f) {
            float alpha = damageFlashTimer / 0.3f * 0.3f;
            ui.drawRect(0, 0, static_cast<float>(ui.windowWidth),
                       static_cast<float>(ui.windowHeight),
                       {0.8f, 0.0f, 0.0f, alpha});
        }

        // Health bar (left side of center)
        float healthBarWidth = 10 * (iconSize + iconSpacing) - iconSpacing;
        float healthStartX = screenCenterX - healthBarWidth - barSpacing / 2;
        renderHealthBar(player, ui, healthStartX, hudY, iconSize, iconSpacing);

        // Hunger bar (right side of center)
        float hungerStartX = screenCenterX + barSpacing / 2;
        renderHungerBar(player, ui, hungerStartX, hudY, iconSize, iconSpacing);

        // Air bar (above hunger when underwater)
        if (player.air < Player::MAX_AIR) {
            renderAirBar(player, ui, hungerStartX, hudY - airOffsetY, iconSize, iconSpacing);
        }
    }

    void update(float deltaTime) {
        if (damageFlashTimer > 0.0f) {
            damageFlashTimer -= deltaTime;
        }
    }

private:
    void renderHealthBar(Player& player, MenuUIRenderer& ui, float startX, float y, float iconSize, float iconSpacing) {
        int hearts = player.health / 2;      // Full hearts
        bool halfHeart = (player.health % 2) == 1;

        for (int i = 0; i < 10; i++) {
            float x = startX + i * (iconSize + iconSpacing);

            // Draw background (empty heart)
            drawHeart(ui, x, y, iconSize, HEART_EMPTY);

            // Draw filled heart
            if (i < hearts) {
                drawHeart(ui, x, y, iconSize, HEART_FULL);
            } else if (i == hearts && halfHeart) {
                drawHalfHeart(ui, x, y, iconSize, HEART_FULL);
            }

            // Draw outline
            drawHeartOutline(ui, x, y, iconSize, HEART_OUTLINE);
        }
    }

    void renderHungerBar(Player& player, MenuUIRenderer& ui, float startX, float y, float iconSize, float iconSpacing) {
        int drumsticks = player.hunger / 2;
        bool halfDrumstick = (player.hunger % 2) == 1;

        for (int i = 0; i < 10; i++) {
            float x = startX + i * (iconSize + iconSpacing);

            // Draw background (empty)
            drawDrumstick(ui, x, y, iconSize, HUNGER_EMPTY);

            // Draw filled drumstick
            if (i < drumsticks) {
                drawDrumstick(ui, x, y, iconSize, HUNGER_FULL);
            } else if (i == drumsticks && halfDrumstick) {
                drawHalfDrumstick(ui, x, y, iconSize, HUNGER_FULL);
            }

            // Draw outline
            drawDrumstickOutline(ui, x, y, iconSize, HUNGER_OUTLINE);
        }
    }

    void renderAirBar(Player& player, MenuUIRenderer& ui, float startX, float y, float iconSize, float iconSpacing) {
        // Air goes from 0-300 (15 seconds), show as 10 bubbles
        int bubbles = (player.air * 10) / Player::MAX_AIR;

        for (int i = 0; i < 10; i++) {
            float x = startX + i * (iconSize + iconSpacing);

            // Draw background (empty bubble)
            drawBubble(ui, x, y, iconSize, AIR_EMPTY);

            // Draw filled bubble
            if (i < bubbles) {
                drawBubble(ui, x, y, iconSize, AIR_FULL);
            }

            // Draw outline
            drawBubbleOutline(ui, x, y, iconSize, AIR_OUTLINE);
        }
    }

    void renderDeathScreen(Player& player, MenuUIRenderer& ui) {
        float scale = getScale();

        // Full screen red overlay
        ui.drawRect(0, 0, static_cast<float>(ui.windowWidth),
                   static_cast<float>(ui.windowHeight), DEATH_OVERLAY);

        // "YOU DIED!" text centered
        float centerY = ui.windowHeight / 2.0f;
        ui.drawTextCentered("YOU DIED!", 0, centerY - 50 * scale,
                           static_cast<float>(ui.windowWidth), DEATH_TEXT, 3.0f * scale);

        // Respawn prompt (after 2 seconds)
        if (player.deathTimer >= 2.0f) {
            ui.drawTextCentered("Press SPACE to respawn", 0, centerY + 30 * scale,
                               static_cast<float>(ui.windowWidth), RESPAWN_TEXT, 1.5f * scale);
        } else {
            // Show countdown
            int remaining = static_cast<int>(std::ceil(2.0f - player.deathTimer));
            std::string countdownText = "Respawn in " + std::to_string(remaining) + "...";
            ui.drawTextCentered(countdownText, 0, centerY + 30 * scale,
                               static_cast<float>(ui.windowWidth),
                               {0.6f, 0.6f, 0.6f, 1.0f}, 1.2f * scale);
        }
    }

    // Heart shape (simplified square with notch)
    void drawHeart(MenuUIRenderer& ui, float x, float y, float s, const glm::vec4& color) {
        // Main body
        ui.drawRect(x + s * 0.15f, y + s * 0.3f, s * 0.7f, s * 0.55f, color);
        // Top left lobe
        ui.drawRect(x, y, s * 0.45f, s * 0.45f, color);
        // Top right lobe
        ui.drawRect(x + s * 0.55f, y, s * 0.45f, s * 0.45f, color);
    }

    void drawHalfHeart(MenuUIRenderer& ui, float x, float y, float s, const glm::vec4& color) {
        // Left half only
        ui.drawRect(x + s * 0.15f, y + s * 0.3f, s * 0.35f, s * 0.55f, color);
        ui.drawRect(x, y, s * 0.45f, s * 0.45f, color);
    }

    void drawHeartOutline(MenuUIRenderer& ui, float x, float y, float s, const glm::vec4& color) {
        float scale = getScale();
        ui.drawRectOutline(x, y, s, s * 0.85f, color, 1.0f * scale);
    }

    // Drumstick shape (simplified rectangle)
    void drawDrumstick(MenuUIRenderer& ui, float x, float y, float s, const glm::vec4& color) {
        // Main meat part
        ui.drawRect(x + s * 0.1f, y + s * 0.15f, s * 0.6f, s * 0.5f, color);
        // Bone part
        ui.drawRect(x + s * 0.5f, y + s * 0.35f, s * 0.4f, s * 0.2f,
                   {color.r * 1.2f, color.g * 1.2f, color.b * 1.2f, color.a});
    }

    void drawHalfDrumstick(MenuUIRenderer& ui, float x, float y, float s, const glm::vec4& color) {
        ui.drawRect(x + s * 0.1f, y + s * 0.15f, s * 0.3f, s * 0.5f, color);
    }

    void drawDrumstickOutline(MenuUIRenderer& ui, float x, float y, float s, const glm::vec4& color) {
        float scale = getScale();
        ui.drawRectOutline(x, y, s, s * 0.85f, color, 1.0f * scale);
    }

    // Bubble shape (circle approximated with square)
    void drawBubble(MenuUIRenderer& ui, float x, float y, float s, const glm::vec4& color) {
        float padding = s * 0.15f;
        ui.drawRect(x + padding, y + padding, s - padding * 2, s - padding * 2, color);
    }

    void drawBubbleOutline(MenuUIRenderer& ui, float x, float y, float s, const glm::vec4& color) {
        float scale = getScale();
        float padding = s * 0.1f;
        ui.drawRectOutline(x + padding, y + padding, s - padding * 2, s - padding * 2, color, 1.0f * scale);
    }
};
