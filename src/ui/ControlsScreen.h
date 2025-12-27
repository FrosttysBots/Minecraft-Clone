#pragma once

#include "MenuUI.h"
#include "../input/KeybindManager.h"
#include <vector>
#include <string>

// Keybind button that can capture new key presses
struct KeybindButton {
    float x, y, width, height;
    KeyAction action;
    bool isPrimary;  // true = primary key, false = secondary key
    bool isCapturing = false;  // Currently waiting for key press
    bool hovered = false;

    void render(MenuUIRenderer& ui, const KeybindManager& km) {
        const Keybind& kb = km.getKeybind(action);
        int key = isPrimary ? kb.primary : kb.secondary;
        std::string keyStr = KeybindManager::keyToString(key, kb.isMouseButton);

        glm::vec4 bgColor;
        if (isCapturing) {
            bgColor = glm::vec4(0.8f, 0.3f, 0.1f, 0.9f);  // Orange when capturing
            keyStr = "> Press a key <";
        } else if (hovered) {
            bgColor = glm::vec4(0.4f, 0.4f, 0.5f, 0.9f);
        } else {
            bgColor = glm::vec4(0.25f, 0.25f, 0.3f, 0.9f);
        }

        ui.drawRect(x, y, width, height, bgColor);
        ui.drawRectOutline(x, y, width, height, glm::vec4(0.5f, 0.5f, 0.6f, 1.0f), 1.0f);
        ui.drawTextCentered(keyStr, x, y + height/2 - 8, width, glm::vec4(1.0f), 0.9f);
    }

    bool contains(float mx, float my) const {
        return mx >= x && mx <= x + width && my >= y && my <= y + height;
    }
};

class ControlsScreen {
public:
    MenuUIRenderer* ui = nullptr;
    MenuInputHandler input;
    bool visible = false;

    // Scroll offset for keybind list
    float scrollOffset = 0.0f;
    float maxScroll = 0.0f;

    // Keybind buttons
    std::vector<KeybindButton> keybindButtons;

    // Currently capturing keybind (if any)
    KeybindButton* capturingButton = nullptr;

    // Category filter
    std::string currentCategory = "All";
    std::vector<std::string> categories;

    // Category buttons
    std::vector<MenuButton> categoryButtons;

    // Action buttons
    MenuButton backButton;
    MenuButton resetButton;

    // Callback when done
    std::function<void()> onBack = nullptr;

    void init(MenuUIRenderer* uiRenderer) {
        ui = uiRenderer;
        KeybindManager::getInstance().init();
        setupUI();
    }

    void setupUI() {
        keybindButtons.clear();
        categoryButtons.clear();

        float centerX = ui->windowWidth / 2.0f;
        float panelWidth = 800.0f;
        float panelHeight = 600.0f;
        float panelX = centerX - panelWidth / 2.0f;
        float panelY = ui->windowHeight / 2.0f - panelHeight / 2.0f;

        // Get categories (excluding Debug from main view)
        categories = {"All", "Movement", "Gameplay", "Inventory", "Misc"};

        // Category buttons - wider to fit text
        float catBtnWidth = 110.0f;
        float catBtnHeight = 30.0f;
        float catStartX = panelX + 60;
        float catY = panelY + 50;

        for (size_t i = 0; i < categories.size(); i++) {
            std::string cat = categories[i];
            MenuButton btn = {
                catStartX + i * (catBtnWidth + 8), catY, catBtnWidth, catBtnHeight, cat,
                [this, cat]() { currentCategory = cat; rebuildKeybindList(); }
            };
            btn.textScale = 0.85f;
            categoryButtons.push_back(btn);
        }

        // Build keybind list
        rebuildKeybindList();

        // Action buttons
        float btnY = panelY + panelHeight - 55;
        backButton = {
            panelX + 30, btnY, 110, 40, "BACK",
            [this]() { hide(); }
        };
        backButton.textScale = 1.1f;

        resetButton = {
            panelX + panelWidth - 180, btnY, 150, 40, "RESET ALL",
            [this]() {
                KeybindManager::getInstance().resetAllToDefaults();
                rebuildKeybindList();
            }
        };
        resetButton.textScale = 1.0f;
    }

    void rebuildKeybindList() {
        keybindButtons.clear();

        float centerX = ui->windowWidth / 2.0f;
        float panelWidth = 800.0f;
        float panelX = centerX - panelWidth / 2.0f;
        float panelY = ui->windowHeight / 2.0f - 300.0f;

        float listStartY = panelY + 110;
        float rowHeight = 38.0f;
        float primaryX = panelX + 320;  // Moved left to give more room
        float secondaryX = panelX + 480;
        float btnWidth = 140.0f;
        float btnHeight = 28.0f;

        auto& km = KeybindManager::getInstance();
        int row = 0;

        // Ordered list of actions to display (excluding dev-only)
        std::vector<KeyAction> actionsToShow;

        // Movement
        if (currentCategory == "All" || currentCategory == "Movement") {
            actionsToShow.push_back(KeyAction::MoveForward);
            actionsToShow.push_back(KeyAction::MoveBackward);
            actionsToShow.push_back(KeyAction::MoveLeft);
            actionsToShow.push_back(KeyAction::MoveRight);
            actionsToShow.push_back(KeyAction::Jump);
            actionsToShow.push_back(KeyAction::Sneak);
            actionsToShow.push_back(KeyAction::Sprint);
        }

        // Gameplay
        if (currentCategory == "All" || currentCategory == "Gameplay") {
            actionsToShow.push_back(KeyAction::Attack);
            actionsToShow.push_back(KeyAction::UseItem);
            actionsToShow.push_back(KeyAction::PickBlock);
            actionsToShow.push_back(KeyAction::DropItem);
            actionsToShow.push_back(KeyAction::OpenInventory);
        }

        // Inventory (Hotbar)
        if (currentCategory == "All" || currentCategory == "Inventory") {
            actionsToShow.push_back(KeyAction::Hotbar1);
            actionsToShow.push_back(KeyAction::Hotbar2);
            actionsToShow.push_back(KeyAction::Hotbar3);
            actionsToShow.push_back(KeyAction::Hotbar4);
            actionsToShow.push_back(KeyAction::Hotbar5);
            actionsToShow.push_back(KeyAction::Hotbar6);
            actionsToShow.push_back(KeyAction::Hotbar7);
            actionsToShow.push_back(KeyAction::Hotbar8);
            actionsToShow.push_back(KeyAction::Hotbar9);
        }

        // Miscellaneous
        if (currentCategory == "All" || currentCategory == "Misc") {
            actionsToShow.push_back(KeyAction::TakeScreenshot);
            actionsToShow.push_back(KeyAction::ToggleDebug);
            actionsToShow.push_back(KeyAction::ToggleFullscreen);
            actionsToShow.push_back(KeyAction::Pause);
        }

        for (KeyAction action : actionsToShow) {
            float y = listStartY + row * rowHeight - scrollOffset;

            // Primary key button
            KeybindButton primaryBtn;
            primaryBtn.x = primaryX;
            primaryBtn.y = y;
            primaryBtn.width = btnWidth;
            primaryBtn.height = btnHeight;
            primaryBtn.action = action;
            primaryBtn.isPrimary = true;
            keybindButtons.push_back(primaryBtn);

            // Secondary key button
            KeybindButton secondaryBtn;
            secondaryBtn.x = secondaryX;
            secondaryBtn.y = y;
            secondaryBtn.width = btnWidth;
            secondaryBtn.height = btnHeight;
            secondaryBtn.action = action;
            secondaryBtn.isPrimary = false;
            keybindButtons.push_back(secondaryBtn);

            row++;
        }

        // Calculate max scroll
        float totalHeight = row * rowHeight;
        float visibleHeight = 400.0f;
        maxScroll = std::max(0.0f, totalHeight - visibleHeight);
    }

    void update(GLFWwindow* window, double mouseX, double mouseY, bool mousePressed, float deltaTime) {
        if (!visible) return;

        input.update(mouseX, mouseY, mousePressed);

        // Handle key capture
        if (capturingButton != nullptr) {
            // Check for key press
            for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; key++) {
                if (glfwGetKey(window, key) == GLFW_PRESS) {
                    // Escape cancels
                    if (key == GLFW_KEY_ESCAPE) {
                        capturingButton->isCapturing = false;
                        capturingButton = nullptr;
                        return;
                    }

                    // Set the new key
                    auto& km = KeybindManager::getInstance();
                    if (capturingButton->isPrimary) {
                        km.setPrimaryKey(capturingButton->action, key);
                    } else {
                        km.setSecondaryKey(capturingButton->action, key);
                    }

                    capturingButton->isCapturing = false;
                    capturingButton = nullptr;
                    return;
                }
            }

            // Check for mouse button press
            for (int btn = GLFW_MOUSE_BUTTON_1; btn <= GLFW_MOUSE_BUTTON_5; btn++) {
                if (glfwGetMouseButton(window, btn) == GLFW_PRESS) {
                    // Don't capture left click on the button itself (that's how we activate it)
                    if (btn == GLFW_MOUSE_BUTTON_LEFT && capturingButton->contains((float)mouseX, (float)mouseY)) {
                        continue;
                    }

                    auto& km = KeybindManager::getInstance();
                    Keybind& kb = km.getKeybind(capturingButton->action);
                    if (kb.isMouseButton || btn != GLFW_MOUSE_BUTTON_LEFT) {
                        if (capturingButton->isPrimary) {
                            km.setPrimaryKey(capturingButton->action, btn);
                        } else {
                            km.setSecondaryKey(capturingButton->action, btn);
                        }
                        // Mark as mouse button if needed
                        kb.isMouseButton = true;
                    }

                    capturingButton->isCapturing = false;
                    capturingButton = nullptr;
                    return;
                }
            }

            return;  // Don't process other input while capturing
        }

        // Category buttons
        for (auto& btn : categoryButtons) {
            input.handleButton(btn);
        }

        // Action buttons
        input.handleButton(backButton);
        input.handleButton(resetButton);

        // Keybind buttons
        for (auto& btn : keybindButtons) {
            btn.hovered = btn.contains((float)mouseX, (float)mouseY);

            if (btn.hovered && input.mouseJustPressed) {
                // Start capturing
                btn.isCapturing = true;
                capturingButton = &btn;
            }
        }

        // Scroll wheel
        // Note: This would need to be hooked up to GLFW scroll callback
    }

    void handleScroll(float yoffset) {
        scrollOffset -= yoffset * 30.0f;
        scrollOffset = std::max(0.0f, std::min(scrollOffset, maxScroll));
        rebuildKeybindList();
    }

    void render() {
        if (!visible || !ui) return;

        float centerX = ui->windowWidth / 2.0f;
        float panelWidth = 800.0f;
        float panelHeight = 600.0f;
        float panelX = centerX - panelWidth / 2.0f;
        float panelY = ui->windowHeight / 2.0f - panelHeight / 2.0f;

        // Darken background
        ui->drawRect(0, 0, (float)ui->windowWidth, (float)ui->windowHeight,
                    glm::vec4(0.0f, 0.0f, 0.0f, 0.8f));

        // Panel background
        ui->drawRect(panelX, panelY, panelWidth, panelHeight, MenuColors::PANEL_BG);
        ui->drawRectOutline(panelX, panelY, panelWidth, panelHeight, MenuColors::ACCENT, 2.0f);

        // Title
        ui->drawTextCentered("CONTROLS", panelX, panelY + 10, panelWidth, MenuColors::ACCENT, 1.8f);

        // Category buttons with active indicator
        for (size_t i = 0; i < categoryButtons.size(); i++) {
            auto& btn = categoryButtons[i];
            if (categories[i] == currentCategory) {
                ui->drawRect(btn.x, btn.y + btn.height - 3, btn.width, 3, MenuColors::ACCENT);
            }
            btn.render(*ui);
        }

        // Column headers
        float listStartY = panelY + 110;
        ui->drawText("Action", panelX + 40, listStartY - 25, MenuColors::TEXT_DIM, 1.0f);
        ui->drawText("Primary", panelX + 340, listStartY - 25, MenuColors::TEXT_DIM, 1.0f);
        ui->drawText("Secondary", panelX + 500, listStartY - 25, MenuColors::TEXT_DIM, 1.0f);

        // Clip region for scrollable list (we'll just render within bounds)
        float clipTop = listStartY;
        float clipBottom = panelY + panelHeight - 80;

        auto& km = KeybindManager::getInstance();

        // Render keybind rows
        int row = 0;
        float rowHeight = 40.0f;

        // Get ordered list of actions (same as rebuildKeybindList)
        std::vector<KeyAction> actionsToShow;
        if (currentCategory == "All" || currentCategory == "Movement") {
            actionsToShow.push_back(KeyAction::MoveForward);
            actionsToShow.push_back(KeyAction::MoveBackward);
            actionsToShow.push_back(KeyAction::MoveLeft);
            actionsToShow.push_back(KeyAction::MoveRight);
            actionsToShow.push_back(KeyAction::Jump);
            actionsToShow.push_back(KeyAction::Sneak);
            actionsToShow.push_back(KeyAction::Sprint);
        }
        if (currentCategory == "All" || currentCategory == "Gameplay") {
            actionsToShow.push_back(KeyAction::Attack);
            actionsToShow.push_back(KeyAction::UseItem);
            actionsToShow.push_back(KeyAction::PickBlock);
            actionsToShow.push_back(KeyAction::DropItem);
            actionsToShow.push_back(KeyAction::OpenInventory);
        }
        if (currentCategory == "All" || currentCategory == "Inventory") {
            actionsToShow.push_back(KeyAction::Hotbar1);
            actionsToShow.push_back(KeyAction::Hotbar2);
            actionsToShow.push_back(KeyAction::Hotbar3);
            actionsToShow.push_back(KeyAction::Hotbar4);
            actionsToShow.push_back(KeyAction::Hotbar5);
            actionsToShow.push_back(KeyAction::Hotbar6);
            actionsToShow.push_back(KeyAction::Hotbar7);
            actionsToShow.push_back(KeyAction::Hotbar8);
            actionsToShow.push_back(KeyAction::Hotbar9);
        }
        if (currentCategory == "All" || currentCategory == "Misc") {
            actionsToShow.push_back(KeyAction::TakeScreenshot);
            actionsToShow.push_back(KeyAction::ToggleDebug);
            actionsToShow.push_back(KeyAction::ToggleFullscreen);
            actionsToShow.push_back(KeyAction::Pause);
        }

        for (size_t i = 0; i < actionsToShow.size(); i++) {
            KeyAction action = actionsToShow[i];
            float y = listStartY + row * rowHeight - scrollOffset;

            // Skip if outside clip region
            if (y + rowHeight < clipTop || y > clipBottom) {
                row++;
                continue;
            }

            const Keybind& kb = km.getKeybind(action);

            // Action name
            ui->drawText(kb.displayName, panelX + 50, y + 8, MenuColors::TEXT, 1.0f);

            row++;
        }

        // Render keybind buttons
        for (auto& btn : keybindButtons) {
            // Skip if outside clip region
            if (btn.y + btn.height < clipTop || btn.y > clipBottom) continue;

            btn.render(*ui, km);
        }

        // Action buttons
        backButton.render(*ui);
        resetButton.render(*ui);

        // Show hint when capturing
        if (capturingButton != nullptr) {
            ui->drawRect(centerX - 200, panelY + panelHeight - 35, 400, 25,
                        glm::vec4(0.1f, 0.1f, 0.1f, 0.9f));
            ui->drawTextCentered("Press any key or ESC to cancel", centerX - 200, panelY + panelHeight - 32,
                                400, glm::vec4(1.0f, 0.8f, 0.3f, 1.0f), 0.9f);
        }
    }

    void show() {
        visible = true;
        scrollOffset = 0.0f;
        currentCategory = "All";
        rebuildKeybindList();
    }

    void hide() {
        visible = false;
        capturingButton = nullptr;
    }

    void resize(int width, int height) {
        if (ui) {
            ui->resize(width, height);
            setupUI();
        }
    }
};
