#pragma once

// 3D Loading Screen
// Displays floating voxel blocks in a void space while world generates

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <random>
#include <cmath>

// A floating block in the loading scene
struct FloatingBlock {
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 rotationSpeed;
    float scale;
    glm::vec3 color;
    float orbitRadius;
    float orbitSpeed;
    float orbitOffset;
    float bobSpeed;
    float bobOffset;
};

class LoadingScreen3D {
public:
    // Shader program for the loading scene
    GLuint shaderProgram = 0;
    GLuint cubeVAO = 0;
    GLuint cubeVBO = 0;

    // Scene objects
    std::vector<FloatingBlock> blocks;

    // Camera
    float cameraDistance = 15.0f;
    float cameraHeight = 3.0f;
    float cameraOrbitAngle = 0.0f;
    float cameraOrbitSpeed = 0.15f;

    // Animation time
    float time = 0.0f;

    // Loading progress
    float progress = 0.0f;
    std::string statusText = "Generating world...";

    // Window dimensions
    int windowWidth = 1600;
    int windowHeight = 900;

    // Block colors (earthy/minecraft-like palette)
    std::vector<glm::vec3> blockColors = {
        glm::vec3(0.3f, 0.6f, 0.2f),   // Grass green
        glm::vec3(0.45f, 0.32f, 0.2f), // Dirt brown
        glm::vec3(0.5f, 0.5f, 0.5f),   // Stone gray
        glm::vec3(0.2f, 0.15f, 0.1f),  // Dark wood
        glm::vec3(0.6f, 0.55f, 0.4f),  // Sand
        glm::vec3(0.3f, 0.4f, 0.5f),   // Blue-gray (ore)
        glm::vec3(0.15f, 0.4f, 0.6f),  // Water blue
        glm::vec3(0.7f, 0.7f, 0.7f),   // Light stone
    };

    void init(int width, int height) {
        windowWidth = width;
        windowHeight = height;

        createShader();
        createCubeGeometry();
        generateBlocks();
    }

    void createShader() {
        const char* vertexShaderSrc = R"(
            #version 330 core
            layout(location = 0) in vec3 aPos;
            layout(location = 1) in vec3 aNormal;

            uniform mat4 model;
            uniform mat4 view;
            uniform mat4 projection;

            out vec3 FragPos;
            out vec3 Normal;

            void main() {
                FragPos = vec3(model * vec4(aPos, 1.0));
                Normal = mat3(transpose(inverse(model))) * aNormal;
                gl_Position = projection * view * model * vec4(aPos, 1.0);
            }
        )";

        const char* fragmentShaderSrc = R"(
            #version 330 core
            in vec3 FragPos;
            in vec3 Normal;

            uniform vec3 blockColor;
            uniform vec3 lightDir;
            uniform vec3 viewPos;
            uniform float ambientStrength;

            out vec4 FragColor;

            void main() {
                // Ambient
                vec3 ambient = ambientStrength * blockColor;

                // Diffuse
                vec3 norm = normalize(Normal);
                vec3 lightDirection = normalize(-lightDir);
                float diff = max(dot(norm, lightDirection), 0.0);
                vec3 diffuse = diff * blockColor * 0.6;

                // Specular
                vec3 viewDir = normalize(viewPos - FragPos);
                vec3 reflectDir = reflect(-lightDirection, norm);
                float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
                vec3 specular = spec * vec3(0.2);

                // Rim lighting for that ethereal look
                float rim = 1.0 - max(dot(viewDir, norm), 0.0);
                rim = pow(rim, 3.0);
                vec3 rimColor = rim * blockColor * 0.3;

                vec3 result = ambient + diffuse + specular + rimColor;

                // Add slight fog/glow based on distance
                float dist = length(FragPos);
                float fog = exp(-dist * 0.02);
                vec3 fogColor = vec3(0.02, 0.02, 0.05);
                result = mix(fogColor, result, fog);

                FragColor = vec4(result, 1.0);
            }
        )";

        // Compile vertex shader
        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertexShaderSrc, nullptr);
        glCompileShader(vertexShader);

        // Compile fragment shader
        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragmentShaderSrc, nullptr);
        glCompileShader(fragmentShader);

        // Link program
        shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vertexShader);
        glAttachShader(shaderProgram, fragmentShader);
        glLinkProgram(shaderProgram);

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
    }

    void createCubeGeometry() {
        // Cube vertices with normals
        float vertices[] = {
            // positions          // normals
            // Front face
            -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
             0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
             0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
             0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
            -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
            -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
            // Back face
            -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
             0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
             0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
             0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
            -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
            -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
            // Left face
            -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
            -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
            -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
            -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
            -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
            -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
            // Right face
             0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
             0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
             0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
             0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
             0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
             0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
            // Top face
            -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
            -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
             0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
             0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
             0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
            -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
            // Bottom face
            -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
             0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
            -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
             0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
            -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
             0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
        };

        glGenVertexArrays(1, &cubeVAO);
        glGenBuffers(1, &cubeVBO);

        glBindVertexArray(cubeVAO);
        glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        // Position attribute
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        // Normal attribute
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glBindVertexArray(0);
    }

    void generateBlocks() {
        blocks.clear();

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> radiusDist(3.0f, 12.0f);
        std::uniform_real_distribution<float> heightDist(-4.0f, 4.0f);
        std::uniform_real_distribution<float> scaleDist(0.3f, 1.2f);
        std::uniform_real_distribution<float> speedDist(0.1f, 0.5f);
        std::uniform_real_distribution<float> offsetDist(0.0f, 6.28f);
        std::uniform_int_distribution<int> colorDist(0, static_cast<int>(blockColors.size()) - 1);

        // Create floating blocks in orbital patterns
        int numBlocks = 40;
        for (int i = 0; i < numBlocks; i++) {
            FloatingBlock block;

            block.orbitRadius = radiusDist(gen);
            block.orbitSpeed = speedDist(gen) * (0.5f + 0.5f / block.orbitRadius);
            block.orbitOffset = offsetDist(gen);

            block.position = glm::vec3(0.0f, heightDist(gen), 0.0f);
            block.rotation = glm::vec3(offsetDist(gen), offsetDist(gen), offsetDist(gen));
            block.rotationSpeed = glm::vec3(speedDist(gen), speedDist(gen), speedDist(gen));
            block.scale = scaleDist(gen);
            block.color = blockColors[colorDist(gen)];
            block.bobSpeed = speedDist(gen) * 2.0f;
            block.bobOffset = offsetDist(gen);

            blocks.push_back(block);
        }

        // Add a few larger "feature" blocks near center
        for (int i = 0; i < 5; i++) {
            FloatingBlock block;
            block.orbitRadius = radiusDist(gen) * 0.4f;
            block.orbitSpeed = speedDist(gen) * 0.3f;
            block.orbitOffset = offsetDist(gen);
            block.position = glm::vec3(0.0f, heightDist(gen) * 0.5f, 0.0f);
            block.rotation = glm::vec3(offsetDist(gen), offsetDist(gen), offsetDist(gen));
            block.rotationSpeed = glm::vec3(speedDist(gen) * 0.5f, speedDist(gen) * 0.5f, speedDist(gen) * 0.5f);
            block.scale = scaleDist(gen) * 1.5f + 0.5f;
            block.color = blockColors[colorDist(gen)] * 1.2f; // Slightly brighter
            block.bobSpeed = speedDist(gen);
            block.bobOffset = offsetDist(gen);
            blocks.push_back(block);
        }
    }

    void update(float deltaTime) {
        time += deltaTime;

        // Rotate camera around the scene
        cameraOrbitAngle += cameraOrbitSpeed * deltaTime;

        // Update block rotations
        for (auto& block : blocks) {
            block.rotation += block.rotationSpeed * deltaTime;
        }
    }

    void setProgress(float p, const std::string& status = "") {
        progress = glm::clamp(p, 0.0f, 1.0f);
        if (!status.empty()) {
            statusText = status;
        }
    }

    void resize(int width, int height) {
        windowWidth = width;
        windowHeight = height;
    }

    void render(MenuUIRenderer* ui) {
        // Clear with dark background
        glClearColor(0.02f, 0.02f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Enable depth testing for 3D rendering
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        // Calculate camera position (orbiting)
        float camX = sin(cameraOrbitAngle) * cameraDistance;
        float camZ = cos(cameraOrbitAngle) * cameraDistance;
        glm::vec3 cameraPos(camX, cameraHeight + sin(time * 0.3f) * 1.0f, camZ);
        glm::vec3 cameraTarget(0.0f, 0.0f, 0.0f);

        // Create matrices
        glm::mat4 view = glm::lookAt(cameraPos, cameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection = glm::perspective(glm::radians(45.0f),
            static_cast<float>(windowWidth) / windowHeight, 0.1f, 100.0f);

        // Use shader
        glUseProgram(shaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, &view[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, &projection[0][0]);
        glUniform3f(glGetUniformLocation(shaderProgram, "lightDir"), -0.5f, -1.0f, -0.3f);
        glUniform3fv(glGetUniformLocation(shaderProgram, "viewPos"), 1, &cameraPos[0]);
        glUniform1f(glGetUniformLocation(shaderProgram, "ambientStrength"), 0.3f);

        // Render blocks
        glBindVertexArray(cubeVAO);

        for (const auto& block : blocks) {
            // Calculate animated position
            float orbitAngle = time * block.orbitSpeed + block.orbitOffset;
            float x = sin(orbitAngle) * block.orbitRadius;
            float z = cos(orbitAngle) * block.orbitRadius;
            float y = block.position.y + sin(time * block.bobSpeed + block.bobOffset) * 0.5f;

            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, glm::vec3(x, y, z));
            model = glm::rotate(model, block.rotation.x + time * block.rotationSpeed.x, glm::vec3(1, 0, 0));
            model = glm::rotate(model, block.rotation.y + time * block.rotationSpeed.y, glm::vec3(0, 1, 0));
            model = glm::rotate(model, block.rotation.z + time * block.rotationSpeed.z, glm::vec3(0, 0, 1));
            model = glm::scale(model, glm::vec3(block.scale));

            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &model[0][0]);
            glUniform3fv(glGetUniformLocation(shaderProgram, "blockColor"), 1, &block.color[0]);

            glDrawArrays(GL_TRIANGLES, 0, 36);
        }

        glBindVertexArray(0);

        // Disable depth test for 2D UI overlay
        glDisable(GL_DEPTH_TEST);

        // Render 2D UI overlay
        if (ui) {
            renderUI(ui);
        }
    }

    void renderUI(MenuUIRenderer* ui) {
        float centerX = windowWidth / 2.0f;
        float centerY = windowHeight / 2.0f;

        // Title text
        ui->drawTextCentered("GENERATING WORLD", 0, windowHeight * 0.25f,
                            static_cast<float>(windowWidth), MenuColors::ACCENT, 2.5f);

        // Status text
        ui->drawTextCentered(statusText, 0, windowHeight * 0.35f,
                            static_cast<float>(windowWidth), MenuColors::TEXT_DIM, 1.2f);

        // Progress bar background
        float barWidth = 500.0f;
        float barHeight = 20.0f;
        float barX = centerX - barWidth / 2.0f;
        float barY = windowHeight * 0.65f;

        // Outer glow/border
        ui->drawRect(barX - 4, barY - 4, barWidth + 8, barHeight + 8,
                    glm::vec4(0.1f, 0.15f, 0.2f, 0.8f));

        // Background
        ui->drawRect(barX, barY, barWidth, barHeight,
                    glm::vec4(0.05f, 0.05f, 0.08f, 1.0f));

        // Progress fill with gradient effect
        if (progress > 0.0f) {
            float fillWidth = barWidth * progress;

            // Main fill
            glm::vec4 fillColor = glm::mix(
                glm::vec4(0.2f, 0.5f, 0.8f, 1.0f),  // Blue
                glm::vec4(0.3f, 0.8f, 0.4f, 1.0f),  // Green
                progress
            );
            ui->drawRect(barX, barY, fillWidth, barHeight, fillColor);

            // Highlight on top
            ui->drawRect(barX, barY, fillWidth, barHeight * 0.3f,
                        glm::vec4(1.0f, 1.0f, 1.0f, 0.2f));
        }

        // Border
        ui->drawRectOutline(barX, barY, barWidth, barHeight, MenuColors::ACCENT, 2.0f);

        // Percentage text
        int percent = static_cast<int>(progress * 100);
        std::string percentStr = std::to_string(percent) + "%";
        ui->drawTextCentered(percentStr, 0, barY + barHeight + 15,
                            static_cast<float>(windowWidth), MenuColors::TEXT, 1.5f);

        // Animated dots for "activity" indicator
        int numDots = static_cast<int>(time * 3) % 4;
        std::string dots = "";
        for (int i = 0; i < numDots; i++) dots += ".";
        ui->drawTextCentered(dots, 0, windowHeight * 0.75f,
                            static_cast<float>(windowWidth), MenuColors::TEXT_DIM, 2.0f);

        // Tip text at bottom
        std::vector<std::string> tips = {
            "Tip: Press F3 to toggle debug info",
            "Tip: Use scroll wheel to change block type",
            "Tip: Left click to break, right click to place",
            "Tip: Press ESC to pause and save your game",
            "Tip: Hold Shift to sprint"
        };
        int tipIndex = static_cast<int>(time / 5.0f) % tips.size();
        ui->drawTextCentered(tips[tipIndex], 0, windowHeight * 0.9f,
                            static_cast<float>(windowWidth),
                            glm::vec4(0.5f, 0.5f, 0.6f, 0.8f), 1.0f);
    }

    void cleanup() {
        if (cubeVAO) {
            glDeleteVertexArrays(1, &cubeVAO);
            cubeVAO = 0;
        }
        if (cubeVBO) {
            glDeleteBuffers(1, &cubeVBO);
            cubeVBO = 0;
        }
        if (shaderProgram) {
            glDeleteProgram(shaderProgram);
            shaderProgram = 0;
        }
    }
};
