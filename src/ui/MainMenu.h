#pragma once

// Main Menu Screen
// Displays Play Game, Multiplayer, Settings, Exit buttons

#include "MenuUI.h"
#include <functional>

// Menu action types
enum class MenuAction {
    NONE,
    PLAY_GAME,
    MULTIPLAYER,
    SETTINGS,
    EXIT
};

class MainMenu {
public:
    MenuUIRenderer* ui = nullptr;
    MenuInputHandler input;

    // Buttons
    MenuButton playButton;
    MenuButton multiplayerButton;
    MenuButton settingsButton;
    MenuButton exitButton;

    // Current action (set when button is clicked)
    MenuAction currentAction = MenuAction::NONE;

    // Version string
    std::string version = "Infdev";

    void init(MenuUIRenderer* uiRenderer) {
        ui = uiRenderer;
        setupButtons();
    }

    void setupButtons() {
        float centerX = ui->windowWidth / 2.0f;
        float btnWidth = 300.0f;
        float btnHeight = 55.0f;
        float btnSpacing = 15.0f;
        float startY = ui->windowHeight / 2.0f - 60.0f;

        // Play Game button
        playButton = {
            centerX - btnWidth / 2, startY,
            btnWidth, btnHeight,
            "PLAY GAME",
            [this]() { currentAction = MenuAction::PLAY_GAME; }
        };
        playButton.textScale = 1.5f;

        // Multiplayer button (disabled)
        multiplayerButton = {
            centerX - btnWidth / 2, startY + btnHeight + btnSpacing,
            btnWidth, btnHeight,
            "MULTIPLAYER",
            [this]() { currentAction = MenuAction::MULTIPLAYER; }
        };
        multiplayerButton.textScale = 1.5f;
        multiplayerButton.enabled = false;  // Greyed out

        // Settings button
        settingsButton = {
            centerX - btnWidth / 2, startY + 2 * (btnHeight + btnSpacing),
            btnWidth, btnHeight,
            "SETTINGS",
            [this]() { currentAction = MenuAction::SETTINGS; }
        };
        settingsButton.textScale = 1.5f;

        // Exit button
        exitButton = {
            centerX - btnWidth / 2, startY + 3 * (btnHeight + btnSpacing),
            btnWidth, btnHeight,
            "EXIT",
            [this]() { currentAction = MenuAction::EXIT; }
        };
        exitButton.textScale = 1.5f;
    }

    void resize(int width, int height) {
        if (ui) {
            ui->resize(width, height);
            setupButtons();
        }
    }

    void update(double mouseX, double mouseY, bool mousePressed) {
        currentAction = MenuAction::NONE;

        input.update(mouseX, mouseY, mousePressed);
        input.handleButton(playButton);
        input.handleButton(multiplayerButton);
        input.handleButton(settingsButton);
        input.handleButton(exitButton);
    }

    void render() {
        if (!ui) return;

        // Title
        float titleY = ui->windowHeight / 2.0f - 200.0f;
        ui->drawTextCentered("VOXEL ENGINE", 0, titleY, static_cast<float>(ui->windowWidth),
                            MenuColors::ACCENT, 3.0f);

        // Subtitle
        ui->drawTextCentered("A Minecraft-like Voxel Game", 0, titleY + 70,
                            static_cast<float>(ui->windowWidth), MenuColors::TEXT_DIM, 1.2f);

        // Buttons
        playButton.render(*ui);
        multiplayerButton.render(*ui);
        settingsButton.render(*ui);
        exitButton.render(*ui);

        // Version
        ui->drawText(version, 20, static_cast<float>(ui->windowHeight) - 40,
                    MenuColors::TEXT_DIM, 1.0f);

        // Credits/Footer
        ui->drawTextCentered("Powered by OpenGL", 0, static_cast<float>(ui->windowHeight) - 40,
                            static_cast<float>(ui->windowWidth), MenuColors::TEXT_DIM, 0.9f);
    }

    MenuAction getAction() const {
        return currentAction;
    }
};
