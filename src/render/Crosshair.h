#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>
#include "../core/Config.h"

// External config reference
extern GameConfig g_config;

class Crosshair {
public:
    GLuint VAO = 0;
    GLuint VBO = 0;
    GLuint shaderProgram = 0;
    GLint scaleLoc = -1;

    // Base crosshair size in NDC (scaled by guiScale)
    static constexpr float BASE_SIZE = 0.02f;

    void init() {
        // Simple crosshair vertices (2D lines in NDC) - will be scaled by shader
        float size = 1.0f;  // Unit size, scaled in shader
        float vertices[] = {
            // Horizontal line
            -size, 0.0f,
             size, 0.0f,
            // Vertical line
            0.0f, -size * 1.5f,  // Slightly longer vertically for aspect ratio
            0.0f,  size * 1.5f,
        };

        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);

        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

        // Create simple shader for crosshair with scale uniform
        const char* vertexSrc = R"(
            #version 460 core
            layout (location = 0) in vec2 aPos;
            uniform float scale;
            void main() {
                gl_Position = vec4(aPos * scale, 0.0, 1.0);
            }
        )";

        const char* fragmentSrc = R"(
            #version 460 core
            out vec4 FragColor;
            void main() {
                FragColor = vec4(1.0, 1.0, 1.0, 0.8);
            }
        )";

        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertexSrc, nullptr);
        glCompileShader(vertexShader);

        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragmentSrc, nullptr);
        glCompileShader(fragmentShader);

        shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vertexShader);
        glAttachShader(shaderProgram, fragmentShader);
        glLinkProgram(shaderProgram);

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        // Get uniform location
        scaleLoc = glGetUniformLocation(shaderProgram, "scale");
    }

    void render() {
        glUseProgram(shaderProgram);
        glBindVertexArray(VAO);

        // Set scale based on GUI scale
        float scale = BASE_SIZE * g_config.guiScale;
        glUniform1f(scaleLoc, scale);

        // Disable depth test for UI
        glDisable(GL_DEPTH_TEST);
        glLineWidth(2.0f * g_config.guiScale);  // Scale line width too
        glDrawArrays(GL_LINES, 0, 4);
        glEnable(GL_DEPTH_TEST);
    }

    void destroy() {
        if (VBO) glDeleteBuffers(1, &VBO);
        if (VAO) glDeleteVertexArrays(1, &VAO);
        if (shaderProgram) glDeleteProgram(shaderProgram);
    }
};
