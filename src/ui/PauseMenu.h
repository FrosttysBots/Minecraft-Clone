#pragma once

// Pause Menu (Escape Menu)
// Displays when player presses ESC during gameplay

#include "MenuUI.h"
#include <functional>

enum class PauseAction {
    NONE,
    RESUME,
    SETTINGS,
    SAVE_GAME,
    QUIT_TO_MENU
};

class PauseMenu {
public:
    MenuUIRenderer* ui = nullptr;
    MenuInputHandler input;

    // Buttons
    MenuButton resumeButton;
    MenuButton settingsButton;
    MenuButton saveButton;
    MenuButton quitButton;

    // Current action
    PauseAction currentAction = PauseAction::NONE;

    // Save feedback
    std::string saveMessage = "";
    float saveMessageTimer = 0.0f;

    void init(MenuUIRenderer* uiRenderer) {
        ui = uiRenderer;
        setupButtons();
    }

    void setupButtons() {
        float centerX = ui->windowWidth / 2.0f;
        float panelWidth = 350.0f;
        float panelHeight = 320.0f;
        float panelX = centerX - panelWidth / 2.0f;
        float panelY = ui->windowHeight / 2.0f - panelHeight / 2.0f;

        float btnWidth = 280.0f;
        float btnHeight = 50.0f;
        float btnSpacing = 12.0f;
        float btnX = centerX - btnWidth / 2.0f;
        float startY = panelY + 60.0f;

        // Resume button
        resumeButton = {
            btnX, startY,
            btnWidth, btnHeight,
            "RESUME",
            [this]() { currentAction = PauseAction::RESUME; }
        };
        resumeButton.textScale = 1.3f;

        // Settings button
        settingsButton = {
            btnX, startY + btnHeight + btnSpacing,
            btnWidth, btnHeight,
            "SETTINGS",
            [this]() { currentAction = PauseAction::SETTINGS; }
        };
        settingsButton.textScale = 1.3f;

        // Save button
        saveButton = {
            btnX, startY + 2 * (btnHeight + btnSpacing),
            btnWidth, btnHeight,
            "SAVE GAME",
            [this]() { currentAction = PauseAction::SAVE_GAME; }
        };
        saveButton.textScale = 1.3f;

        // Quit to menu button
        quitButton = {
            btnX, startY + 3 * (btnHeight + btnSpacing),
            btnWidth, btnHeight,
            "QUIT TO MENU",
            [this]() { currentAction = PauseAction::QUIT_TO_MENU; }
        };
        quitButton.textScale = 1.3f;
    }

    void resize(int width, int height) {
        if (ui) {
            ui->resize(width, height);
            setupButtons();
        }
    }

    void update(double mouseX, double mouseY, bool mousePressed, float deltaTime) {
        currentAction = PauseAction::NONE;

        input.update(mouseX, mouseY, mousePressed);
        input.handleButton(resumeButton);
        input.handleButton(settingsButton);
        input.handleButton(saveButton);
        input.handleButton(quitButton);

        // Update save message timer
        if (saveMessageTimer > 0.0f) {
            saveMessageTimer -= deltaTime;
            if (saveMessageTimer <= 0.0f) {
                saveMessage = "";
            }
        }
    }

    void showSaveMessage(const std::string& message, float duration = 3.0f) {
        saveMessage = message;
        saveMessageTimer = duration;
    }

    void render() {
        if (!ui) return;

        float centerX = ui->windowWidth / 2.0f;
        float panelWidth = 350.0f;
        float panelHeight = 340.0f;
        float panelX = centerX - panelWidth / 2.0f;
        float panelY = ui->windowHeight / 2.0f - panelHeight / 2.0f;

        // Darken background
        ui->drawRect(0, 0, static_cast<float>(ui->windowWidth), static_cast<float>(ui->windowHeight),
                    glm::vec4(0.0f, 0.0f, 0.0f, 0.6f));

        // Panel background
        ui->drawRect(panelX, panelY, panelWidth, panelHeight, MenuColors::PANEL_BG);
        ui->drawRectOutline(panelX, panelY, panelWidth, panelHeight, MenuColors::ACCENT, 2.0f);

        // Title
        ui->drawTextCentered("GAME PAUSED", panelX, panelY + 15, panelWidth, MenuColors::ACCENT, 1.8f);

        // Buttons
        resumeButton.render(*ui);
        settingsButton.render(*ui);
        saveButton.render(*ui);
        quitButton.render(*ui);

        // Save message
        if (!saveMessage.empty()) {
            ui->drawTextCentered(saveMessage, panelX, panelY + panelHeight - 30, panelWidth,
                                MenuColors::SUCCESS, 1.0f);
        }
    }

    PauseAction getAction() const {
        return currentAction;
    }
};
