#include "BackendSelector.h"

#include <glad/gl.h>
#include <iostream>

namespace Render {

bool BackendSelector::isBackendAvailable(RendererType renderer) {
    switch (renderer) {
        case RendererType::OPENGL:
            // OpenGL 4.6 is always assumed available on desktop
            return true;

        case RendererType::VULKAN:
            // Check if GLFW supports Vulkan
            return glfwVulkanSupported() == GLFW_TRUE;

        default:
            return false;
    }
}

std::vector<RendererType> BackendSelector::getAvailableBackends() {
    std::vector<RendererType> backends;

    if (isBackendAvailable(RendererType::OPENGL)) {
        backends.push_back(RendererType::OPENGL);
    }
    if (isBackendAvailable(RendererType::VULKAN)) {
        backends.push_back(RendererType::VULKAN);
    }

    return backends;
}

std::string BackendSelector::getBackendName(RendererType renderer) {
    switch (renderer) {
        case RendererType::OPENGL: return "OpenGL 4.6";
        case RendererType::VULKAN: return "Vulkan";
        default: return "Unknown";
    }
}

bool BackendSelector::configureGLFW(RendererType renderer) {
    if (!isBackendAvailable(renderer)) {
        std::cerr << "[BackendSelector] " << getBackendName(renderer)
                  << " is not available on this system" << std::endl;
        return false;
    }

    // Reset all window hints to defaults first
    glfwDefaultWindowHints();

    switch (renderer) {
        case RendererType::OPENGL:
            // Configure for OpenGL 4.6 Core Profile
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_FALSE);
#ifndef NDEBUG
            glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif
            break;

        case RendererType::VULKAN:
            // No OpenGL context for Vulkan
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            break;

        default:
            return false;
    }

    // Common settings
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);

    return true;
}

GLFWwindow* BackendSelector::createWindow(RendererType renderer, int width, int height,
                                           const char* title, bool fullscreen) {
    if (!configureGLFW(renderer)) {
        return nullptr;
    }

    GLFWwindow* window = nullptr;

    if (fullscreen) {
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        window = glfwCreateWindow(mode->width, mode->height, title, monitor, nullptr);
    } else {
        window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    }

    if (!window) {
        std::cerr << "[BackendSelector] Failed to create GLFW window for "
                  << getBackendName(renderer) << std::endl;
        return nullptr;
    }

    // For OpenGL, make context current
    if (renderer == RendererType::OPENGL) {
        glfwMakeContextCurrent(window);
    }

    std::cout << "[BackendSelector] Created window for " << getBackendName(renderer) << std::endl;
    return window;
}

bool BackendSelector::initializeGraphicsAPI(RendererType renderer, GLFWwindow* window) {
    if (!window) {
        std::cerr << "[BackendSelector] Invalid window" << std::endl;
        return false;
    }

    switch (renderer) {
        case RendererType::OPENGL: {
            // Load OpenGL functions using GLAD
            int version = gladLoadGL(glfwGetProcAddress);
            if (version == 0) {
                std::cerr << "[BackendSelector] Failed to initialize GLAD" << std::endl;
                return false;
            }

            std::cout << "[BackendSelector] OpenGL initialized" << std::endl;
            std::cout << "  Version: " << glGetString(GL_VERSION) << std::endl;
            std::cout << "  Renderer: " << glGetString(GL_RENDERER) << std::endl;
            std::cout << "  Vendor: " << glGetString(GL_VENDOR) << std::endl;

            // Check OpenGL version
            GLint major, minor;
            glGetIntegerv(GL_MAJOR_VERSION, &major);
            glGetIntegerv(GL_MINOR_VERSION, &minor);
            if (major < 4 || (major == 4 && minor < 6)) {
                std::cerr << "[BackendSelector] OpenGL 4.6 required, got "
                          << major << "." << minor << std::endl;
                return false;
            }

            return true;
        }

        case RendererType::VULKAN:
            // Vulkan initialization is handled by VKDevice
            // volk is loaded there
            std::cout << "[BackendSelector] Vulkan API ready (device creation will init)" << std::endl;
            return true;

        default:
            return false;
    }
}

std::unique_ptr<RHI::RHIDevice> BackendSelector::createDevice(RendererType renderer, GLFWwindow* window) {
    if (!window) {
        std::cerr << "[BackendSelector] Cannot create device: invalid window" << std::endl;
        return nullptr;
    }

    RHI::Backend backend = toRHIBackend(renderer);
    return RHI::RHIDevice::create(backend, window);
}

RHI::Backend BackendSelector::toRHIBackend(RendererType renderer) {
    switch (renderer) {
        case RendererType::VULKAN: return RHI::Backend::Vulkan;
        case RendererType::OPENGL:
        default: return RHI::Backend::OpenGL;
    }
}

RendererType BackendSelector::fromRHIBackend(RHI::Backend backend) {
    switch (backend) {
        case RHI::Backend::Vulkan: return RendererType::VULKAN;
        case RHI::Backend::OpenGL:
        default: return RendererType::OPENGL;
    }
}

void BackendSelector::printBackendInfo() {
    std::cout << "\n=== Available Rendering Backends ===" << std::endl;

    auto backends = getAvailableBackends();
    for (auto backend : backends) {
        std::cout << "  - " << getBackendName(backend);
        if (backend == RendererType::OPENGL) {
            std::cout << " (default)";
        }
        std::cout << std::endl;
    }

    if (backends.empty()) {
        std::cout << "  (none available!)" << std::endl;
    }

    std::cout << "====================================\n" << std::endl;
}

} // namespace Render
