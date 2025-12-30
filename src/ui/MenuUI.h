#pragma once

// Menu UI System for ForgeBound
// Adapted from GLLauncher.cpp for in-game menus

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "stb_easy_font.h"

#include <string>
#include <vector>
#include <functional>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <iostream>

// ============================================
// COLOR SCHEME (Dark Theme)
// ============================================
namespace MenuColors {
    const glm::vec4 BG_DARK      = {0.08f, 0.08f, 0.10f, 1.0f};
    const glm::vec4 BG_OVERLAY   = {0.0f, 0.0f, 0.0f, 0.7f};
    const glm::vec4 PANEL_BG     = {0.10f, 0.10f, 0.12f, 0.95f};
    const glm::vec4 BUTTON_BG    = {0.15f, 0.15f, 0.18f, 1.0f};
    const glm::vec4 BUTTON_HOVER = {0.22f, 0.22f, 0.26f, 1.0f};
    const glm::vec4 BUTTON_PRESS = {0.18f, 0.18f, 0.21f, 1.0f};
    const glm::vec4 BUTTON_DISABLED = {0.12f, 0.12f, 0.14f, 0.6f};
    const glm::vec4 ACCENT       = {0.85f, 0.65f, 0.25f, 1.0f};  // Gold
    const glm::vec4 ACCENT_DIM   = {0.65f, 0.50f, 0.20f, 1.0f};
    const glm::vec4 TEXT         = {0.92f, 0.92f, 0.92f, 1.0f};
    const glm::vec4 TEXT_DIM     = {0.60f, 0.60f, 0.62f, 1.0f};
    const glm::vec4 TEXT_DISABLED = {0.40f, 0.40f, 0.42f, 1.0f};
    const glm::vec4 DIVIDER      = {0.25f, 0.25f, 0.28f, 1.0f};
    const glm::vec4 SLIDER_BG    = {0.20f, 0.20f, 0.22f, 1.0f};
    const glm::vec4 SLIDER_FILL  = {0.75f, 0.55f, 0.20f, 1.0f};
    const glm::vec4 INPUT_BG     = {0.12f, 0.12f, 0.15f, 1.0f};
    const glm::vec4 INPUT_FOCUS  = {0.15f, 0.15f, 0.18f, 1.0f};
    const glm::vec4 ERROR_COLOR  = {0.85f, 0.25f, 0.25f, 1.0f};
    const glm::vec4 SUCCESS      = {0.25f, 0.75f, 0.35f, 1.0f};
}

// ============================================
// MENU UI RENDERER
// ============================================
class MenuUIRenderer {
public:
    GLuint shaderProgram = 0;
    GLuint quadVAO = 0, quadVBO = 0;
    GLuint textVAO = 0, textVBO = 0;
    GLuint texVAO = 0, texVBO = 0;  // For texture rendering
    GLuint texShaderProgram = 0;    // Texture shader
    glm::mat4 projection;
    int windowWidth = 1920;
    int windowHeight = 1080;
    bool initialized = false;

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

    // Texture shaders
    const char* texVertexShaderSrc = R"(
        #version 330 core
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec2 aTexCoord;
        out vec2 TexCoord;
        uniform mat4 projection;
        uniform mat4 model;
        void main() {
            gl_Position = projection * model * vec4(aPos, 0.0, 1.0);
            TexCoord = aTexCoord;
        }
    )";

    const char* texFragmentShaderSrc = R"(
        #version 330 core
        in vec2 TexCoord;
        out vec4 FragColor;
        uniform sampler2D tex;
        void main() {
            FragColor = texture(tex, TexCoord);
        }
    )";

    GLuint compileShader(GLenum type, const char* source) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);

        // Check for compilation errors
        GLint success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetShaderInfoLog(shader, 512, nullptr, infoLog);
            std::cerr << "MenuUI shader compilation failed: " << infoLog << std::endl;
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

        // Check for linking errors
        GLint success;
        glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetProgramInfoLog(shaderProgram, 512, nullptr, infoLog);
            std::cerr << "MenuUI program linking failed: " << infoLog << std::endl;
            return false;
        }

        glDeleteShader(vs);
        glDeleteShader(fs);

        std::cout << "MenuUI shaders compiled and linked successfully" << std::endl;

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

        // Create texture shader program
        GLuint texVs = compileShader(GL_VERTEX_SHADER, texVertexShaderSrc);
        GLuint texFs = compileShader(GL_FRAGMENT_SHADER, texFragmentShaderSrc);
        texShaderProgram = glCreateProgram();
        glAttachShader(texShaderProgram, texVs);
        glAttachShader(texShaderProgram, texFs);
        glLinkProgram(texShaderProgram);
        glDeleteShader(texVs);
        glDeleteShader(texFs);

        // Create texture quad VAO/VBO (with UV coordinates)
        float texQuadVertices[] = {
            // pos      // uv
            0.0f, 0.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            0.0f, 0.0f, 0.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            0.0f, 1.0f, 0.0f, 1.0f
        };

        glGenVertexArrays(1, &texVAO);
        glGenBuffers(1, &texVBO);
        glBindVertexArray(texVAO);
        glBindBuffer(GL_ARRAY_BUFFER, texVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(texQuadVertices), texQuadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

        glBindVertexArray(0);
        initialized = true;
        return true;
    }

    void resize(int width, int height) {
        windowWidth = width;
        windowHeight = height;
        projection = glm::ortho(0.0f, static_cast<float>(width),
                               static_cast<float>(height), 0.0f, -1.0f, 1.0f);
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

    // Draw a texture at the specified position and size
    void drawTexture(GLuint textureID, float x, float y, float w, float h) {
        if (textureID == 0) return;

        glUseProgram(texShaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(texShaderProgram, "projection"), 1, GL_FALSE, &projection[0][0]);

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(x, y, 0.0f));
        model = glm::scale(model, glm::vec3(w, h, 1.0f));
        glUniformMatrix4fv(glGetUniformLocation(texShaderProgram, "model"), 1, GL_FALSE, &model[0][0]);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureID);
        glUniform1i(glGetUniformLocation(texShaderProgram, "tex"), 0);

        glBindVertexArray(texVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }

    float getTextWidth(const std::string& text, float scale = 1.0f) {
        // stb_easy_font uses ~6 pixels per character width at scale 1
        return text.length() * 6.0f * scale * 2.0f;
    }

    float getTextHeight(float scale = 1.0f) {
        return 12.0f * scale * 2.0f;
    }

    void drawText(const std::string& text, float x, float y, const glm::vec4& color, float scale = 1.0f) {
        static std::vector<float> vertexBuffer(60000);

        int numQuads = stb_easy_font_print(0, 0, const_cast<char*>(text.c_str()), nullptr,
                                           vertexBuffer.data(), static_cast<int>(vertexBuffer.size()));

        if (numQuads == 0) return;

        // Convert quads to triangles
        std::vector<float> triangleVerts;
        triangleVerts.reserve(numQuads * 6 * 2);

        float* ptr = vertexBuffer.data();
        for (int q = 0; q < numQuads; q++) {
            float x0 = ptr[0], y0 = ptr[1];
            float x1 = ptr[4], y1 = ptr[5];
            float x2 = ptr[8], y2 = ptr[9];
            float x3 = ptr[12], y3 = ptr[13];

            triangleVerts.push_back(x0); triangleVerts.push_back(y0);
            triangleVerts.push_back(x1); triangleVerts.push_back(y1);
            triangleVerts.push_back(x2); triangleVerts.push_back(y2);

            triangleVerts.push_back(x0); triangleVerts.push_back(y0);
            triangleVerts.push_back(x2); triangleVerts.push_back(y2);
            triangleVerts.push_back(x3); triangleVerts.push_back(y3);

            ptr += 16;
        }

        glUseProgram(shaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, &projection[0][0]);

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(x, y, 0.0f));
        model = glm::scale(model, glm::vec3(scale * 2.0f, scale * 2.0f, 1.0f));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &model[0][0]);

        glUniform4fv(glGetUniformLocation(shaderProgram, "color"), 1, &color[0]);

        glBindVertexArray(textVAO);
        glBindBuffer(GL_ARRAY_BUFFER, textVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, triangleVerts.size() * sizeof(float), triangleVerts.data());
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(triangleVerts.size() / 2));
    }

    void drawTextCentered(const std::string& text, float x, float y, float width,
                          const glm::vec4& color, float scale = 1.0f) {
        float textWidth = getTextWidth(text, scale);
        drawText(text, x + (width - textWidth) / 2, y, color, scale);
    }

    void drawTextRightAligned(const std::string& text, float x, float y, float width,
                              const glm::vec4& color, float scale = 1.0f) {
        float textWidth = getTextWidth(text, scale);
        drawText(text, x + width - textWidth - 10, y, color, scale);
    }

    // Draw a tooltip box with text - positions itself to stay on screen
    void drawTooltip(const std::string& text, float mouseX, float mouseY) {
        if (text.empty()) return;

        float padding = 10.0f;
        float textScale = 0.9f;
        float textWidth = getTextWidth(text, textScale);
        float boxWidth = textWidth + padding * 2;
        float boxHeight = 28.0f;

        // Position tooltip to the right of cursor, or left if it would go off screen
        float tooltipX = mouseX + 15;
        float tooltipY = mouseY - boxHeight - 5;

        // Keep tooltip on screen
        if (tooltipX + boxWidth > windowWidth - 10) {
            tooltipX = mouseX - boxWidth - 10;
        }
        if (tooltipY < 10) {
            tooltipY = mouseY + 25;
        }

        // Draw background with slight transparency
        glm::vec4 bgColor = {0.05f, 0.05f, 0.08f, 0.95f};
        glm::vec4 borderColor = MenuColors::ACCENT_DIM;

        drawRect(tooltipX, tooltipY, boxWidth, boxHeight, bgColor);
        drawRectOutline(tooltipX, tooltipY, boxWidth, boxHeight, borderColor, 1.0f);
        drawText(text, tooltipX + padding, tooltipY + 8, MenuColors::TEXT, textScale);
    }

    // Draw a progress bar (used for VRAM display)
    void drawProgressBar(float x, float y, float width, float height,
                         float progress, const glm::vec4& fillColor,
                         const std::string& label = "", const std::string& valueText = "") {
        // Background
        drawRect(x, y, width, height, MenuColors::SLIDER_BG);

        // Fill
        float fillWidth = width * std::clamp(progress, 0.0f, 1.0f);
        if (fillWidth > 0) {
            drawRect(x, y, fillWidth, height, fillColor);
        }

        // Border
        drawRectOutline(x, y, width, height, MenuColors::DIVIDER, 1.0f);

        // Label on left
        if (!label.empty()) {
            drawText(label, x, y - 22, MenuColors::TEXT, 0.9f);
        }

        // Value on right
        if (!valueText.empty()) {
            float textWidth = getTextWidth(valueText, 0.85f);
            drawText(valueText, x + width - textWidth, y - 22, MenuColors::TEXT_DIM, 0.85f);
        }
    }

    void cleanup() {
        if (shaderProgram) glDeleteProgram(shaderProgram);
        if (quadVAO) glDeleteVertexArrays(1, &quadVAO);
        if (quadVBO) glDeleteBuffers(1, &quadVBO);
        if (textVAO) glDeleteVertexArrays(1, &textVAO);
        if (textVBO) glDeleteBuffers(1, &textVBO);
        initialized = false;
    }

    void beginFrame() {
        // Make sure we're rendering to the default framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glViewport(0, 0, windowWidth, windowHeight);
    }

    void endFrame() {
        glEnable(GL_DEPTH_TEST);
    }
};

// ============================================
// UI ELEMENTS
// ============================================

struct MenuButton {
    float x, y, width, height;
    std::string text;
    std::function<void()> onClick;
    bool hovered = false;
    bool pressed = false;
    bool visible = true;
    bool enabled = true;
    float textScale = 1.2f;
    std::string tooltip;  // Tooltip text shown on hover

    bool contains(float mx, float my) const {
        return visible && enabled && mx >= x && mx <= x + width && my >= y && my <= y + height;
    }

    void render(MenuUIRenderer& ui) {
        if (!visible) return;

        glm::vec4 bgColor;
        glm::vec4 textColor;

        if (!enabled) {
            bgColor = MenuColors::BUTTON_DISABLED;
            textColor = MenuColors::TEXT_DISABLED;
        } else if (pressed) {
            bgColor = MenuColors::BUTTON_PRESS;
            textColor = MenuColors::ACCENT;
        } else if (hovered) {
            bgColor = MenuColors::BUTTON_HOVER;
            textColor = MenuColors::ACCENT;
        } else {
            bgColor = MenuColors::BUTTON_BG;
            textColor = MenuColors::TEXT;
        }

        ui.drawRect(x, y, width, height, bgColor);

        if (hovered && enabled) {
            ui.drawRectOutline(x, y, width, height, MenuColors::ACCENT, 2.0f);
        }

        ui.drawTextCentered(text, x, y + height / 2 - 8 * textScale, width, textColor, textScale);
    }
};

struct MenuSlider {
    float x, y, width, height;
    std::string label;
    float minVal, maxVal, value;
    std::function<void(float)> onChange;
    bool dragging = false;
    bool visible = true;
    bool showIntValue = true;
    bool hovered = false;
    std::string tooltip;  // Tooltip text shown on hover

    bool contains(float mx, float my) const {
        return visible && mx >= x && mx <= x + width && my >= y && my <= y + height;
    }

    void render(MenuUIRenderer& ui) {
        if (!visible) return;

        // Label
        ui.drawText(label, x, y - 24, MenuColors::TEXT, 1.0f);

        // Background track
        ui.drawRect(x, y + height / 2 - 4, width, 8, MenuColors::SLIDER_BG);

        // Filled portion
        float fillWidth = (value - minVal) / (maxVal - minVal) * width;
        ui.drawRect(x, y + height / 2 - 4, fillWidth, 8, MenuColors::SLIDER_FILL);

        // Handle
        float handleX = x + fillWidth - 8;
        glm::vec4 handleColor = dragging ? MenuColors::ACCENT : MenuColors::TEXT;
        ui.drawRect(handleX, y, 16, height, handleColor);

        // Value text
        std::stringstream ss;
        if (showIntValue) {
            ss << static_cast<int>(value);
        } else {
            ss.precision(2);
            ss << std::fixed << value;
        }
        ui.drawText(ss.str(), x + width + 15, y + 4, MenuColors::TEXT_DIM, 1.0f);
    }

    void updateFromMouse(float mx) {
        float t = (mx - x) / width;
        t = std::clamp(t, 0.0f, 1.0f);
        value = minVal + t * (maxVal - minVal);
        if (onChange) onChange(value);
    }
};

struct MenuCheckbox {
    float x, y, size;
    std::string label;
    bool checked;
    std::function<void(bool)> onChange;
    bool hovered = false;
    bool visible = true;
    std::string tooltip;  // Tooltip text shown on hover

    bool contains(float mx, float my) const {
        return visible && mx >= x && mx <= x + size && my >= y && my <= y + size;
    }

    void render(MenuUIRenderer& ui) {
        if (!visible) return;

        glm::vec4 boxColor = hovered ? MenuColors::BUTTON_HOVER : MenuColors::BUTTON_BG;
        ui.drawRect(x, y, size, size, boxColor);
        ui.drawRectOutline(x, y, size, size, hovered ? MenuColors::ACCENT : MenuColors::DIVIDER, 2.0f);

        if (checked) {
            float padding = 5;
            ui.drawRect(x + padding, y + padding, size - padding * 2, size - padding * 2, MenuColors::ACCENT);
        }

        ui.drawText(label, x + size + 12, y + 4, MenuColors::TEXT, 1.0f);
    }
};

struct MenuDropdown {
    float x, y, width, height;
    std::string label;
    std::vector<std::string> options;
    int selectedIndex = 0;
    std::function<void(int)> onChange;
    bool open = false;
    bool hovered = false;
    int hoveredOption = -1;
    bool visible = true;
    std::string tooltip;  // Tooltip text shown on hover

    bool contains(float mx, float my) const {
        return visible && mx >= x && mx <= x + width && my >= y && my <= y + height;
    }

    bool containsOption(float mx, float my, int index) const {
        if (!open || !visible) return false;
        float optY = y + height + index * height;
        return mx >= x && mx <= x + width && my >= optY && my <= optY + height;
    }

    void render(MenuUIRenderer& ui) {
        if (!visible) return;

        // Label
        ui.drawText(label, x, y - 24, MenuColors::TEXT, 1.0f);

        // Main box
        glm::vec4 boxColor = hovered || open ? MenuColors::BUTTON_HOVER : MenuColors::BUTTON_BG;
        ui.drawRect(x, y, width, height, boxColor);
        ui.drawRectOutline(x, y, width, height, open ? MenuColors::ACCENT : MenuColors::DIVIDER, 1.0f);

        // Selected text
        if (selectedIndex >= 0 && selectedIndex < static_cast<int>(options.size())) {
            ui.drawText(options[selectedIndex], x + 10, y + height / 2 - 7, MenuColors::TEXT, 1.0f);
        }

        // Arrow
        ui.drawText(open ? "^" : "v", x + width - 20, y + height / 2 - 7, MenuColors::TEXT_DIM, 1.0f);
    }

    void renderOptions(MenuUIRenderer& ui) {
        if (!visible || !open) return;

        float totalHeight = static_cast<float>(options.size()) * height;
        ui.drawRect(x, y + height, width, totalHeight, MenuColors::PANEL_BG);

        for (size_t i = 0; i < options.size(); i++) {
            float optY = y + height + static_cast<float>(i) * height;
            glm::vec4 optColor = (static_cast<int>(i) == hoveredOption) ? MenuColors::BUTTON_HOVER : MenuColors::PANEL_BG;
            ui.drawRect(x, optY, width, height, optColor);
            ui.drawText(options[i], x + 10, optY + height / 2 - 7, MenuColors::TEXT, 1.0f);
        }
        ui.drawRectOutline(x, y + height, width, totalHeight, MenuColors::ACCENT, 1.0f);
    }
};

struct MenuTextInput {
    float x, y, width, height;
    std::string label;
    std::string text;
    std::string placeholder;
    std::function<void(const std::string&)> onChange;
    bool focused = false;
    bool hovered = false;
    bool visible = true;
    size_t cursorPos = 0;
    size_t maxLength = 64;
    float cursorBlinkTime = 0.0f;
    bool showCursor = true;
    std::string tooltip;  // Tooltip text shown on hover

    bool contains(float mx, float my) const {
        return visible && mx >= x && mx <= x + width && my >= y && my <= y + height;
    }

    void render(MenuUIRenderer& ui, float deltaTime) {
        if (!visible) return;

        // Label
        ui.drawText(label, x, y - 24, MenuColors::TEXT, 1.0f);

        // Background
        glm::vec4 bgColor = focused ? MenuColors::INPUT_FOCUS : MenuColors::INPUT_BG;
        ui.drawRect(x, y, width, height, bgColor);
        ui.drawRectOutline(x, y, width, height, focused ? MenuColors::ACCENT : MenuColors::DIVIDER, 1.0f);

        // Text or placeholder
        if (text.empty() && !focused) {
            ui.drawText(placeholder, x + 10, y + height / 2 - 7, MenuColors::TEXT_DIM, 1.0f);
        } else {
            ui.drawText(text, x + 10, y + height / 2 - 7, MenuColors::TEXT, 1.0f);
        }

        // Cursor
        if (focused) {
            cursorBlinkTime += deltaTime;
            if (cursorBlinkTime > 1.0f) cursorBlinkTime = 0.0f;
            showCursor = cursorBlinkTime < 0.5f;

            if (showCursor) {
                float cursorX = x + 10 + ui.getTextWidth(text.substr(0, cursorPos), 1.0f);
                ui.drawRect(cursorX, y + 6, 2, height - 12, MenuColors::ACCENT);
            }
        }
    }

    void handleKeyInput(int key, int action, int mods) {
        if (!focused || action == 0) return;  // GLFW_RELEASE = 0

        if (key == 259) {  // GLFW_KEY_BACKSPACE
            if (cursorPos > 0) {
                text.erase(cursorPos - 1, 1);
                cursorPos--;
                if (onChange) onChange(text);
            }
        }
        else if (key == 261) {  // GLFW_KEY_DELETE
            if (cursorPos < text.length()) {
                text.erase(cursorPos, 1);
                if (onChange) onChange(text);
            }
        }
        else if (key == 263) {  // GLFW_KEY_LEFT
            if (cursorPos > 0) cursorPos--;
        }
        else if (key == 262) {  // GLFW_KEY_RIGHT
            if (cursorPos < text.length()) cursorPos++;
        }
        else if (key == 268) {  // GLFW_KEY_HOME
            cursorPos = 0;
        }
        else if (key == 269) {  // GLFW_KEY_END
            cursorPos = text.length();
        }
    }

    void handleCharInput(unsigned int codepoint) {
        if (!focused) return;
        if (text.length() >= maxLength) return;
        if (codepoint < 32 || codepoint > 126) return;  // Printable ASCII only

        text.insert(cursorPos, 1, static_cast<char>(codepoint));
        cursorPos++;
        if (onChange) onChange(text);
    }
};

// ============================================
// MENU INPUT HANDLER
// ============================================
class MenuInputHandler {
public:
    double mouseX = 0, mouseY = 0;
    bool mousePressed = false;
    bool mouseJustPressed = false;
    bool mouseJustReleased = false;
    MenuTextInput* focusedInput = nullptr;

    void update(double mx, double my, bool pressed) {
        mouseX = mx;
        mouseY = my;
        bool wasPressed = mousePressed;
        mousePressed = pressed;
        mouseJustPressed = pressed && !wasPressed;
        mouseJustReleased = !pressed && wasPressed;
    }

    void handleButton(MenuButton& btn) {
        float mx = static_cast<float>(mouseX);
        float my = static_cast<float>(mouseY);

        btn.hovered = btn.contains(mx, my);
        btn.pressed = btn.hovered && mousePressed;

        if (btn.hovered && mouseJustReleased && btn.onClick && btn.enabled) {
            btn.onClick();
        }
    }

    void handleSlider(MenuSlider& slider) {
        float mx = static_cast<float>(mouseX);
        float my = static_cast<float>(mouseY);

        if (slider.dragging) {
            slider.updateFromMouse(mx);
            if (!mousePressed) slider.dragging = false;
        }
        else if (slider.contains(mx, my) && mouseJustPressed) {
            slider.dragging = true;
            slider.updateFromMouse(mx);
        }
    }

    void handleCheckbox(MenuCheckbox& cb) {
        float mx = static_cast<float>(mouseX);
        float my = static_cast<float>(mouseY);

        cb.hovered = cb.contains(mx, my);
        if (cb.hovered && mouseJustReleased) {
            cb.checked = !cb.checked;
            if (cb.onChange) cb.onChange(cb.checked);
        }
    }

    void handleDropdown(MenuDropdown& dd, std::vector<MenuDropdown*>& allDropdowns) {
        float mx = static_cast<float>(mouseX);
        float my = static_cast<float>(mouseY);

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
            // Close all other dropdowns first
            for (auto* other : allDropdowns) {
                if (other != &dd) other->open = false;
            }
            dd.open = true;
        }
    }

    void handleTextInput(MenuTextInput& input) {
        float mx = static_cast<float>(mouseX);
        float my = static_cast<float>(mouseY);

        input.hovered = input.contains(mx, my);

        if (mouseJustPressed) {
            if (input.hovered) {
                // Focus this input
                if (focusedInput && focusedInput != &input) {
                    focusedInput->focused = false;
                }
                input.focused = true;
                input.cursorPos = input.text.length();
                focusedInput = &input;
            } else if (input.focused) {
                // Unfocus if clicking elsewhere
                input.focused = false;
                if (focusedInput == &input) focusedInput = nullptr;
            }
        }
    }

    void clearFocus() {
        if (focusedInput) {
            focusedInput->focused = false;
            focusedInput = nullptr;
        }
    }
};
