#pragma once

// World Creation Screen
// Allows player to configure world generation parameters

#include "MenuUI.h"
#include "../world/WorldPresets.h"
#include "../world/TerraMath.h"
#include <vector>
#include <string>

enum class WorldCreateAction {
    NONE,
    BACK,
    CREATE_WORLD,
    LOAD_PRESET,
    SAVE_PRESET
};

class WorldCreateScreen {
public:
    MenuUIRenderer* ui = nullptr;
    MenuInputHandler input;

    // World settings being configured
    WorldSettings settings;

    // UI Elements
    MenuTextInput worldNameInput;
    MenuTextInput seedInput;
    MenuDropdown generationTypeDropdown;

    MenuSlider maxHeightSlider;
    MenuSlider minBiomeSizeSlider;
    MenuSlider maxBiomeSizeSlider;

    MenuSlider continentScaleSlider;
    MenuSlider mountainScaleSlider;
    MenuSlider detailScaleSlider;

    MenuTextInput equationInput;
    MenuButton validateEquationButton;
    std::string equationValidationMessage;
    bool equationValid = true;

    MenuDropdown presetDropdown;
    MenuButton loadPresetButton;
    MenuButton savePresetButton;
    MenuTextInput presetNameInput;

    MenuButton backButton;
    MenuButton createWorldButton;

    // Dropdown collection for z-ordering
    std::vector<MenuDropdown*> allDropdowns;

    // Current action
    WorldCreateAction currentAction = WorldCreateAction::NONE;
    float deltaTime = 0.016f;

    // TerraMath for equation validation
    TerraMath::ExpressionParser expressionParser;

    void init(MenuUIRenderer* uiRenderer) {
        ui = uiRenderer;
        settings = WorldSettings();
        loadPresetList();
        setupUI();
    }

    void loadPresetList() {
        // Get list of presets
        auto presetNames = PresetManager::listPresets();
        if (presetNames.empty()) {
            // Add built-in presets
            PresetManager::createDefaultPresetFiles();
            presetNames = PresetManager::listPresets();
        }
        presetDropdown.options = presetNames.empty() ?
            std::vector<std::string>{"default", "amplified", "superflat", "mountains", "islands", "caves"} :
            presetNames;
    }

    void setupUI() {
        float panelX = 100;
        float panelWidth = ui->windowWidth - 200;
        float col1X = panelX + 30;
        float col2X = panelX + panelWidth / 2 + 30;
        float inputWidth = panelWidth / 2 - 90;
        float startY = 130;
        float rowHeight = 75;
        int row = 0;

        // World Name
        worldNameInput = {
            col1X, startY + row * rowHeight, inputWidth, 40,
            "World Name",
            settings.worldName,
            "New World",
            [this](const std::string& text) { settings.worldName = text; }
        };
        worldNameInput.maxLength = 32;

        // Seed
        seedInput = {
            col2X, startY + row * rowHeight, inputWidth, 40,
            "Seed (empty = random)",
            settings.seed,
            "Leave empty for random",
            [this](const std::string& text) { settings.seed = text; }
        };
        seedInput.maxLength = 20;
        row++;

        // Generation Type
        generationTypeDropdown = {
            col1X, startY + row * rowHeight, inputWidth, 40,
            "Generation Type",
            getGenerationTypeNames(),
            static_cast<int>(settings.generationType),
            [this](int idx) {
                settings.generationType = static_cast<GenerationType>(idx);
                updateEquationVisibility();
            }
        };
        allDropdowns.push_back(&generationTypeDropdown);

        // Preset Dropdown
        presetDropdown = {
            col2X, startY + row * rowHeight, inputWidth - 120, 40,
            "Load Preset",
            {"default", "amplified", "superflat", "mountains", "islands", "caves"},
            0,
            nullptr
        };
        allDropdowns.push_back(&presetDropdown);

        loadPresetButton = {
            col2X + inputWidth - 110, startY + row * rowHeight + 24, 110, 40,
            "LOAD",
            [this]() { loadSelectedPreset(); }
        };
        loadPresetButton.textScale = 1.0f;
        row++;

        // Max Y Height
        maxHeightSlider = {
            col1X, startY + row * rowHeight + 10, inputWidth - 50, 28,
            "Max Y Height",
            64.0f, 512.0f, static_cast<float>(settings.maxYHeight),
            [this](float val) { settings.maxYHeight = static_cast<int>(val); }
        };
        row++;

        // Biome Size
        minBiomeSizeSlider = {
            col1X, startY + row * rowHeight + 10, inputWidth / 2 - 40, 28,
            "Min Biome Size (chunks)",
            1.0f, 16.0f, static_cast<float>(settings.minBiomeSize),
            [this](float val) {
                settings.minBiomeSize = static_cast<int>(val);
                if (settings.minBiomeSize > settings.maxBiomeSize) {
                    settings.maxBiomeSize = settings.minBiomeSize;
                    maxBiomeSizeSlider.value = static_cast<float>(settings.maxBiomeSize);
                }
            }
        };

        maxBiomeSizeSlider = {
            col1X + inputWidth / 2, startY + row * rowHeight + 10, inputWidth / 2 - 40, 28,
            "Max Biome Size (chunks)",
            1.0f, 32.0f, static_cast<float>(settings.maxBiomeSize),
            [this](float val) {
                settings.maxBiomeSize = static_cast<int>(val);
                if (settings.maxBiomeSize < settings.minBiomeSize) {
                    settings.minBiomeSize = settings.maxBiomeSize;
                    minBiomeSizeSlider.value = static_cast<float>(settings.minBiomeSize);
                }
            }
        };
        row++;

        // Scale Parameters - spread across full width with more spacing
        float scaleSliderWidth = (panelWidth - 180) / 3;  // Use full panel width
        float scaleSliderGap = 60.0f;  // Gap between sliders for value display

        continentScaleSlider = {
            col1X, startY + row * rowHeight + 10, scaleSliderWidth - 40, 28,
            "Continent",
            0.0f, 100.0f, settings.continentScale,
            [this](float val) { settings.continentScale = val; }
        };
        continentScaleSlider.showIntValue = false;

        mountainScaleSlider = {
            col1X + scaleSliderWidth + scaleSliderGap, startY + row * rowHeight + 10, scaleSliderWidth - 40, 28,
            "Mountain",
            0.0f, 150.0f, settings.mountainScale,
            [this](float val) { settings.mountainScale = val; }
        };
        mountainScaleSlider.showIntValue = false;

        detailScaleSlider = {
            col1X + 2 * (scaleSliderWidth + scaleSliderGap), startY + row * rowHeight + 10, scaleSliderWidth - 40, 28,
            "Detail",
            0.0f, 20.0f, settings.detailScale,
            [this](float val) { settings.detailScale = val; }
        };
        detailScaleSlider.showIntValue = false;
        row++;

        // Custom Equation (only visible when CUSTOM_EQUATION selected)
        equationInput = {
            col1X, startY + row * rowHeight, panelWidth - 200, 40,
            "Custom Terrain Equation",
            settings.customEquation,
            "baseHeight + continent*20 + mountain*30 + detail*4",
            [this](const std::string& text) {
                settings.customEquation = text;
                validateEquation();
            }
        };
        equationInput.maxLength = 256;

        validateEquationButton = {
            col1X + panelWidth - 190, startY + row * rowHeight + 24, 100, 40,
            "VALIDATE",
            [this]() { validateEquation(); }
        };
        validateEquationButton.textScale = 0.9f;
        row++;

        // Save Preset
        presetNameInput = {
            col1X, startY + row * rowHeight, inputWidth - 120, 40,
            "Save As Preset",
            "",
            "Enter preset name",
            nullptr
        };
        presetNameInput.maxLength = 32;

        savePresetButton = {
            col1X + inputWidth - 110, startY + row * rowHeight + 24, 110, 40,
            "SAVE",
            [this]() { saveCurrentPreset(); }
        };
        savePresetButton.textScale = 1.0f;
        row += 2;

        // Bottom buttons
        float bottomY = ui->windowHeight - 80;
        backButton = {
            panelX + 40, bottomY, 150, 50,
            "BACK",
            [this]() { currentAction = WorldCreateAction::BACK; }
        };
        backButton.textScale = 1.3f;

        createWorldButton = {
            panelX + panelWidth - 190, bottomY, 200, 50,
            "CREATE WORLD",
            [this]() {
                if (settings.generationType == GenerationType::CUSTOM_EQUATION && !equationValid) {
                    return;  // Don't create with invalid equation
                }
                settings.computeSeed();
                currentAction = WorldCreateAction::CREATE_WORLD;
            }
        };
        createWorldButton.textScale = 1.3f;

        updateEquationVisibility();
    }

    void updateEquationVisibility() {
        bool showEquation = (settings.generationType == GenerationType::CUSTOM_EQUATION);
        equationInput.visible = showEquation;
        validateEquationButton.visible = showEquation;
    }

    void validateEquation() {
        std::string error = expressionParser.validate(settings.customEquation);
        if (error.empty()) {
            equationValidationMessage = "Equation is valid!";
            equationValid = true;
        } else {
            equationValidationMessage = "Error: " + error;
            equationValid = false;
        }
    }

    void loadSelectedPreset() {
        if (presetDropdown.selectedIndex >= 0 &&
            presetDropdown.selectedIndex < static_cast<int>(presetDropdown.options.size())) {
            std::string presetName = presetDropdown.options[presetDropdown.selectedIndex];
            GenerationPreset preset = PresetManager::loadFromFile(presetName);
            preset.applyToSettings(settings);

            // Update UI to reflect loaded settings
            generationTypeDropdown.selectedIndex = static_cast<int>(settings.generationType);
            maxHeightSlider.value = static_cast<float>(settings.maxYHeight);
            minBiomeSizeSlider.value = static_cast<float>(settings.minBiomeSize);
            maxBiomeSizeSlider.value = static_cast<float>(settings.maxBiomeSize);
            continentScaleSlider.value = settings.continentScale;
            mountainScaleSlider.value = settings.mountainScale;
            detailScaleSlider.value = settings.detailScale;
            equationInput.text = settings.customEquation;
            equationInput.cursorPos = settings.customEquation.length();

            updateEquationVisibility();
            if (settings.generationType == GenerationType::CUSTOM_EQUATION) {
                validateEquation();
            }
        }
    }

    void saveCurrentPreset() {
        if (presetNameInput.text.empty()) {
            return;
        }

        GenerationPreset preset;
        preset.name = presetNameInput.text;
        preset.description = "Custom preset";
        preset.type = settings.generationType;
        preset.baseHeight = settings.baseHeight;
        preset.seaLevel = settings.seaLevel;
        preset.maxHeight = settings.maxYHeight;
        preset.continentScale = settings.continentScale;
        preset.mountainScale = settings.mountainScale;
        preset.detailScale = settings.detailScale;
        preset.minBiomeChunks = settings.minBiomeSize;
        preset.maxBiomeChunks = settings.maxBiomeSize;
        preset.customEquation = settings.customEquation;

        if (PresetManager::saveToFile(preset)) {
            // Refresh preset list
            loadPresetList();
            presetNameInput.text = "";
            presetNameInput.cursorPos = 0;
        }
    }

    void resize(int width, int height) {
        if (ui) {
            ui->resize(width, height);
            setupUI();
        }
    }

    void update(double mouseX, double mouseY, bool mousePressed, float dt) {
        currentAction = WorldCreateAction::NONE;
        deltaTime = dt;

        input.update(mouseX, mouseY, mousePressed);

        // Handle buttons
        input.handleButton(backButton);
        input.handleButton(createWorldButton);
        input.handleButton(loadPresetButton);
        input.handleButton(savePresetButton);
        if (validateEquationButton.visible) {
            input.handleButton(validateEquationButton);
        }

        // Handle text inputs
        input.handleTextInput(worldNameInput);
        input.handleTextInput(seedInput);
        input.handleTextInput(presetNameInput);
        if (equationInput.visible) {
            input.handleTextInput(equationInput);
        }

        // Handle sliders
        input.handleSlider(maxHeightSlider);
        input.handleSlider(minBiomeSizeSlider);
        input.handleSlider(maxBiomeSizeSlider);
        input.handleSlider(continentScaleSlider);
        input.handleSlider(mountainScaleSlider);
        input.handleSlider(detailScaleSlider);

        // Handle dropdowns
        input.handleDropdown(generationTypeDropdown, allDropdowns);
        input.handleDropdown(presetDropdown, allDropdowns);
    }

    void handleKeyInput(int key, int action, int mods) {
        if (input.focusedInput) {
            input.focusedInput->handleKeyInput(key, action, mods);
        }
    }

    void handleCharInput(unsigned int codepoint) {
        if (input.focusedInput) {
            input.focusedInput->handleCharInput(codepoint);
        }
    }

    void render() {
        if (!ui) return;

        float panelX = 100;
        float panelWidth = ui->windowWidth - 200;
        float panelHeight = ui->windowHeight - 100;

        // Panel background
        ui->drawRect(panelX, 50, panelWidth, panelHeight, MenuColors::PANEL_BG);
        ui->drawRectOutline(panelX, 50, panelWidth, panelHeight, MenuColors::DIVIDER, 2.0f);

        // Title
        ui->drawTextCentered("CREATE NEW WORLD", panelX, 70, panelWidth, MenuColors::ACCENT, 2.0f);

        // Divider under title
        ui->drawRect(panelX + 40, 115, panelWidth - 80, 2, MenuColors::DIVIDER);

        // Input fields
        worldNameInput.render(*ui, deltaTime);
        seedInput.render(*ui, deltaTime);

        // Dropdowns (main box only)
        generationTypeDropdown.render(*ui);
        presetDropdown.render(*ui);
        loadPresetButton.render(*ui);

        // Sliders
        maxHeightSlider.render(*ui);
        minBiomeSizeSlider.render(*ui);
        maxBiomeSizeSlider.render(*ui);
        continentScaleSlider.render(*ui);
        mountainScaleSlider.render(*ui);
        detailScaleSlider.render(*ui);

        // Custom equation (if visible)
        if (equationInput.visible) {
            equationInput.render(*ui, deltaTime);
            validateEquationButton.render(*ui);

            // Validation message
            if (!equationValidationMessage.empty()) {
                glm::vec4 msgColor = equationValid ? MenuColors::SUCCESS : MenuColors::ERROR;
                ui->drawText(equationValidationMessage,
                            equationInput.x, equationInput.y + equationInput.height + 5,
                            msgColor, 0.9f);
            }
        }

        // Save preset
        presetNameInput.render(*ui, deltaTime);
        savePresetButton.render(*ui);

        // Bottom buttons
        backButton.render(*ui);
        createWorldButton.render(*ui);

        // Render dropdown options last (on top of everything)
        generationTypeDropdown.renderOptions(*ui);
        presetDropdown.renderOptions(*ui);
    }

    WorldCreateAction getAction() const {
        return currentAction;
    }

    const WorldSettings& getSettings() const {
        return settings;
    }
};
