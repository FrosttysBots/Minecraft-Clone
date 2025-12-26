#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

class BlockHighlight {
public:
    GLuint VAO = 0;
    GLuint VBO = 0;
    GLuint shaderProgram = 0;
    GLint viewLoc = -1;
    GLint projectionLoc = -1;
    GLint modelLoc = -1;

    void init() {
        // Wireframe cube vertices (lines)
        // Slightly larger than 1.0 to avoid z-fighting
        float s = 1.002f;
        float o = -0.001f; // Small offset
        float vertices[] = {
            // Bottom face
            o, o, o,      s, o, o,
            s, o, o,      s, o, s,
            s, o, s,      o, o, s,
            o, o, s,      o, o, o,

            // Top face
            o, s, o,      s, s, o,
            s, s, o,      s, s, s,
            s, s, s,      o, s, s,
            o, s, s,      o, s, o,

            // Vertical edges
            o, o, o,      o, s, o,
            s, o, o,      s, s, o,
            s, o, s,      s, s, s,
            o, o, s,      o, s, s,
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

        // Shader
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
            void main() {
                FragColor = vec4(0.0, 0.0, 0.0, 0.8);
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
    }

    void render(const glm::ivec3& blockPos, const glm::mat4& view, const glm::mat4& projection) {
        glUseProgram(shaderProgram);

        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(blockPos));

        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));

        glBindVertexArray(VAO);
        glLineWidth(2.0f);
        glDrawArrays(GL_LINES, 0, 24);
    }

    void destroy() {
        if (VBO) glDeleteBuffers(1, &VBO);
        if (VAO) glDeleteVertexArrays(1, &VAO);
        if (shaderProgram) glDeleteProgram(shaderProgram);
    }
};
