#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

enum class CameraMovement {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT,
    UP,
    DOWN
};

class Camera {
public:
    // Camera attributes
    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    glm::vec3 worldUp;

    // Euler angles
    float yaw;
    float pitch;

    // Camera options
    float movementSpeed;
    float mouseSensitivity;
    float fov;

    // Constructor
    Camera(glm::vec3 startPosition = glm::vec3(0.0f, 0.0f, 3.0f),
           glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f),
           float startYaw = -90.0f,
           float startPitch = 0.0f)
        : position(startPosition)
        , front(glm::vec3(0.0f, 0.0f, -1.0f))
        , worldUp(up)
        , yaw(startYaw)
        , pitch(startPitch)
        , movementSpeed(5.0f)
        , mouseSensitivity(0.1f)
        , fov(70.0f)
    {
        updateCameraVectors();
    }

    // Get view matrix
    glm::mat4 getViewMatrix() const {
        return glm::lookAt(position, position + front, up);
    }

    // Get projection matrix
    glm::mat4 getProjectionMatrix(float aspectRatio, float nearPlane = 0.1f, float farPlane = 1000.0f) const {
        return glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
    }

    // Process keyboard input
    void processKeyboard(CameraMovement direction, float deltaTime) {
        float velocity = movementSpeed * deltaTime;

        switch (direction) {
            case CameraMovement::FORWARD:
                position += front * velocity;
                break;
            case CameraMovement::BACKWARD:
                position -= front * velocity;
                break;
            case CameraMovement::LEFT:
                position -= right * velocity;
                break;
            case CameraMovement::RIGHT:
                position += right * velocity;
                break;
            case CameraMovement::UP:
                position += worldUp * velocity;
                break;
            case CameraMovement::DOWN:
                position -= worldUp * velocity;
                break;
        }
    }

    // Process mouse movement
    void processMouseMovement(float xOffset, float yOffset, bool constrainPitch = true) {
        xOffset *= mouseSensitivity;
        yOffset *= mouseSensitivity;

        yaw += xOffset;
        pitch += yOffset;

        // Constrain pitch to avoid flipping
        if (constrainPitch) {
            if (pitch > 89.0f) pitch = 89.0f;
            if (pitch < -89.0f) pitch = -89.0f;
        }

        updateCameraVectors();
    }

    // Process mouse scroll (zoom)
    void processMouseScroll(float yOffset) {
        fov -= yOffset;
        if (fov < 1.0f) fov = 1.0f;
        if (fov > 120.0f) fov = 120.0f;
    }

    // Set sprint mode
    void setSprinting(bool sprinting) {
        movementSpeed = sprinting ? 10.0f : 5.0f;
    }

private:
    // Recalculate camera vectors from euler angles
    void updateCameraVectors() {
        glm::vec3 newFront;
        newFront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        newFront.y = sin(glm::radians(pitch));
        newFront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        front = glm::normalize(newFront);

        // Recalculate right and up vectors
        right = glm::normalize(glm::cross(front, worldUp));
        up = glm::normalize(glm::cross(right, front));
    }
};
