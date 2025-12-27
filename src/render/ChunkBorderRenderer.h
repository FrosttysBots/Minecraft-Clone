#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "../world/Chunk.h"

// Renders chunk boundaries as red wireframe boxes (F3+G in Minecraft)
class ChunkBorderRenderer {
public:
    GLuint VAO = 0;
    GLuint VBO = 0;
    GLuint shaderProgram = 0;
    GLint viewLoc = -1;
    GLint projectionLoc = -1;
    GLint modelLoc = -1;
    GLint colorLoc = -1;

    void init() {
        // Wireframe chunk vertices (lines)
        // Chunk is 16x256x16, but we only draw the outer border
        float w = static_cast<float>(CHUNK_SIZE_X);  // 16
        float h = static_cast<float>(CHUNK_SIZE_Y);  // 256
        float d = static_cast<float>(CHUNK_SIZE_Z);  // 16

        float vertices[] = {
            // Bottom face (y=0)
            0, 0, 0,      w, 0, 0,
            w, 0, 0,      w, 0, d,
            w, 0, d,      0, 0, d,
            0, 0, d,      0, 0, 0,

            // Top face (y=256)
            0, h, 0,      w, h, 0,
            w, h, 0,      w, h, d,
            w, h, d,      0, h, d,
            0, h, d,      0, h, 0,

            // Vertical edges (corners)
            0, 0, 0,      0, h, 0,
            w, 0, 0,      w, h, 0,
            w, 0, d,      w, h, d,
            0, 0, d,      0, h, d,

            // Horizontal grid lines at sub-chunk boundaries (every 16 blocks in Y)
            // Y=16
            0, 16, 0,     w, 16, 0,
            w, 16, 0,     w, 16, d,
            w, 16, d,     0, 16, d,
            0, 16, d,     0, 16, 0,
            // Y=32
            0, 32, 0,     w, 32, 0,
            w, 32, 0,     w, 32, d,
            w, 32, d,     0, 32, d,
            0, 32, d,     0, 32, 0,
            // Y=48
            0, 48, 0,     w, 48, 0,
            w, 48, 0,     w, 48, d,
            w, 48, d,     0, 48, d,
            0, 48, d,     0, 48, 0,
            // Y=64
            0, 64, 0,     w, 64, 0,
            w, 64, 0,     w, 64, d,
            w, 64, d,     0, 64, d,
            0, 64, d,     0, 64, 0,
            // Y=80
            0, 80, 0,     w, 80, 0,
            w, 80, 0,     w, 80, d,
            w, 80, d,     0, 80, d,
            0, 80, d,     0, 80, 0,
            // Y=96
            0, 96, 0,     w, 96, 0,
            w, 96, 0,     w, 96, d,
            w, 96, d,     0, 96, d,
            0, 96, d,     0, 96, 0,
            // Y=112
            0, 112, 0,    w, 112, 0,
            w, 112, 0,    w, 112, d,
            w, 112, d,    0, 112, d,
            0, 112, d,    0, 112, 0,
            // Y=128
            0, 128, 0,    w, 128, 0,
            w, 128, 0,    w, 128, d,
            w, 128, d,    0, 128, d,
            0, 128, d,    0, 128, 0,
        };

        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);

        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

        // Shader - simple colored lines
        const char* vertexSrc = R"(
            #version 460 core
            layout (location = 0) in vec3 aPos;

            uniform mat4 model;
            uniform mat4 view;
            uniform mat4 projection;

            void main() {
                gl_Position = projection * view * model * vec4(aPos, 1.0);
            }
        )";

        const char* fragmentSrc = R"(
            #version 460 core
            out vec4 FragColor;
            uniform vec4 color;
            void main() {
                FragColor = color;
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

        viewLoc = glGetUniformLocation(shaderProgram, "view");
        projectionLoc = glGetUniformLocation(shaderProgram, "projection");
        modelLoc = glGetUniformLocation(shaderProgram, "model");
        colorLoc = glGetUniformLocation(shaderProgram, "color");
    }

    // Render chunk borders around player
    void render(const glm::vec3& playerPos, int renderDistance,
                const glm::mat4& view, const glm::mat4& projection) {
        glUseProgram(shaderProgram);
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));

        glBindVertexArray(VAO);
        glLineWidth(1.5f);

        // Disable depth writing but keep depth testing
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Get player chunk position
        int playerChunkX = static_cast<int>(std::floor(playerPos.x / CHUNK_SIZE_X));
        int playerChunkZ = static_cast<int>(std::floor(playerPos.z / CHUNK_SIZE_Z));

        // Red color for chunk borders (Minecraft style)
        glm::vec4 borderColor(1.0f, 0.0f, 0.0f, 0.6f);
        // Yellow for the chunk the player is in
        glm::vec4 currentChunkColor(1.0f, 1.0f, 0.0f, 0.8f);

        // Only render borders for nearby chunks (within 3 chunks for performance)
        int borderDist = std::min(3, renderDistance);

        for (int cx = playerChunkX - borderDist; cx <= playerChunkX + borderDist; cx++) {
            for (int cz = playerChunkZ - borderDist; cz <= playerChunkZ + borderDist; cz++) {
                // Calculate world position of chunk corner
                float worldX = static_cast<float>(cx * CHUNK_SIZE_X);
                float worldZ = static_cast<float>(cz * CHUNK_SIZE_Z);

                glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(worldX, 0.0f, worldZ));
                glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

                // Highlight the current chunk differently
                if (cx == playerChunkX && cz == playerChunkZ) {
                    glUniform4fv(colorLoc, 1, glm::value_ptr(currentChunkColor));
                } else {
                    glUniform4fv(colorLoc, 1, glm::value_ptr(borderColor));
                }

                // 24 lines for outer box + 32 lines for sub-chunk boundaries = 56 lines = 112 vertices
                glDrawArrays(GL_LINES, 0, 24 + 64);  // 24 for box, 64 for 8 horizontal slices
            }
        }

        // Restore state
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glBindVertexArray(0);
    }

    void destroy() {
        if (VBO) glDeleteBuffers(1, &VBO);
        if (VAO) glDeleteVertexArrays(1, &VAO);
        if (shaderProgram) glDeleteProgram(shaderProgram);
    }
};
