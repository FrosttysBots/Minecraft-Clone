#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Renders a procedural crack overlay on blocks being mined
class MiningCrackOverlay {
public:
    GLuint VAO = 0;
    GLuint VBO = 0;
    GLuint shaderProgram = 0;
    GLint viewLoc = -1;
    GLint projectionLoc = -1;
    GLint modelLoc = -1;
    GLint progressLoc = -1;

    void init() {
        // Cube faces with UV coordinates for crack pattern
        // Slightly larger than 1.0 to render on top of block
        float s = 1.003f;
        float o = -0.0015f;

        // position (3) + uv (2) per vertex, 6 faces * 6 vertices = 36 vertices
        float vertices[] = {
            // Front face (Z+)
            o, o, s,  0, 0,
            s, o, s,  1, 0,
            s, s, s,  1, 1,
            o, o, s,  0, 0,
            s, s, s,  1, 1,
            o, s, s,  0, 1,

            // Back face (Z-)
            s, o, o,  0, 0,
            o, o, o,  1, 0,
            o, s, o,  1, 1,
            s, o, o,  0, 0,
            o, s, o,  1, 1,
            s, s, o,  0, 1,

            // Top face (Y+)
            o, s, o,  0, 0,
            o, s, s,  1, 0,
            s, s, s,  1, 1,
            o, s, o,  0, 0,
            s, s, s,  1, 1,
            s, s, o,  0, 1,

            // Bottom face (Y-)
            o, o, s,  0, 0,
            o, o, o,  1, 0,
            s, o, o,  1, 1,
            o, o, s,  0, 0,
            s, o, o,  1, 1,
            s, o, s,  0, 1,

            // Right face (X+)
            s, o, s,  0, 0,
            s, o, o,  1, 0,
            s, s, o,  1, 1,
            s, o, s,  0, 0,
            s, s, o,  1, 1,
            s, s, s,  0, 1,

            // Left face (X-)
            o, o, o,  0, 0,
            o, o, s,  1, 0,
            o, s, s,  1, 1,
            o, o, o,  0, 0,
            o, s, s,  1, 1,
            o, s, o,  0, 1,
        };

        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);

        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        // Position attribute
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        // UV attribute
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

        // Shader with procedural crack generation
        const char* vertexSrc = R"(
            #version 460 core
            layout (location = 0) in vec3 aPos;
            layout (location = 1) in vec2 aUV;

            uniform mat4 model;
            uniform mat4 view;
            uniform mat4 projection;

            out vec2 vUV;

            void main() {
                vUV = aUV;
                gl_Position = projection * view * model * vec4(aPos, 1.0);
            }
        )";

        const char* fragmentSrc = R"(
            #version 460 core
            in vec2 vUV;
            out vec4 FragColor;

            uniform float progress; // 0.0 to 1.0

            // Pseudo-random function
            float hash(vec2 p) {
                return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
            }

            // Value noise
            float noise(vec2 p) {
                vec2 i = floor(p);
                vec2 f = fract(p);
                f = f * f * (3.0 - 2.0 * f);

                float a = hash(i);
                float b = hash(i + vec2(1.0, 0.0));
                float c = hash(i + vec2(0.0, 1.0));
                float d = hash(i + vec2(1.0, 1.0));

                return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
            }

            // Fractal noise for crack pattern
            float crackNoise(vec2 uv, float detail) {
                float n = 0.0;
                float amp = 1.0;
                float freq = 1.0;
                for (int i = 0; i < 4; i++) {
                    n += amp * noise(uv * freq * detail);
                    amp *= 0.5;
                    freq *= 2.0;
                }
                return n;
            }

            void main() {
                if (progress < 0.01) {
                    discard;
                }

                // Generate crack pattern based on progress
                // More cracks appear as progress increases
                vec2 uv = vUV;

                // Create multiple crack lines
                float crack = 0.0;

                // Stage 1-3 cracks based on progress
                int stages = int(progress * 10.0);

                // Primary diagonal crack
                if (stages >= 1) {
                    float d1 = abs(uv.x + uv.y - 1.0 + crackNoise(uv * 3.0, 2.0) * 0.3 - 0.15);
                    crack = max(crack, smoothstep(0.08, 0.0, d1));
                }

                // Secondary crossing crack
                if (stages >= 2) {
                    float d2 = abs(uv.x - uv.y + crackNoise(uv * 4.0 + 1.0, 2.0) * 0.25 - 0.12);
                    crack = max(crack, smoothstep(0.06, 0.0, d2));
                }

                // Tertiary horizontal/vertical cracks
                if (stages >= 4) {
                    float d3 = abs(uv.y - 0.5 + crackNoise(uv * 5.0 + 2.0, 3.0) * 0.2 - 0.1);
                    crack = max(crack, smoothstep(0.05, 0.0, d3) * 0.7);
                }

                if (stages >= 5) {
                    float d4 = abs(uv.x - 0.5 + crackNoise(uv * 5.0 + 3.0, 3.0) * 0.2 - 0.1);
                    crack = max(crack, smoothstep(0.05, 0.0, d4) * 0.7);
                }

                // More fragmentation at higher progress
                if (stages >= 7) {
                    float d5 = abs(uv.x + uv.y * 0.5 - 0.75 + crackNoise(uv * 6.0 + 4.0, 2.0) * 0.15);
                    crack = max(crack, smoothstep(0.04, 0.0, d5) * 0.8);

                    float d6 = abs(uv.x * 0.5 + uv.y - 0.75 + crackNoise(uv * 6.0 + 5.0, 2.0) * 0.15);
                    crack = max(crack, smoothstep(0.04, 0.0, d6) * 0.8);
                }

                // Final stage - heavy fragmentation
                if (stages >= 9) {
                    for (int i = 0; i < 3; i++) {
                        float angle = float(i) * 1.047 + crackNoise(uv + float(i), 1.0) * 0.5;
                        vec2 dir = vec2(cos(angle), sin(angle));
                        float d = abs(dot(uv - 0.5, dir) + crackNoise(uv * 7.0 + float(i) * 2.0, 2.0) * 0.1);
                        crack = max(crack, smoothstep(0.03, 0.0, d) * 0.6);
                    }
                }

                if (crack < 0.01) {
                    discard;
                }

                // Dark cracks with slight transparency
                float alpha = crack * (0.5 + progress * 0.4);
                FragColor = vec4(0.0, 0.0, 0.0, alpha);
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
        progressLoc = glGetUniformLocation(shaderProgram, "progress");
    }

    void render(const glm::ivec3& blockPos, float progress, const glm::mat4& view, const glm::mat4& projection) {
        if (progress < 0.01f) return;

        glUseProgram(shaderProgram);

        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(blockPos));

        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glUniform1f(progressLoc, progress);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);

        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);

        glEnable(GL_CULL_FACE);
    }

    void destroy() {
        if (VBO) glDeleteBuffers(1, &VBO);
        if (VAO) glDeleteVertexArrays(1, &VAO);
        if (shaderProgram) glDeleteProgram(shaderProgram);
    }
};
