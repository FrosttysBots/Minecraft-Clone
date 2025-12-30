#pragma once

// Texture Pack Selection Screen
// Allows players to browse and select texture packs at runtime

#include "MenuUI.h"
#include "../render/TexturePackLoader.h"
#include <functional>
#include <vector>
#include <string>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#define STB_IMAGE_IMPLEMENTATION_GUARD
#ifndef STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#endif

// Information about an available texture pack
struct TexturePackInfo {
    std::string name;
    std::string folderPath;
    bool hasNormalMaps = false;
    int textureCount = 0;
    bool isBuiltIn = false;  // Procedural pack
    bool isSelected = false;
    GLuint iconTexture = 0;  // Pack icon texture (0 = no custom icon)
};

enum class TexturePackAction {
    NONE,
    DONE,
    PACK_CHANGED
};

class TexturePackScreen {
public:
    MenuUIRenderer* ui = nullptr;
    MenuInputHandler input;
    TexturePackLoader* texturePack = nullptr;  // Reference to the active texture pack

    // Available texture packs
    std::vector<TexturePackInfo> availablePacks;
    int selectedPackIndex = 0;
    std::string currentPackName = "procedural";

    // UI elements
    MenuButton doneButton;
    MenuButton applyButton;
    MenuButton openFolderButton;

    // Scroll state
    float scrollOffset = 0.0f;
    float maxScroll = 0.0f;
    float packEntryHeight = 70.0f;
    float listHeight = 400.0f;

    // Panel dimensions
    float panelWidth = 700.0f;
    float panelHeight = 500.0f;
    float panelX = 0.0f;
    float panelY = 0.0f;

    // Current action
    TexturePackAction currentAction = TexturePackAction::NONE;

    // Status message
    std::string statusMessage = "";
    float statusTimer = 0.0f;

    // Load a texture pack icon from file
    // Returns OpenGL texture ID, or 0 if not found/failed
    GLuint loadPackIcon(const std::string& folderPath) {
        // Try common icon filenames
        std::vector<std::string> iconNames = {"pack.png", "icon.png", "pack_icon.png"};

        for (const auto& iconName : iconNames) {
            std::string iconPath = folderPath + "/" + iconName;
            if (std::filesystem::exists(iconPath)) {
                int width, height, channels;
                stbi_set_flip_vertically_on_load(true);
                unsigned char* data = stbi_load(iconPath.c_str(), &width, &height, &channels, 4);

                if (data) {
                    GLuint textureID;
                    glGenTextures(1, &textureID);
                    glBindTexture(GL_TEXTURE_2D, textureID);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    stbi_image_free(data);
                    return textureID;
                }
            }
        }
        return 0;  // No icon found
    }

    // Clean up loaded icon textures
    void cleanupIcons() {
        for (auto& pack : availablePacks) {
            if (pack.iconTexture != 0) {
                glDeleteTextures(1, &pack.iconTexture);
                pack.iconTexture = 0;
            }
        }
    }

    // Open the textures folder in the system file explorer
    void openTexturesFolder() {
        // Ensure the textures folder exists
        std::filesystem::path texturesDir("assets/textures");
        if (!std::filesystem::exists(texturesDir)) {
            std::filesystem::create_directories(texturesDir);
        }

        // Get absolute path
        std::string absolutePath = std::filesystem::absolute(texturesDir).string();

#ifdef _WIN32
        // Windows: Use ShellExecute to open folder
        ShellExecuteA(NULL, "explore", absolutePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
        // macOS: Use open command
        std::string command = "open \"" + absolutePath + "\"";
        system(command.c_str());
#else
        // Linux: Use xdg-open
        std::string command = "xdg-open \"" + absolutePath + "\"";
        system(command.c_str());
#endif

        statusMessage = "Opened textures folder";
        statusTimer = 2.0f;
    }

    void init(MenuUIRenderer* uiRenderer, TexturePackLoader* loader) {
        ui = uiRenderer;
        texturePack = loader;
        currentPackName = loader ? loader->packName : "procedural";
        setupUI();
        refreshPackList();
    }

    void setupUI() {
        float centerX = ui->windowWidth / 2.0f;
        panelWidth = 700.0f;
        panelHeight = 500.0f;
        panelX = centerX - panelWidth / 2.0f;
        panelY = ui->windowHeight / 2.0f - panelHeight / 2.0f;

        // Done button (bottom right)
        doneButton = {
            panelX + panelWidth - 140, panelY + panelHeight - 55, 120, 40, "DONE",
            [this]() { currentAction = TexturePackAction::DONE; }
        };
        doneButton.textScale = 1.2f;

        // Apply button (next to done)
        applyButton = {
            panelX + panelWidth - 270, panelY + panelHeight - 55, 120, 40, "APPLY",
            [this]() { applySelectedPack(); }
        };
        applyButton.textScale = 1.2f;

        // Open folder button (bottom left)
        openFolderButton = {
            panelX + 20, panelY + panelHeight - 55, 140, 40, "OPEN FOLDER",
            [this]() { openTexturesFolder(); }
        };
        openFolderButton.textScale = 1.1f;

        listHeight = panelHeight - 130;
    }

    void resize(int width, int height) {
        if (ui) {
            ui->resize(width, height);
            setupUI();
        }
    }

    // Scan for available texture packs
    void refreshPackList() {
        // Clean up old icon textures before refreshing
        cleanupIcons();
        availablePacks.clear();

        // Always add the built-in procedural pack first
        TexturePackInfo proceduralPack;
        proceduralPack.name = "Default (Procedural)";
        proceduralPack.folderPath = "";
        proceduralPack.hasNormalMaps = true;
        proceduralPack.textureCount = 24;
        proceduralPack.isBuiltIn = true;
        proceduralPack.isSelected = (currentPackName == "procedural");
        proceduralPack.iconTexture = 0;  // Built-in has no custom icon
        availablePacks.push_back(proceduralPack);

        if (proceduralPack.isSelected) {
            selectedPackIndex = 0;
        }

        // Scan assets/textures/ folder for texture packs
        std::filesystem::path texturesDir("assets/textures");
        if (std::filesystem::exists(texturesDir) && std::filesystem::is_directory(texturesDir)) {
            int idx = 1;
            for (const auto& entry : std::filesystem::directory_iterator(texturesDir)) {
                if (entry.is_directory()) {
                    TexturePackInfo packInfo;
                    packInfo.name = entry.path().filename().string();
                    packInfo.folderPath = entry.path().string();
                    packInfo.isBuiltIn = false;

                    // Count textures and check for normal maps
                    int texCount = 0;
                    int normalCount = 0;
                    for (const auto& file : std::filesystem::directory_iterator(entry.path())) {
                        if (file.is_regular_file()) {
                            std::string ext = file.path().extension().string();
                            std::string stem = file.path().stem().string();
                            if (ext == ".png" || ext == ".PNG") {
                                if (stem.length() > 2 && stem.substr(stem.length() - 2) == "_n") {
                                    normalCount++;
                                } else {
                                    texCount++;
                                }
                            }
                        }
                    }
                    packInfo.textureCount = texCount;
                    packInfo.hasNormalMaps = (normalCount > 0);
                    packInfo.isSelected = (currentPackName == packInfo.name);

                    // Try to load pack icon (pack.png, icon.png, or pack_icon.png)
                    packInfo.iconTexture = loadPackIcon(packInfo.folderPath);

                    if (packInfo.isSelected) {
                        selectedPackIndex = idx;
                    }

                    availablePacks.push_back(packInfo);
                    idx++;
                }
            }
        }

        // Calculate max scroll
        float totalHeight = availablePacks.size() * packEntryHeight;
        maxScroll = std::max(0.0f, totalHeight - listHeight);
    }

    void applySelectedPack() {
        if (!texturePack || selectedPackIndex < 0 || selectedPackIndex >= static_cast<int>(availablePacks.size())) {
            return;
        }

        const auto& pack = availablePacks[selectedPackIndex];

        // Destroy current textures
        texturePack->destroy();

        bool success = false;
        if (pack.isBuiltIn) {
            // Load procedural textures
            texturePack->generateProcedural();
            success = true;
            currentPackName = "procedural";
        } else {
            // Try to load from folder
            success = texturePack->loadFromFolder(pack.folderPath);
            if (!success) {
                // Fall back to procedural
                texturePack->generateProcedural();
                statusMessage = "Failed to load pack, using default";
                statusTimer = 3.0f;
            } else {
                currentPackName = pack.name;
            }
        }

        if (success) {
            statusMessage = "Texture pack applied: " + (pack.isBuiltIn ? "Default" : pack.name);
            statusTimer = 2.0f;
            currentAction = TexturePackAction::PACK_CHANGED;

            // Update selection state
            for (auto& p : availablePacks) {
                p.isSelected = false;
            }
            availablePacks[selectedPackIndex].isSelected = true;
        }
    }

    void update(double mouseX, double mouseY, bool mousePressed, float deltaTime) {
        currentAction = TexturePackAction::NONE;

        input.update(mouseX, mouseY, mousePressed);
        input.handleButton(doneButton);
        input.handleButton(applyButton);
        input.handleButton(openFolderButton);

        // Handle list scrolling
        float listTop = panelY + 80;
        float listBottom = listTop + listHeight;
        if (mouseX >= panelX + 30 && mouseX <= panelX + panelWidth - 30 &&
            mouseY >= listTop && mouseY <= listBottom) {
            // Could add scroll wheel support here
        }

        // Handle pack selection clicks
        if (input.mouseJustPressed) {
            float listX = panelX + 30;
            float listWidth = panelWidth - 60;

            for (size_t i = 0; i < availablePacks.size(); i++) {
                float entryY = listTop + i * packEntryHeight - scrollOffset;

                // Check if entry is visible
                if (entryY + packEntryHeight < listTop || entryY > listBottom) {
                    continue;
                }

                // Check if click is within this entry
                if (mouseX >= listX && mouseX <= listX + listWidth &&
                    mouseY >= entryY && mouseY <= entryY + packEntryHeight - 5) {
                    selectedPackIndex = static_cast<int>(i);
                    break;
                }
            }
        }

        // Update status timer
        if (statusTimer > 0.0f) {
            statusTimer -= deltaTime;
            if (statusTimer <= 0.0f) {
                statusMessage = "";
            }
        }
    }

    void handleScroll(float yoffset) {
        scrollOffset -= yoffset * 30.0f;
        scrollOffset = std::max(0.0f, std::min(scrollOffset, maxScroll));
    }

    void render() {
        if (!ui) return;

        float centerX = ui->windowWidth / 2.0f;

        // Darken background
        ui->drawRect(0, 0, static_cast<float>(ui->windowWidth), static_cast<float>(ui->windowHeight),
                    glm::vec4(0.0f, 0.0f, 0.0f, 0.7f));

        // Panel background
        ui->drawRect(panelX, panelY, panelWidth, panelHeight, MenuColors::PANEL_BG);
        ui->drawRectOutline(panelX, panelY, panelWidth, panelHeight, MenuColors::ACCENT, 2.0f);

        // Title
        ui->drawTextCentered("TEXTURE PACKS", panelX, panelY + 15, panelWidth, MenuColors::ACCENT, 2.0f);

        // Subtitle with current pack
        std::string subtitle = "Current: " + currentPackName;
        ui->drawTextCentered(subtitle, panelX, panelY + 50, panelWidth, MenuColors::TEXT_DIM, 1.0f);

        // List area background
        float listX = panelX + 30;
        float listY = panelY + 80;
        float listWidth = panelWidth - 60;
        ui->drawRect(listX, listY, listWidth, listHeight, glm::vec4(0.0f, 0.0f, 0.0f, 0.3f));

        // Render pack entries
        // Enable scissor test for clipping
        glEnable(GL_SCISSOR_TEST);
        glScissor(static_cast<int>(listX),
                  static_cast<int>(ui->windowHeight - listY - listHeight),
                  static_cast<int>(listWidth),
                  static_cast<int>(listHeight));

        for (size_t i = 0; i < availablePacks.size(); i++) {
            float entryY = listY + i * packEntryHeight - scrollOffset;

            // Skip if not visible
            if (entryY + packEntryHeight < listY || entryY > listY + listHeight) {
                continue;
            }

            const auto& pack = availablePacks[i];
            bool isHovered = (static_cast<int>(i) == selectedPackIndex);

            // Entry background
            glm::vec4 entryBg = isHovered ?
                glm::vec4(0.2f, 0.4f, 0.6f, 0.6f) :
                glm::vec4(0.15f, 0.15f, 0.2f, 0.4f);

            if (pack.isSelected) {
                entryBg = glm::vec4(0.1f, 0.5f, 0.3f, 0.6f);  // Green for active
            }

            ui->drawRect(listX + 5, entryY + 2, listWidth - 10, packEntryHeight - 8, entryBg);

            // Pack icon
            float iconSize = 50.0f;
            float iconX = listX + 15;
            float iconY = entryY + (packEntryHeight - iconSize) / 2;

            if (pack.iconTexture != 0) {
                // Draw custom pack icon
                ui->drawTexture(pack.iconTexture, iconX, iconY, iconSize, iconSize);
            } else {
                // Draw placeholder icon (colored square with pattern)
                glm::vec4 iconColor = pack.isBuiltIn ?
                    glm::vec4(0.4f, 0.6f, 0.9f, 1.0f) :  // Blue for procedural
                    glm::vec4(0.6f, 0.5f, 0.4f, 1.0f);   // Brown for custom
                ui->drawRect(iconX, iconY, iconSize, iconSize, iconColor);

                // Draw a simple checkerboard pattern on placeholder
                ui->drawRect(iconX + 5, iconY + 5, 20, 20, glm::vec4(0.3f, 0.3f, 0.3f, 0.8f));
                ui->drawRect(iconX + 25, iconY + 25, 20, 20, glm::vec4(0.3f, 0.3f, 0.3f, 0.8f));
            }

            // Pack name
            float textX = iconX + iconSize + 15;
            ui->drawText(pack.name, textX, entryY + 15, MenuColors::TEXT, 1.3f);

            // Pack details
            std::string details = std::to_string(pack.textureCount) + " textures";
            if (pack.hasNormalMaps) {
                details += " + normal maps";
            }
            if (pack.isBuiltIn) {
                details += " (built-in)";
            }
            ui->drawText(details, textX, entryY + 38, MenuColors::TEXT_DIM, 0.9f);

            // Selected indicator
            if (pack.isSelected) {
                ui->drawText("[ACTIVE]", listX + listWidth - 100, entryY + 25,
                            MenuColors::SUCCESS, 1.0f);
            }
        }

        glDisable(GL_SCISSOR_TEST);

        // Scroll indicator if needed
        if (maxScroll > 0) {
            float scrollBarHeight = listHeight * (listHeight / (listHeight + maxScroll));
            float scrollBarY = listY + (scrollOffset / maxScroll) * (listHeight - scrollBarHeight);
            ui->drawRect(listX + listWidth - 8, scrollBarY, 6, scrollBarHeight,
                        glm::vec4(0.5f, 0.5f, 0.5f, 0.5f));
        }

        // Status message
        if (!statusMessage.empty()) {
            ui->drawTextCentered(statusMessage, panelX, panelY + panelHeight - 85, panelWidth,
                                MenuColors::SUCCESS, 1.0f);
        }

        // Buttons
        openFolderButton.render(*ui);
        applyButton.render(*ui);
        doneButton.render(*ui);
    }

    TexturePackAction getAction() const {
        return currentAction;
    }

    // Get selected pack folder path (for loading)
    std::string getSelectedPackPath() const {
        if (selectedPackIndex >= 0 && selectedPackIndex < static_cast<int>(availablePacks.size())) {
            return availablePacks[selectedPackIndex].folderPath;
        }
        return "";
    }
};
