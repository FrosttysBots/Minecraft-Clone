#pragma once

// World Selection Screen
// Shows saved worlds and allows creating new ones (like Minecraft's world select)

#include "MenuUI.h"
#include "../render/Screenshot.h"
#include <functional>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <ctime>

// Information about a saved world
struct SavedWorldInfo {
    std::string name;
    std::string folderPath;
    int seed = 0;
    int generationType = 0;
    int maxHeight = 256;
    time_t lastPlayed = 0;
    std::string lastPlayedStr;
    bool isValid = false;
    GLuint thumbnailTexture = 0;  // OpenGL texture for thumbnail
    bool hasThumbnail = false;
};

enum class WorldSelectAction {
    NONE,
    BACK,
    CREATE_NEW,
    PLAY_SELECTED,
    DELETE_SELECTED
};

class WorldSelectScreen {
public:
    MenuUIRenderer* ui = nullptr;
    MenuInputHandler input;

    // List of saved worlds
    std::vector<SavedWorldInfo> savedWorlds;
    int selectedWorldIndex = -1;

    // UI elements
    MenuButton backButton;
    MenuButton createNewButton;
    MenuButton playButton;
    MenuButton deleteButton;

    // Scroll state for world list
    float scrollOffset = 0.0f;
    float maxScroll = 0.0f;
    float worldEntryHeight = 80.0f;
    float listHeight = 400.0f;

    // Current action
    WorldSelectAction currentAction = WorldSelectAction::NONE;

    // Confirmation dialog for delete
    bool showDeleteConfirm = false;
    MenuButton confirmDeleteButton;
    MenuButton cancelDeleteButton;

    void init(MenuUIRenderer* uiRenderer) {
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

        // Back button (bottom left)
        backButton = {
            panelX + 30, panelY + panelHeight - 60, 120, 45, "BACK",
            [this]() { currentAction = WorldSelectAction::BACK; }
        };
        backButton.textScale = 1.2f;

        // Create New World button (bottom center)
        createNewButton = {
            centerX - 100, panelY + panelHeight - 60, 200, 45, "CREATE NEW WORLD",
            [this]() { currentAction = WorldSelectAction::CREATE_NEW; }
        };
        createNewButton.textScale = 1.0f;

        // Play button (bottom right) - only enabled when world selected
        playButton = {
            panelX + panelWidth - 250, panelY + panelHeight - 60, 100, 45, "PLAY",
            [this]() {
                if (selectedWorldIndex >= 0) {
                    currentAction = WorldSelectAction::PLAY_SELECTED;
                }
            }
        };
        playButton.textScale = 1.2f;

        // Delete button (next to play)
        deleteButton = {
            panelX + panelWidth - 140, panelY + panelHeight - 60, 100, 45, "DELETE",
            [this]() {
                if (selectedWorldIndex >= 0) {
                    showDeleteConfirm = true;
                }
            }
        };
        deleteButton.textScale = 1.0f;

        // Delete confirmation buttons
        confirmDeleteButton = {
            centerX - 110, ui->windowHeight / 2.0f + 20, 100, 40, "DELETE",
            [this]() {
                currentAction = WorldSelectAction::DELETE_SELECTED;
                showDeleteConfirm = false;
            }
        };
        confirmDeleteButton.textScale = 1.0f;

        cancelDeleteButton = {
            centerX + 10, ui->windowHeight / 2.0f + 20, 100, 40, "CANCEL",
            [this]() { showDeleteConfirm = false; }
        };
        cancelDeleteButton.textScale = 1.0f;

        listHeight = panelHeight - 140;  // Space for title and buttons
    }

    void resize(int width, int height) {
        if (ui) {
            ui->resize(width, height);
            setupUI();
        }
    }

    // Refresh list of saved worlds from disk
    // Clean up thumbnail textures
    void cleanupThumbnails() {
        for (auto& world : savedWorlds) {
            if (world.thumbnailTexture != 0) {
                glDeleteTextures(1, &world.thumbnailTexture);
                world.thumbnailTexture = 0;
            }
        }
    }

    void refreshWorldList() {
        // Clean up old thumbnails first
        cleanupThumbnails();
        savedWorlds.clear();
        selectedWorldIndex = -1;

        std::string savesPath = "saves";

        // Create saves directory if it doesn't exist
        if (!std::filesystem::exists(savesPath)) {
            std::filesystem::create_directories(savesPath);
            return;
        }

        // Scan for world folders
        for (const auto& entry : std::filesystem::directory_iterator(savesPath)) {
            if (entry.is_directory()) {
                SavedWorldInfo info;
                info.folderPath = entry.path().string();
                info.name = entry.path().filename().string();

                // Try to load world metadata
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

                // Format last played time
                if (info.lastPlayed > 0) {
                    char buffer[64];
                    struct tm* timeinfo = localtime(&info.lastPlayed);
                    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", timeinfo);
                    info.lastPlayedStr = buffer;
                } else {
                    info.lastPlayedStr = "Never";
                }

                // Load thumbnail if it exists
                std::string thumbPath = info.folderPath + "/thumbnail.png";
                if (std::filesystem::exists(thumbPath)) {
                    info.thumbnailTexture = Screenshot::loadThumbnailTexture(thumbPath);
                    info.hasThumbnail = (info.thumbnailTexture != 0);
                }

                savedWorlds.push_back(info);
            }
        }

        // Sort by last played (most recent first)
        std::sort(savedWorlds.begin(), savedWorlds.end(),
            [](const SavedWorldInfo& a, const SavedWorldInfo& b) {
                return a.lastPlayed > b.lastPlayed;
            });

        // Calculate max scroll
        float totalHeight = savedWorlds.size() * worldEntryHeight;
        maxScroll = std::max(0.0f, totalHeight - listHeight);
    }

    void update(double mouseX, double mouseY, bool mousePressed, float deltaTime) {
        (void)deltaTime;
        currentAction = WorldSelectAction::NONE;

        input.update(mouseX, mouseY, mousePressed);

        if (showDeleteConfirm) {
            // Only handle delete confirmation dialog
            input.handleButton(confirmDeleteButton);
            input.handleButton(cancelDeleteButton);
            return;
        }

        // Handle buttons
        input.handleButton(backButton);
        input.handleButton(createNewButton);

        if (selectedWorldIndex >= 0) {
            input.handleButton(playButton);
            input.handleButton(deleteButton);
        }

        // Handle world list clicks
        float centerX = ui->windowWidth / 2.0f;
        float panelWidth = 800.0f;
        float panelX = centerX - panelWidth / 2.0f;
        float panelY = ui->windowHeight / 2.0f - 275.0f;
        float listY = panelY + 70;
        float listX = panelX + 20;
        float entryWidth = panelWidth - 40;

        // Check if mouse is in list area
        if (mouseX >= listX && mouseX <= listX + entryWidth &&
            mouseY >= listY && mouseY <= listY + listHeight) {

            // Handle scroll (simple click-based for now)
            // TODO: Add proper scroll wheel support

            // Check for world selection on click
            if (input.mouseJustPressed) {
                float relY = static_cast<float>(mouseY) - listY + scrollOffset;
                int clickedIndex = static_cast<int>(relY / worldEntryHeight);

                if (clickedIndex >= 0 && clickedIndex < static_cast<int>(savedWorlds.size())) {
                    if (selectedWorldIndex == clickedIndex) {
                        // Double-click to play (simplified: just play on second click)
                        currentAction = WorldSelectAction::PLAY_SELECTED;
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

        // Darken background
        ui->drawRect(0, 0, static_cast<float>(ui->windowWidth), static_cast<float>(ui->windowHeight),
                    glm::vec4(0.0f, 0.0f, 0.0f, 0.7f));

        // Panel background
        ui->drawRect(panelX, panelY, panelWidth, panelHeight, MenuColors::PANEL_BG);
        ui->drawRectOutline(panelX, panelY, panelWidth, panelHeight, MenuColors::ACCENT, 2.0f);

        // Title
        ui->drawTextCentered("SELECT WORLD", panelX, panelY + 15, panelWidth, MenuColors::ACCENT, 2.0f);

        // World list area
        float listY = panelY + 70;
        float listX = panelX + 20;
        float entryWidth = panelWidth - 40;

        // List background
        ui->drawRect(listX, listY, entryWidth, listHeight, glm::vec4(0.05f, 0.05f, 0.08f, 1.0f));

        // Render world entries
        float y = listY - scrollOffset;
        for (size_t i = 0; i < savedWorlds.size(); i++) {
            if (y + worldEntryHeight < listY) {
                y += worldEntryHeight;
                continue;  // Skip entries above visible area
            }
            if (y > listY + listHeight) {
                break;  // Stop at entries below visible area
            }

            const auto& world = savedWorlds[i];

            // Entry background
            glm::vec4 bgColor = (static_cast<int>(i) == selectedWorldIndex)
                ? glm::vec4(0.2f, 0.3f, 0.4f, 1.0f)
                : glm::vec4(0.1f, 0.1f, 0.15f, 1.0f);

            // Hover effect
            if (input.mouseX >= listX && input.mouseX <= listX + entryWidth &&
                input.mouseY >= y && input.mouseY <= y + worldEntryHeight - 5) {
                bgColor = glm::vec4(0.15f, 0.2f, 0.25f, 1.0f);
            }

            ui->drawRect(listX + 5, y + 2, entryWidth - 10, worldEntryHeight - 5, bgColor);

            // Thumbnail dimensions
            float thumbWidth = 120.0f;
            float thumbHeight = 68.0f;
            float thumbX = listX + 15;
            float thumbY = y + 6;

            // Draw thumbnail or placeholder
            if (world.hasThumbnail && world.thumbnailTexture != 0) {
                ui->drawTexture(world.thumbnailTexture, thumbX, thumbY, thumbWidth, thumbHeight);
            } else {
                // Placeholder - dark gray box with "No Preview" text
                ui->drawRect(thumbX, thumbY, thumbWidth, thumbHeight, glm::vec4(0.15f, 0.15f, 0.2f, 1.0f));
                ui->drawTextCentered("No Preview", thumbX, thumbY + thumbHeight / 2 - 8, thumbWidth,
                                    glm::vec4(0.4f, 0.4f, 0.4f, 1.0f), 0.8f);
            }

            // World name (offset to right of thumbnail)
            float textX = thumbX + thumbWidth + 15;
            ui->drawText(world.name, textX, y + 12, MenuColors::TEXT, 1.5f);

            // World info
            std::string infoText = "Seed: " + std::to_string(world.seed);
            ui->drawText(infoText, textX, y + 40, MenuColors::TEXT_DIM, 0.9f);

            std::string playedText = "Last played: " + world.lastPlayedStr;
            ui->drawText(playedText, textX, y + 58, MenuColors::TEXT_DIM, 0.9f);

            y += worldEntryHeight;
        }

        // Empty list message
        if (savedWorlds.empty()) {
            ui->drawTextCentered("No saved worlds found", listX, listY + listHeight / 2 - 10,
                                entryWidth, MenuColors::TEXT_DIM, 1.2f);
            ui->drawTextCentered("Click 'Create New World' to start", listX, listY + listHeight / 2 + 20,
                                entryWidth, MenuColors::TEXT_DIM, 1.0f);
        }

        // Buttons
        backButton.render(*ui);
        createNewButton.render(*ui);

        // Only show play/delete buttons if world selected
        if (selectedWorldIndex >= 0) {
            playButton.render(*ui);
            deleteButton.render(*ui);
        } else {
            // Render disabled versions
            ui->drawRect(playButton.x, playButton.y, playButton.width, playButton.height,
                        glm::vec4(0.15f, 0.15f, 0.2f, 0.5f));
            ui->drawTextCentered("PLAY", playButton.x, playButton.y + 12, playButton.width,
                                glm::vec4(0.4f, 0.4f, 0.4f, 1.0f), playButton.textScale);

            ui->drawRect(deleteButton.x, deleteButton.y, deleteButton.width, deleteButton.height,
                        glm::vec4(0.15f, 0.15f, 0.2f, 0.5f));
            ui->drawTextCentered("DELETE", deleteButton.x, deleteButton.y + 12, deleteButton.width,
                                glm::vec4(0.4f, 0.4f, 0.4f, 1.0f), deleteButton.textScale);
        }

        // Delete confirmation dialog
        if (showDeleteConfirm) {
            // Darken everything
            ui->drawRect(0, 0, static_cast<float>(ui->windowWidth), static_cast<float>(ui->windowHeight),
                        glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));

            // Dialog box
            float dialogW = 350.0f;
            float dialogH = 150.0f;
            float dialogX = centerX - dialogW / 2.0f;
            float dialogY = ui->windowHeight / 2.0f - dialogH / 2.0f;

            ui->drawRect(dialogX, dialogY, dialogW, dialogH, MenuColors::PANEL_BG);
            ui->drawRectOutline(dialogX, dialogY, dialogW, dialogH, glm::vec4(0.8f, 0.3f, 0.3f, 1.0f), 2.0f);

            ui->drawTextCentered("Delete World?", dialogX, dialogY + 20, dialogW,
                                glm::vec4(0.9f, 0.4f, 0.4f, 1.0f), 1.5f);

            if (selectedWorldIndex >= 0 && selectedWorldIndex < static_cast<int>(savedWorlds.size())) {
                ui->drawTextCentered(savedWorlds[selectedWorldIndex].name, dialogX, dialogY + 55, dialogW,
                                    MenuColors::TEXT, 1.2f);
            }

            confirmDeleteButton.render(*ui);
            cancelDeleteButton.render(*ui);
        }
    }

    WorldSelectAction getAction() const {
        return currentAction;
    }

    // Get selected world info
    const SavedWorldInfo* getSelectedWorld() const {
        if (selectedWorldIndex >= 0 && selectedWorldIndex < static_cast<int>(savedWorlds.size())) {
            return &savedWorlds[selectedWorldIndex];
        }
        return nullptr;
    }

    // Delete the selected world
    bool deleteSelectedWorld() {
        if (selectedWorldIndex < 0 || selectedWorldIndex >= static_cast<int>(savedWorlds.size())) {
            return false;
        }

        const auto& world = savedWorlds[selectedWorldIndex];
        try {
            std::filesystem::remove_all(world.folderPath);
            refreshWorldList();
            return true;
        } catch (...) {
            return false;
        }
    }
};
