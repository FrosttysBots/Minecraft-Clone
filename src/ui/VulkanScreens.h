#pragma once

// Vulkan Screen Classes
// Mirrors OpenGL menu screens but uses VulkanMenuUIRenderer
// Includes: WorldSelectScreen, WorldCreateScreen, PauseMenu

#include "VulkanMenuUI.h"
#include "VulkanMainMenu.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <ctime>
#include <sstream>

// Information about a saved world (Vulkan version - no texture)
struct VulkanSavedWorldInfo {
    std::string name;
    std::string folderPath;
    int seed = 0;
    int generationType = 0;
    int maxHeight = 256;
    time_t lastPlayed = 0;
    std::string lastPlayedStr;
    bool isValid = false;
};

enum class VulkanWorldSelectAction {
    NONE,
    BACK,
    CREATE_WORLD,  // Renamed from CREATE_NEW to avoid Windows macro conflict
    PLAY_SELECTED,
    DELETE_SELECTED
};

// ========================================
// World Select Screen
// ========================================
class VulkanWorldSelectScreen {
public:
    VulkanMenuUIRenderer* ui = nullptr;
    VulkanMenuInputHandler input;

    std::vector<VulkanSavedWorldInfo> savedWorlds;
    int selectedWorldIndex = -1;

    VulkanMenuButton backButton;
    VulkanMenuButton createNewButton;
    VulkanMenuButton playButton;
    VulkanMenuButton deleteButton;

    float scrollOffset = 0.0f;
    float maxScroll = 0.0f;
    float worldEntryHeight = 80.0f;
    float listHeight = 400.0f;

    VulkanWorldSelectAction currentAction = VulkanWorldSelectAction::NONE;

    bool showDeleteConfirm = false;
    VulkanMenuButton confirmDeleteButton;
    VulkanMenuButton cancelDeleteButton;

    void init(VulkanMenuUIRenderer* uiRenderer) {
        ui = uiRenderer;
        setupUI();
        refreshWorldList();
    }

    void setupUI() {
        float centerX = ui->windowWidth / 2.0f;
        float panelWidth = 800.0f;
        float panelHeight = 550.0f;
        float panelX = centerX - panelWidth / 2.0f;
        float panelY = ui->windowHeight / 2.0f - panelHeight / 2.0f;

        backButton = {
            panelX + 30, panelY + panelHeight - 60, 120, 45, "BACK",
            [this]() { currentAction = VulkanWorldSelectAction::BACK; }
        };
        backButton.textScale = 1.2f;

        createNewButton = {
            centerX - 100, panelY + panelHeight - 60, 200, 45, "CREATE NEW WORLD",
            [this]() { currentAction = VulkanWorldSelectAction::CREATE_WORLD; }
        };
        createNewButton.textScale = 1.0f;

        playButton = {
            panelX + panelWidth - 250, panelY + panelHeight - 60, 100, 45, "PLAY",
            [this]() {
                if (selectedWorldIndex >= 0) {
                    currentAction = VulkanWorldSelectAction::PLAY_SELECTED;
                }
            }
        };
        playButton.textScale = 1.2f;

        deleteButton = {
            panelX + panelWidth - 140, panelY + panelHeight - 60, 100, 45, "DELETE",
            [this]() {
                if (selectedWorldIndex >= 0) {
                    showDeleteConfirm = true;
                }
            }
        };
        deleteButton.textScale = 1.0f;

        confirmDeleteButton = {
            centerX - 110, ui->windowHeight / 2.0f + 20, 100, 40, "DELETE",
            [this]() {
                currentAction = VulkanWorldSelectAction::DELETE_SELECTED;
                showDeleteConfirm = false;
            }
        };
        confirmDeleteButton.textScale = 1.0f;

        cancelDeleteButton = {
            centerX + 10, ui->windowHeight / 2.0f + 20, 100, 40, "CANCEL",
            [this]() { showDeleteConfirm = false; }
        };
        cancelDeleteButton.textScale = 1.0f;

        listHeight = panelHeight - 140;
    }

    void resize(int width, int height) {
        if (ui) {
            ui->resize(width, height);
            setupUI();
        }
    }

    void refreshWorldList() {
        savedWorlds.clear();
        selectedWorldIndex = -1;

        std::string savesPath = "saves";

        if (!std::filesystem::exists(savesPath)) {
            std::filesystem::create_directories(savesPath);
            return;
        }

        for (const auto& entry : std::filesystem::directory_iterator(savesPath)) {
            if (entry.is_directory()) {
                VulkanSavedWorldInfo info;
                info.folderPath = entry.path().string();
                info.name = entry.path().filename().string();

                std::string metaPath = info.folderPath + "/world.meta";
                if (std::filesystem::exists(metaPath)) {
                    std::ifstream metaFile(metaPath);
                    if (metaFile.is_open()) {
                        std::string line;
                        while (std::getline(metaFile, line)) {
                            size_t eq = line.find('=');
                            if (eq != std::string::npos) {
                                std::string key = line.substr(0, eq);
                                std::string value = line.substr(eq + 1);

                                if (key == "name") info.name = value;
                                else if (key == "seed") info.seed = std::stoi(value);
                                else if (key == "generationType") info.generationType = std::stoi(value);
                                else if (key == "maxHeight") info.maxHeight = std::stoi(value);
                                else if (key == "lastPlayed") info.lastPlayed = std::stoll(value);
                            }
                        }
                        info.isValid = true;
                    }
                }

                if (info.lastPlayed > 0) {
                    char buffer[64];
                    struct tm timeinfo;
#ifdef _WIN32
                    localtime_s(&timeinfo, &info.lastPlayed);
#else
                    localtime_r(&info.lastPlayed, &timeinfo);
#endif
                    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &timeinfo);
                    info.lastPlayedStr = buffer;
                } else {
                    info.lastPlayedStr = "Never";
                }

                savedWorlds.push_back(info);
            }
        }

        std::sort(savedWorlds.begin(), savedWorlds.end(),
            [](const VulkanSavedWorldInfo& a, const VulkanSavedWorldInfo& b) {
                return a.lastPlayed > b.lastPlayed;
            });

        float totalHeight = savedWorlds.size() * worldEntryHeight;
        maxScroll = std::max(0.0f, totalHeight - listHeight);
    }

    void update(double mouseX, double mouseY, bool mousePressed, float deltaTime) {
        (void)deltaTime;
        currentAction = VulkanWorldSelectAction::NONE;

        input.update(mouseX, mouseY, mousePressed);

        if (showDeleteConfirm) {
            input.handleButton(confirmDeleteButton);
            input.handleButton(cancelDeleteButton);
            return;
        }

        input.handleButton(backButton);
        input.handleButton(createNewButton);

        if (selectedWorldIndex >= 0) {
            input.handleButton(playButton);
            input.handleButton(deleteButton);
        }

        float centerX = ui->windowWidth / 2.0f;
        float panelWidth = 800.0f;
        float panelX = centerX - panelWidth / 2.0f;
        float panelY = ui->windowHeight / 2.0f - 275.0f;
        float listY = panelY + 70;
        float listX = panelX + 20;
        float entryWidth = panelWidth - 40;

        if (mouseX >= listX && mouseX <= listX + entryWidth &&
            mouseY >= listY && mouseY <= listY + listHeight) {

            if (input.mouseJustPressed) {
                float relY = static_cast<float>(mouseY) - listY + scrollOffset;
                int clickedIndex = static_cast<int>(relY / worldEntryHeight);

                if (clickedIndex >= 0 && clickedIndex < static_cast<int>(savedWorlds.size())) {
                    if (selectedWorldIndex == clickedIndex) {
                        currentAction = VulkanWorldSelectAction::PLAY_SELECTED;
                    } else {
                        selectedWorldIndex = clickedIndex;
                    }
                }
            }
        }
    }

    void handleScroll(float yOffset) {
        scrollOffset -= yOffset * 30.0f;
        scrollOffset = std::max(0.0f, std::min(scrollOffset, maxScroll));
    }

    void render() {
        if (!ui) return;

        float centerX = ui->windowWidth / 2.0f;
        float panelWidth = 800.0f;
        float panelHeight = 550.0f;
        float panelX = centerX - panelWidth / 2.0f;
        float panelY = ui->windowHeight / 2.0f - panelHeight / 2.0f;

        ui->drawRect(0, 0, static_cast<float>(ui->windowWidth), static_cast<float>(ui->windowHeight),
                    glm::vec4(0.0f, 0.0f, 0.0f, 0.7f));

        ui->drawRect(panelX, panelY, panelWidth, panelHeight, MenuColors::PANEL_BG);
        ui->drawRectOutline(panelX, panelY, panelWidth, panelHeight, MenuColors::ACCENT, 2.0f);

        ui->drawTextCentered("SELECT WORLD", panelX, panelY + 15, panelWidth, MenuColors::ACCENT, 2.0f);

        float listY = panelY + 70;
        float listX = panelX + 20;
        float entryWidth = panelWidth - 40;

        ui->drawRect(listX, listY, entryWidth, listHeight, glm::vec4(0.05f, 0.05f, 0.08f, 1.0f));

        float y = listY - scrollOffset;
        for (size_t i = 0; i < savedWorlds.size(); i++) {
            if (y + worldEntryHeight < listY) {
                y += worldEntryHeight;
                continue;
            }
            if (y > listY + listHeight) {
                break;
            }

            const auto& world = savedWorlds[i];

            glm::vec4 bgColor = (static_cast<int>(i) == selectedWorldIndex)
                ? glm::vec4(0.2f, 0.3f, 0.4f, 1.0f)
                : glm::vec4(0.1f, 0.1f, 0.15f, 1.0f);

            if (input.mouseX >= listX && input.mouseX <= listX + entryWidth &&
                input.mouseY >= y && input.mouseY <= y + worldEntryHeight - 5) {
                bgColor = glm::vec4(0.15f, 0.2f, 0.25f, 1.0f);
            }

            ui->drawRect(listX + 5, y + 2, entryWidth - 10, worldEntryHeight - 5, bgColor);

            float thumbWidth = 120.0f;
            float thumbHeight = 68.0f;
            float thumbX = listX + 15;
            float thumbY = y + 6;

            ui->drawRect(thumbX, thumbY, thumbWidth, thumbHeight, glm::vec4(0.15f, 0.15f, 0.2f, 1.0f));
            ui->drawTextCentered("No Preview", thumbX, thumbY + thumbHeight / 2 - 8, thumbWidth,
                                glm::vec4(0.4f, 0.4f, 0.4f, 1.0f), 0.8f);

            float textX = thumbX + thumbWidth + 15;
            ui->drawText(world.name, textX, y + 12, MenuColors::TEXT, 1.5f);

            std::string infoText = "Seed: " + std::to_string(world.seed);
            ui->drawText(infoText, textX, y + 40, MenuColors::TEXT_DIM, 0.9f);

            std::string playedText = "Last played: " + world.lastPlayedStr;
            ui->drawText(playedText, textX, y + 58, MenuColors::TEXT_DIM, 0.9f);

            y += worldEntryHeight;
        }

        if (savedWorlds.empty()) {
            ui->drawTextCentered("No saved worlds found", listX, listY + listHeight / 2 - 10,
                                entryWidth, MenuColors::TEXT_DIM, 1.2f);
            ui->drawTextCentered("Click 'Create New World' to start", listX, listY + listHeight / 2 + 20,
                                entryWidth, MenuColors::TEXT_DIM, 1.0f);
        }

        backButton.render(*ui);
        createNewButton.render(*ui);

        if (selectedWorldIndex >= 0) {
            playButton.render(*ui);
            deleteButton.render(*ui);
        } else {
            ui->drawRect(playButton.x, playButton.y, playButton.width, playButton.height,
                        glm::vec4(0.15f, 0.15f, 0.2f, 0.5f));
            ui->drawTextCentered("PLAY", playButton.x, playButton.y + 12, playButton.width,
                                glm::vec4(0.4f, 0.4f, 0.4f, 1.0f), playButton.textScale);

            ui->drawRect(deleteButton.x, deleteButton.y, deleteButton.width, deleteButton.height,
                        glm::vec4(0.15f, 0.15f, 0.2f, 0.5f));
            ui->drawTextCentered("DELETE", deleteButton.x, deleteButton.y + 12, deleteButton.width,
                                glm::vec4(0.4f, 0.4f, 0.4f, 1.0f), deleteButton.textScale);
        }

        if (showDeleteConfirm) {
            ui->drawRect(0, 0, static_cast<float>(ui->windowWidth), static_cast<float>(ui->windowHeight),
                        glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));

            float dialogW = 400, dialogH = 150;
            float dialogX = centerX - dialogW / 2;
            float dialogY = ui->windowHeight / 2.0f - dialogH / 2;

            ui->drawRect(dialogX, dialogY, dialogW, dialogH, MenuColors::PANEL_BG);
            ui->drawRectOutline(dialogX, dialogY, dialogW, dialogH, MenuColors::ERROR_COLOR, 2.0f);

            ui->drawTextCentered("Delete this world?", dialogX, dialogY + 20, dialogW, MenuColors::TEXT, 1.5f);
            if (selectedWorldIndex >= 0 && selectedWorldIndex < static_cast<int>(savedWorlds.size())) {
                ui->drawTextCentered(savedWorlds[selectedWorldIndex].name, dialogX, dialogY + 55, dialogW,
                                    MenuColors::TEXT_DIM, 1.2f);
            }

            confirmDeleteButton.render(*ui);
            cancelDeleteButton.render(*ui);
        }
    }

    const VulkanSavedWorldInfo* getSelectedWorld() const {
        if (selectedWorldIndex >= 0 && selectedWorldIndex < static_cast<int>(savedWorlds.size())) {
            return &savedWorlds[selectedWorldIndex];
        }
        return nullptr;
    }

    VulkanWorldSelectAction getAction() const { return currentAction; }
};

// ========================================
// World Create Screen
// ========================================
enum class VulkanWorldCreateAction {
    NONE,
    BACK,
    CREATE
};

class VulkanWorldCreateScreen {
public:
    VulkanMenuUIRenderer* ui = nullptr;
    VulkanMenuInputHandler input;

    VulkanMenuTextInput worldNameInput;
    VulkanMenuTextInput seedInput;
    VulkanMenuDropdown worldTypeDropdown;

    VulkanMenuButton backButton;
    VulkanMenuButton createButton;

    VulkanWorldCreateAction currentAction = VulkanWorldCreateAction::NONE;

    std::string worldName = "New World";
    std::string seed = "";
    int worldType = 0;

    void init(VulkanMenuUIRenderer* uiRenderer) {
        ui = uiRenderer;
        setupUI();
    }

    void setupUI() {
        float centerX = ui->windowWidth / 2.0f;
        float panelWidth = 600.0f;
        float panelHeight = 400.0f;
        float panelX = centerX - panelWidth / 2.0f;
        float panelY = ui->windowHeight / 2.0f - panelHeight / 2.0f;

        float inputWidth = 400.0f;
        float inputHeight = 40.0f;
        float inputX = centerX - inputWidth / 2.0f;
        float startY = panelY + 80;

        worldNameInput = {
            inputX, startY, inputWidth, inputHeight,
            "World Name", worldName, "Enter world name...",
            [this](const std::string& text) { worldName = text; }
        };

        seedInput = {
            inputX, startY + 80, inputWidth, inputHeight,
            "Seed (optional)", seed, "Leave blank for random",
            [this](const std::string& text) { seed = text; }
        };

        worldTypeDropdown = {
            inputX, startY + 160, inputWidth, inputHeight,
            "World Type",
            {"Default", "Superflat", "Amplified", "Mountains", "Islands", "Caves"},
            worldType,
            [this](int idx) { worldType = idx; }
        };

        backButton = {
            panelX + 30, panelY + panelHeight - 60, 120, 45, "CANCEL",
            [this]() { currentAction = VulkanWorldCreateAction::BACK; }
        };
        backButton.textScale = 1.2f;

        createButton = {
            panelX + panelWidth - 200, panelY + panelHeight - 60, 170, 45, "CREATE WORLD",
            [this]() { currentAction = VulkanWorldCreateAction::CREATE; }
        };
        createButton.textScale = 1.2f;
    }

    void resize(int width, int height) {
        if (ui) {
            ui->resize(width, height);
            setupUI();
        }
    }

    void update(double mouseX, double mouseY, bool mousePressed, float deltaTime) {
        currentAction = VulkanWorldCreateAction::NONE;

        input.update(mouseX, mouseY, mousePressed);

        input.handleTextInput(worldNameInput);
        input.handleTextInput(seedInput);

        std::vector<VulkanMenuDropdown*> dropdowns = {&worldTypeDropdown};
        input.handleDropdown(worldTypeDropdown, dropdowns);

        input.handleButton(backButton);
        input.handleButton(createButton);
    }

    void handleKeyInput(int key, int action, int mods) {
        worldNameInput.handleKeyInput(key, action, mods);
        seedInput.handleKeyInput(key, action, mods);
    }

    void handleCharInput(unsigned int codepoint) {
        worldNameInput.handleCharInput(codepoint);
        seedInput.handleCharInput(codepoint);
    }

    void render(float deltaTime) {
        if (!ui) return;

        float centerX = ui->windowWidth / 2.0f;
        float panelWidth = 600.0f;
        float panelHeight = 400.0f;
        float panelX = centerX - panelWidth / 2.0f;
        float panelY = ui->windowHeight / 2.0f - panelHeight / 2.0f;

        ui->drawRect(0, 0, static_cast<float>(ui->windowWidth), static_cast<float>(ui->windowHeight),
                    glm::vec4(0.0f, 0.0f, 0.0f, 0.7f));

        ui->drawRect(panelX, panelY, panelWidth, panelHeight, MenuColors::PANEL_BG);
        ui->drawRectOutline(panelX, panelY, panelWidth, panelHeight, MenuColors::ACCENT, 2.0f);

        ui->drawTextCentered("CREATE NEW WORLD", panelX, panelY + 20, panelWidth, MenuColors::ACCENT, 2.0f);

        worldNameInput.render(*ui, deltaTime);
        seedInput.render(*ui, deltaTime);
        worldTypeDropdown.render(*ui);
        worldTypeDropdown.renderOptions(*ui);

        backButton.render(*ui);
        createButton.render(*ui);
    }

    VulkanWorldCreateAction getAction() const { return currentAction; }
    std::string getWorldName() const { return worldName.empty() ? "New World" : worldName; }
    std::string getSeed() const { return seed; }
    int getWorldType() const { return worldType; }

    void reset() {
        worldName = "New World";
        seed = "";
        worldType = 0;
        worldNameInput.text = worldName;
        worldNameInput.cursorPos = worldName.length();
        seedInput.text = "";
        seedInput.cursorPos = 0;
        worldTypeDropdown.selectedIndex = 0;
    }
};

// ========================================
// Pause Menu
// ========================================
enum class VulkanPauseAction {
    NONE,
    RESUME,
    SETTINGS,
    SAVE_QUIT
};

class VulkanPauseMenu {
public:
    VulkanMenuUIRenderer* ui = nullptr;
    VulkanMenuInputHandler input;

    VulkanMenuButton resumeButton;
    VulkanMenuButton settingsButton;
    VulkanMenuButton saveQuitButton;

    VulkanPauseAction currentAction = VulkanPauseAction::NONE;

    void init(VulkanMenuUIRenderer* uiRenderer) {
        ui = uiRenderer;
        setupUI();
    }

    void setupUI() {
        float centerX = ui->windowWidth / 2.0f;
        float btnWidth = 300.0f;
        float btnHeight = 55.0f;
        float btnSpacing = 15.0f;
        float startY = ui->windowHeight / 2.0f - 80.0f;

        resumeButton = {
            centerX - btnWidth / 2, startY,
            btnWidth, btnHeight, "RESUME",
            [this]() { currentAction = VulkanPauseAction::RESUME; }
        };
        resumeButton.textScale = 1.5f;

        settingsButton = {
            centerX - btnWidth / 2, startY + btnHeight + btnSpacing,
            btnWidth, btnHeight, "SETTINGS",
            [this]() { currentAction = VulkanPauseAction::SETTINGS; }
        };
        settingsButton.textScale = 1.5f;

        saveQuitButton = {
            centerX - btnWidth / 2, startY + 2 * (btnHeight + btnSpacing),
            btnWidth, btnHeight, "SAVE & QUIT",
            [this]() { currentAction = VulkanPauseAction::SAVE_QUIT; }
        };
        saveQuitButton.textScale = 1.5f;
    }

    void resize(int width, int height) {
        if (ui) {
            ui->resize(width, height);
            setupUI();
        }
    }

    void update(double mouseX, double mouseY, bool mousePressed) {
        currentAction = VulkanPauseAction::NONE;

        input.update(mouseX, mouseY, mousePressed);
        input.handleButton(resumeButton);
        input.handleButton(settingsButton);
        input.handleButton(saveQuitButton);
    }

    void render() {
        if (!ui) return;

        float centerX = ui->windowWidth / 2.0f;

        ui->drawRect(0, 0, static_cast<float>(ui->windowWidth), static_cast<float>(ui->windowHeight),
                    glm::vec4(0.0f, 0.0f, 0.0f, 0.6f));

        ui->drawTextCentered("PAUSED", 0, ui->windowHeight / 2.0f - 160.0f,
                            static_cast<float>(ui->windowWidth), MenuColors::ACCENT, 2.5f);

        resumeButton.render(*ui);
        settingsButton.render(*ui);
        saveQuitButton.render(*ui);
    }

    VulkanPauseAction getAction() const { return currentAction; }
};

// ========================================
// Loading Screen
// ========================================
class VulkanLoadingScreen {
public:
    VulkanMenuUIRenderer* ui = nullptr;
    std::string message = "Loading...";
    float progress = 0.0f;

    void init(VulkanMenuUIRenderer* uiRenderer) {
        ui = uiRenderer;
    }

    void setMessage(const std::string& msg) { message = msg; }
    void setProgress(float p) { progress = std::clamp(p, 0.0f, 1.0f); }

    void render() {
        if (!ui) return;

        float centerX = ui->windowWidth / 2.0f;
        float centerY = ui->windowHeight / 2.0f;

        ui->drawRect(0, 0, static_cast<float>(ui->windowWidth), static_cast<float>(ui->windowHeight),
                    glm::vec4(0.08f, 0.08f, 0.12f, 1.0f));

        ui->drawTextCentered("FORGEBOUND", 0, centerY - 100,
                            static_cast<float>(ui->windowWidth), MenuColors::ACCENT, 2.5f);

        ui->drawTextCentered(message, 0, centerY,
                            static_cast<float>(ui->windowWidth), MenuColors::TEXT, 1.2f);

        float barWidth = 400.0f;
        float barHeight = 20.0f;
        float barX = centerX - barWidth / 2;
        float barY = centerY + 50;

        ui->drawRect(barX, barY, barWidth, barHeight, MenuColors::SLIDER_BG);
        ui->drawRect(barX, barY, barWidth * progress, barHeight, MenuColors::ACCENT);
        ui->drawRectOutline(barX, barY, barWidth, barHeight, MenuColors::DIVIDER, 1.0f);

        std::stringstream ss;
        ss << static_cast<int>(progress * 100) << "%";
        ui->drawTextCentered(ss.str(), 0, barY + barHeight + 15,
                            static_cast<float>(ui->windowWidth), MenuColors::TEXT_DIM, 1.0f);
    }
};
