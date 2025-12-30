#pragma once

// Settings Menu
// In-game settings accessible from pause menu or main menu
// Matches all settings available in the launcher

#include "MenuUI.h"
#include "../core/Config.h"
#include <functional>
#include <algorithm>
#include <fstream>
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#endif

enum class SettingsAction {
    NONE,
    BACK,
    APPLY
};

enum class SettingsTab {
    GRAPHICS,
    EFFECTS,
    PERFORMANCE,
    CONTROLS,
    AUDIO,
    TITLE_SCREEN
};

class SettingsMenu {
public:
    MenuUIRenderer* ui = nullptr;
    MenuInputHandler input;

    // Current tab
    SettingsTab currentTab = SettingsTab::GRAPHICS;

    // Tab buttons
    MenuButton graphicsTabBtn;
    MenuButton effectsTabBtn;
    MenuButton performanceTabBtn;
    MenuButton controlsTabBtn;
    MenuButton audioTabBtn;
    MenuButton titleScreenTabBtn;

    // === GRAPHICS TAB ===
    MenuDropdown graphicsPresetDropdown;
    MenuSlider renderDistanceSlider;
    MenuSlider fovSlider;
    MenuDropdown guiScaleDropdown;

    // GUI Scale values: Small=0.75, Normal=1.0, Large=1.5, Extra Large=2.0
    static constexpr float GUI_SCALE_VALUES[] = {0.75f, 1.0f, 1.5f, 2.0f};
    static constexpr int GUI_SCALE_COUNT = 4;
    MenuCheckbox vsyncCheckbox;
    MenuCheckbox fullscreenCheckbox;
    MenuDropdown aaDropdown;
    MenuDropdown textureQualityDropdown;
    MenuDropdown anisotropicDropdown;

    // === EFFECTS TAB ===
    MenuDropdown shadowQualityDropdown;
    MenuDropdown aoQualityDropdown;
    MenuCheckbox bloomCheckbox;
    MenuSlider bloomIntensitySlider;
    MenuSlider brightnessSlider;
    MenuCheckbox motionBlurCheckbox;
    MenuDropdown upscaleDropdown;
    MenuCheckbox waterAnimationCheckbox;
    MenuCheckbox cloudsCheckbox;
    MenuDropdown cloudQualityDropdown;
    MenuCheckbox volumetricCloudsCheckbox;

    // === PERFORMANCE TAB ===
    MenuCheckbox hiZCheckbox;
    MenuCheckbox batchedRenderingCheckbox;
    MenuSlider chunkSpeedSlider;
    MenuSlider meshSpeedSlider;

    // === CONTROLS TAB ===
    MenuSlider sensitivitySlider;
    MenuCheckbox invertYCheckbox;
    MenuButton configureControlsButton;
    bool openControlsScreen = false;

    // === AUDIO TAB === (placeholder)
    MenuSlider masterVolumeSlider;
    MenuSlider musicVolumeSlider;
    MenuSlider sfxVolumeSlider;

    // === TITLE SCREEN TAB ===
    MenuDropdown titleSourceDropdown;
    MenuTextInput titleSeedInput;
    MenuSlider titleRenderDistSlider;
    MenuDropdown titleWorldDropdown;
    std::vector<std::string> savedWorldNames;
    std::vector<std::string> savedWorldPaths;

    // Action buttons
    MenuButton backButton;
    MenuButton applyButton;

    // All dropdowns for z-ordering
    std::vector<MenuDropdown*> allDropdowns;

    // Current action
    SettingsAction currentAction = SettingsAction::NONE;

    // "APPLIED" feedback animation
    bool showAppliedFeedback = false;
    float appliedFeedbackTimer = 0.0f;
    static constexpr float APPLIED_FEEDBACK_DURATION = 2.5f;  // seconds (longer for visibility)

    // Delta time for text input rendering
    float currentDeltaTime = 0.016f;

    // Tooltip system
    std::string currentTooltip;
    float tooltipMouseX = 0.0f;
    float tooltipMouseY = 0.0f;

    // VRAM monitoring
    int totalVRAM_MB = 0;
    int availableVRAM_MB = 0;
    int usedVRAM_MB = 0;

    void init(MenuUIRenderer* uiRenderer) {
        ui = uiRenderer;
        loadFromConfig();
        setupUI();
        queryVRAM();  // Initial VRAM query
    }

    // Query current VRAM usage from GPU
    void queryVRAM() {
        // NVIDIA: GL_NVX_gpu_memory_info
        GLint totalKB = 0, availKB = 0;
        glGetIntegerv(0x9048, &totalKB);  // GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX
        if (totalKB > 0) {
            totalVRAM_MB = totalKB / 1024;
            glGetIntegerv(0x9049, &availKB);  // GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX
            availableVRAM_MB = availKB / 1024;
            usedVRAM_MB = totalVRAM_MB - availableVRAM_MB;
        } else {
            // AMD: GL_ATI_meminfo (VBO free memory)
            GLint vboFreeKB[4] = {0};
            glGetIntegerv(0x87FB, vboFreeKB);  // GL_VBO_FREE_MEMORY_ATI
            if (vboFreeKB[0] > 0) {
                availableVRAM_MB = vboFreeKB[0] / 1024;
                // AMD doesn't provide total, use hardware detection
                totalVRAM_MB = g_hardware.vramMB;
                usedVRAM_MB = totalVRAM_MB - availableVRAM_MB;
            } else {
                // Fallback to hardware detection
                totalVRAM_MB = g_hardware.vramMB;
                availableVRAM_MB = 0;
                usedVRAM_MB = 0;
            }
        }
    }

    void loadFromConfig() {
        // Load current values from config
    }

    void setupUI() {
        allDropdowns.clear();

        float centerX = ui->windowWidth / 2.0f;
        float panelWidth = 750.0f;
        float panelHeight = 580.0f;
        float panelX = centerX - panelWidth / 2.0f;
        float panelY = ui->windowHeight / 2.0f - panelHeight / 2.0f;

        float tabWidth = 110.0f;  // Narrower to fit 6 tabs
        float tabHeight = 36.0f;
        float tabY = panelY + 50.0f;
        float tabSpacing = 6.0f;

        // Tab buttons (6 tabs)
        float tabStartX = panelX + 20;

        graphicsTabBtn = {
            tabStartX, tabY, tabWidth, tabHeight, "GRAPHICS",
            [this]() { currentTab = SettingsTab::GRAPHICS; }
        };
        graphicsTabBtn.textScale = 0.85f;

        effectsTabBtn = {
            tabStartX + (tabWidth + tabSpacing), tabY, tabWidth, tabHeight, "EFFECTS",
            [this]() { currentTab = SettingsTab::EFFECTS; }
        };
        effectsTabBtn.textScale = 0.85f;

        performanceTabBtn = {
            tabStartX + 2 * (tabWidth + tabSpacing), tabY, tabWidth, tabHeight, "PERFORM",
            [this]() { currentTab = SettingsTab::PERFORMANCE; }
        };
        performanceTabBtn.textScale = 0.85f;

        controlsTabBtn = {
            tabStartX + 3 * (tabWidth + tabSpacing), tabY, tabWidth, tabHeight, "CONTROLS",
            [this]() { currentTab = SettingsTab::CONTROLS; }
        };
        controlsTabBtn.textScale = 0.85f;

        audioTabBtn = {
            tabStartX + 4 * (tabWidth + tabSpacing), tabY, tabWidth, tabHeight, "AUDIO",
            [this]() { currentTab = SettingsTab::AUDIO; }
        };
        audioTabBtn.textScale = 0.85f;

        titleScreenTabBtn = {
            tabStartX + 5 * (tabWidth + tabSpacing), tabY, tabWidth, tabHeight, "TITLE",
            [this]() { currentTab = SettingsTab::TITLE_SCREEN; }
        };
        titleScreenTabBtn.textScale = 0.85f;

        float contentY = tabY + tabHeight + 35;  // More space after tabs
        float col1X = panelX + 30;
        float col2X = panelX + panelWidth / 2 + 20;  // More separation between columns
        float sliderWidth = 200.0f;  // Slightly narrower to fit better
        float dropdownWidth = 180.0f;
        float rowSpacing = 70.0f;  // More vertical space between rows

        // === GRAPHICS TAB ===
        graphicsPresetDropdown = {
            col1X, contentY, dropdownWidth, 32, "Graphics Preset",
            {"Low", "Medium", "High", "Ultra", "Custom"},
            static_cast<int>(g_config.graphicsPreset),
            [](int idx) { g_config.graphicsPreset = static_cast<GraphicsPreset>(idx); }
        };
        allDropdowns.push_back(&graphicsPresetDropdown);

        renderDistanceSlider = {
            col2X, contentY + 5, sliderWidth, 26, "Render Distance",
            4.0f, 48.0f, static_cast<float>(g_config.renderDistance),
            [](float val) { g_config.renderDistance = static_cast<int>(val); }
        };

        fovSlider = {
            col1X, contentY + rowSpacing + 5, sliderWidth, 26, "Field of View",
            50.0f, 120.0f, static_cast<float>(g_config.fov),
            [](float val) { g_config.fov = static_cast<int>(val); }
        };

        // Find current GUI scale index
        int guiScaleIndex = 1;  // Default to "Normal" (1.0)
        for (int i = 0; i < GUI_SCALE_COUNT; i++) {
            if (std::abs(g_config.guiScale - GUI_SCALE_VALUES[i]) < 0.1f) {
                guiScaleIndex = i;
                break;
            }
        }

        guiScaleDropdown = {
            col2X, contentY + rowSpacing + 5, sliderWidth, 26, "GUI Scale",
            {"Small (75%)", "Normal (100%)", "Large (150%)", "Extra Large (200%)"},
            guiScaleIndex,
            [this](int idx) {
                if (idx >= 0 && idx < GUI_SCALE_COUNT) {
                    g_config.guiScale = GUI_SCALE_VALUES[idx];
                }
            }
        };
        allDropdowns.push_back(&guiScaleDropdown);

        vsyncCheckbox = {
            col1X, contentY + rowSpacing * 2, 24, "VSync", g_config.vsync,
            [](bool val) { g_config.vsync = val; }
        };

        fullscreenCheckbox = {
            col2X, contentY + rowSpacing * 2, 24, "Fullscreen", g_config.fullscreen,
            [](bool val) { g_config.fullscreen = val; }
        };

        textureQualityDropdown = {
            col1X, contentY + rowSpacing * 3 - 5, dropdownWidth, 32, "Texture Quality",
            {"Low", "Medium", "High", "Ultra"},
            static_cast<int>(g_config.textureQuality),
            [](int idx) { g_config.textureQuality = static_cast<TextureQuality>(idx); }
        };
        allDropdowns.push_back(&textureQualityDropdown);

        aaDropdown = {
            col2X, contentY + rowSpacing * 3 - 5, dropdownWidth, 32, "Anti-Aliasing",
            {"Off", "FXAA", "MSAA 2x", "MSAA 4x", "MSAA 8x"},
            static_cast<int>(g_config.antiAliasing),
            [](int idx) { g_config.antiAliasing = static_cast<AntiAliasMode>(idx); }
        };
        allDropdowns.push_back(&aaDropdown);

        // Map dropdown index to anisotropic values: 0=1, 1=2, 2=4, 3=8, 4=16
        int anisoIndex = 0;
        if (g_config.anisotropicFiltering >= 16) anisoIndex = 4;
        else if (g_config.anisotropicFiltering >= 8) anisoIndex = 3;
        else if (g_config.anisotropicFiltering >= 4) anisoIndex = 2;
        else if (g_config.anisotropicFiltering >= 2) anisoIndex = 1;

        anisotropicDropdown = {
            col1X, contentY + rowSpacing * 4 - 5, dropdownWidth, 32, "Anisotropic Filter",
            {"Off", "2x", "4x", "8x", "16x"},
            anisoIndex,
            [](int idx) {
                static const int anisoValues[] = {1, 2, 4, 8, 16};
                g_config.anisotropicFiltering = anisoValues[idx];
            }
        };
        allDropdowns.push_back(&anisotropicDropdown);

        // === EFFECTS TAB ===
        shadowQualityDropdown = {
            col1X, contentY, dropdownWidth, 32, "Shadow Quality",
            {"Off", "Low", "Medium", "High", "Ultra"},
            static_cast<int>(g_config.shadowQuality),
            [](int idx) { g_config.shadowQuality = static_cast<ShadowQuality>(idx); }
        };
        allDropdowns.push_back(&shadowQualityDropdown);

        aoQualityDropdown = {
            col2X, contentY, dropdownWidth, 32, "Ambient Occlusion",
            {"Off", "Low", "Medium", "High", "Ultra"},
            static_cast<int>(g_config.aoQuality),
            [](int idx) { g_config.aoQuality = static_cast<AOQuality>(idx); }
        };
        allDropdowns.push_back(&aoQualityDropdown);

        bloomCheckbox = {
            col1X, contentY + rowSpacing, 24, "Bloom", g_config.enableBloom,
            [](bool val) { g_config.enableBloom = val; }
        };

        bloomIntensitySlider = {
            col2X, contentY + rowSpacing + 5, sliderWidth, 26, "Bloom Intensity",
            0.0f, 100.0f, g_config.bloomIntensity * 100.0f,
            [](float val) { g_config.bloomIntensity = val / 100.0f; }
        };

        motionBlurCheckbox = {
            col1X, contentY + rowSpacing * 2, 24, "Motion Blur", g_config.enableMotionBlur,
            [](bool val) { g_config.enableMotionBlur = val; }
        };

        upscaleDropdown = {
            col2X, contentY + rowSpacing * 2 - 5, 210.0f, 32, "FSR Upscaling",  // Wider to fit options
            {"Native (1.0x)", "Quality (1.5x)", "Balanced (1.7x)", "Performance (2.0x)"},
            static_cast<int>(g_config.upscaleMode),
            [](int idx) { g_config.upscaleMode = static_cast<UpscaleMode>(idx); }
        };
        upscaleDropdown.tooltip = "AMD FidelityFX upscaling - render at lower res for better FPS";
        allDropdowns.push_back(&upscaleDropdown);

        waterAnimationCheckbox = {
            col1X, contentY + rowSpacing * 3, 24, "Water Animation", g_config.enableWaterAnimation,
            [](bool val) { g_config.enableWaterAnimation = val; }
        };

        cloudsCheckbox = {
            col2X, contentY + rowSpacing * 3, 24, "Clouds", g_config.enableClouds,
            [](bool val) { g_config.enableClouds = val; }
        };

        cloudQualityDropdown = {
            col1X, contentY + rowSpacing * 4 - 5, dropdownWidth, 32, "Cloud Quality",
            {"Very Low", "Low", "Medium", "High"},
            static_cast<int>(g_config.cloudQuality),
            [](int idx) { g_config.cloudQuality = static_cast<CloudQuality>(idx); }
        };
        allDropdowns.push_back(&cloudQualityDropdown);

        volumetricCloudsCheckbox = {
            col2X, contentY + rowSpacing * 4, 24, "Volumetric [Experimental]",
            g_config.cloudStyle == CloudStyle::VOLUMETRIC,
            [](bool val) { g_config.cloudStyle = val ? CloudStyle::VOLUMETRIC : CloudStyle::SIMPLE; }
        };

        brightnessSlider = {
            col1X, contentY + rowSpacing * 5 + 5, sliderWidth + 80, 26, "Brightness",
            50.0f, 150.0f, g_config.brightness * 100.0f,
            [](float val) { g_config.brightness = val / 100.0f; }
        };

        // === PERFORMANCE TAB ===
        hiZCheckbox = {
            col1X, contentY, 24, "Hi-Z Occlusion Culling", g_config.enableHiZCulling,
            [](bool val) { g_config.enableHiZCulling = val; }
        };

        batchedRenderingCheckbox = {
            col2X, contentY, 24, "Batched Rendering", g_config.enableBatchedRendering,
            [](bool val) { g_config.enableBatchedRendering = val; }
        };

        chunkSpeedSlider = {
            col1X, contentY + rowSpacing + 5, sliderWidth, 26, "Chunks per Frame",
            1.0f, 32.0f, static_cast<float>(g_config.maxChunksPerFrame),
            [](float val) { g_config.maxChunksPerFrame = static_cast<int>(val); }
        };

        meshSpeedSlider = {
            col2X, contentY + rowSpacing + 5, sliderWidth, 26, "Meshes per Frame",
            1.0f, 32.0f, static_cast<float>(g_config.maxMeshesPerFrame),
            [](float val) { g_config.maxMeshesPerFrame = static_cast<int>(val); }
        };

        // === CONTROLS TAB ===
        sensitivitySlider = {
            col1X, contentY + 5, sliderWidth, 26, "Mouse Sensitivity",
            1.0f, 100.0f, g_config.mouseSensitivity * 100.0f,
            [](float val) { g_config.mouseSensitivity = val / 100.0f; }
        };

        invertYCheckbox = {
            col1X, contentY + rowSpacing, 24, "Invert Y-Axis", g_config.invertY,
            [](bool val) { g_config.invertY = val; }
        };

        configureControlsButton = {
            col1X, contentY + rowSpacing * 2, 250, 40, "Key Bindings...",
            [this]() { openControlsScreen = true; }
        };
        configureControlsButton.textScale = 1.1f;

        // === AUDIO TAB ===
        masterVolumeSlider = {
            col1X, contentY + 5, sliderWidth, 26, "Master Volume",
            0.0f, 100.0f, 100.0f,
            nullptr
        };

        musicVolumeSlider = {
            col1X, contentY + rowSpacing + 5, sliderWidth, 26, "Music Volume",
            0.0f, 100.0f, 50.0f,
            nullptr
        };

        sfxVolumeSlider = {
            col1X, contentY + rowSpacing * 2 + 5, sliderWidth, 26, "SFX Volume",
            0.0f, 100.0f, 80.0f,
            nullptr
        };

        // === TITLE SCREEN TAB ===
        // Load saved worlds list
        loadSavedWorldsList();

        titleSourceDropdown = {
            col1X, contentY, dropdownWidth + 40, 32, "Background Source",
            {"Random Each Launch", "Custom Seed", "Saved World"},
            static_cast<int>(g_config.titleScreen.sourceMode),
            [this](int idx) {
                g_config.titleScreen.sourceMode = static_cast<TitleScreenSource>(idx);
            }
        };
        allDropdowns.push_back(&titleSourceDropdown);

        titleSeedInput = {
            col1X, contentY + rowSpacing, sliderWidth + 80, 40,
            "Custom Seed",
            g_config.titleScreen.customSeed,
            "Enter seed...",
            [](const std::string& text) { g_config.titleScreen.customSeed = text; }
        };
        titleSeedInput.maxLength = 20;

        titleWorldDropdown = {
            col1X, contentY + rowSpacing * 2, dropdownWidth + 80, 32, "Saved World",
            savedWorldNames.empty() ? std::vector<std::string>{"(No saved worlds)"} : savedWorldNames,
            0,
            [this](int idx) {
                if (idx >= 0 && idx < static_cast<int>(savedWorldPaths.size())) {
                    g_config.titleScreen.savedWorldPath = savedWorldPaths[idx];
                }
            }
        };
        allDropdowns.push_back(&titleWorldDropdown);

        titleRenderDistSlider = {
            col1X, contentY + rowSpacing * 3 + 5, sliderWidth, 26, "Title Render Distance",
            32.0f, 64.0f, static_cast<float>(g_config.titleScreen.renderDistance),
            [](float val) { g_config.titleScreen.renderDistance = static_cast<int>(val); }
        };

        // Action buttons
        float btnY = panelY + panelHeight - 55;
        backButton = {
            panelX + 30, btnY, 110, 40, "BACK",
            [this]() { currentAction = SettingsAction::BACK; }
        };
        backButton.textScale = 1.1f;

        applyButton = {
            panelX + panelWidth - 140, btnY, 110, 40, "APPLY",
            [this]() {
                g_config.save();
                currentAction = SettingsAction::APPLY;
                // Trigger APPLIED feedback animation
                showAppliedFeedback = true;
                appliedFeedbackTimer = APPLIED_FEEDBACK_DURATION;
            }
        };
        applyButton.textScale = 1.1f;
    }

    void resize(int width, int height) {
        if (ui) {
            ui->resize(width, height);
            setupUI();
        }
    }

    void loadSavedWorldsList() {
        savedWorldNames.clear();
        savedWorldPaths.clear();

        // Scan saves directory for worlds
        std::string savesPath = "saves";
        #ifdef _WIN32
        WIN32_FIND_DATAA findData;
        HANDLE hFind = FindFirstFileA((savesPath + "/*").c_str(), &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    std::string name = findData.cFileName;
                    if (name != "." && name != "..") {
                        std::string worldPath = savesPath + "/" + name;
                        std::string metaPath = worldPath + "/world.meta";
                        std::ifstream metaFile(metaPath);
                        if (metaFile.is_open()) {
                            savedWorldNames.push_back(name);
                            savedWorldPaths.push_back(worldPath);
                            metaFile.close();
                        }
                    }
                }
            } while (FindNextFileA(hFind, &findData));
            FindClose(hFind);
        }
        #endif

        if (savedWorldNames.empty()) {
            savedWorldNames.push_back("(No saved worlds)");
        }
    }

    void refreshFromConfig() {
        // Graphics
        graphicsPresetDropdown.selectedIndex = static_cast<int>(g_config.graphicsPreset);
        renderDistanceSlider.value = static_cast<float>(g_config.renderDistance);
        fovSlider.value = static_cast<float>(g_config.fov);
        // Find GUI scale index from current value
        for (int i = 0; i < GUI_SCALE_COUNT; i++) {
            if (std::abs(g_config.guiScale - GUI_SCALE_VALUES[i]) < 0.1f) {
                guiScaleDropdown.selectedIndex = i;
                break;
            }
        }
        vsyncCheckbox.checked = g_config.vsync;
        fullscreenCheckbox.checked = g_config.fullscreen;
        aaDropdown.selectedIndex = static_cast<int>(g_config.antiAliasing);
        textureQualityDropdown.selectedIndex = static_cast<int>(g_config.textureQuality);

        // Map anisotropic value to dropdown index
        int anisoIdx = 0;
        if (g_config.anisotropicFiltering >= 16) anisoIdx = 4;
        else if (g_config.anisotropicFiltering >= 8) anisoIdx = 3;
        else if (g_config.anisotropicFiltering >= 4) anisoIdx = 2;
        else if (g_config.anisotropicFiltering >= 2) anisoIdx = 1;
        anisotropicDropdown.selectedIndex = anisoIdx;

        // Effects
        shadowQualityDropdown.selectedIndex = static_cast<int>(g_config.shadowQuality);
        aoQualityDropdown.selectedIndex = static_cast<int>(g_config.aoQuality);
        bloomCheckbox.checked = g_config.enableBloom;
        bloomIntensitySlider.value = g_config.bloomIntensity * 100.0f;
        motionBlurCheckbox.checked = g_config.enableMotionBlur;
        upscaleDropdown.selectedIndex = static_cast<int>(g_config.upscaleMode);
        waterAnimationCheckbox.checked = g_config.enableWaterAnimation;
        cloudsCheckbox.checked = g_config.enableClouds;
        cloudQualityDropdown.selectedIndex = static_cast<int>(g_config.cloudQuality);
        volumetricCloudsCheckbox.checked = (g_config.cloudStyle == CloudStyle::VOLUMETRIC);
        brightnessSlider.value = g_config.brightness * 100.0f;

        // Performance
        hiZCheckbox.checked = g_config.enableHiZCulling;
        batchedRenderingCheckbox.checked = g_config.enableBatchedRendering;
        chunkSpeedSlider.value = static_cast<float>(g_config.maxChunksPerFrame);
        meshSpeedSlider.value = static_cast<float>(g_config.maxMeshesPerFrame);

        // Controls
        sensitivitySlider.value = g_config.mouseSensitivity * 100.0f;
        invertYCheckbox.checked = g_config.invertY;

        // Title Screen
        titleSourceDropdown.selectedIndex = static_cast<int>(g_config.titleScreen.sourceMode);
        titleSeedInput.text = g_config.titleScreen.customSeed;
        titleRenderDistSlider.value = static_cast<float>(g_config.titleScreen.renderDistance);
        loadSavedWorldsList();

        // Setup tooltips for all settings
        setupTooltips();
    }

    void setupTooltips() {
        // === GRAPHICS TAB ===
        graphicsPresetDropdown.tooltip = "Quick preset to adjust all graphics settings at once";
        renderDistanceSlider.tooltip = "How far you can see - higher uses more VRAM";
        fovSlider.tooltip = "Field of view angle - higher shows more but may distort edges";
        guiScaleDropdown.tooltip = "Size of UI elements - adjust for your screen size and preference";
        vsyncCheckbox.tooltip = "Sync to monitor refresh rate - prevents tearing but adds input lag";
        fullscreenCheckbox.tooltip = "Run in fullscreen mode for better performance";
        aaDropdown.tooltip = "Smooths jagged edges - MSAA is higher quality, FXAA is faster";
        textureQualityDropdown.tooltip = "Texture resolution - higher uses more VRAM";
        anisotropicDropdown.tooltip = "Improves texture quality at angles - minimal performance cost";

        // === EFFECTS TAB ===
        shadowQualityDropdown.tooltip = "Shadow map resolution and cascade count";
        aoQualityDropdown.tooltip = "Screen-space ambient occlusion - adds depth to corners";
        bloomCheckbox.tooltip = "Glow effect around bright objects";
        bloomIntensitySlider.tooltip = "How strong the bloom glow effect is";
        motionBlurCheckbox.tooltip = "Blur effect when moving camera quickly";
        // upscaleDropdown tooltip already set inline
        waterAnimationCheckbox.tooltip = "Animated water surface waves";
        cloudsCheckbox.tooltip = "Enable cloud rendering";
        cloudQualityDropdown.tooltip = "Ray-march steps for volumetric clouds";
        volumetricCloudsCheckbox.tooltip = "Full 3D volumetric clouds - experimental, may impact FPS";
        brightnessSlider.tooltip = "Adjust overall screen brightness (50-150%)";

        // === PERFORMANCE TAB ===
        hiZCheckbox.tooltip = "Hierarchical-Z occlusion culling - skips hidden geometry";
        batchedRenderingCheckbox.tooltip = "Batch draw calls for better CPU efficiency";
        chunkSpeedSlider.tooltip = "Max chunks to generate per frame - higher loads faster";
        meshSpeedSlider.tooltip = "Max meshes to upload per frame - higher loads faster";

        // === CONTROLS TAB ===
        sensitivitySlider.tooltip = "Mouse look sensitivity";
        invertYCheckbox.tooltip = "Invert vertical mouse axis";

        // === AUDIO TAB ===
        masterVolumeSlider.tooltip = "Overall game volume";
        musicVolumeSlider.tooltip = "Background music volume";
        sfxVolumeSlider.tooltip = "Sound effects volume";

        // === TITLE SCREEN TAB ===
        titleSourceDropdown.tooltip = "What world to show on the title screen background";
        titleSeedInput.tooltip = "World generation seed for title screen";
        titleRenderDistSlider.tooltip = "Render distance for title screen world";
    }

    void update(double mouseX, double mouseY, bool mousePressed, float deltaTime = 0.016f) {
        currentAction = SettingsAction::NONE;
        currentDeltaTime = deltaTime;  // Store for render()

        // Update APPLIED feedback animation
        if (showAppliedFeedback) {
            appliedFeedbackTimer -= deltaTime;
            if (appliedFeedbackTimer <= 0.0f) {
                showAppliedFeedback = false;
                appliedFeedbackTimer = 0.0f;
            }
        }

        input.update(mouseX, mouseY, mousePressed);

        // Tab buttons
        input.handleButton(graphicsTabBtn);
        input.handleButton(effectsTabBtn);
        input.handleButton(performanceTabBtn);
        input.handleButton(controlsTabBtn);
        input.handleButton(audioTabBtn);
        input.handleButton(titleScreenTabBtn);

        // Action buttons
        input.handleButton(backButton);
        input.handleButton(applyButton);

        // Current tab content
        switch (currentTab) {
            case SettingsTab::GRAPHICS:
                input.handleDropdown(graphicsPresetDropdown, allDropdowns);
                input.handleSlider(renderDistanceSlider);
                input.handleSlider(fovSlider);
                input.handleDropdown(guiScaleDropdown, allDropdowns);
                input.handleCheckbox(vsyncCheckbox);
                input.handleCheckbox(fullscreenCheckbox);
                input.handleDropdown(aaDropdown, allDropdowns);
                input.handleDropdown(textureQualityDropdown, allDropdowns);
                input.handleDropdown(anisotropicDropdown, allDropdowns);
                break;

            case SettingsTab::EFFECTS:
                input.handleDropdown(shadowQualityDropdown, allDropdowns);
                input.handleDropdown(aoQualityDropdown, allDropdowns);
                input.handleCheckbox(bloomCheckbox);
                input.handleSlider(bloomIntensitySlider);
                input.handleCheckbox(motionBlurCheckbox);
                input.handleDropdown(upscaleDropdown, allDropdowns);
                input.handleCheckbox(waterAnimationCheckbox);
                input.handleCheckbox(cloudsCheckbox);
                input.handleDropdown(cloudQualityDropdown, allDropdowns);
                input.handleCheckbox(volumetricCloudsCheckbox);
                input.handleSlider(brightnessSlider);
                break;

            case SettingsTab::PERFORMANCE:
                input.handleCheckbox(hiZCheckbox);
                input.handleCheckbox(batchedRenderingCheckbox);
                input.handleSlider(chunkSpeedSlider);
                input.handleSlider(meshSpeedSlider);
                break;

            case SettingsTab::CONTROLS:
                input.handleSlider(sensitivitySlider);
                input.handleCheckbox(invertYCheckbox);
                input.handleButton(configureControlsButton);
                break;

            case SettingsTab::AUDIO:
                input.handleSlider(masterVolumeSlider);
                input.handleSlider(musicVolumeSlider);
                input.handleSlider(sfxVolumeSlider);
                break;

            case SettingsTab::TITLE_SCREEN:
                input.handleDropdown(titleSourceDropdown, allDropdowns);
                // Only show relevant controls based on source mode
                if (g_config.titleScreen.sourceMode == TitleScreenSource::CUSTOM_SEED) {
                    input.handleTextInput(titleSeedInput);
                } else if (g_config.titleScreen.sourceMode == TitleScreenSource::SAVED_WORLD) {
                    input.handleDropdown(titleWorldDropdown, allDropdowns);
                }
                input.handleSlider(titleRenderDistSlider);
                break;
        }

        // Track tooltip for hovered widget
        updateTooltip(static_cast<float>(mouseX), static_cast<float>(mouseY));

        // Periodically update VRAM usage
        static float vramUpdateTimer = 0.0f;
        vramUpdateTimer += deltaTime;
        if (vramUpdateTimer > 2.0f) {  // Update every 2 seconds
            queryVRAM();
            vramUpdateTimer = 0.0f;
        }
    }

    void updateTooltip(float mx, float my) {
        currentTooltip.clear();
        tooltipMouseX = mx;
        tooltipMouseY = my;

        // Check widgets based on current tab
        auto checkSlider = [&](MenuSlider& s) {
            if (s.visible && s.contains(mx, my)) { currentTooltip = s.tooltip; return true; }
            return false;
        };
        auto checkCheckbox = [&](MenuCheckbox& c) {
            // Expand hitbox to include label
            if (c.visible && mx >= c.x && mx <= c.x + c.size + 150 && my >= c.y && my <= c.y + c.size) {
                currentTooltip = c.tooltip; return true;
            }
            return false;
        };
        auto checkDropdown = [&](MenuDropdown& d) {
            if (d.visible && d.contains(mx, my)) { currentTooltip = d.tooltip; return true; }
            return false;
        };

        switch (currentTab) {
            case SettingsTab::GRAPHICS:
                if (checkDropdown(graphicsPresetDropdown)) return;
                if (checkSlider(renderDistanceSlider)) return;
                if (checkSlider(fovSlider)) return;
                if (checkDropdown(guiScaleDropdown)) return;
                if (checkCheckbox(vsyncCheckbox)) return;
                if (checkCheckbox(fullscreenCheckbox)) return;
                if (checkDropdown(aaDropdown)) return;
                if (checkDropdown(textureQualityDropdown)) return;
                if (checkDropdown(anisotropicDropdown)) return;
                break;

            case SettingsTab::EFFECTS:
                if (checkDropdown(shadowQualityDropdown)) return;
                if (checkDropdown(aoQualityDropdown)) return;
                if (checkCheckbox(bloomCheckbox)) return;
                if (checkSlider(bloomIntensitySlider)) return;
                if (checkCheckbox(motionBlurCheckbox)) return;
                if (checkDropdown(upscaleDropdown)) return;
                if (checkCheckbox(waterAnimationCheckbox)) return;
                if (checkCheckbox(cloudsCheckbox)) return;
                if (checkDropdown(cloudQualityDropdown)) return;
                if (checkCheckbox(volumetricCloudsCheckbox)) return;
                if (checkSlider(brightnessSlider)) return;
                break;

            case SettingsTab::PERFORMANCE:
                if (checkCheckbox(hiZCheckbox)) return;
                if (checkCheckbox(batchedRenderingCheckbox)) return;
                if (checkSlider(chunkSpeedSlider)) return;
                if (checkSlider(meshSpeedSlider)) return;
                break;

            case SettingsTab::CONTROLS:
                if (checkSlider(sensitivitySlider)) return;
                if (checkCheckbox(invertYCheckbox)) return;
                break;

            case SettingsTab::AUDIO:
                if (checkSlider(masterVolumeSlider)) return;
                if (checkSlider(musicVolumeSlider)) return;
                if (checkSlider(sfxVolumeSlider)) return;
                break;

            case SettingsTab::TITLE_SCREEN:
                if (checkDropdown(titleSourceDropdown)) return;
                if (checkSlider(titleRenderDistSlider)) return;
                break;
        }
    }

    void render() {
        if (!ui) return;

        float centerX = ui->windowWidth / 2.0f;
        float panelWidth = 750.0f;
        float panelHeight = 580.0f;
        float panelX = centerX - panelWidth / 2.0f;
        float panelY = ui->windowHeight / 2.0f - panelHeight / 2.0f;

        // Darken background
        ui->drawRect(0, 0, static_cast<float>(ui->windowWidth), static_cast<float>(ui->windowHeight),
                    glm::vec4(0.0f, 0.0f, 0.0f, 0.7f));

        // Panel background
        ui->drawRect(panelX, panelY, panelWidth, panelHeight, MenuColors::PANEL_BG);
        ui->drawRectOutline(panelX, panelY, panelWidth, panelHeight, MenuColors::ACCENT, 2.0f);

        // Title
        ui->drawTextCentered("SETTINGS", panelX, panelY + 10, panelWidth, MenuColors::ACCENT, 1.8f);

        // Tab buttons with active indicator
        auto renderTab = [this](MenuButton& btn, SettingsTab tab) {
            if (currentTab == tab) {
                ui->drawRect(btn.x, btn.y + btn.height - 3, btn.width, 3, MenuColors::ACCENT);
            }
            btn.render(*ui);
        };

        renderTab(graphicsTabBtn, SettingsTab::GRAPHICS);
        renderTab(effectsTabBtn, SettingsTab::EFFECTS);
        renderTab(performanceTabBtn, SettingsTab::PERFORMANCE);
        renderTab(controlsTabBtn, SettingsTab::CONTROLS);
        renderTab(audioTabBtn, SettingsTab::AUDIO);
        renderTab(titleScreenTabBtn, SettingsTab::TITLE_SCREEN);

        // Tab content
        switch (currentTab) {
            case SettingsTab::GRAPHICS:
                graphicsPresetDropdown.render(*ui);
                renderDistanceSlider.render(*ui);
                fovSlider.render(*ui);
                guiScaleDropdown.render(*ui);
                vsyncCheckbox.render(*ui);
                fullscreenCheckbox.render(*ui);
                aaDropdown.render(*ui);
                textureQualityDropdown.render(*ui);
                anisotropicDropdown.render(*ui);

                // VRAM Usage Bar at bottom of graphics tab
                {
                    float vramBarX = panelX + 30;
                    float vramBarY = panelY + panelHeight - 85;
                    float vramBarWidth = panelWidth - 60;
                    float vramBarHeight = 16;

                    if (totalVRAM_MB > 0 && usedVRAM_MB > 0) {
                        float vramUsage = static_cast<float>(usedVRAM_MB) / static_cast<float>(totalVRAM_MB);

                        // Color based on usage: green < 60%, yellow 60-80%, red > 80%
                        glm::vec4 vramColor;
                        if (vramUsage < 0.6f) {
                            vramColor = MenuColors::SUCCESS;  // Green
                        } else if (vramUsage < 0.8f) {
                            vramColor = glm::vec4(0.9f, 0.75f, 0.2f, 1.0f);  // Yellow/Orange
                        } else {
                            vramColor = MenuColors::ERROR_COLOR;  // Red
                        }

                        std::string vramText = std::to_string(usedVRAM_MB) + " / " + std::to_string(totalVRAM_MB) + " MB";
                        ui->drawProgressBar(vramBarX, vramBarY, vramBarWidth, vramBarHeight,
                                           vramUsage, vramColor, "VRAM Usage", vramText);
                    } else if (totalVRAM_MB > 0) {
                        // Have total but not current usage
                        ui->drawText("VRAM: " + std::to_string(totalVRAM_MB) + " MB (usage unavailable)",
                                    vramBarX, vramBarY, MenuColors::TEXT_DIM, 0.9f);
                    }
                }

                // Render dropdown options last (on top)
                graphicsPresetDropdown.renderOptions(*ui);
                guiScaleDropdown.renderOptions(*ui);
                aaDropdown.renderOptions(*ui);
                textureQualityDropdown.renderOptions(*ui);
                anisotropicDropdown.renderOptions(*ui);
                break;

            case SettingsTab::EFFECTS:
                shadowQualityDropdown.render(*ui);
                aoQualityDropdown.render(*ui);
                bloomCheckbox.render(*ui);
                bloomIntensitySlider.render(*ui);
                motionBlurCheckbox.render(*ui);
                upscaleDropdown.render(*ui);
                waterAnimationCheckbox.render(*ui);
                cloudsCheckbox.render(*ui);
                cloudQualityDropdown.render(*ui);
                volumetricCloudsCheckbox.render(*ui);
                brightnessSlider.render(*ui);
                // Render dropdown options last
                shadowQualityDropdown.renderOptions(*ui);
                aoQualityDropdown.renderOptions(*ui);
                upscaleDropdown.renderOptions(*ui);
                cloudQualityDropdown.renderOptions(*ui);
                break;

            case SettingsTab::PERFORMANCE:
                hiZCheckbox.render(*ui);
                batchedRenderingCheckbox.render(*ui);
                chunkSpeedSlider.render(*ui);
                meshSpeedSlider.render(*ui);
                break;

            case SettingsTab::CONTROLS:
                sensitivitySlider.render(*ui);
                invertYCheckbox.render(*ui);
                configureControlsButton.render(*ui);
                break;

            case SettingsTab::AUDIO:
                masterVolumeSlider.render(*ui);
                musicVolumeSlider.render(*ui);
                sfxVolumeSlider.render(*ui);
                ui->drawText("(Audio not yet implemented)", panelX + 40,
                            panelY + 300, MenuColors::TEXT_DIM, 1.0f);
                break;

            case SettingsTab::TITLE_SCREEN:
                titleSourceDropdown.render(*ui);
                // Only show relevant controls based on source mode
                if (g_config.titleScreen.sourceMode == TitleScreenSource::CUSTOM_SEED) {
                    titleSeedInput.render(*ui, currentDeltaTime);
                } else if (g_config.titleScreen.sourceMode == TitleScreenSource::SAVED_WORLD) {
                    titleWorldDropdown.render(*ui);
                }
                titleRenderDistSlider.render(*ui);
                // Info text
                ui->drawText("Changes take effect on next launch or menu return",
                            panelX + 40, panelY + 380, MenuColors::TEXT_DIM, 0.9f);
                // Render dropdown options last
                titleSourceDropdown.renderOptions(*ui);
                if (g_config.titleScreen.sourceMode == TitleScreenSource::SAVED_WORLD) {
                    titleWorldDropdown.renderOptions(*ui);
                }
                break;
        }

        // Action buttons
        backButton.render(*ui);
        applyButton.render(*ui);

        // Render "APPLIED" feedback animation (Borderlands 2 style)
        if (showAppliedFeedback) {
            float progress = appliedFeedbackTimer / APPLIED_FEEDBACK_DURATION;

            // Fade in quickly, fade out slowly
            float alpha;
            if (progress > 0.8f) {
                // Fade in (first 20% of time = 0.8 to 1.0 progress)
                alpha = (1.0f - progress) / 0.2f;
            } else {
                // Fade out (last 80% of time)
                alpha = progress / 0.8f;
            }
            alpha = std::min(1.0f, std::max(0.0f, alpha));

            // Scale effect - starts big and shrinks slightly
            float scale = 3.0f + (1.0f - progress) * 0.5f;

            // Position: center of screen, slightly above center
            float textX = ui->windowWidth / 2.0f;
            float textY = ui->windowHeight / 2.0f - 50.0f;

            // Draw rotated "APPLIED!" text at a funky angle (-12 degrees)
            // Since we don't have rotation in the UI, we'll fake it with offset positioning
            glm::vec4 textColor = glm::vec4(0.2f, 1.0f, 0.3f, alpha);  // Bright green

            // Draw shadow first
            glm::vec4 shadowColor = glm::vec4(0.0f, 0.0f, 0.0f, alpha * 0.7f);
            ui->drawTextCentered("APPLIED!", textX + 4, textY + 4, 0.0f, shadowColor, scale);

            // Draw main text
            ui->drawTextCentered("APPLIED!", textX, textY, 0.0f, textColor, scale);

            // Draw accent lines (like Borderlands style)
            float lineWidth = 300.0f * alpha;
            float lineY = textY + 60.0f;
            ui->drawRect(textX - lineWidth/2, lineY, lineWidth, 4.0f,
                        glm::vec4(0.2f, 1.0f, 0.3f, alpha * 0.8f));
            ui->drawRect(textX - lineWidth/2 + 20, lineY + 10, lineWidth - 40, 2.0f,
                        glm::vec4(0.2f, 1.0f, 0.3f, alpha * 0.5f));
        }

        // Render tooltip last (on top of everything)
        if (!currentTooltip.empty()) {
            ui->drawTooltip(currentTooltip, tooltipMouseX, tooltipMouseY);
        }
    }

    SettingsAction getAction() const {
        return currentAction;
    }
};
