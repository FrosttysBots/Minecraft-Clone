// ForgeBound OpenGL Launcher
// Bethesda-style launcher with dark theme and modern UI

#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "stb_easy_font.h"

#include "../core/Config.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <functional>
#include <array>
#include <cmath>
#include <algorithm>
#include <memory>

// ============================================
// COLOR SCHEME (Bethesda Dark Theme)
// ============================================
namespace Colors {
    const glm::vec4 BG_DARK      = {0.08f, 0.08f, 0.10f, 1.0f};
    const glm::vec4 BG_GRADIENT  = {0.12f, 0.12f, 0.15f, 1.0f};
    const glm::vec4 PANEL_BG     = {0.10f, 0.10f, 0.12f, 0.95f};
    const glm::vec4 BUTTON_BG    = {0.15f, 0.15f, 0.18f, 1.0f};
    const glm::vec4 BUTTON_HOVER = {0.22f, 0.22f, 0.26f, 1.0f};
    const glm::vec4 BUTTON_PRESS = {0.18f, 0.18f, 0.21f, 1.0f};
    const glm::vec4 ACCENT       = {0.85f, 0.65f, 0.25f, 1.0f};  // Gold
    const glm::vec4 ACCENT_DIM   = {0.65f, 0.50f, 0.20f, 1.0f};
    const glm::vec4 TEXT         = {0.92f, 0.92f, 0.92f, 1.0f};
    const glm::vec4 TEXT_DIM     = {0.60f, 0.60f, 0.62f, 1.0f};
    const glm::vec4 DIVIDER      = {0.25f, 0.25f, 0.28f, 1.0f};
    const glm::vec4 SLIDER_BG    = {0.20f, 0.20f, 0.22f, 1.0f};
    const glm::vec4 SLIDER_FILL  = {0.75f, 0.55f, 0.20f, 1.0f};
    const glm::vec4 TAB_ACTIVE   = {0.18f, 0.18f, 0.21f, 1.0f};
    const glm::vec4 TAB_INACTIVE = {0.12f, 0.12f, 0.14f, 1.0f};
}

// ============================================
// LAUNCHER STATE
// ============================================
enum class LauncherState {
    MAIN_MENU,
    SETTINGS,
    EXITING
};

enum class SettingsTab {
    DISPLAY,
    GRAPHICS,
    QUALITY,
    ADVANCED,
    CONTROLS
};

// ============================================
// UI RENDERER (using stb_easy_font)
// ============================================
class UIRenderer {
public:
    GLuint shaderProgram = 0;
    GLuint quadVAO = 0, quadVBO = 0;
    GLuint textVAO = 0, textVBO = 0;
    glm::mat4 projection;
    int windowWidth = 1024;
    int windowHeight = 640;

    const char* vertexShaderSrc = R"(
        #version 330 core
        layout(location = 0) in vec2 aPos;
        uniform mat4 projection;
        uniform mat4 model;
        void main() {
            gl_Position = projection * model * vec4(aPos, 0.0, 1.0);
        }
    )";

    const char* fragmentShaderSrc = R"(
        #version 330 core
        out vec4 FragColor;
        uniform vec4 color;
        void main() {
            FragColor = color;
        }
    )";

    GLuint compileShader(GLenum type, const char* source) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);

        int success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetShaderInfoLog(shader, 512, nullptr, infoLog);
            std::cerr << "Shader compilation failed: " << infoLog << std::endl;
        }
        return shader;
    }

    bool init(int width, int height) {
        windowWidth = width;
        windowHeight = height;
        projection = glm::ortho(0.0f, static_cast<float>(width),
                               static_cast<float>(height), 0.0f, -1.0f, 1.0f);

        // Create shader program
        GLuint vs = compileShader(GL_VERTEX_SHADER, vertexShaderSrc);
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSrc);
        shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vs);
        glAttachShader(shaderProgram, fs);
        glLinkProgram(shaderProgram);
        glDeleteShader(vs);
        glDeleteShader(fs);

        // Create quad VAO/VBO
        float quadVertices[] = {
            0.0f, 0.0f,
            1.0f, 0.0f,
            1.0f, 1.0f,
            0.0f, 0.0f,
            1.0f, 1.0f,
            0.0f, 1.0f
        };

        glGenVertexArrays(1, &quadVAO);
        glGenBuffers(1, &quadVBO);
        glBindVertexArray(quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

        // Create text VAO/VBO (dynamic, for stb_easy_font quads)
        glGenVertexArrays(1, &textVAO);
        glGenBuffers(1, &textVBO);
        glBindVertexArray(textVAO);
        glBindBuffer(GL_ARRAY_BUFFER, textVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 2 * 60000, nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

        glBindVertexArray(0);
        return true;
    }

    void drawRect(float x, float y, float w, float h, const glm::vec4& color) {
        glUseProgram(shaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, &projection[0][0]);

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(x, y, 0.0f));
        model = glm::scale(model, glm::vec3(w, h, 1.0f));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &model[0][0]);

        glUniform4fv(glGetUniformLocation(shaderProgram, "color"), 1, &color[0]);

        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    void drawRectOutline(float x, float y, float w, float h, const glm::vec4& color, float thickness = 1.0f) {
        drawRect(x, y, w, thickness, color);  // Top
        drawRect(x, y + h - thickness, w, thickness, color);  // Bottom
        drawRect(x, y, thickness, h, color);  // Left
        drawRect(x + w - thickness, y, thickness, h, color);  // Right
    }

    void drawGradientRect(float x, float y, float w, float h,
                          const glm::vec4& topColor, const glm::vec4& bottomColor) {
        int steps = 20;
        float stepH = h / steps;
        for (int i = 0; i < steps; i++) {
            float t = static_cast<float>(i) / (steps - 1);
            glm::vec4 color = glm::mix(topColor, bottomColor, t);
            drawRect(x, y + i * stepH, w, stepH + 1, color);
        }
    }

    float getTextWidth(const std::string& text, float scale = 1.0f) {
        // stb_easy_font uses ~6 pixels per character width at scale 1
        return text.length() * 6.0f * scale;
    }

    void drawText(const std::string& text, float x, float y, const glm::vec4& color, float scale = 1.0f) {
        static std::vector<float> vertexBuffer(60000);

        int numQuads = stb_easy_font_print(0, 0, const_cast<char*>(text.c_str()), nullptr,
                                           vertexBuffer.data(), static_cast<int>(vertexBuffer.size()));

        if (numQuads == 0) return;

        // stb_easy_font outputs quads with 4 vertices each (x,y,z,color) per vertex
        // We need to convert to triangles with just (x,y)
        std::vector<float> triangleVerts;
        triangleVerts.reserve(numQuads * 6 * 2);  // 6 verts per quad, 2 floats per vert

        float* ptr = vertexBuffer.data();
        for (int q = 0; q < numQuads; q++) {
            // Each quad has 4 vertices, each vertex is 4 floats (x, y, z, color)
            float x0 = ptr[0], y0 = ptr[1];
            float x1 = ptr[4], y1 = ptr[5];
            float x2 = ptr[8], y2 = ptr[9];
            float x3 = ptr[12], y3 = ptr[13];

            // Triangle 1
            triangleVerts.push_back(x0); triangleVerts.push_back(y0);
            triangleVerts.push_back(x1); triangleVerts.push_back(y1);
            triangleVerts.push_back(x2); triangleVerts.push_back(y2);

            // Triangle 2
            triangleVerts.push_back(x0); triangleVerts.push_back(y0);
            triangleVerts.push_back(x2); triangleVerts.push_back(y2);
            triangleVerts.push_back(x3); triangleVerts.push_back(y3);

            ptr += 16;  // Move to next quad (4 vertices * 4 floats)
        }

        glUseProgram(shaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, &projection[0][0]);

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(x, y, 0.0f));
        model = glm::scale(model, glm::vec3(scale * 2.0f, scale * 2.0f, 1.0f));  // Scale up since stb_easy_font is small
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &model[0][0]);

        glUniform4fv(glGetUniformLocation(shaderProgram, "color"), 1, &color[0]);

        glBindVertexArray(textVAO);
        glBindBuffer(GL_ARRAY_BUFFER, textVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, triangleVerts.size() * sizeof(float), triangleVerts.data());
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(triangleVerts.size() / 2));
    }

    void drawTextCentered(const std::string& text, float x, float y, float width,
                          const glm::vec4& color, float scale = 1.0f) {
        float textWidth = getTextWidth(text, scale * 2.0f);
        drawText(text, x + (width - textWidth) / 2, y, color, scale);
    }

    void cleanup() {
        if (shaderProgram) glDeleteProgram(shaderProgram);
        if (quadVAO) glDeleteVertexArrays(1, &quadVAO);
        if (quadVBO) glDeleteBuffers(1, &quadVBO);
        if (textVAO) glDeleteVertexArrays(1, &textVAO);
        if (textVBO) glDeleteBuffers(1, &textVBO);
    }
};

// ============================================
// UI ELEMENTS
// ============================================
struct Button {
    float x, y, width, height;
    std::string text;
    std::function<void()> onClick;
    bool hovered = false;
    bool pressed = false;
    bool visible = true;
    float textScale = 1.2f;

    bool contains(float mx, float my) const {
        return visible && mx >= x && mx <= x + width && my >= y && my <= y + height;
    }

    void render(UIRenderer& ui) {
        if (!visible) return;

        glm::vec4 bgColor = pressed ? Colors::BUTTON_PRESS :
                           (hovered ? Colors::BUTTON_HOVER : Colors::BUTTON_BG);

        ui.drawRect(x, y, width, height, bgColor);

        if (hovered) {
            ui.drawRectOutline(x, y, width, height, Colors::ACCENT, 2.0f);
        }

        glm::vec4 textColor = hovered ? Colors::ACCENT : Colors::TEXT;
        ui.drawTextCentered(text, x, y + height / 2 - 8 * textScale, width, textColor, textScale);
    }
};

struct Slider {
    float x, y, width, height;
    std::string label;
    float minVal, maxVal, value;
    std::function<void(float)> onChange;
    bool dragging = false;
    bool visible = true;

    bool contains(float mx, float my) const {
        return visible && mx >= x && mx <= x + width && my >= y && my <= y + height;
    }

    void render(UIRenderer& ui) {
        if (!visible) return;

        // Label
        ui.drawText(label, x, y - 20, Colors::TEXT, 0.9f);

        // Background track
        ui.drawRect(x, y + height / 2 - 4, width, 8, Colors::SLIDER_BG);

        // Filled portion
        float fillWidth = (value - minVal) / (maxVal - minVal) * width;
        ui.drawRect(x, y + height / 2 - 4, fillWidth, 8, Colors::SLIDER_FILL);

        // Handle
        float handleX = x + fillWidth - 8;
        glm::vec4 handleColor = dragging ? Colors::ACCENT : Colors::TEXT;
        ui.drawRect(handleX, y, 16, height, handleColor);

        // Value text
        std::stringstream ss;
        ss << static_cast<int>(value);
        ui.drawText(ss.str(), x + width + 15, y + 4, Colors::TEXT_DIM, 0.9f);
    }

    void updateFromMouse(float mx) {
        float t = (mx - x) / width;
        t = std::clamp(t, 0.0f, 1.0f);
        value = minVal + t * (maxVal - minVal);
        if (onChange) onChange(value);
    }
};

struct Checkbox {
    float x, y, size;
    std::string label;
    bool checked;
    std::function<void(bool)> onChange;
    bool hovered = false;
    bool visible = true;

    bool contains(float mx, float my) const {
        return visible && mx >= x && mx <= x + size && my >= y && my <= y + size;
    }

    void render(UIRenderer& ui) {
        if (!visible) return;

        glm::vec4 boxColor = hovered ? Colors::BUTTON_HOVER : Colors::BUTTON_BG;
        ui.drawRect(x, y, size, size, boxColor);
        ui.drawRectOutline(x, y, size, size, hovered ? Colors::ACCENT : Colors::DIVIDER, 2.0f);

        if (checked) {
            float padding = 5;
            ui.drawRect(x + padding, y + padding, size - padding * 2, size - padding * 2, Colors::ACCENT);
        }

        ui.drawText(label, x + size + 12, y + 4, Colors::TEXT, 0.9f);
    }
};

struct Dropdown {
    float x, y, width, height;
    std::string label;
    std::vector<std::string> options;
    int selectedIndex;
    std::function<void(int)> onChange;
    bool open = false;
    bool hovered = false;
    int hoveredOption = -1;
    bool visible = true;

    bool contains(float mx, float my) const {
        return visible && mx >= x && mx <= x + width && my >= y && my <= y + height;
    }

    bool containsOption(float mx, float my, int index) const {
        if (!open || !visible) return false;
        float optY = y + height + index * height;
        return mx >= x && mx <= x + width && my >= optY && my <= optY + height;
    }

    // Render just the main dropdown box (not the options)
    void render(UIRenderer& ui) {
        if (!visible) return;

        // Label
        ui.drawText(label, x, y - 20, Colors::TEXT, 0.9f);

        // Main box
        glm::vec4 boxColor = hovered || open ? Colors::BUTTON_HOVER : Colors::BUTTON_BG;
        ui.drawRect(x, y, width, height, boxColor);
        ui.drawRectOutline(x, y, width, height, open ? Colors::ACCENT : Colors::DIVIDER, 1.0f);

        // Selected text
        if (selectedIndex >= 0 && selectedIndex < static_cast<int>(options.size())) {
            ui.drawText(options[selectedIndex], x + 10, y + height / 2 - 7, Colors::TEXT, 0.9f);
        }

        // Arrow
        ui.drawText(open ? "^" : "v", x + width - 20, y + height / 2 - 7, Colors::TEXT_DIM, 0.9f);
    }

    // Render dropdown options (call this last for z-ordering)
    void renderOptions(UIRenderer& ui) {
        if (!visible || !open) return;

        // Background for dropdown options (solid to cover elements below)
        float totalHeight = static_cast<float>(options.size()) * height;
        ui.drawRect(x, y + height, width, totalHeight, Colors::PANEL_BG);

        for (size_t i = 0; i < options.size(); i++) {
            float optY = y + height + static_cast<float>(i) * height;
            glm::vec4 optColor = (static_cast<int>(i) == hoveredOption) ? Colors::BUTTON_HOVER : Colors::PANEL_BG;
            ui.drawRect(x, optY, width, height, optColor);
            ui.drawText(options[i], x + 10, optY + height / 2 - 7, Colors::TEXT, 0.9f);
        }
        ui.drawRectOutline(x, y + height, width, totalHeight, Colors::ACCENT, 1.0f);
    }
};

// ============================================
// LAUNCHER APPLICATION
// ============================================
class LauncherApp {
public:
    GLFWwindow* window = nullptr;
    UIRenderer ui;
    LauncherState state = LauncherState::MAIN_MENU;
    SettingsTab currentTab = SettingsTab::DISPLAY;

    double mouseX = 0, mouseY = 0;
    bool mousePressed = false;
    bool mouseJustPressed = false;
    bool mouseJustReleased = false;

    // Main menu buttons
    Button playButton, settingsButton, exitButton;

    // Settings tabs
    std::array<Button, 5> tabButtons;
    Button backButton, applyButton, autoDetectButton;

    // Display settings
    Dropdown resolutionDropdown;
    Dropdown displayModeDropdown;
    Checkbox vsyncCheckbox;

    // Graphics settings
    Dropdown presetDropdown;
    Slider renderDistanceSlider;
    Slider fovSlider;
    Dropdown aaDropdown;
    Dropdown textureQualityDropdown;
    Dropdown anisotropicDropdown;

    // Quality settings
    Dropdown shadowQualityDropdown;
    Dropdown aoQualityDropdown;
    Checkbox bloomCheckbox;
    Slider bloomIntensitySlider;
    Checkbox motionBlurCheckbox;
    Dropdown upscaleDropdown;
    Checkbox cloudsCheckbox;
    Dropdown cloudQualityDropdown;
    Checkbox volumetricCloudsCheckbox;

    // Advanced settings
    Dropdown rendererDropdown;  // OpenGL / Vulkan selection
    Checkbox hiZCheckbox;
    Slider chunkSpeedSlider;
    Slider meshSpeedSlider;

    // Controls settings
    Slider sensitivitySlider;
    Checkbox invertYCheckbox;

    // APPLIED animation state
    bool showAppliedFeedback = false;
    float appliedFeedbackTimer = 0.0f;
    static constexpr float APPLIED_FEEDBACK_DURATION = 2.5f;  // Longer duration
    double lastFrameTime = 0.0;

    bool init() {
        if (!glfwInit()) {
            std::cerr << "Failed to initialize GLFW" << std::endl;
            return false;
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        window = glfwCreateWindow(1024, 640, "ForgeBound Launcher", nullptr, nullptr);
        if (!window) {
            std::cerr << "Failed to create window" << std::endl;
            glfwTerminate();
            return false;
        }

        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);

        if (!gladLoadGL(glfwGetProcAddress)) {
            std::cerr << "Failed to initialize GLAD" << std::endl;
            return false;
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        ui.init(1024, 640);

        // Load config
        g_config.load();

        setupUI();

        return true;
    }

    void setupUI() {
        float rightMargin = 100;
        float btnWidth = 200;
        float btnHeight = 50;
        float btnSpacing = 15;
        float startY = 220;

        // Main menu buttons (right-aligned, Bethesda style)
        playButton = {1024 - rightMargin - btnWidth, startY, btnWidth, btnHeight, "PLAY",
                     [this]() { launchGame(); }};
        playButton.textScale = 1.5f;

        settingsButton = {1024 - rightMargin - btnWidth, startY + btnHeight + btnSpacing, btnWidth, btnHeight, "SETTINGS",
                         [this]() { state = LauncherState::SETTINGS; }};
        settingsButton.textScale = 1.5f;

        exitButton = {1024 - rightMargin - btnWidth, startY + 2 * (btnHeight + btnSpacing), btnWidth, btnHeight, "EXIT",
                     [this]() { state = LauncherState::EXITING; }};
        exitButton.textScale = 1.5f;

        // Settings tabs
        const char* tabNames[] = {"DISPLAY", "GRAPHICS", "QUALITY", "ADVANCED", "CONTROLS"};
        float tabWidth = 120;
        float tabStartX = 50;
        for (int i = 0; i < 5; i++) {
            tabButtons[i] = {tabStartX + i * (tabWidth + 5), 80, tabWidth, 35, tabNames[i],
                            [this, i]() { currentTab = static_cast<SettingsTab>(i); }};
            tabButtons[i].textScale = 0.9f;
        }

        // Back and Apply buttons
        backButton = {50, 580, 100, 40, "BACK", [this]() {
            state = LauncherState::MAIN_MENU;
            g_config.save();
        }};
        backButton.textScale = 1.0f;

        applyButton = {160, 580, 100, 40, "APPLY", [this]() {
            g_config.save();
            showAppliedFeedback = true;
            appliedFeedbackTimer = APPLIED_FEEDBACK_DURATION;
        }};
        applyButton.textScale = 1.0f;

        autoDetectButton = {860, 580, 150, 40, "AUTO DETECT", [this]() {
            autoDetectHardware();
        }};
        autoDetectButton.textScale = 0.95f;

        setupDisplaySettings();
        setupGraphicsSettings();
        setupQualitySettings();
        setupAdvancedSettings();
        setupControlsSettings();
    }

    void setupDisplaySettings() {
        float col1 = 80, col2 = 450;
        float startY = 160;
        float spacing = 80;

        resolutionDropdown = {col1, startY, 280, 35, "Resolution",
            {"1280 x 720", "1600 x 900", "1920 x 1080", "2560 x 1440", "3840 x 2160"},
            getResolutionIndex(), [this](int idx) { applyResolution(idx); }};

        displayModeDropdown = {col2, startY, 280, 35, "Display Mode",
            {"Windowed", "Fullscreen", "Borderless"},
            g_config.fullscreen ? 1 : 0, [](int idx) {
                g_config.fullscreen = (idx == 1);
            }};

        vsyncCheckbox = {col1, startY + spacing, 26, "VSync", g_config.vsync,
            [](bool val) { g_config.vsync = val; }};
    }

    void setupGraphicsSettings() {
        float col1 = 80, col2 = 450;
        float startY = 160;
        float spacing = 80;

        presetDropdown = {col1, startY, 280, 35, "Graphics Preset",
            {"Low", "Medium", "High", "Ultra", "Custom"},
            static_cast<int>(g_config.graphicsPreset),
            [this](int idx) {
                g_config.applyPreset(static_cast<GraphicsPreset>(idx));
                refreshSettingsUI();
            }};

        renderDistanceSlider = {col2, startY + 10, 220, 28, "Render Distance",
            4, 48, static_cast<float>(g_config.renderDistance),
            [](float val) { g_config.renderDistance = static_cast<int>(val); }};

        fovSlider = {col1, startY + spacing + 10, 220, 28, "Field of View",
            60, 120, static_cast<float>(g_config.fov),
            [](float val) { g_config.fov = static_cast<int>(val); }};

        aaDropdown = {col2, startY + spacing, 280, 35, "Anti-Aliasing",
            {"Off", "FXAA", "MSAA 2x", "MSAA 4x", "MSAA 8x", "TAA"},
            static_cast<int>(g_config.antiAliasing),
            [](int idx) { g_config.antiAliasing = static_cast<AntiAliasMode>(idx); }};

        textureQualityDropdown = {col1, startY + spacing * 2, 280, 35, "Texture Quality",
            {"Low", "Medium", "High", "Ultra"},
            static_cast<int>(g_config.textureQuality),
            [](int idx) { g_config.textureQuality = static_cast<TextureQuality>(idx); }};

        anisotropicDropdown = {col2, startY + spacing * 2, 280, 35, "Anisotropic Filtering",
            {"1x", "2x", "4x", "8x", "16x"},
            getAnisotropicIndex(),
            [](int idx) {
                int values[] = {1, 2, 4, 8, 16};
                g_config.anisotropicFiltering = values[idx];
            }};
    }

    void setupQualitySettings() {
        float col1 = 80, col2 = 450;
        float startY = 160;
        float spacing = 70;

        shadowQualityDropdown = {col1, startY, 280, 35, "Shadow Quality",
            {"Off", "Low", "Medium", "High", "Ultra"},
            static_cast<int>(g_config.shadowQuality),
            [](int idx) { g_config.shadowQuality = static_cast<ShadowQuality>(idx); }};

        aoQualityDropdown = {col2, startY, 280, 35, "Ambient Occlusion",
            {"Off", "Low", "Medium", "High", "Ultra"},
            static_cast<int>(g_config.aoQuality),
            [](int idx) { g_config.aoQuality = static_cast<AOQuality>(idx); }};

        bloomCheckbox = {col1, startY + spacing, 26, "Bloom", g_config.enableBloom,
            [](bool val) { g_config.enableBloom = val; }};

        bloomIntensitySlider = {col2, startY + spacing + 10, 220, 28, "Bloom Intensity",
            0, 200, g_config.bloomIntensity * 100,
            [](float val) { g_config.bloomIntensity = val / 100.0f; }};

        motionBlurCheckbox = {col1, startY + spacing * 2, 26, "Motion Blur", g_config.enableMotionBlur,
            [](bool val) { g_config.enableMotionBlur = val; }};

        upscaleDropdown = {col2, startY + spacing * 2, 280, 35, "FSR Upscaling",
            {"Native", "Quality 1.5x", "Balanced 1.7x", "Performance 2x", "Ultra Perf 3x"},
            static_cast<int>(g_config.upscaleMode),
            [](int idx) {
                g_config.upscaleMode = static_cast<UpscaleMode>(idx);
                g_config.enableFSR = (idx > 0);
            }};

        // Cloud settings
        cloudsCheckbox = {col1, startY + spacing * 3, 26, "Clouds", g_config.enableClouds,
            [](bool val) { g_config.enableClouds = val; }};

        cloudQualityDropdown = {col2, startY + spacing * 3, 280, 35, "Cloud Quality",
            {"Very Low", "Low", "Medium", "High"},
            static_cast<int>(g_config.cloudQuality),
            [](int idx) { g_config.cloudQuality = static_cast<CloudQuality>(idx); }};

        volumetricCloudsCheckbox = {col1, startY + spacing * 4, 26, "Volumetric Clouds [Experimental]",
            g_config.cloudStyle == CloudStyle::VOLUMETRIC,
            [](bool val) { g_config.cloudStyle = val ? CloudStyle::VOLUMETRIC : CloudStyle::SIMPLE; }};
    }

    void setupAdvancedSettings() {
        float col1 = 80, col2 = 450;
        float startY = 160;
        float spacing = 80;

        // Renderer backend selection (requires restart)
        std::vector<std::string> rendererOptions = {"OpenGL 4.6"};
        // Check if Vulkan is available
        if (glfwVulkanSupported() == GLFW_TRUE) {
            rendererOptions.push_back("Vulkan");
        }
        rendererDropdown = {col1, startY, 280, 35, "Renderer (requires restart)",
            rendererOptions,
            static_cast<int>(g_config.renderer),
            [](int idx) { g_config.renderer = static_cast<RendererType>(idx); }};

        float row2Y = startY + spacing;
        hiZCheckbox = {col1, row2Y, 26, "Hi-Z Occlusion Culling", g_config.enableHiZCulling,
            [](bool val) { g_config.enableHiZCulling = val; }};

        float row3Y = row2Y + spacing;
        chunkSpeedSlider = {col1, row3Y + 10, 220, 28, "Chunks per Frame",
            1, 32, static_cast<float>(g_config.maxChunksPerFrame),
            [](float val) { g_config.maxChunksPerFrame = static_cast<int>(val); }};

        meshSpeedSlider = {col2, row3Y + 10, 220, 28, "Meshes per Frame",
            1, 32, static_cast<float>(g_config.maxMeshesPerFrame),
            [](float val) { g_config.maxMeshesPerFrame = static_cast<int>(val); }};
    }

    void setupControlsSettings() {
        float col1 = 80;
        float startY = 160;
        float spacing = 80;

        sensitivitySlider = {col1, startY + 10, 220, 28, "Mouse Sensitivity",
            1, 100, g_config.mouseSensitivity * 100,
            [](float val) { g_config.mouseSensitivity = val / 100.0f; }};

        invertYCheckbox = {col1, startY + spacing, 26, "Invert Y-Axis", g_config.invertY,
            [](bool val) { g_config.invertY = val; }};
    }

    int getResolutionIndex() {
        if (g_config.windowWidth == 1280) return 0;
        if (g_config.windowWidth == 1600) return 1;
        if (g_config.windowWidth == 1920) return 2;
        if (g_config.windowWidth == 2560) return 3;
        if (g_config.windowWidth == 3840) return 4;
        return 2;
    }

    void applyResolution(int idx) {
        int widths[] = {1280, 1600, 1920, 2560, 3840};
        int heights[] = {720, 900, 1080, 1440, 2160};
        g_config.windowWidth = widths[idx];
        g_config.windowHeight = heights[idx];
    }

    int getAnisotropicIndex() {
        switch (g_config.anisotropicFiltering) {
            case 1: return 0;
            case 2: return 1;
            case 4: return 2;
            case 8: return 3;
            case 16: return 4;
            default: return 3;
        }
    }

    void refreshSettingsUI() {
        renderDistanceSlider.value = static_cast<float>(g_config.renderDistance);
        fovSlider.value = static_cast<float>(g_config.fov);
        aaDropdown.selectedIndex = static_cast<int>(g_config.antiAliasing);
        textureQualityDropdown.selectedIndex = static_cast<int>(g_config.textureQuality);
        shadowQualityDropdown.selectedIndex = static_cast<int>(g_config.shadowQuality);
        aoQualityDropdown.selectedIndex = static_cast<int>(g_config.aoQuality);
        bloomCheckbox.checked = g_config.enableBloom;
        bloomIntensitySlider.value = g_config.bloomIntensity * 100;
        motionBlurCheckbox.checked = g_config.enableMotionBlur;
        upscaleDropdown.selectedIndex = static_cast<int>(g_config.upscaleMode);
        rendererDropdown.selectedIndex = static_cast<int>(g_config.renderer);
        hiZCheckbox.checked = g_config.enableHiZCulling;
        chunkSpeedSlider.value = static_cast<float>(g_config.maxChunksPerFrame);
        meshSpeedSlider.value = static_cast<float>(g_config.maxMeshesPerFrame);
        cloudsCheckbox.checked = g_config.enableClouds;
        cloudQualityDropdown.selectedIndex = static_cast<int>(g_config.cloudQuality);
        volumetricCloudsCheckbox.checked = (g_config.cloudStyle == CloudStyle::VOLUMETRIC);
    }

    void autoDetectHardware() {
        // Query OpenGL for GPU info
        const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));

        if (renderer) g_hardware.gpuName = renderer;
        if (vendor) g_hardware.gpuVendor = vendor;

        // Try to get VRAM (NVIDIA extension)
        GLint vramKB = 0;
        glGetIntegerv(0x9048, &vramKB);  // GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX
        if (vramKB > 0) {
            g_hardware.vramMB = vramKB / 1024;
        }

        g_hardware.classifyGPU();
        g_hardware.calculateRecommendations();
        g_config.autoTune();

        // Update preset dropdown and refresh all UI
        presetDropdown.selectedIndex = static_cast<int>(g_config.graphicsPreset);
        refreshSettingsUI();

        std::cout << "Hardware detected: " << g_hardware.gpuName << std::endl;
        std::cout << "Applied preset: " << GameConfig::getPresetName(g_config.graphicsPreset) << std::endl;
    }

    void launchGame() {
        g_config.save();

        // Get the path to this executable
        char exePath[MAX_PATH];
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);

        // Get the directory containing this executable
        std::string exeDir = exePath;
        size_t lastSlash = exeDir.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            exeDir = exeDir.substr(0, lastSlash + 1);
        }

        // Build path to VoxelEngine.exe in the same directory
        std::string gamePath = exeDir + "VoxelEngine.exe";

        // Launch the game executable
        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};

        if (CreateProcessA(gamePath.c_str(), nullptr, nullptr, nullptr, FALSE,
                          0, nullptr, exeDir.c_str(), &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            state = LauncherState::EXITING;
        } else {
            std::string errorMsg = "Failed to launch game!\nPath: " + gamePath;
            MessageBoxA(nullptr, errorMsg.c_str(), "Error", MB_OK | MB_ICONERROR);
        }
    }

    void handleInput() {
        glfwGetCursorPos(window, &mouseX, &mouseY);

        bool currentlyPressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        mouseJustPressed = currentlyPressed && !mousePressed;
        mouseJustReleased = !currentlyPressed && mousePressed;
        mousePressed = currentlyPressed;

        float mx = static_cast<float>(mouseX);
        float my = static_cast<float>(mouseY);

        if (state == LauncherState::MAIN_MENU) {
            handleButton(playButton, mx, my);
            handleButton(settingsButton, mx, my);
            handleButton(exitButton, mx, my);
        }
        else if (state == LauncherState::SETTINGS) {
            handleButton(backButton, mx, my);
            handleButton(applyButton, mx, my);
            handleButton(autoDetectButton, mx, my);

            for (int i = 0; i < 5; i++) {
                handleButton(tabButtons[i], mx, my);
            }

            handleSettingsInput(mx, my);
        }
    }

    void handleButton(Button& btn, float mx, float my) {
        btn.hovered = btn.contains(mx, my);
        btn.pressed = btn.hovered && mousePressed;

        if (btn.hovered && mouseJustReleased && btn.onClick) {
            btn.onClick();
        }
    }

    void handleSlider(Slider& slider, float mx, float my) {
        if (!slider.visible) return;

        if (slider.dragging) {
            slider.updateFromMouse(mx);
            if (!mousePressed) slider.dragging = false;
        }
        else if (slider.contains(mx, my) && mouseJustPressed) {
            slider.dragging = true;
            slider.updateFromMouse(mx);
        }
    }

    void handleCheckbox(Checkbox& cb, float mx, float my) {
        if (!cb.visible) return;

        cb.hovered = cb.contains(mx, my);
        if (cb.hovered && mouseJustReleased) {
            cb.checked = !cb.checked;
            if (cb.onChange) cb.onChange(cb.checked);
        }
    }

    void handleDropdown(Dropdown& dd, float mx, float my) {
        if (!dd.visible) return;

        dd.hovered = dd.contains(mx, my);

        if (dd.open) {
            dd.hoveredOption = -1;
            for (size_t i = 0; i < dd.options.size(); i++) {
                if (dd.containsOption(mx, my, static_cast<int>(i))) {
                    dd.hoveredOption = static_cast<int>(i);
                    if (mouseJustReleased) {
                        dd.selectedIndex = static_cast<int>(i);
                        dd.open = false;
                        if (dd.onChange) dd.onChange(dd.selectedIndex);
                    }
                }
            }

            if (mouseJustPressed && !dd.contains(mx, my) && dd.hoveredOption == -1) {
                dd.open = false;
            }
        }
        else if (dd.hovered && mouseJustReleased) {
            closeAllDropdowns();
            dd.open = true;
        }
    }

    void closeAllDropdowns() {
        resolutionDropdown.open = false;
        displayModeDropdown.open = false;
        presetDropdown.open = false;
        aaDropdown.open = false;
        textureQualityDropdown.open = false;
        anisotropicDropdown.open = false;
        shadowQualityDropdown.open = false;
        aoQualityDropdown.open = false;
        upscaleDropdown.open = false;
        cloudQualityDropdown.open = false;
        rendererDropdown.open = false;
    }

    void handleSettingsInput(float mx, float my) {
        switch (currentTab) {
            case SettingsTab::DISPLAY:
                handleDropdown(resolutionDropdown, mx, my);
                handleDropdown(displayModeDropdown, mx, my);
                handleCheckbox(vsyncCheckbox, mx, my);
                break;

            case SettingsTab::GRAPHICS:
                handleDropdown(presetDropdown, mx, my);
                handleSlider(renderDistanceSlider, mx, my);
                handleSlider(fovSlider, mx, my);
                handleDropdown(aaDropdown, mx, my);
                handleDropdown(textureQualityDropdown, mx, my);
                handleDropdown(anisotropicDropdown, mx, my);
                break;

            case SettingsTab::QUALITY:
                handleDropdown(shadowQualityDropdown, mx, my);
                handleDropdown(aoQualityDropdown, mx, my);
                handleCheckbox(bloomCheckbox, mx, my);
                handleSlider(bloomIntensitySlider, mx, my);
                handleCheckbox(motionBlurCheckbox, mx, my);
                handleDropdown(upscaleDropdown, mx, my);
                handleCheckbox(cloudsCheckbox, mx, my);
                handleDropdown(cloudQualityDropdown, mx, my);
                handleCheckbox(volumetricCloudsCheckbox, mx, my);
                break;

            case SettingsTab::ADVANCED:
                handleDropdown(rendererDropdown, mx, my);
                handleCheckbox(hiZCheckbox, mx, my);
                handleSlider(chunkSpeedSlider, mx, my);
                handleSlider(meshSpeedSlider, mx, my);
                break;

            case SettingsTab::CONTROLS:
                handleSlider(sensitivitySlider, mx, my);
                handleCheckbox(invertYCheckbox, mx, my);
                break;
        }
    }

    void render() {
        glClearColor(Colors::BG_DARK.r, Colors::BG_DARK.g, Colors::BG_DARK.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Draw background gradient
        ui.drawGradientRect(0, 0, 1024, 640, Colors::BG_DARK, Colors::BG_GRADIENT);

        if (state == LauncherState::MAIN_MENU) {
            renderMainMenu();
        }
        else if (state == LauncherState::SETTINGS) {
            renderSettings();
        }

        glfwSwapBuffers(window);
    }

    void renderMainMenu() {
        // Title (positioned to fit on screen)
        ui.drawText("FORGE", 1024 - 100 - 200, 80, Colors::ACCENT, 2.5f);
        ui.drawText("BOUND", 1024 - 100 - 200, 130, Colors::ACCENT, 2.5f);

        // Version
        ui.drawText("InfDev 2.0", 1024 - 100 - 200, 180, Colors::TEXT_DIM, 1.0f);

        // Menu buttons
        playButton.render(ui);
        settingsButton.render(ui);
        exitButton.render(ui);

        // Decorative line
        ui.drawRect(50, 590, 350, 2, Colors::DIVIDER);

        // Footer
        ui.drawText("Powered by OpenGL 3.3", 50, 600, Colors::TEXT_DIM, 0.9f);
    }

    void renderSettings() {
        // Header
        ui.drawText("SETTINGS", 50, 30, Colors::ACCENT, 2.0f);

        // Tab bar background
        ui.drawRect(40, 75, 680, 45, Colors::PANEL_BG);

        // Tabs
        for (int i = 0; i < 5; i++) {
            bool isActive = (static_cast<int>(currentTab) == i);
            if (isActive) {
                ui.drawRect(tabButtons[i].x, tabButtons[i].y, tabButtons[i].width, tabButtons[i].height, Colors::TAB_ACTIVE);
                ui.drawRect(tabButtons[i].x, tabButtons[i].y + tabButtons[i].height - 3, tabButtons[i].width, 3, Colors::ACCENT);
            }
            tabButtons[i].render(ui);
        }

        // Settings panel background
        ui.drawRect(40, 130, 970, 430, Colors::PANEL_BG);

        // Render current tab content
        switch (currentTab) {
            case SettingsTab::DISPLAY:
                renderDisplayTab();
                break;
            case SettingsTab::GRAPHICS:
                renderGraphicsTab();
                break;
            case SettingsTab::QUALITY:
                renderQualityTab();
                break;
            case SettingsTab::ADVANCED:
                renderAdvancedTab();
                break;
            case SettingsTab::CONTROLS:
                renderControlsTab();
                break;
        }

        // Bottom buttons
        backButton.render(ui);
        applyButton.render(ui);
        autoDetectButton.render(ui);

        // Hardware info
        if (!g_hardware.gpuName.empty() && g_hardware.gpuName != "Unknown") {
            std::string gpuInfo = "GPU: " + g_hardware.gpuName;
            if (gpuInfo.length() > 60) gpuInfo = gpuInfo.substr(0, 57) + "...";
            ui.drawText(gpuInfo, 280, 592, Colors::TEXT_DIM, 0.8f);
        }

        // Render APPLIED! animation (Borderlands 2 style)
        if (showAppliedFeedback) {
            float progress = appliedFeedbackTimer / APPLIED_FEEDBACK_DURATION;

            // Fade in quickly, fade out slowly
            float alpha;
            if (progress > 0.8f) {
                // Fade in (first 20% of time)
                alpha = (1.0f - progress) / 0.2f;
            } else {
                // Fade out (last 80% of time)
                alpha = progress / 0.8f;
            }
            alpha = std::min(1.0f, alpha);

            // Scale animation - starts big, settles to normal
            float scale = 3.0f + (1.0f - progress) * 0.8f;

            // Center position with slight offset for style
            float textX = 512;  // Center of 1024 width
            float textY = 320;  // Center of 640 height

            // Bright green color (like Borderlands)
            glm::vec4 textColor = glm::vec4(0.2f, 1.0f, 0.3f, alpha);
            glm::vec4 shadowColor = glm::vec4(0.0f, 0.0f, 0.0f, alpha * 0.7f);

            // Draw shadow first (offset)
            ui.drawTextCentered("APPLIED!", textX + 4, textY + 4, 0, shadowColor, scale);

            // Draw main text
            ui.drawTextCentered("APPLIED!", textX, textY, 0, textColor, scale);

            // Draw accent lines (Borderlands style)
            float lineAlpha = alpha * 0.8f;
            glm::vec4 lineColor = glm::vec4(0.2f, 1.0f, 0.3f, lineAlpha);
            float lineWidth = 200 * (1.0f + (1.0f - progress) * 0.3f);
            float lineHeight = 3.0f;

            // Lines above and below the text
            ui.drawRect(textX - lineWidth/2, textY - 30, lineWidth, lineHeight, lineColor);
            ui.drawRect(textX - lineWidth/2, textY + 50, lineWidth, lineHeight, lineColor);
        }
    }

    void renderDisplayTab() {
        resolutionDropdown.render(ui);
        displayModeDropdown.render(ui);
        vsyncCheckbox.render(ui);

        // Render dropdown options last (on top)
        resolutionDropdown.renderOptions(ui);
        displayModeDropdown.renderOptions(ui);
    }

    void renderGraphicsTab() {
        presetDropdown.render(ui);
        renderDistanceSlider.render(ui);
        fovSlider.render(ui);
        aaDropdown.render(ui);
        textureQualityDropdown.render(ui);
        anisotropicDropdown.render(ui);

        // Render dropdown options last (on top)
        presetDropdown.renderOptions(ui);
        aaDropdown.renderOptions(ui);
        textureQualityDropdown.renderOptions(ui);
        anisotropicDropdown.renderOptions(ui);
    }

    void renderQualityTab() {
        shadowQualityDropdown.render(ui);
        aoQualityDropdown.render(ui);
        bloomCheckbox.render(ui);
        bloomIntensitySlider.render(ui);
        motionBlurCheckbox.render(ui);
        upscaleDropdown.render(ui);
        cloudsCheckbox.render(ui);
        cloudQualityDropdown.render(ui);
        volumetricCloudsCheckbox.render(ui);

        // Render dropdown options last (on top)
        shadowQualityDropdown.renderOptions(ui);
        aoQualityDropdown.renderOptions(ui);
        upscaleDropdown.renderOptions(ui);
        cloudQualityDropdown.renderOptions(ui);
    }

    void renderAdvancedTab() {
        rendererDropdown.render(ui);
        hiZCheckbox.render(ui);
        chunkSpeedSlider.render(ui);
        meshSpeedSlider.render(ui);

        // Render dropdown options last (on top)
        rendererDropdown.renderOptions(ui);
    }

    void renderControlsTab() {
        sensitivitySlider.render(ui);
        invertYCheckbox.render(ui);
    }

    void run() {
        lastFrameTime = glfwGetTime();
        while (!glfwWindowShouldClose(window) && state != LauncherState::EXITING) {
            double currentTime = glfwGetTime();
            float deltaTime = static_cast<float>(currentTime - lastFrameTime);
            lastFrameTime = currentTime;

            glfwPollEvents();
            handleInput();
            update(deltaTime);
            render();
        }
    }

    void update(float deltaTime) {
        // Update APPLIED animation
        if (showAppliedFeedback) {
            appliedFeedbackTimer -= deltaTime;
            if (appliedFeedbackTimer <= 0.0f) {
                showAppliedFeedback = false;
                appliedFeedbackTimer = 0.0f;
            }
        }
    }

    void cleanup() {
        ui.cleanup();
        if (window) glfwDestroyWindow(window);
        glfwTerminate();
    }
};

// ============================================
// ENTRY POINT
// ============================================
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    LauncherApp app;

    if (!app.init()) {
        MessageBoxA(nullptr, "Failed to initialize launcher", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    app.run();
    app.cleanup();

    return 0;
}
